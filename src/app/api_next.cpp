// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// /api/next: Daten fuer WetterCube Next, siehe api_next.h. Das JSON entsteht mit snprintf in einem PSRAM-Puffer (der interne RAM ist knapp).
#include "api_next.h"
#include "app_state.h"
#include "app_config.h"
#include "lueften.h"
#include "../sensors/sensors.h"
#include "../platform/WebServer.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"

namespace {
const size_t BUF = 9000;
const char* const BIO_KAT[7] = {"Allg. Befinden", "Hypotonie", "Bluthochdruck", "Rheuma entz.", "Rheuma deg.", "Asthma", "Wärmebelast."};

struct Schreiber {
  char* b; size_t n, k = 0;
  void roh(const char* s) { size_t l = strlen(s); if (k + l + 1 < n) { memcpy(b + k, s, l); k += l; b[k] = 0; } }
  void f(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list a; va_start(a, fmt); int r = vsnprintf(b + k, n - k, fmt, a); va_end(a);
    if (r > 0 && k + (size_t)r < n) k += r;
  }
  void text(const char* t) {                      // JSON-String mit Escapes
    roh("\"");
    for (; *t; t++) {
      if (*t == '"' || *t == '\\') { char e[3] = {'\\', *t, 0}; roh(e); }
      else if ((unsigned char)*t < 0x20) roh(" ");
      else { char e[2] = {*t, 0}; roh(e); }
    }
    roh("\"");
  }
};

bool will(const char* liste, const char* name) {
  if (!liste || !liste[0]) return true;
  const size_t l = strlen(name);
  for (const char* p = liste; *p;) {
    const char* e = strchr(p, ',');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n == l && strncmp(p, name, l) == 0) return true;
    if (!e) break;
    p = e + 1;
  }
  return false;
}

float dwdWert(int i, int tag) {                  // DWD-Pollenflug (0-3) je Allergen in der Reihenfolge von POLLEN_ANZEIGE
  switch (i) {
    case 0: return tag == 0 ? pollen.dwd_hasel    : tag == 1 ? pollen.dwd_hasel_tmr    : pollen.dwd_hasel_da;
    case 1: return tag == 0 ? pollen.dwd_erle     : tag == 1 ? pollen.dwd_erle_tmr     : pollen.dwd_erle_da;
    case 2: return tag == 0 ? pollen.dwd_esche    : tag == 1 ? pollen.dwd_esche_tmr    : pollen.dwd_esche_da;
    case 3: return tag == 0 ? pollen.dwd_birke    : tag == 1 ? pollen.dwd_birke_tmr    : pollen.dwd_birke_da;
    case 4: return tag == 0 ? pollen.dwd_graeser  : tag == 1 ? pollen.dwd_graeser_tmr  : pollen.dwd_graeser_da;
    case 5: return tag == 0 ? pollen.dwd_roggen   : tag == 1 ? pollen.dwd_roggen_tmr   : pollen.dwd_roggen_da;
    case 6: return tag == 0 ? pollen.dwd_beifuss  : tag == 1 ? pollen.dwd_beifuss_tmr  : pollen.dwd_beifuss_da;
    default: return tag == 0 ? pollen.dwd_ambrosia : tag == 1 ? pollen.dwd_ambrosia_tmr : pollen.dwd_ambrosia_da;
  }
}

