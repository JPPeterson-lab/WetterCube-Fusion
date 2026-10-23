// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// DWD-Biowetter (gesundheitliche Wettereinflüsse je Zone)
#include "app_state.h"
#include <ArduinoJson.h>

// 0=kein Einfluss 1=positiv 2=gering 3=hoch/stark
static int bioWertToInt(const char* v) {
  if (!v)                      return 0;
  if (strstr(v, "positiver"))  return 1;
  if (strstr(v, "geringe"))    return 2;
  if (strstr(v, "hohe"))       return 3;
  if (strstr(v, "schwache"))   return 2;   // Wärmebelastung schwach
  if (strstr(v, "mäßige"))     return 3;   // Wärmebelastung mäßig/stark
  return 0;
}

void fetchBioWetter() {
  spur("Biowetter: Abruf");
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure sc;
  HTTPClient http;
  http.begin(sc, "https://opendata.dwd.de/climate_environment/health/alerts/biowetter.json");
  http.setTimeout(15000);
  int code = http.GET();
  if (code != 200) {
    logHttpFehler("Bio", code, sc);
    http.end();
    return;
  }
  int erwartet = http.getSize();     // Content-Length (-1 = unbekannt/chunked)
  String body = http.getString();   // ca. 110 KB, liegt im PSRAM (String braucht dafuer BOARD_HAS_PSRAM)
  http.end();
  if (body.length() < 1000) { logDiag("[Bio] Antwort zu kurz: %u Bytes (erwartet %d)", (unsigned)body.length(), erwartet); return; }

  static const char* perioden[BIO_PERIODEN] = {
    "today_morning", "today_afternoon", "tomorrow_morning", "tomorrow_afternoon", "dayafter_to_morning"
  };
  JsonDocument filter;
  filter["zone"][0]["id"] = true;
  for (auto p : perioden) filter["zone"][0][p]["effect"][0]["value"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();
  if (err != DeserializationError::Ok) {
    logDiag("[Bio] JSON-Fehler: %s", err.c_str());
    return;
  }

  for (JsonObject z : doc["zone"].as<JsonArray>()) {
    if (z["id"].as<String>() != stdZone()) continue;
    LvLock lock;
    for (int p = 0; p < BIO_PERIODEN; p++) {
      JsonArray eff = z[perioden[p]]["effect"];
      bio.vorhanden[p] = eff.size() >= 7;   // erst ab sieben Kategorien gilt die Periode als geliefert (auch „kein Einfluss“ überall ist gültig)
      for (int k = 0; k < 7; k++)
        bio.wert[p][k] = (k < (int)eff.size()) ? bioWertToInt(eff[k]["value"].as<const char*>()) : 0;
    }
    bio.geladen = true;
    abrufOk(AB_BIO);
    // Diagnose: je Periode "vorhanden/Anzahl Kategorien mit Einfluss"
    int n[BIO_PERIODEN] = {};
    for (int q = 0; q < BIO_PERIODEN; q++) for (int k = 0; k < 7; k++) if (bio.wert[q][k]) n[q]++;
    logDiag("[Bio] Zone %s (vorhanden/mit Einfluss): heute vm %d/%d nm %d/%d, morgen vm %d/%d nm %d/%d, uebermorgen vm %d/%d",
            stdZone().c_str(), bio.vorhanden[0], n[0], bio.vorhanden[1], n[1], bio.vorhanden[2], n[2],
            bio.vorhanden[3], n[3], bio.vorhanden[4], n[4]);
    return;
  }
  logDiag("[Bio] Zone %s nicht gefunden", stdZone().c_str());
}
