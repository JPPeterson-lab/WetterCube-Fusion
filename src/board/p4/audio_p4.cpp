// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Lautsprecher des Waveshare ESP32-P4-WIFI6-Touch-LCD-7B: Audio-Chip ES8311 (Steuerung ueber den I2C-Bus des Touch, Daten ueber I2S),
// Verstaerker an GPIO 53. Die Alarmtoene werden im Gerat erzeugt (Sinus, 16 kHz, mono), es werden keine Audiodateien gebraucht.
// Pins und Aufbau nach der Waveshare-Komponente waveshare/esp32_p4_wifi6_touch_lcd_7b (BSP 3.0.1).
#include "../board.h"
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char* TAG = "audio_p4";

#define PIN_I2S_MCLK  GPIO_NUM_13
#define PIN_I2S_SCLK  GPIO_NUM_12
#define PIN_I2S_LRCK  GPIO_NUM_10
#define PIN_I2S_DOUT  GPIO_NUM_9
#define PIN_I2S_DIN   GPIO_NUM_11
#define PIN_VERSTAERKER GPIO_NUM_53
#define ABTAST_HZ     16000
#define BLOCK         320                       // 20 ms

const char* const BOARD_AUDIO_TONNAMEN[BOARD_AUDIO_TOENE] = {"Piepen", "Glocke", "Melodie", "Sirene", "Sanft"};

static i2s_chan_handle_t s_tx = nullptr;
static esp_codec_dev_handle_t s_codec = nullptr;
static TaskHandle_t s_task = nullptr;
static volatile bool s_spielt = false;
static volatile int s_ton = 0, s_laut = 50, s_dauerMs = 0;
static bool s_init = false, s_ok = false;

static bool audioInit() {
  if (s_init) return s_ok;
  s_init = true;
  if (!boardI2cBus()) { ESP_LOGE(TAG, "I2C-Bus fehlt"); return false; }
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &s_tx, nullptr) != ESP_OK) { ESP_LOGE(TAG, "I2S-Kanal"); return false; }
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ABTAST_HZ);
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  sc.gpio_cfg.mclk = PIN_I2S_MCLK; sc.gpio_cfg.bclk = PIN_I2S_SCLK; sc.gpio_cfg.ws = PIN_I2S_LRCK;
  sc.gpio_cfg.dout = PIN_I2S_DOUT; sc.gpio_cfg.din = PIN_I2S_DIN;
  if (i2s_channel_init_std_mode(s_tx, &sc) != ESP_OK || i2s_channel_enable(s_tx) != ESP_OK) { ESP_LOGE(TAG, "I2S-Start"); return false; }

  audio_codec_i2s_cfg_t i2s_cfg = {}; i2s_cfg.port = I2S_NUM_0; i2s_cfg.tx_handle = s_tx;
  const audio_codec_data_if_t* data_if = audio_codec_new_i2s_data(&i2s_cfg);
  audio_codec_i2c_cfg_t i2c_cfg = {}; i2c_cfg.port = I2C_NUM_0; i2c_cfg.addr = ES8311_CODEC_DEFAULT_ADDR; i2c_cfg.bus_handle = boardI2cBus();
  const audio_codec_ctrl_if_t* ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
  const audio_codec_gpio_if_t* gpio_if = audio_codec_new_gpio();
  if (!data_if || !ctrl_if || !gpio_if) { ESP_LOGE(TAG, "Codec-Schnittstellen"); return false; }
  es8311_codec_cfg_t ec = {};
  ec.ctrl_if = ctrl_if; ec.gpio_if = gpio_if; ec.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
  ec.pa_pin = PIN_VERSTAERKER; ec.pa_reverted = false; ec.master_mode = false; ec.use_mclk = true;
  ec.hw_gain.pa_voltage = 5.0f; ec.hw_gain.codec_dac_voltage = 3.3f;
  const audio_codec_if_t* codec_if = es8311_codec_new(&ec);
  if (!codec_if) { ESP_LOGE(TAG, "ES8311 antwortet nicht"); return false; }
  esp_codec_dev_cfg_t dc = {}; dc.dev_type = ESP_CODEC_DEV_TYPE_OUT; dc.codec_if = codec_if; dc.data_if = data_if;
  s_codec = esp_codec_dev_new(&dc);
  s_ok = s_codec != nullptr;
  if (s_ok) ESP_LOGI(TAG, "Audio bereit (ES8311)"); else ESP_LOGE(TAG, "Audio nicht verfuegbar");
  return s_ok;
}

bool boardAudioVorhanden() { return true; }

