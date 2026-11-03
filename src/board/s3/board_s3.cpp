// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Board-Schicht fuer das Waveshare ESP32-S3-Touch-LCD-7B (Backup-Board): RGB-Display, IO-Erweiterung, Touch GT911, SD ueber SPI.
#include "../board.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "i2c.h"
#include "io_extension.h"
#include "rgb_lcd_port.h"
#include "../../hardware/gt911.h"

static const char* TAG = "board_s3";
#define PIN_TOUCH_INT GPIO_NUM_4          // Interrupt-Pin des Touch-Controllers (Reset haengt an EXIO1 der IO-Erweiterung)

const char* boardName() { return "ESP32-S3-Touch-LCD-7B"; }

esp_lcd_touch_handle_t boardTouchInit() {
  esp_lcd_panel_io_handle_t tp_io_handle = NULL;
  const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
  // Touch vor der Benutzung zuruecksetzen (Adresse 0x5D: INT beim Reset auf Low)
  DEV_I2C_Port port = DEV_I2C_Init();
  IO_EXTENSION_Init();
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_reset_pin(PIN_TOUCH_INT); gpio_set_direction(PIN_TOUCH_INT, GPIO_MODE_OUTPUT);
  IO_EXTENSION_Output(IO_EXTENSION_IO_1, 0);
  vTaskDelay(pdMS_TO_TICKS(100));
  gpio_set_level(PIN_TOUCH_INT, 0);
  vTaskDelay(pdMS_TO_TICKS(100));
  IO_EXTENSION_Output(IO_EXTENSION_IO_1, 1);
  vTaskDelay(pdMS_TO_TICKS(200));

  ESP_LOGI(TAG, "Initialize I2C panel IO");
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(port.bus, &tp_io_config, &tp_io_handle));
  ESP_LOGI(TAG, "Initialize touch controller GT911");
  const esp_lcd_touch_config_t tp_cfg = {
    .x_max = BOARD_LCD_H_RES, .y_max = BOARD_LCD_V_RES,
    .rst_gpio_num = GPIO_NUM_NC, .int_gpio_num = PIN_TOUCH_INT,
    .levels = { .reset = 0, .interrupt = 0 },
    .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
  };
  esp_lcd_touch_handle_t tp = NULL;
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp));
  return tp;
}

esp_lcd_panel_handle_t boardPanelInit() { return waveshare_esp32_s3_rgb_lcd_init(); }
void boardDrawWarten() {}
void boardFramebuffer(void** fb) { waveshare_get_frame_buffer(fb, nullptr); }
void boardBacklightAn() { wavesahre_rgb_lcd_bl_on(); }
uint32_t boardBilderGezaehlt() { return rgbBilderGezaehlt(); }
i2c_master_bus_handle_t boardI2cBus() { return DEV_I2C_GetBus(); }

void boardHelligkeit(int prozent) {
  // Der PWM-Wert des IO-Expanders wirkt umgekehrt: 0 = am hellsten, 97 = am dunkelsten (der Treiber begrenzt auf 97,
  // damit das Display nie ganz ausgeht). Helligkeit 3-100 % -> Wert 97-0.
  if (prozent < 3) prozent = 3;
  if (prozent > 100) prozent = 100;
  IO_EXTENSION_Pwm_Output((uint8_t)(100 - prozent));
}

// SD im SPI-Modus (MOSI 11, SCK 12, MISO 13); Chip-Select haengt an EXIO4 der IO-Erweiterung (aktiv Low) und wird dauerhaft auf Low
// gelegt, die Karte ist der einzige Teilnehmer am Bus.
bool boardSdEinhaengen(const char* mount, sdmmc_card_t** card, char* diag, size_t diagGroesse) {
  static bool busInit = false;
  IO_EXTENSION_Output(IO_EXTENSION_IO_4, 0);
  if (!busInit) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = 11; bus.miso_io_num = 13; bus.sclk_io_num = 12;
    bus.quadwp_io_num = -1; bus.quadhd_io_num = -1; bus.max_transfer_sz = 4000;   // klein halten: SPI-DMA-Puffer liegen im knappen internen RAM
    esp_err_t e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { ESP_LOGW(TAG, "SPI-Bus: %s", esp_err_to_name(e)); return false; }
    busInit = true;
  }
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;
  host.max_freq_khz = 20000;
  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.host_id = SPI2_HOST;
  slot.gpio_cs = GPIO_NUM_NC;
  esp_vfs_fat_sdmmc_mount_config_t cfgM = {};
  cfgM.format_if_mount_failed = false;                       // fremde oder defekte Karten nie ungefragt formatieren
  cfgM.max_files = 2;
  cfgM.allocation_unit_size = 0;
  esp_err_t e = esp_vfs_fat_sdspi_mount(mount, &host, &slot, &cfgM, card);
  if (e != ESP_OK) {
    // Diagnose: steht der Chip-Select (EXIO4) wirklich auf aktiv (Low)? Und was liegt an MISO?
    snprintf(diag, diagGroesse, "Einhaengen fehlgeschlagen (%s), EXIO4 gelesen: %d, MISO(13): %d", esp_err_to_name(e),
             IO_EXTENSION_Input(IO_EXTENSION_IO_4), gpio_get_level((gpio_num_t)13));
    return false;
  }
  return true;
}

// Der S3 hat keinen Lautsprecher: Alarme bleiben optisch (blinkendes Display)
const char* const BOARD_AUDIO_TONNAMEN[BOARD_AUDIO_TOENE] = {"Piepen", "Glocke", "Melodie", "Sirene", "Sanft"};
bool boardAudioVorhanden() { return false; }
void boardAudioTon(int, int, int) {}
bool boardAudioSpielt() { return false; }
void boardAudioStopp() {}
