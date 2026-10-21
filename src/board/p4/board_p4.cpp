// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Board-Schicht fuer das Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (Hauptboard): MIPI-DSI-Display (Controller EK79007, 1024x600,
// 2 Lanes), Touch GT911 am I2C (SDA 7, SCL 8), Helligkeit per PWM (GPIO 32), SD-Karte ueber SDMMC (4 Bit), WLAN ueber den ESP32-C6.
// Pins und Ablauf nach den Waveshare-Beispielen (Komponente waveshare/esp32_p4_wifi6_touch_lcd_7b).
// ERSTER ENTWURF ohne Board: wird erst mit der Hardware geprueft.
#include "../board.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/sdmmc_host.h"
#include "esp_ldo_regulator.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_vfs_fat.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ek79007.h"
#include "../../hardware/gt911.h"

static const char* TAG = "board_p4";

#define PIN_I2C_SDA        GPIO_NUM_7
#define PIN_I2C_SCL        GPIO_NUM_8
#define PIN_LCD_BACKLIGHT  GPIO_NUM_32
#define PIN_LCD_RESET      GPIO_NUM_33
#define DSI_LANES          2
#define DSI_LANE_MBPS      1000
#define DSI_PHY_LDO_KANAL  3              // LDO_VO3 speist VDD_MIPI_DPHY
#define DSI_PHY_LDO_MV     2500
#define SD_LDO_KANAL       4              // LDO_VO4 speist die Speicherkarte

static i2c_master_bus_handle_t s_bus = nullptr;
static esp_lcd_panel_handle_t s_panel = nullptr;
static int s_helligkeit = 80;

const char* boardName() { return "ESP32-P4-WIFI6-Touch-LCD-7B"; }
i2c_master_bus_handle_t boardI2cBus() { return s_bus; }
uint32_t boardBilderGezaehlt() { return 0; }          // der DSI-Treiber zaehlt nicht mit

// ── I2C und Touch ───────────────────────────────────────────────────────────────────────────────────────────────────────
static esp_err_t i2cInit() {
  if (s_bus) return ESP_OK;
  i2c_master_bus_config_t c = {};
  c.clk_source = I2C_CLK_SRC_DEFAULT;
  c.i2c_port = I2C_NUM_0;
  c.sda_io_num = PIN_I2C_SDA; c.scl_io_num = PIN_I2C_SCL;
  c.glitch_ignore_cnt = 7;
  c.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&c, &s_bus);
}

esp_lcd_touch_handle_t boardTouchInit() {
  ESP_ERROR_CHECK(i2cInit());
  // GT911: Adresse 0x5D oder 0x14, je nachdem, wie INT beim Start lag (am P4-Board ist weder Reset noch INT herausgefuehrt)
  esp_lcd_panel_io_i2c_config_t io = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
  if (i2c_master_probe(s_bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 100) == ESP_OK) ESP_LOGI(TAG, "Touch 0x5d gefunden");
  else if (i2c_master_probe(s_bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, 100) == ESP_OK) { io.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP; ESP_LOGI(TAG, "Touch 0x14 gefunden"); }
  else { ESP_LOGE(TAG, "Touch nicht gefunden"); return nullptr; }
  esp_lcd_panel_io_handle_t tpIo = nullptr;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_bus, &io, &tpIo));
  const esp_lcd_touch_config_t tp = {
    .x_max = BOARD_LCD_H_RES, .y_max = BOARD_LCD_V_RES,
    .rst_gpio_num = GPIO_NUM_NC, .int_gpio_num = GPIO_NUM_NC,
    .levels = { .reset = 0, .interrupt = 0 },
    .flags = { .swap_xy = 0, .mirror_x = 1, .mirror_y = 1 },   // wie in den Waveshare-Beispielen
  };
  esp_lcd_touch_handle_t h = nullptr;
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tpIo, &tp, &h));
  return h;
}

// ── Helligkeit (PWM, Ausgang invertiert) ──────────────────────────────────────────────────────────────────────────────────
static void helligkeitInit() {
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE; t.duty_resolution = LEDC_TIMER_10_BIT; t.timer_num = LEDC_TIMER_1;
  t.freq_hz = 5000; t.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&t));
  ledc_channel_config_t c = {};
  c.gpio_num = PIN_LCD_BACKLIGHT; c.speed_mode = LEDC_LOW_SPEED_MODE; c.channel = LEDC_CHANNEL_1; c.intr_type = LEDC_INTR_DISABLE;
  c.timer_sel = LEDC_TIMER_1; c.duty = 0; c.hpoint = 0; c.flags.output_invert = 1;
  ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void boardHelligkeit(int prozent) {
  if (prozent < 3) prozent = 3;
  if (prozent > 100) prozent = 100;
  s_helligkeit = prozent;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, (1023u * (uint32_t)prozent) / 100u);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

