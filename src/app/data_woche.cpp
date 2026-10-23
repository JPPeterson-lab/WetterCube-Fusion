// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Open-Meteo: 7-Tage-Vorschau (Tageswerte). Eigene, kleine Abfrage: die stuendlichen Werte der Hauptabfrage reichen nur 2 Tage weit.
#include "app_state.h"
#include <ArduinoJson.h>

bool fetchWoche() {
  spur("Woche: Abruf");
  if (WiFi.status() != WL_CONNECTED || cfg.lat == 0.0f) return false;
  WiFiClientSecure sc;
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast";
  url += "?latitude="  + String(cfg.lat, 4);
  url += "&longitude=" + String(cfg.lon, 4);
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,wind_speed_10m_max";
  url += "&timezone=auto&forecast_days=7";
  http.begin(sc, url);
  int httpCode = http.GET();
  if (httpCode != 200) { logHttpFehler("Woche", httpCode, sc); http.end(); return false; }
  String body = http.getString();
  http.end();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  body = String();
  if (err != DeserializationError::Ok) { logDiag("[Woche] JSON-Parse fehlgeschlagen: %s", err.c_str()); return false; }
  JsonObject d = doc["daily"];
  int n = d["time"].size();
  if (n <= 0) return false;
  if (n > WocheDaten::N) n = WocheDaten::N;
  LvLock lock;   // die UI liest die Daten
  for (int i = 0; i < n; i++) {
    WocheTag& t = woche.tag[i];
    String datum = d["time"][i].as<String>();
    strlcpy(t.datum, datum.c_str(), sizeof(t.datum));
    t.wmo      = d["weather_code"][i].as<int>();
    t.tmax     = d["temperature_2m_max"][i].as<float>();
    t.tmin     = d["temperature_2m_min"][i].as<float>();
    t.regen_mm = d["precipitation_sum"][i].isNull() ? 0.0f : d["precipitation_sum"][i].as<float>();
    t.regen_p  = d["precipitation_probability_max"][i].isNull() ? -1 : d["precipitation_probability_max"][i].as<int>();
    t.wind_max = d["wind_speed_10m_max"][i].as<float>();
  }
  woche.n = n;
  woche.geladen = true;
  return true;
}
