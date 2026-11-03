// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Frost-Waechter fuer Garten und Balkon (aus dem WetterCube Apex, im Fusion nur P4, Anzeige im Zusatzfeld): bewertet die naechsten Naechte (18 bis 8 Uhr) am aktiven Standort nach Luftfrost und Bodenfrost.
// Regeln wie auf draussenwetter.de: Luftfrost nach der Temperatur in 2 m (kein Frost, leichter Frost ab unter 0 Grad, Frost ab -2, strenger Frost ab -5);
// Bodenfrost, wenn die Nacht klar und ruhig ist (hoechstens 40 % Bewoelkung, hoechstens 12 km/h Wind im Mittel) und es in der Luft hoechstens 4 Grad
// kalt wird, oder wenn das Modell fuer die Bodenoberflaeche 0 Grad oder weniger rechnet. Daten: Open-Meteo (CC BY 4.0). Orientierung nach
// Gartenratgebern, keine Garantie: wie viel Frost eine Sorte vertraegt, haengt von Sorte, Standort und Abhaertung ab.
#include "app_state.h"
#if WC_P4   // nur P4
#include "ntfy.h"
#include "../sensors/sensors.h"
#include <ArduinoJson.h>
#include "esp_heap_caps.h"
#include <math.h>
#include <time.h>

// ArduinoJson-Speicher im PSRAM (die Antwort hat 96 Stunden mal 4 Werte)
namespace {
struct PsramJsonAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};
ArduinoJson::Allocator* psramJson() { static PsramJsonAllocator a; return &a; }
}

FrostDaten frost;

bool frostNoetig() { return cfg.zf_frost != 0 || ntfyQuelleAktiv(NTFY_Q_FROST); }

const char* frostStufeName(int stufe) {
  static const char* const N[5] = {"kein Frost", "Bodenfrost möglich", "leichter Frost", "Frost", "strenger Frost"};
  return N[stufe < 0 ? 0 : stufe > 4 ? 4 : stufe];
}

// Kurzer Rat je Pflanzengruppe (0 Sommerblumen und Gemuese, 1 Kuebelpflanzen, 2 winterharte Pflanzen im Topf) und Stufe
const char* frostRat(int gruppe, int stufe) {
  static const char* const R[3][5] = {
    {"nichts zu tun", "abdecken", "reinholen oder abdecken", "reinholen", "reinholen"},
    {"nichts zu tun", "empfindliche rein", "empfindliche rein, Rest schützen", "alle reinholen", "unbedingt rein"},
    {"nichts zu tun", "nichts zu tun", "nichts zu tun", "Ballen schützen", "Topf einpacken"},
  };
  return R[gruppe < 0 || gruppe > 2 ? 0 : gruppe][stufe < 0 ? 0 : stufe > 4 ? 4 : stufe];
}

static int stufeVon(float tmin, bool klar, bool hatBoden, float boden0) {
  if (tmin <= -5.0f) return 4;
  if (tmin <= -2.0f) return 3;
  if (tmin < 0.0f) return 2;
  if ((tmin <= 4.0f && klar) || (hatBoden && boden0 <= 0.0f)) return 1;
  return 0;
}