// Inhalt ohne Kopf (ohne Uhrzeit), damit die Pruefsumme nur von den Daten abhaengt
void inhalt(Schreiber& w, const char* bl) {
  w.f(",\"nacht\":%s", nachtJetzt() ? "true" : "false");   // Nachtmodus der Fusion: das Next kann ihm folgen
  if (will(bl, "wetter") && wetter.geladen) {
    w.f(",\"wetter\":{\"temp\":%.1f,\"gefuehlt\":%.1f,\"feuchte\":%.0f,\"druck\":%.0f,\"wind\":%.0f,\"grad\":%d,\"richtung\":", wetter.temp, wetter.feels_like,
        wetter.humidity, wetter.pressure, wetter.wind_speed, wetter.wind_dir);
    w.text(windRichtung(wetter.wind_dir));
    w.f(",\"wmo\":%d,\"tag\":%s,\"text\":", wetter.wmo_code, wetter.is_day ? "true" : "false");
    w.text(wetterText(wetter.wmo_code));
    w.f(",\"tmin\":%.0f,\"tmax\":%.0f,\"uv\":%.1f,\"uvmax\":%.1f,\"auf\":", wetter.temp_min, wetter.temp_max, wetter.uv_jetzt, wetter.uv_index);
    w.text(wetter.sunrise.c_str()); w.roh(",\"unter\":"); w.text(wetter.sunset.c_str());
    struct tm ti;
    if (getLocalTime(&ti, 0)) { float hell; int ph; mondPhase(time(nullptr), &hell, &ph); w.f(",\"mond\":{\"phase\":%d,\"hell\":%.0f,\"name\":", ph, hell); w.text(mondName(ph)); w.roh("}"); }
    w.roh("}");
  }
  if (will(bl, "vorhersage") && wetter.geladen) {
    w.roh(",\"vorhersage\":[");
    for (int i = 0; i < 4; i++) {
      w.f("%s{\"zeit\":", i ? "," : ""); w.text(wetter.time_forecast[i].c_str());
      w.f(",\"temp\":%.0f,\"wmo\":%d,\"tag\":%s,\"wind\":%.0f,\"richtung\":", wetter.temp_forecast[i], wetter.wmo_forecast[i], wetter.tag_forecast[i] ? "true" : "false", wetter.wind_speed_forecast[i]);
      w.text(windRichtung(wetter.wind_dir_forecast[i])); w.roh(",\"text\":"); w.text(wetterText(wetter.wmo_forecast[i])); w.roh("}");
    }
    w.roh("]");
  }
  if (will(bl, "pollen")) {                      // je Allergen: Name, gewaehlt, jetzt, heute, morgen, uebermorgen (Stufen 0-3, -1 = unbekannt)
    w.roh(",\"pollen\":[");
    for (int i = 0; i < 8; i++) {
      w.f("%s{\"n\":", i ? "," : ""); w.text(POLLEN_ANZEIGE[i]);
      w.f(",\"g\":%d,\"j\":%.1f,\"h\":%.1f,\"m\":%.1f,\"u\":%.1f}", pollenGewaehlt(i) ? 1 : 0, pollenWertWarnung(i), dwdWert(i, 0), dwdWert(i, 1), dwdWert(i, 2));
    }
    w.roh("]");
  }
  if (will(bl, "bio") && bio.geladen) {          // 5 Zeitraeume (heute vor-/nachmittags, morgen vor-/nachmittags, uebermorgen vormittags) x 7 Kategorien; 0 kein Einfluss, 1 positiv, 2 gering, 3 hoch
    w.roh(",\"bio\":{\"k\":[");
    for (int c = 0; c < 7; c++) { w.f("%s", c ? "," : ""); w.text(BIO_KAT[c]); }
    w.roh("],\"ok\":[");
    for (int p = 0; p < BIO_PERIODEN; p++) w.f("%s%d", p ? "," : "", bio.vorhanden[p] ? 1 : 0);
    w.roh("],\"p\":[");
    for (int p = 0; p < BIO_PERIODEN; p++) {
      w.f("%s[", p ? "," : "");
      for (int c = 0; c < 7; c++) w.f("%s%d", c ? "," : "", bio.wert[p][c]);
      w.roh("]");
    }
    w.f("],\"sel\":[%d,%d,%d,%d]}", cfg.ws_cat0, cfg.ws_cat1, cfg.ws_cat2, cfg.ws_cat3);   // Kategorien der Kurzuebersicht (Einstellung der Fusion)
  }
  if (will(bl, "luft") && pollen.aqi >= 0) {
    w.f(",\"luft\":{\"aqi\":%d,\"text\":", pollen.aqi); w.text(aqiStatusText(pollen.aqi));
    w.f(",\"pm25\":%.1f,\"pm10\":%.1f,\"o3\":%.0f,\"no2\":%.0f,\"so2\":%.0f,\"co\":%.0f,\"staub\":%.0f}", pollen.pm25, pollen.pm10, pollen.o3, pollen.no2, pollen.so2, pollen.co, pollen.staub);
  }
  if (will(bl, "sensoren")) {
    w.roh(",\"sensoren\":[");
    int idx[SENSOR_MAX]; const int n = sensorenSichtbar(idx, SENSOR_MAX); bool erste = true;
    const int64_t jetzt = esp_timer_get_time() / 1000;
    for (int k = 0; k < n; k++) {
      SensorCfg c; SensorWert v;
      if (!sensorenHole(idx[k], &c, &v) || !v.gueltig) continue;
      w.f("%s{\"id\":", erste ? "" : ","); erste = false;
      w.text(c.id); w.roh(",\"raum\":"); w.text(c.raum); w.roh(",\"name\":"); w.text(c.name); w.f(",\"wert\":%.*f,\"einheit\":", c.dezimal, v.wert); w.text(c.einheit);
      w.f(",\"ok\":%s}", (v.online && jetzt - v.zuletzt_ms < 15 * 60 * 1000) ? "true" : "false");
    }
    w.roh("]");
  }
  if (will(bl, "lueften")) {
    LueftenStatus ls = lueftenKopie();
    w.f(",\"lueften\":{\"z\":%d,\"kopf\":", (int)ls.zustand); w.text(ls.kopf); w.f(",\"warnung\":%s}", ls.warnAktiv ? "true" : "false");
  }
}

