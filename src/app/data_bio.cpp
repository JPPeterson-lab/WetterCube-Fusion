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

// Perioden der Datei in die fuenf Anzeige-Plaetze einordnen: 0 heute vm, 1 heute nm, 2 morgen vm, 3 morgen nm, 4 uebermorgen vm.
// Die Datei kommt einmal am Tag gegen 11 Uhr; bis dahin steht in "today_*" noch das Datum von gestern. Deshalb zaehlt das Datum je Periode (Feld "date"),
// nicht der Name, und Perioden aus der Vergangenheit fallen weg.
bool fetchBioWetter() {
  spur("Biowetter: Abruf");
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure sc;
  HTTPClient http;
  http.begin(sc, "https://opendata.dwd.de/climate_environment/health/alerts/biowetter.json");
  http.setTimeout(15000);
  int code = http.GET();
  if (code != 200) {
    logHttpFehler("Bio", code, sc);
    http.end();
    return false;
  }
  int erwartet = http.getSize();     // Content-Length (-1 = unbekannt/chunked)
  String body = http.getString();   // ca. 110 KB, liegt im PSRAM (String braucht dafuer BOARD_HAS_PSRAM)
  http.end();
  if (body.length() < 1000) { logDiag("[Bio] Antwort zu kurz: %u Bytes (erwartet %d)", (unsigned)body.length(), erwartet); return false; }

  struct P { const char* key; int vorTage; int haelfte; };   // vorTage: Tage nach dem Dateidatum laut Name, haelfte 0 = vormittag, 1 = nachmittag
  static const P perioden[] = {
    {"today_morning", 0, 0}, {"today_afternoon", 0, 1}, {"tomorrow_morning", 1, 0}, {"tomorrow_afternoon", 1, 1},
    {"dayafter_to_morning", 2, 0}, {"dayafter_to_afternoon", 2, 1}
  };
  JsonDocument filter;
  filter["zone"][0]["id"] = true;
  filter["last_update"] = true;
  for (auto& p : perioden) { filter["zone"][0][p.key]["effect"][0]["value"] = true; filter["zone"][0][p.key]["date"] = true; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();
  if (err != DeserializationError::Ok) {
    logDiag("[Bio] JSON-Fehler: %s", err.c_str());
    return false;
  }

  long heute = 0; const bool uhrOk = heuteTagNr(&heute);
  long dateiTag = 0; const bool dateiDatum = datumTagNr(doc["last_update"] | "", &dateiTag);
  bool aktuell = true; int veraltetTage = 0;
  for (JsonObject z : doc["zone"].as<JsonArray>()) {
    if (z["id"].as<String>() != stdZone()) continue;
    LvLock lock;
    for (int p = 0; p < BIO_PERIODEN; p++) { bio.vorhanden[p] = false; for (int k = 0; k < 7; k++) bio.wert[p][k] = 0; }
    int eingeordnet = 0;
    for (auto& per : perioden) {
      JsonObject o = z[per.key];
      JsonArray eff = o["effect"];
      if (o.isNull() || eff.size() < 7) continue;   // erst ab sieben Kategorien gilt die Periode als geliefert (auch "kein Einfluss" ueberall ist gueltig)
      long tag = 0; int versatz = per.vorTage;
      if (uhrOk && datumTagNr(o["date"] | "", &tag)) versatz = (int)(tag - heute);   // echter Abstand zu heute
      if (versatz < 0 || versatz > 2) continue;                                     // gestern oder zu weit voraus
      int platz = (versatz == 2) ? (per.haelfte == 0 ? 4 : -1) : versatz * 2 + per.haelfte;
      if (platz < 0) continue;
      bio.vorhanden[platz] = true;
      for (int k = 0; k < 7; k++) bio.wert[platz][k] = bioWertToInt(eff[k]["value"].as<const char*>());
      eingeordnet++;
    }
    bio.geladen = true;
    abrufOk(AB_BIO);
    if (uhrOk && dateiDatum) { veraltetTage = (int)(heute - dateiTag); aktuell = veraltetTage <= 0; }
    // Diagnose: je Platz "vorhanden/Anzahl Kategorien mit Einfluss"
    int n[BIO_PERIODEN] = {};
    for (int q = 0; q < BIO_PERIODEN; q++) for (int k = 0; k < 7; k++) if (bio.wert[q][k]) n[q]++;
    logDiag("[Bio] Zone %s, Datei vom %s%s (vorhanden/mit Einfluss): heute vm %d/%d nm %d/%d, morgen vm %d/%d nm %d/%d, uebermorgen vm %d/%d",
            stdZone().c_str(), (const char*)(doc["last_update"] | "?"), veraltetTage > 0 ? " = VERALTET, Daten nach Datum eingeordnet" : "",
            bio.vorhanden[0], n[0], bio.vorhanden[1], n[1], bio.vorhanden[2], n[2], bio.vorhanden[3], n[3], bio.vorhanden[4], n[4]);
    return aktuell;
  }
  logDiag("[Bio] Zone %s nicht gefunden", stdZone().c_str());
  return false;
}
