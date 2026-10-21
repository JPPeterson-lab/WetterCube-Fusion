// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Verlaufsdaten für die Diagramme: Ringspeicher je Messreihe (7 Tage, ein Wert alle 10 Minuten), Außenwerte von Open-Meteo
// und 30-Tage-Auswertung. Läuft im Netzwerk-Task; die Oberfläche liest nur über historieHole().
#include "historie.h"
#include "app_state.h"
#include "../sensors/sensors.h"
#include "../storage/storage.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <math.h>
#include <string.h>

#define RING        1008          // 7 Tage zu je 144 Werten
#define SLOT_SEK    600           // 10 Minuten
#define MAX_REIHEN  24
#define GUELTIG_AB  1700000000    // Uhr ist gestellt (Ende 2023)

struct Reihe { char id[24]; float* v; int64_t letzter; };   // letzter = zuletzt belegter Slot (-1 = leer)
static Reihe s_r[MAX_REIHEN];
static int   s_nr = 0;

static Reihe* finde(const char* id, bool anlegen) {
  for (int i = 0; i < s_nr; i++) if (strcmp(s_r[i].id, id) == 0) return &s_r[i];
  if (!anlegen || s_nr >= MAX_REIHEN) return nullptr;
  float* v = (float*)heap_caps_malloc(RING * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!v) return nullptr;
  for (int i = 0; i < RING; i++) v[i] = NAN;
  Reihe& r = s_r[s_nr++];
  strlcpy(r.id, id, sizeof(r.id)); r.v = v; r.letzter = -1;
  return &r;
}

static float lese(const Reihe* r, int64_t slot) {
  if (r->letzter < 0 || slot > r->letzter || slot <= r->letzter - RING) return NAN;
  return r->v[slot % RING];
}

// nurLeer: vorhandene Werte (zum Beispiel selbst gemessene) nicht durch nachgeladene ersetzen
static void schreibe(Reihe* r, int64_t slot, float w, bool nurLeer) {
  if (!r || isnan(w)) return;
  if (r->letzter < 0) r->letzter = slot;
  else if (slot > r->letzter) {
    int64_t von = r->letzter + 1;
    if (slot - von >= RING) von = slot - RING + 1;
    for (int64_t s = von; s < slot; s++) r->v[s % RING] = NAN;     // Lücke leeren, sonst stünden dort Werte von vor einer Woche
    r->letzter = slot;
  } else if (slot <= r->letzter - RING) return;
  float& z = r->v[slot % RING];
  if (nurLeer && !isnan(z)) return;
  z = w;
}

static int aussenIndex(const char* id) {
  if (!strcmp(id, "aussen_temp")) return 0;
  if (!strcmp(id, "aussen_feuchte")) return 1;
  if (!strcmp(id, "aussen_druck")) return 2;
  return -1;
}
static const char* AUSSEN_ID[3] = { "aussen_temp", "aussen_feuchte", "aussen_druck" };

// ── Messwerte sammeln (alle 10 Minuten) ──────────────────────────────────────
static void proben(int64_t slot) {
  const int64_t jetztMs = esp_timer_get_time() / 1000;
  for (int i = 0; i < sensorenAnzahl(); i++) {
    SensorCfg c; SensorWert w;
    if (!sensorenHole(i, &c, &w) || !w.gueltig) continue;
    if (jetztMs - w.zuletzt_ms > 15 * 60 * 1000) continue;         // veraltete Werte nicht als aktuell eintragen
    schreibe(finde(c.id, true), slot, w.wert, false);
  }
  if (wetter.geladen) {
    schreibe(finde("aussen_temp", true), slot, wetter.temp, false);
    schreibe(finde("aussen_feuchte", true), slot, wetter.humidity, false);
    schreibe(finde("aussen_druck", true), slot, wetter.pressure, false);
  }
}