uint32_t hash(const char* s, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; } return h; }
}  // namespace

// Pruefsumme (ETag) der Daten ausserhalb des Kopfs; Rueckgabe: das ganze JSON
static std::string nextAufbauen(const char* bloecke, char* etag, size_t etagN) {
  char* b = (char*)heap_caps_malloc(BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!b) { etag[0] = 0; return "{}"; }
  Schreiber w{b, BUF}; b[0] = 0;
  inhalt(w, bloecke);
  snprintf(etag, etagN, "%08x", (unsigned)hash(b, w.k));
  time_t jetzt = 0; struct tm ti; if (getLocalTime(&ti, 0)) jetzt = time(nullptr);
  char kopf[160];
  snprintf(kopf, sizeof(kopf), "{\"v\":1,\"t\":%lld,\"tz\":\"CET-1CEST,M3.5.0,M10.5.0/3\",\"version\":\"%s\",\"etag\":\"%s\"", (long long)jetzt, FIRMWARE_VERSION, etag);
  std::string out = kopf;
  out.append(b, w.k);
  out += "}";
  heap_caps_free(b);
  return out;
}

std::string nextJson(const char* bloecke) { char e[12]; return nextAufbauen(bloecke, e, sizeof(e)); }

void apiNextRegistrieren(WebServer& server) {
  server.on("/api/next", HTTP_GET, [&server]() {
    String bl = server.arg("b");
    char etag[12]; std::string j = nextAufbauen(bl.c_str(), etag, sizeof(etag));
    // Unveraendert seit dem letzten Abruf (Parameter e oder Kopfzeile If-None-Match): nur 304 ohne Daten
    String e = server.arg("e"); if (e.length() == 0) e = server.header("If-None-Match");
    e.replace("\"", "");
    server.sendHeader("ETag", String("\"") + etag + "\"");
    server.sendHeader("Cache-Control", "no-cache");
    if (e.length() && strcmp(e.c_str(), etag) == 0) { server.send(304); return; }
    server.send(200, "application/json; charset=utf-8", j.c_str());
  });
}
