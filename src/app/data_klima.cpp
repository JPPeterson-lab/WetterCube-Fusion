// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Open-Meteo: Luftdruck der letzten Stunden (Trend) und Niederschlag der letzten drei Tage plus Vorhersage (Giessen-Tipp).
// Eine kleine eigene Abfrage, damit die Zaehlung der Stundenwerte in fetchWetter() unveraendert bleibt.
#include "app_state.h"
#include <ArduinoJson.h>

bool fetchKlima() {
  spur("Klima: Abruf");
  if (WiFi.status() != WL_CONNECTED || cfg.lat == 0.0f) return false;
  WiFiClientSecure sc;
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast";
  url += "?latitude="  + String(cfg.lat, 4);
  url += "&longitude=" + String(cfg.lon, 4);
  url += "&hourly=surface_pressure&daily=precipitation_sum,temperature_2m_max";
  url += "&past_days=3&forecast_days=2&timezone=auto";   // Stundenwerte ab 00:00 vor drei Tagen: "jetzt" liegt bei Index 72 + Stunde
  http.begin(sc, url);
  int code = http.GET();
  if (code != 200) { logHttpFehler("Klima", code, sc); http.end(); return false; }
  String body = http.getString();
  http.end();
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) { logDiag("[Klima] JSON-Parse fehlgeschlagen"); return false; }
  body = String();

  struct tm ti; getLocalTime(&ti);
  const int jetzt = 72 + ti.tm_hour;
  JsonObject hr = doc["hourly"], dy = doc["daily"];
  auto h = [&](int i) -> float { JsonVariant v = hr["surface_pressure"][i]; return v.isNull() ? NAN : v.as<float>(); };
  auto d = [&](const char* k, int i) -> float { JsonVariant v = dy[k][i]; return v.isNull() ? NAN : v.as<float>(); };
  float p0 = h(jetzt), p3 = h(jetzt - 3);
  KlimaDaten k;
  if (!isnan(p0) && !isnan(p3)) { k.druck = p0; k.druckDelta3h = p0 - p3; k.druckOk = true; }
  float s = 0; int n = 0;
  for (int i = 0; i < 3; i++) { float v = d("precipitation_sum", i); if (!isnan(v)) { s += v; n++; } }
  if (n == 3) { k.regen3Tage = s; k.regenOk = true; }
  float rh = d("precipitation_sum", 3), rm = d("precipitation_sum", 4);
  k.regenHeute = isnan(rh) ? 0 : rh; k.regenMorgen = isnan(rm) ? 0 : rm;
  float th = d("temperature_2m_max", 3); k.tmaxHeute = isnan(th) ? 0 : th;
  k.geladen = k.druckOk || k.regenOk;
  { LvLock lock; klima = k; }
  logDiag("[Klima] Druck %s%.1f hPa in 3 h, Regen %.1f mm in 3 Tagen, heute %.1f, morgen %.1f", k.druckDelta3h >= 0 ? "+" : "", k.druckDelta3h, k.regen3Tage, k.regenHeute, k.regenMorgen);
  return true;
}