// ── Tonerzeugung: liefert den Abtastwert (-1..1) zum Zeitpunkt t (Sekunden) im Takt des jeweiligen Tons ───────────────────────────────
static float sinus(float f, float t) { return sinf(6.2831853f * f * t); }
static float huellkurve(float t, float anstieg, float abfall) { return (t < anstieg ? t / anstieg : 1.0f) * expf(-t / abfall); }

static float tonWert(int ton, float t) {
  switch (ton) {
    case 0: {   // Piepen: vier kurze Pieptoene, dann Pause
      float z = fmodf(t, 1.5f);
      if (z < 0.8f) { float p = fmodf(z, 0.2f); return p < 0.1f ? 0.7f * sinus(2400, t) : 0.0f; }
      return 0.0f;
    }
    case 1: {   // Glocke: Grundton mit Obertoenen, klingt aus
      float z = fmodf(t, 2.0f);
      return 0.55f * huellkurve(z, 0.004f, 0.45f) * (sinus(880, z) + 0.5f * sinus(1760, z) + 0.25f * sinus(2640, z)) / 1.75f * 1.6f;
    }
    case 2: {   // Melodie: aufsteigender Dreiklang C-E-G-C
      static const float N[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
      float z = fmodf(t, 2.0f);
      if (z < 0.8f) { int i = (int)(z / 0.2f); float lt = z - i * 0.2f; return 0.6f * huellkurve(lt, 0.005f, 0.12f) * sinus(N[i], z); }
      return 0.0f;
    }
    case 3: {   // Sirene: Tonhoehe laeuft auf und ab
      float z = fmodf(t, 3.5f);
      if (z < 3.0f) { float ph = fmodf(z, 1.0f); float f = ph < 0.5f ? 600 + 1200 * ph : 1200 - 1200 * (ph - 0.5f); return 0.6f * sinus(f, z); }
      return 0.0f;
    }
    default: {  // Sanft: zwei weiche Glockentoene
      float z = fmodf(t, 3.0f);
      if (z < 1.2f) return 0.5f * huellkurve(z, 0.03f, 0.35f) * sinus(659.25f, z);
      if (z < 2.4f) { float lt = z - 1.2f; return 0.5f * huellkurve(lt, 0.03f, 0.35f) * sinus(880.0f, lt); }
      return 0.0f;
    }
  }
}

static void audioTask(void*) {
  static int16_t puffer[BLOCK];
  esp_codec_dev_sample_info_t fs = {};
  fs.bits_per_sample = 16; fs.channel = 1; fs.sample_rate = ABTAST_HZ;
  if (esp_codec_dev_open(s_codec, &fs) != ESP_CODEC_DEV_OK) { ESP_LOGE(TAG, "Codec oeffnen fehlgeschlagen"); s_spielt = false; s_task = nullptr; vTaskDelete(nullptr); return; }
  esp_codec_dev_set_out_vol(s_codec, (float)s_laut);
  uint32_t n = 0;
  while (s_spielt) {
    if (s_dauerMs > 0 && (int64_t)n * 1000 / ABTAST_HZ >= s_dauerMs) break;   // zeitlich begrenzter Ton (Warnung)
    int ton = s_ton;
    for (int i = 0; i < BLOCK; i++, n++) {
      float v = tonWert(ton, (float)(n % (ABTAST_HZ * 60)) / ABTAST_HZ);
      puffer[i] = (int16_t)(fmaxf(-1.0f, fminf(1.0f, v)) * 30000.0f);
    }
    esp_codec_dev_write(s_codec, puffer, sizeof(puffer));
  }
  memset(puffer, 0, sizeof(puffer));
  esp_codec_dev_write(s_codec, puffer, sizeof(puffer));   // Stille hinterherschicken, damit nichts knackt
  esp_codec_dev_close(s_codec);                           // schaltet auch den Verstaerker aus
  s_spielt = false;
  s_task = nullptr;
  vTaskDelete(nullptr);
}

bool boardAudioSpielt() { return s_spielt; }

void boardAudioTon(int ton, int lautstaerke, int dauerMs) {
  if (!audioInit()) return;
  if (ton < 0 || ton >= BOARD_AUDIO_TOENE) ton = 0;
  if (lautstaerke < 0) lautstaerke = 0;
  if (lautstaerke > 100) lautstaerke = 100;
  s_ton = ton; s_laut = lautstaerke; s_dauerMs = dauerMs;
  if (s_spielt && s_task) { esp_codec_dev_set_out_vol(s_codec, (float)lautstaerke); return; }   // laeuft schon: nur Ton und Lautstaerke aendern
  s_spielt = true;
  if (xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, &s_task, 1) != pdPASS) { s_spielt = false; s_task = nullptr; }
}

void boardAudioStopp() { s_spielt = false; }