// ── Außenwerte der letzten Tage von Open-Meteo (Stundenwerte) ───────────────
struct Std { int n = 0; int64_t* z = nullptr; float* v[3] = {nullptr, nullptr, nullptr}; };
static void stdFrei(Std& s) { heap_caps_free(s.z); for (float* p : s.v) heap_caps_free(p); s = Std(); }

static bool aussenAbruf(int tage, Std& out) {
  spur("Historie: Abruf");
  if (WiFi.status() != WL_CONNECTED || cfg.lat == 0.0f) return false;
  WiFiClientSecure sc;
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast";
  url += "?latitude="  + String(cfg.lat, 4);
  url += "&longitude=" + String(cfg.lon, 4);
  url += "&hourly=temperature_2m,relative_humidity_2m,surface_pressure";
  url += "&past_days=" + String(tage) + "&forecast_days=1&timeformat=unixtime";
  http.begin(sc, url);
  int code = http.GET();
  if (code != 200) { logHttpFehler("Historie", code, sc); http.end(); return false; }
  String body = http.getString();
  http.end();
  JsonDocument filter;
  filter["hourly"]["time"] = true; filter["hourly"]["temperature_2m"] = true;
  filter["hourly"]["relative_humidity_2m"] = true; filter["hourly"]["surface_pressure"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok) { logDiag("[Historie] JSON-Parse fehlgeschlagen"); return false; }
  body = String();
  JsonObject hr = doc["hourly"];
  JsonArray zeit = hr["time"];
  const int n = zeit.size();
  if (n < 2) return false;
  out.z = (int64_t*)heap_caps_malloc(n * sizeof(int64_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  for (int k = 0; k < 3; k++) out.v[k] = (float*)heap_caps_malloc(n * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!out.z || !out.v[0] || !out.v[1] || !out.v[2]) { stdFrei(out); return false; }
  static const char* feld[3] = { "temperature_2m", "relative_humidity_2m", "surface_pressure" };
  const time_t jetzt = time(nullptr);
  int m = 0;
  for (int i = 0; i < n; i++) {
    int64_t t = zeit[i].as<int64_t>();
    if (t > jetzt) break;                                         // nur Vergangenheit, die Vorhersage von heute Abend nicht
    out.z[m] = t;
    for (int k = 0; k < 3; k++) { JsonVariant v = hr[feld[k]][i]; out.v[k][m] = v.isNull() ? NAN : v.as<float>(); }
    m++;
  }
  out.n = m;
  if (m < 2) { stdFrei(out); return false; }
  return true;
}

// Stundenwerte linear auf 10-Minuten-Plätze verteilen, damit 24-Stunden-Ansicht und Messwerte zusammenpassen
static void ringAusStd(const Std& s) {
  const time_t jetzt = time(nullptr);
  for (int k = 0; k < 3; k++) {
    Reihe* r = finde(AUSSEN_ID[k], true);
    if (!r) continue;
    for (int i = 0; i < s.n; i++) {
      const float v0 = s.v[k][i], v1 = i + 1 < s.n ? s.v[k][i + 1] : NAN;
      for (int j = 0; j < 6; j++) {
        const int64_t slot = s.z[i] / SLOT_SEK + j;
        if (slot * SLOT_SEK > jetzt) break;
        const float w = isnan(v0) ? NAN : (isnan(v1) ? v0 : v0 + (v1 - v0) * j / 6.0f);
        schreibe(r, slot, w, true);
      }
    }
  }
}

// ── 30 Tage ──────────────────────────────────────────────────────────────────
static float*    s_h30 = nullptr;                     // Stundenwerte, HIST_MAX_PUNKTE Stück
static int64_t   s_h30h0 = 0;                         // erste Stunde (Stundenzahl seit 1970)
static char      s_h30id[24] = "";
static volatile int s_h30state = H30_LEER;
static unsigned long s_h30zeit = 0;
static char      s_anfId[24] = "";
static volatile bool s_anf = false;
static char      s_jobId[24] = "";
static bool      s_h30job = false;
static int       s_jobTag = 0;
static float*    s_sum = nullptr;
static uint16_t* s_cnt = nullptr;

void historie30Anfordern(const char* id) {
  if (!id || !id[0]) return;
  if (s_h30state == H30_FERTIG && !strcmp(s_h30id, id) && millis() - s_h30zeit < 30UL * 60 * 1000) return;
  strlcpy(s_anfId, id, sizeof(s_anfId));
  s_h30state = H30_LAEDT;
  s_anf = true;
}
int historie30Status() { return s_h30state; }

static void h30Fertig(bool ok) {
  if (s_sum)  { heap_caps_free(s_sum);  s_sum = nullptr; }
  if (s_cnt)  { heap_caps_free(s_cnt);  s_cnt = nullptr; }
  s_h30job = false;
  if (ok) { strlcpy(s_h30id, s_jobId, sizeof(s_h30id)); s_h30zeit = millis(); s_h30state = H30_FERTIG; }
  else s_h30state = H30_FEHLT;
}

static void cbH30(time_t t, const char* id, float w, void*) {
  if (strcmp(id, s_jobId) != 0 || !s_sum) return;
  const int64_t i = (int64_t)(t / 3600) - s_h30h0;
  if (i < 0 || i >= HIST_MAX_PUNKTE) return;
  s_sum[i] += w; s_cnt[i]++;
}

static void tagDatum(int tageZurueck, int* j, int* m, int* d) {
  time_t t = time(nullptr) - (time_t)tageZurueck * 86400;
  struct tm ti; localtime_r(&t, &ti);
  *j = ti.tm_year + 1900; *m = ti.tm_mon + 1; *d = ti.tm_mday;
}

static void h30Starten() {
  strlcpy(s_jobId, s_anfId, sizeof(s_jobId));
  if (!s_h30) s_h30 = (float*)heap_caps_malloc(HIST_MAX_PUNKTE * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_h30) { s_h30state = H30_FEHLT; return; }
  s_h30h0 = (int64_t)(time(nullptr) / 3600) - (HIST_MAX_PUNKTE - 1);
  for (int i = 0; i < HIST_MAX_PUNKTE; i++) s_h30[i] = NAN;
  const int ai = aussenIndex(s_jobId);
  if (ai >= 0) {                                                  // Außenwerte: direkt von Open-Meteo
    Std s;
    if (!aussenAbruf(30, s)) { h30Fertig(false); return; }
    for (int i = 0; i < s.n; i++) {
      const int64_t k = s.z[i] / 3600 - s_h30h0;
      if (k >= 0 && k < HIST_MAX_PUNKTE) s_h30[k] = s.v[ai][i];
    }
    stdFrei(s);
    h30Fertig(true);
    return;
  }
  if (!speicherBereit()) { h30Fertig(false); return; }            // Sensoren: nur mit Speicherkarte
  s_sum = (float*)heap_caps_calloc(HIST_MAX_PUNKTE, sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_cnt = (uint16_t*)heap_caps_calloc(HIST_MAX_PUNKTE, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_sum || !s_cnt) { h30Fertig(false); return; }
  s_h30job = true; s_jobTag = 29;
}

static void h30Schritt() {                                        // ein Tag je Aufruf, damit der Netzwerk-Task nicht lange stockt
  int j, m, d; tagDatum(s_jobTag, &j, &m, &d);
  speicherVerlaufTag(j, m, d, cbH30, nullptr);
  if (--s_jobTag >= 0) return;
  for (int i = 0; i < HIST_MAX_PUNKTE; i++) s_h30[i] = s_cnt[i] ? s_sum[i] / s_cnt[i] : NAN;
  h30Fertig(true);
}

// ── Beim Start: 7 Tage aus der Speicherkarte und von Open-Meteo einlesen ─────
static bool s_preFertig = false;
static int  s_preTag = 6;

static void cbRing(time_t t, const char* id, float w, void*) {
  if (sensorenSuche(id) < 0 && aussenIndex(id) < 0) return;      // entfernte Sensoren nicht wieder anlegen
  schreibe(finde(id, true), t / SLOT_SEK, w, true);
}

static void preSchritt() {
  int j, m, d; tagDatum(s_preTag, &j, &m, &d);
  speicherVerlaufTag(j, m, d, cbRing, nullptr);
  if (--s_preTag < 0) { s_preFertig = true; logDiag("[Historie] 7 Tage von der Karte gelesen, %d Messreihen", s_nr); }
}

// ── Takt ─────────────────────────────────────────────────────────────────────
void historieTick() {
  const time_t jetzt = time(nullptr);
  if (jetzt < GUELTIG_AB) return;                                 // ohne gültige Uhr keine Zeitachse
  static int64_t letzterSlot = -1;
  const int64_t slot = jetzt / SLOT_SEK;
  if (slot != letzterSlot) { letzterSlot = slot; proben(slot); }

  if (s_anf && !s_h30job) { s_anf = false; h30Starten(); return; }
  if (s_h30job) { h30Schritt(); return; }
  if (!s_preFertig && speicherBereit()) { preSchritt(); return; }

  static unsigned long tAussen = 0; static bool aussenOk = false, aussenVersucht = false;
  const unsigned long warte = aussenOk ? 3UL * 3600 * 1000 : 5UL * 60 * 1000;
  if ((!aussenVersucht || millis() - tAussen >= warte) && wetter.geladen && WiFi.status() == WL_CONNECTED && cfg.lat != 0.0f) {
    aussenVersucht = true; tAussen = millis();
    Std s;
    aussenOk = aussenAbruf(7, s);
    if (aussenOk) { ringAusStd(s); stdFrei(s); logDiag("[Historie] Außenwerte der letzten 7 Tage geladen"); }
  }
}

// ── Lesen für die Oberfläche ─────────────────────────────────────────────────
int historieHole(const char* id, int bereich, float* out, int max, time_t* t0, int* schrittSek) {
  const time_t jetzt = time(nullptr);
  if (!id || jetzt < GUELTIG_AB) return 0;
  int n = 0;
  if (bereich == HB_30D) {
    if (s_h30state != H30_FERTIG || strcmp(s_h30id, id) != 0 || !s_h30) return 0;
    n = max < HIST_MAX_PUNKTE ? max : HIST_MAX_PUNKTE;
    for (int i = 0; i < n; i++) out[i] = s_h30[HIST_MAX_PUNKTE - n + i];
    *t0 = (time_t)((s_h30h0 + (HIST_MAX_PUNKTE - n)) * 3600); *schrittSek = 3600;
  } else {
    Reihe* r = finde(id, false);
    if (!r) return 0;
    const int64_t slot = jetzt / SLOT_SEK;
    if (bereich == HB_24H) {
      n = max < 145 ? max : 145;
      for (int k = 0; k < n; k++) out[k] = lese(r, slot - (n - 1) + k);
      *t0 = (time_t)((slot - (n - 1)) * SLOT_SEK); *schrittSek = SLOT_SEK;
    } else {
      n = max < 169 ? max : 169;
      const int64_t stunde = jetzt / 3600;
      for (int h = 0; h < n; h++) {
        float s = 0; int c = 0;
        for (int j = 0; j < 6; j++) { float v = lese(r, (stunde - (n - 1) + h) * 6 + j); if (!isnan(v)) { s += v; c++; } }
        out[h] = c ? s / c : NAN;
      }
      *t0 = (time_t)((stunde - (n - 1)) * 3600); *schrittSek = 3600;
    }
  }
  int gueltig = 0;
  for (int i = 0; i < n; i++) if (!isnan(out[i])) gueltig++;
  return gueltig >= 2 ? n : 0;
}
