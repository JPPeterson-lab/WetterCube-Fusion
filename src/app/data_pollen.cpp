// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Pollen (DWD-Stufen + Open-Meteo stündlich) und Luftqualität (Open-Meteo)
#include "app_state.h"
#include <ArduinoJson.h>

// DWD liefert Werte wie "0", "1-2", "2" oder "-1" (keine Daten)
static float parseDwdWert(const char* s) {
  if (!s || strlen(s) == 0) return -1.0f;
  String str(s); str.trim();
  if (str == "-1") return -1.0f;
  int dash = str.indexOf('-');
  if (dash > 0) {
    float a = str.substring(0, dash).toFloat();
    float b = str.substring(dash + 1).toFloat();
    return (a + b) / 2.0f;
  }
  return str.toFloat();
}

// ── DWD Pollenflug (Stufen heute / morgen / übermorgen, 8 Allergene) ─────────
void fetchDwdPollen() {
  spur("DWD-Pollen: Abruf");
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure sc;
  HTTPClient http;
  http.begin(sc, "https://opendata.dwd.de/climate_environment/health/alerts/s31fg.json");
  int httpCode = http.GET();
  if (httpCode != 200) {
    logHttpFehler("Pollen", httpCode, sc);
    http.end();
    return;
  }
  String body = http.getString();
  http.end();

  JsonDocument filter;
  filter["content"][0]["region_id"] = true;
  filter["content"][0]["Pollen"]    = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();
  if (err != DeserializationError::Ok) {
    logDiag("[Pollen] DWD JSON: %s", err.c_str());
    return;
  }

  for (JsonObject region : doc["content"].as<JsonArray>()) {
    if (region["region_id"].as<int>() != stdRegion()) continue;
    JsonObject p = region["Pollen"];
    LvLock lock;
    #define DWD(name, key) \
      pollen.dwd_##name       = parseDwdWert(p[key]["today"]        | ""); \
      pollen.dwd_##name##_tmr = parseDwdWert(p[key]["tomorrow"]     | ""); \
      pollen.dwd_##name##_da  = parseDwdWert(p[key]["dayafter_to"]  | "");
    DWD(birke, "Birke")      DWD(hasel, "Hasel")       DWD(erle, "Erle")       DWD(esche, "Esche")
    DWD(graeser, "Graeser")  DWD(roggen, "Roggen")     DWD(beifuss, "Beifuss") DWD(ambrosia, "Ambrosia")
    #undef DWD
    abrufOk(AB_DWD_POLLEN);
    return;
  }
  logDiag("[Pollen] DWD-Region %d nicht gefunden", stdRegion());
}

// ── Open-Meteo: Pollen stündlich + Luftqualität ──────────────────────────────
void fetchOpenMeteoPollen() {
  spur("Pollen/Luft: Abruf");
  if (WiFi.status() != WL_CONNECTED || stdLat() == 0.0f) return;
  WiFiClientSecure sc;
  HTTPClient http;
  String url = "https://air-quality-api.open-meteo.com/v1/air-quality";
  url += "?latitude="  + String(stdLat(), 4);
  url += "&longitude=" + String(stdLon(), 4);
  url += "&hourly=birch_pollen,grass_pollen,alder_pollen,mugwort_pollen,ragweed_pollen";
  url += ",european_aqi,pm2_5,pm10,nitrogen_dioxide,ozone";
  url += ",sulphur_dioxide,carbon_monoxide,dust";
  url += ",european_aqi_pm2_5,european_aqi_pm10,european_aqi_nitrogen_dioxide,european_aqi_ozone,european_aqi_sulphur_dioxide";
  url += "&timezone=auto&forecast_days=2";
  http.begin(sc, url);
  int code = http.GET();
  if (code != 200) {
    logHttpFehler("Luft", code, sc);
    http.end();
    return;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    logDiag("[Luft] JSON-Parse fehlgeschlagen");
    return;
  }
  body = String();

  struct tm ti;
  getLocalTime(&ti);
  int hNow = ti.tm_hour;
  int h    = min(hNow + 1, 47);   // nächste Stunde (läuft über Mitternacht in den 2. Tag)

  LvLock lock;
  JsonObject hr = doc["hourly"];
  pollen.birke    = hr["birch_pollen"][h].as<float>();
  pollen.graeser  = hr["grass_pollen"][h].as<float>();
  pollen.erle     = hr["alder_pollen"][h].as<float>();
  pollen.beifuss  = hr["mugwort_pollen"][h].as<float>();
  pollen.ambrosia = hr["ragweed_pollen"][h].as<float>();
  pollen.aqi       = hr["european_aqi"][hNow].as<int>();
  pollen.aqi_next  = hr["european_aqi"][h].as<int>();
  pollen.pm25      = hr["pm2_5"][hNow].as<float>();            pollen.pm25_next = hr["pm2_5"][h].as<float>();
  pollen.pm10      = hr["pm10"][hNow].as<float>();             pollen.pm10_next = hr["pm10"][h].as<float>();
  pollen.no2       = hr["nitrogen_dioxide"][hNow].as<float>(); pollen.no2_next  = hr["nitrogen_dioxide"][h].as<float>();
  pollen.o3        = hr["ozone"][hNow].as<float>();            pollen.o3_next   = hr["ozone"][h].as<float>();
  // Weitere Werte, Einzel-AQI (Leitschadstoff) und der Verlauf der naechsten 24 Stunden
  auto zahl = [&](const char* k, int i, float fehlt) { JsonVariant v = hr[k][i]; return v.isNull() ? fehlt : v.as<float>(); };
  pollen.so2   = zahl("sulphur_dioxide", hNow, -1);
  pollen.co    = zahl("carbon_monoxide", hNow, -1);
  pollen.staub = zahl("dust", hNow, -1);
  static const char* EINZEL[5] = {"european_aqi_pm2_5", "european_aqi_pm10", "european_aqi_nitrogen_dioxide", "european_aqi_ozone", "european_aqi_sulphur_dioxide"};
  for (int k = 0; k < 5; k++) pollen.idx[k] = (int)zahl(EINZEL[k], hNow, -1);
  pollen.aqi_h0 = hNow;
  for (int i = 0; i < 24; i++) {
    int slot = min(hNow + i, 47);
    pollen.aqi_h[i] = (int)zahl("european_aqi", slot, -1);
    float m = -1;
    static const char* PK[5] = {"birch_pollen", "grass_pollen", "alder_pollen", "mugwort_pollen", "ragweed_pollen"};
    for (int k = 0; k < 5; k++) { float v = zahl(PK[k], slot, -1); if (v > m) m = v; }
    pollen.pollen_h[i] = m;
  }
  for (int i = 0; i < 3; i++) {
    int slot = min(h + i, 47);
    pollen.h_slot[i]     = slot % 24;
    pollen.h_birke[i]    = hr["birch_pollen"][slot].as<float>();
    pollen.h_graeser[i]  = hr["grass_pollen"][slot].as<float>();
    pollen.h_erle[i]     = hr["alder_pollen"][slot].as<float>();
    pollen.h_beifuss[i]  = hr["mugwort_pollen"][slot].as<float>();
    pollen.h_ambrosia[i] = hr["ragweed_pollen"][slot].as<float>();
    // Roggen, Esche, Hasel liefert Open-Meteo nicht stündlich → DWD-Tageswert als Näherung
    pollen.h_roggen[i]   = pollen.dwd_roggen;
    pollen.h_esche[i]    = pollen.dwd_esche;
    pollen.h_hasel[i]    = pollen.dwd_hasel;
  }
  abrufOk(AB_POLLEN_OM);
}
