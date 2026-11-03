// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Kleiner Speicher fuer Zusatzfunktionen (Schimmelwaechter): ein JSON-Text je Funktion als Blob im NVS.
// Der NVS-Bereich ist klein (20 KB): nur Einstellungen ablegen, keine Daten (die liegen auf der Speicherkarte).
#include <string>
#include "nvs.h"

inline std::string modulLesen(const char* ns, const char* key) {
  std::string r; nvs_handle_t h;
  if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return r;
  size_t n = 0;
  if (nvs_get_blob(h, key, nullptr, &n) == ESP_OK && n > 0 && n < 8192) { r.resize(n); if (nvs_get_blob(h, key, &r[0], &n) != ESP_OK) r.clear(); }
  nvs_close(h);
  return r;
}

inline bool modulSchreiben(const char* ns, const char* key, const std::string& s) {
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t e = s.empty() ? nvs_erase_key(h, key) : nvs_set_blob(h, key, s.data(), s.size());
  if (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) e = nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}
