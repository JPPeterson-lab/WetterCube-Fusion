// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Interner Umweltsensor: Modul AHT20 (Temperatur, Feuchte) + BMP280 (Druck) am I2C-Bus des Boards.
// Reine ESP-IDF-Treiber (i2c_master), ohne Arduino. Fehlt der Sensor, wird er alle 60 s erneut gesucht.
#include "sensors_intern.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../board/board.h"

static const char* TAG = "sens-int";
#define AHT20_ADDR 0x38
#define BMP_ADDR_1 0x77
#define BMP_ADDR_2 0x76

static i2c_master_dev_handle_t s_aht = nullptr, s_bmp = nullptr;

static i2c_master_dev_handle_t geraet(i2c_master_bus_handle_t bus, uint8_t addr) {
  i2c_device_config_t c = {};
  c.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  c.device_address = addr;
  c.scl_speed_hz = 100000;
  i2c_master_dev_handle_t h = nullptr;
  return i2c_master_bus_add_device(bus, &c, &h) == ESP_OK ? h : nullptr;
}

// ── AHT20 ────────────────────────────────────────────────────────────────────
static bool ahtInit() {
  vTaskDelay(pdMS_TO_TICKS(40));
  uint8_t st = 0;
  if (i2c_master_receive(s_aht, &st, 1, 100) != ESP_OK) return false;
  if ((st & 0x18) != 0x18) {                       // nicht kalibriert: Initialisierungsbefehl
    const uint8_t cmd[3] = {0xBE, 0x08, 0x00};
    if (i2c_master_transmit(s_aht, cmd, 3, 100) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return true;
}

static bool ahtLies(float* t, float* h) {
  const uint8_t cmd[3] = {0xAC, 0x33, 0x00};
  if (i2c_master_transmit(s_aht, cmd, 3, 100) != ESP_OK) return false;
  uint8_t d[7];
  for (int versuch = 0; versuch < 5; versuch++) {
    vTaskDelay(pdMS_TO_TICKS(85));
    if (i2c_master_receive(s_aht, d, 7, 100) != ESP_OK) return false;
    if (!(d[0] & 0x80)) break;                      // Bit 7: Messung laeuft noch
    if (versuch == 4) return false;
  }
  uint32_t rh = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
  uint32_t rt = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
  *h = rh * 100.0f / 1048576.0f;
  *t = rt * 200.0f / 1048576.0f - 50.0f;
  return true;
}

// ── BMP280 (Kompensation nach Datenblatt, 32-Bit-Ganzzahlen) ─────────────────
static uint16_t dig_T1, dig_P1; static int16_t dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

static bool bmpReg(uint8_t reg, uint8_t* d, size_t n) { return i2c_master_transmit_receive(s_bmp, &reg, 1, d, n, 100) == ESP_OK; }

static bool bmpInit() {
  uint8_t id = 0;
  if (!bmpReg(0xD0, &id, 1) || (id != 0x58 && id != 0x60)) return false;      // 0x58 BMP280, 0x60 BME280
  uint8_t c[24];
  if (!bmpReg(0x88, c, 24)) return false;
  dig_T1 = c[0] | (c[1] << 8); dig_T2 = c[2] | (c[3] << 8); dig_T3 = c[4] | (c[5] << 8);
  dig_P1 = c[6] | (c[7] << 8); dig_P2 = c[8] | (c[9] << 8); dig_P3 = c[10] | (c[11] << 8);
  dig_P4 = c[12] | (c[13] << 8); dig_P5 = c[14] | (c[15] << 8); dig_P6 = c[16] | (c[17] << 8);
  dig_P7 = c[18] | (c[19] << 8); dig_P8 = c[20] | (c[21] << 8); dig_P9 = c[22] | (c[23] << 8);
  return true;
}

static bool bmpLies(float* t, float* p_hPa) {
  const uint8_t messen[2] = {0xF4, 0x55};          // Forced Mode, Temperatur x2, Druck x16: wenig Eigenerwaermung
  if (i2c_master_transmit(s_bmp, messen, 2, 100) != ESP_OK) return false;
  vTaskDelay(pdMS_TO_TICKS(50));
  uint8_t d[6];
  if (!bmpReg(0xF7, d, 6)) return false;
  int32_t adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
  int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
  if (adc_T == 0x80000 || adc_P == 0x80000) return false;
  int32_t v1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * (int32_t)dig_T2) >> 11;
  int32_t v2 = (((((adc_T >> 4) - (int32_t)dig_T1) * ((adc_T >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
  int32_t t_fine = v1 + v2;
  *t = ((t_fine * 5 + 128) >> 8) / 100.0f;
  int64_t a = (int64_t)t_fine - 128000;
  int64_t b = a * a * (int64_t)dig_P6;
  b += (a * (int64_t)dig_P5) << 17;
  b += ((int64_t)dig_P4) << 35;
  a = ((a * a * (int64_t)dig_P3) >> 8) + ((a * (int64_t)dig_P2) << 12);
  a = (((((int64_t)1) << 47) + a)) * ((int64_t)dig_P1) >> 33;
  if (a == 0) return false;
  int64_t p = 1048576 - adc_P;
  p = (((p << 31) - b) * 3125) / a;
  a = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  b = (((int64_t)dig_P8) * p) >> 19;
  p = ((p + a + b) >> 8) + (((int64_t)dig_P7) << 4);
  *p_hPa = (float)p / 256.0f / 100.0f;
  return true;
}

// ── Task ─────────────────────────────────────────────────────────────────────
static void internTask(void*) {
  for (;;) {
    i2c_master_bus_handle_t bus = boardI2cBus();
    if (!bus) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
    if (!s_aht && i2c_master_probe(bus, AHT20_ADDR, 50) == ESP_OK) s_aht = geraet(bus, AHT20_ADDR);
    if (!s_bmp) {
      uint8_t a = i2c_master_probe(bus, BMP_ADDR_1, 50) == ESP_OK ? BMP_ADDR_1 : (i2c_master_probe(bus, BMP_ADDR_2, 50) == ESP_OK ? BMP_ADDR_2 : 0);
      if (a) s_bmp = geraet(bus, a);
    }
    if (s_aht && !ahtInit()) { ESP_LOGW(TAG, "AHT20 antwortet nicht"); }
    if (s_bmp && !bmpInit()) { ESP_LOGW(TAG, "BMP280 nicht erkannt"); i2c_master_bus_rm_device(s_bmp); s_bmp = nullptr; }
    if (!s_aht && !s_bmp) {
      vTaskDelay(pdMS_TO_TICKS(60000));            // kein Sensor angeschlossen: spaeter erneut suchen
      continue;
    }
    g_internVorhanden = true;
    sensorenInternAnlegen();
    ESP_LOGI(TAG, "Sensor erkannt: AHT20 %s, BMP280 %s", s_aht ? "ja" : "nein", s_bmp ? "ja" : "nein");
    for (;;) {                                      // Messschleife, alle 30 s (wie im ESPHome-Code)
      float t = NAN, h = NAN, tb = NAN, p = NAN;
      bool ok1 = s_aht && ahtLies(&t, &h);
      bool ok2 = s_bmp && bmpLies(&tb, &p);
      if (ok1) { sensorenInternMelde(SQ_INT_TEMP, t); sensorenInternMelde(SQ_INT_FEUCHTE, h); }
      else if (ok2) sensorenInternMelde(SQ_INT_TEMP, tb);   // ohne AHT20: Temperatur des BMP280
      if (ok2) sensorenInternMelde(SQ_INT_DRUCK, p);
      if (!ok1 && !ok2) ESP_LOGW(TAG, "Messung fehlgeschlagen");
      vTaskDelay(pdMS_TO_TICKS(30000));
    }
  }
}

void sensorenInternStart() {
  xTaskCreate(internTask, "sens-int", 4096, nullptr, 3, nullptr);
}
