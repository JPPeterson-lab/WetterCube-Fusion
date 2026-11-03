// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Open-Meteo: aktuelles Wetter, Vorhersage, Sonne, UV
#include "app_state.h"
#include <ArduinoJson.h>

bool fetchWetter() {
  spur("Wetter: Abruf");
  if (WiFi.status() != WL_CONNECTED || stdLat() == 0.0f) return false;
  WiFiClientSecure sc;
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast";
  url += "?latitude="  + String(stdLat(), 4);
  url += "&longitude=" + String(stdLon(), 4);
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature";
  url += ",wind_speed_10m,wind_direction_10m,surface_pressure,weather_code,is_day";
  url += "&daily=uv_index_max,sunrise,sunset,temperature_2m_min,temperature_2m_max";
  url += "&hourly=temperature_2m,weather_code,wind_speed_10m,wind_direction_10m,uv_index,precipitation,is_day";
  url += "&minutely_15=precipitation&forecast_minutely_15=12";   // Regen-Nowcast: nächste 3 Stunden
  url += "&timezone=auto&forecast_days=2";
  http.begin(sc, url);
  int httpCode = http.GET();
  if (httpCode != 200) {
    logHttpFehler("Wetter", httpCode, sc);
    if (httpCode > 0) {   // Anfang der Server-Antwort ins Log (z. B. bei 503)
      String antwort = http.getString(); antwort.replace("\n", " ");
      logDiag("[Wetter] Antwort: %s", antwort.substring(0, 60).c_str());
    }
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError jsonErr = deserializeJson(doc, body);
  body = String();
  if (jsonErr != DeserializationError::Ok) {
    logDiag("[Wetter] JSON-Parse fehlgeschlagen: %s (freeHeap=%u)", jsonErr.c_str(), (unsigned)ESP.getFreeHeap());
    return false;
  }

  struct tm ti;
  getLocalTime(&ti);
  int h = ti.tm_hour;

  LvLock lock;   // Daten werden von der UI gelesen
  auto c = doc["current"];
  wetter.temp       = c["temperature_2m"].as<float>();
  wetter.feels_like = c["apparent_temperature"].as<float>();
  wetter.humidity   = c["relative_humidity_2m"].as<float>();
  wetter.wind_speed = c["wind_speed_10m"].as<float>();
  wetter.wind_dir   = c["wind_direction_10m"].as<int>();
  wetter.pressure   = c["surface_pressure"].as<float>();
  wetter.wmo_code   = c["weather_code"].as<int>();
  wetter.is_day     = c["is_day"].as<int>() != 0;
  wetter.uv_index   = doc["daily"]["uv_index_max"][0].as<float>();
  { JsonVariantConst uj = doc["hourly"]["uv_index"][min(h, 47)]; wetter.uv_jetzt = uj.isNull() ? 0.0f : uj.as<float>(); }
  wetter.temp_min   = doc["daily"]["temperature_2m_min"][0].as<float>();
  wetter.temp_max   = doc["daily"]["temperature_2m_max"][0].as<float>();
  String sr = doc["daily"]["sunrise"][0].as<String>();
  String ss = doc["daily"]["sunset"][0].as<String>();
  wetter.sunrise = sr.length() >= 16 ? sr.substring(11, 16) : sr;
  wetter.sunset  = ss.length() >= 16 ? ss.substring(11, 16) : ss;

  // 4-Stunden-Vorhersage
  for (int i = 0; i < 4; i++) {
    int idx = h + 1 + i;
    wetter.temp_forecast[i]       = doc["hourly"]["temperature_2m"][idx].as<float>();
    wetter.wmo_forecast[i]        = doc["hourly"]["weather_code"][idx].as<int>();
    wetter.tag_forecast[i]        = doc["hourly"]["is_day"][idx].isNull() ? true : doc["hourly"]["is_day"][idx].as<int>() != 0;   // Symbol je Stunde fuer Tag oder Nacht
    wetter.wind_speed_forecast[i] = doc["hourly"]["wind_speed_10m"][idx].as<float>();
    wetter.wind_dir_forecast[i]   = doc["hourly"]["wind_direction_10m"][idx].as<int>();
    String t = doc["hourly"]["time"][idx].as<String>();
    wetter.time_forecast[i] = t.length() >= 16 ? t.substring(11, 16) : t;
  }
  // UV-Maximum der nächsten 3 Stunden
  float uvMax = 0.0f;
  for (int i = 1; i <= 3; i++) {
    float v = doc["hourly"]["uv_index"][min(h + i, 47)].as<float>();
    if (v > uvMax) uvMax = v;
  }
  wetter.uv_next3h = uvMax;
  // UV-Hinweis (jetzt bis +3 h) und Frost (jetzt bis +12 h); fehlende Werte nicht als 0 werten
  float uvH = 0.0f;
  for (int i = 0; i <= 3; i++) {
    JsonVariantConst v = doc["hourly"]["uv_index"][min(h + i, 47)];
    if (!v.isNull() && v.as<float>() > uvH) uvH = v.as<float>();
  }
  wetter.uv_hinweis = uvH;
  float fMin = 99.0f; int frostIdx = -1;
  for (int i = 0; i <= 12 && h + i < 48; i++) {
    JsonVariantConst v = doc["hourly"]["temperature_2m"][h + i];
    if (v.isNull()) continue;
    float tv = v.as<float>();
    if (tv < fMin) fMin = tv;
    if (frostIdx < 0 && tv <= 0.0f) frostIdx = i;
  }
  // Temperaturverlauf (jetzt bis +24 h)
  {
    int n = 0;
    for (int i = 0; i <= 24 && h + i < 48; i++) {
      JsonVariantConst vt = doc["hourly"]["temperature_2m"][h + i];
      if (vt.isNull()) break;
      wetter.vl_temp[n] = vt.as<float>();
      JsonVariantConst vr = doc["hourly"]["precipitation"][h + i];
      wetter.vl_regen[n] = vr.isNull() ? 0.0f : vr.as<float>();
      n++;
    }
    wetter.vl_n  = n;
    wetter.vl_h0 = h;
  }
  wetter.frost_min   = fMin;
  wetter.frost_jetzt = (frostIdx == 0);
  wetter.frost_ab    = "";
  if (frostIdx > 0) {
    String ft = doc["hourly"]["time"][h + frostIdx].as<String>();
    if (ft.length() >= 16) wetter.frost_ab = ft.substring(11, 16);
  }
  // Regen-Nowcast: 15-Minuten-Slots der nächsten 3 Stunden (Zeit in lokaler Zeit, TZ ist gesetzt)
  JsonArray ncT = doc["minutely_15"]["time"].as<JsonArray>();
  JsonArray ncP = doc["minutely_15"]["precipitation"].as<JsonArray>();
  int ncN = 0;
  for (size_t i = 0; i < ncT.size() && i < ncP.size() && ncN < WetterDaten::NC_MAX; i++) {
    int y, mo, d, hh, mi;
    if (sscanf(ncT[i] | "", "%d-%d-%dT%d:%d", &y, &mo, &d, &hh, &mi) != 5) continue;
    struct tm t = {};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = hh; t.tm_min = mi; t.tm_isdst = -1;
    wetter.nc_start[ncN] = mktime(&t);
    wetter.nc_mm[ncN]    = ncP[i].as<float>();
    ncN++;
  }
  wetter.nc_n = ncN;
  wetter.geladen = true;

  // Erholung nach Ausfall protokollieren (Erfolge nicht jedes Mal, sonst läuft der Ringpuffer über)
  if (letzterWetterFetchErfolg != 0 && millis() - letzterWetterFetchErfolg > 900000UL)
    logDiag("[Wetter] Fetch erfolgreich nach Unterbrechung (freeHeap=%u)", (unsigned)ESP.getFreeHeap());
  letzterWetterFetchErfolg = millis();
  abrufOk(AB_WETTER);
  return true;
}