void boardBacklightAn() { boardHelligkeit(s_helligkeit); }

// ── Display ───────────────────────────────────────────────────────────────────────────────────────────────────────────────
esp_lcd_panel_handle_t boardPanelInit() {
  helligkeitInit();                                    // Beleuchtung bleibt aus (Tastverhaeltnis 0), bis boardBacklightAn() kommt
  esp_ldo_channel_handle_t phy = nullptr;              // Strom fuer den MIPI-DSI-PHY
  esp_ldo_channel_config_t ldo = {}; ldo.chan_id = DSI_PHY_LDO_KANAL; ldo.voltage_mv = DSI_PHY_LDO_MV;
  ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo, &phy));

  esp_lcd_dsi_bus_handle_t bus = nullptr;
  esp_lcd_dsi_bus_config_t bc = {};
  bc.bus_id = 0; bc.num_data_lanes = DSI_LANES; bc.phy_clk_src = (mipi_dsi_phy_clock_source_t)0; bc.lane_bit_rate_mbps = DSI_LANE_MBPS;
  ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bc, &bus));

  esp_lcd_panel_io_handle_t io = nullptr;              // Befehle an den Display-Controller laufen ueber DBI
  esp_lcd_dbi_io_config_t dbi = {}; dbi.virtual_channel = 0; dbi.lcd_cmd_bits = 8; dbi.lcd_param_bits = 8;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(bus, &dbi, &io));

  esp_lcd_dpi_panel_config_t dpi = EK79007_1024_600_PANEL_60HZ_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
  dpi.num_fbs = 1;                                     // ein Framebuffer im PSRAM; LVGL kopiert Teilbereiche hinein (wie beim S3)
  ek79007_vendor_config_t vendor = {};
  vendor.mipi_config.dsi_bus = bus; vendor.mipi_config.dpi_config = &dpi;
  esp_lcd_panel_dev_config_t dev = {};
  dev.bits_per_pixel = 16; dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB; dev.reset_gpio_num = PIN_LCD_RESET; dev.vendor_config = &vendor;
  ESP_ERROR_CHECK(esp_lcd_new_panel_ek79007(io, &dev, &s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
  ESP_LOGI(TAG, "Display bereit");
  return s_panel;
}

void boardFramebuffer(void** fb) {
  if (!fb) return;
  *fb = nullptr;
  if (s_panel) esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, fb);
}

// ── Speicherkarte (SDMMC, 4 Bit, Anschluss ueber IO-MUX, Strom ueber den internen LDO 4) ──────────────────────────────────────
bool boardSdEinhaengen(const char* mount, sdmmc_card_t** card, char* diag, size_t diagGroesse) {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
  static sd_pwr_ctrl_handle_t pwr = nullptr;
  if (!pwr) {
    sd_pwr_ctrl_ldo_config_t ldo = {}; ldo.ldo_chan_id = SD_LDO_KANAL;
    esp_err_t e = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &pwr);
    if (e != ESP_OK) { snprintf(diag, diagGroesse, "SD-Stromversorgung (LDO %d): %s", SD_LDO_KANAL, esp_err_to_name(e)); pwr = nullptr; return false; }
  }
  host.pwr_ctrl_handle = pwr;
  sdmmc_slot_config_t slot = {};
  slot.cd = SDMMC_SLOT_NO_CD; slot.wp = SDMMC_SLOT_NO_WP; slot.width = 4; slot.flags = 0;
  esp_vfs_fat_sdmmc_mount_config_t cfg = {};
  cfg.format_if_mount_failed = false;                  // fremde oder defekte Karten nie ungefragt formatieren
  cfg.max_files = 2;
  cfg.allocation_unit_size = 0;
  esp_err_t e = esp_vfs_fat_sdmmc_mount(mount, &host, &slot, &cfg, card);
  if (e != ESP_OK) { snprintf(diag, diagGroesse, "Einhaengen fehlgeschlagen (%s)", esp_err_to_name(e)); return false; }
  return true;
}
