// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "sd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../src/board/board.h"

static sdmmc_card_t* s_card = nullptr;
bool sdBereit() { return s_card != nullptr; }

// Das Einhaengen selbst steckt in der Board-Schicht (S3: SPI, P4: SDMMC)
bool sdEinhaengen() {
  if (s_card) return true;
  // Die Karte braucht nach dem Einschalten einen Moment, deshalb mehrere Versuche mit kurzer Pause
  for (int versuch = 1; versuch <= 4; versuch++) {
    vTaskDelay(pdMS_TO_TICKS(150));
    char diag[128] = "";
    if (boardSdEinhaengen("/sdcard", &s_card, diag, sizeof(diag))) return true;
    s_card = nullptr;
    ESP_LOGW("sd", "Versuch %d: %s", versuch, diag);
  }
  return false;
}
