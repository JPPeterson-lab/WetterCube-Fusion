// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Einstellungsspeicher auf dem NVS (eigener Code). Speichert dieselben Datentypen wie die früher genutzte Arduino-Preferences-Klasse
// (Zahlen als i32/u8, Kommazahlen als 4-Byte-Blob, Texte als String), damit vorhandene Einstellungen nach dem Wechsel erhalten bleiben.
#include "Arduino.h"
#include "nvs.h"

class Preferences {
 public:
  bool begin(const char* ns, bool readOnly = false) {
    end();
    return nvs_open(ns, readOnly ? NVS_READONLY : NVS_READWRITE, &h_) == ESP_OK && (offen_ = true);
  }
  void end() { if (offen_) { nvs_commit(h_); nvs_close(h_); offen_ = false; } }
  bool clear() { return offen_ && nvs_erase_all(h_) == ESP_OK; }

  String getString(const char* key, const String& def = String()) {
    if (!offen_) return def;
    size_t n = 0;
    if (nvs_get_str(h_, key, nullptr, &n) != ESP_OK || n == 0) return def;
    char* buf = (char*)malloc(n);
    if (!buf) return def;
    String r = def;
    if (nvs_get_str(h_, key, buf, &n) == ESP_OK) r = String(buf);
    free(buf);
    return r;
  }
  size_t putString(const char* key, const String& v) { return offen_ && nvs_set_str(h_, key, v.c_str()) == ESP_OK ? v.length() : 0; }
  int32_t getInt(const char* key, int32_t def = 0) { int32_t v; return offen_ && nvs_get_i32(h_, key, &v) == ESP_OK ? v : def; }
  size_t putInt(const char* key, int32_t v) { return offen_ && nvs_set_i32(h_, key, v) == ESP_OK ? 4 : 0; }
  uint8_t getUChar(const char* key, uint8_t def = 0) { uint8_t v; return offen_ && nvs_get_u8(h_, key, &v) == ESP_OK ? v : def; }
  size_t putUChar(const char* key, uint8_t v) { return offen_ && nvs_set_u8(h_, key, v) == ESP_OK ? 1 : 0; }
  bool getBool(const char* key, bool def = false) { return getUChar(key, def ? 1 : 0) == 1; }
  size_t putBool(const char* key, bool v) { return putUChar(key, v ? 1 : 0); }
  float getFloat(const char* key, float def = 0.0f) {
    float v; size_t n = sizeof(v);
    return offen_ && nvs_get_blob(h_, key, &v, &n) == ESP_OK && n == sizeof(v) ? v : def;
  }
  size_t putFloat(const char* key, float v) { return offen_ && nvs_set_blob(h_, key, &v, sizeof(v)) == ESP_OK ? sizeof(v) : 0; }
  ~Preferences() { end(); }

 private:
  nvs_handle_t h_ = 0;
  bool offen_ = false;
};