bool fetchFrost() {
  spur("Frost: Abruf");
  if (!frostNoetig() || stdLat() == 0.0f || WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure sc; HTTPClient http;
  http.begin(sc, "https://api.open-meteo.com/v1/forecast?latitude=" + String(stdLat(), 4) + "&longitude=" + String(stdLon(), 4) +
                 "&hourly=temperature_2m,cloud_cover,wind_speed_10m,soil_temperature_0cm&forecast_days=4&wind_speed_unit=kmh&timezone=auto");
  int code = http.GET();
  if (code != 200) { logHttpFehler("Frost", code, sc); http.end(); return false; }
  String body = http.getString(); http.end();
  JsonDocument doc(psramJson());
  if (deserializeJson(doc, body) != DeserializationError::Ok) { logDiag("[Frost] JSON-Fehler"); return false; }
  JsonObject h = doc["hourly"];
  JsonArray zeit = h["time"];
  const int n = zeit.size();
  struct tm jetzt; if (!getLocalTime(&jetzt, 0)) return false;
  const int ab = jetzt.tm_hour;   // die Reihe beginnt heute um 0 Uhr: Stunde ab jetzt
  if (n < ab + 12) { logDiag("[Frost] zu wenig Stunden (%d)", n); return false; }

  FrostDaten d;
  d.h0 = ab;
  d.nTemp = 0;
  for (int i = ab; i < n && d.nTemp < 24; i++) d.t24[d.nTemp++] = h["temperature_2m"][i] | 0.0f;

  struct Akku { int n = 0; float tmin = 99; int hmin = 0; float wolken = 0, wind = 0; int nw = 0, nwi = 0; float boden = 99; bool hatBoden = false; int jahr = 0, mon = 0, tag = 0; bool lauft = false; };
  Akku a; bool imNacht = false;
  auto abschluss = [&]() {
    if (a.n >= 3 && d.n < 3) {
      FrostNacht& f = d.nacht[d.n];
      const float mw = a.nw ? a.wolken / a.nw : 100.0f, mwi = a.nwi ? a.wind / a.nwi : 99.0f;
      f.klar = a.nw && a.nwi && mw <= 40.0f && mwi <= 12.0f;
      f.tmin = a.tmin; f.h = a.hmin; f.lauft = a.lauft;
      f.hatBoden = a.hatBoden; f.boden = a.hatBoden ? a.boden : 0.0f;
      f.wolken = (uint8_t)(mw > 100.0f ? 100 : lroundf(mw)); f.wind = (uint8_t)(mwi > 250.0f ? 250 : lroundf(mwi));
      f.stufe = (uint8_t)stufeVon(a.tmin, f.klar, a.hatBoden, a.boden);
      struct tm m = {}; m.tm_year = a.jahr - 1900; m.tm_mon = a.mon - 1; m.tm_mday = a.tag + (a.lauft ? 0 : 1); m.tm_hour = 12; mktime(&m);   // Wochentag des Morgens nach der Nacht
      static const char* const WT[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
      strlcpy(f.tag, WT[m.tm_wday], sizeof(f.tag));
      d.n++;
    }
    a = Akku(); imNacht = false;
  };
  for (int i = ab; i < n && d.n < 3; i++) {
    const char* t = zeit[i] | "";
    int J, M, T, H;
    if (sscanf(t, "%d-%d-%dT%d", &J, &M, &T, &H) != 4) continue;
    const bool nacht = H >= 18 || H < 8;
    if (!nacht) { if (imNacht) abschluss(); continue; }
    if (!imNacht) { imNacht = true; a.jahr = J; a.mon = M; a.tag = T; a.lauft = (i == ab && H < 8); }
    a.n++;
    const float tv = h["temperature_2m"][i] | 99.0f;
    if (tv < a.tmin) { a.tmin = tv; a.hmin = H; }
    if (!h["cloud_cover"][i].isNull()) { a.wolken += h["cloud_cover"][i].as<float>(); a.nw++; }
    if (!h["wind_speed_10m"][i].isNull()) { a.wind += h["wind_speed_10m"][i].as<float>(); a.nwi++; }
    if (!h["soil_temperature_0cm"][i].isNull()) { const float b = h["soil_temperature_0cm"][i].as<float>(); if (b < a.boden) a.boden = b; a.hatBoden = true; }
  }
  if (imNacht) abschluss();
  if (d.n == 0) { logDiag("[Frost] keine auswertbare Nacht"); return false; }
  // Eigener Aussensensor (abschaltbar): die Abweichung Messung minus Vorhersage der jetzigen Stunde gilt fuer die naechste Nacht, solange sie nah ist
  // (ab 12 Uhr oder schon im Gang), hoechstens 4 Grad. Laeuft die Nacht, ist der Tiefstwert mindestens so kalt wie die Messung.
  if (cfg.frost_sensor && d.nTemp > 0) {
    float tm; char nm[24];
    if (sensorenAussenTemp(&tm, nm, sizeof(nm), nullptr, 0, 60)) {
      FrostNacht& f = d.nacht[0];
      float k = tm - d.t24[0]; if (k > 4.0f) k = 4.0f; if (k < -4.0f) k = -4.0f;
      d.gemessen = true; d.tMess = tm; strlcpy(d.quelle, nm, sizeof(d.quelle));
      d.korr = (f.lauft || jetzt.tm_hour >= 12) ? k : 0.0f;
      d.tminModell = f.tmin;
      f.tmin += d.korr;
      if (f.lauft && tm < f.tmin) f.tmin = tm;
      f.stufe = (uint8_t)stufeVon(f.tmin, f.klar, f.hatBoden, f.boden);
      logDiag("[Frost] Aussensensor '%s' %.1f Grad (Vorhersage jetzt %.1f): Tiefstwert der Nacht %.1f statt %.1f", nm, tm, d.t24[0], f.tmin, d.tminModell);
    }
  }
  d.geladen = true;
  { LvLock lock; frost = d; }
  logDiag("[Frost] %d Naechte, erste: %s, Tiefstwert %.1f Grad um %02d Uhr, %s", d.n, frostStufeName(d.nacht[0].stufe), d.nacht[0].tmin, d.nacht[0].h, d.nacht[0].klar ? "klar und ruhig" : "bewoelkt oder windig");

  // Meldung aufs Handy: abends, wenn die kommende Nacht Frost oder Bodenfrost bringt (einmal je Abend)
  static int s_gemeldet = -1;
  if (ntfyQuelleAktiv(NTFY_Q_FROST) && jetzt.tm_hour >= 15 && jetzt.tm_hour < 23 && !d.nacht[0].lauft && d.nacht[0].stufe >= 1) {
    const int key = jetzt.tm_year * 400 + jetzt.tm_yday;
    if (key != s_gemeldet) {
      s_gemeldet = key;
      char titel[48], text[200];
      snprintf(titel, sizeof(titel), "Heute Nacht %s", d.nacht[0].stufe == 1 ? "Bodenfrost" : frostStufeName(d.nacht[0].stufe));
      snprintf(text, sizeof(text), "Tiefstwert %.0f Grad gegen %02d Uhr. Sommerblumen und Gemüse: %s. Kübelpflanzen: %s.", d.nacht[0].tmin, d.nacht[0].h, frostRat(0, d.nacht[0].stufe), frostRat(1, d.nacht[0].stufe));
      ntfySenden(titel, text, d.nacht[0].stufe >= 3 ? 4 : 3, "snowflake");
    }
  }
  return true;
}
#endif  // WC_P4
