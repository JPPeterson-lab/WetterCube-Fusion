// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Sensor-Grenzwerte: Meldungen im Log, aufs Handy (ntfy) und in der Hinweis-Kachel des Hauptscreens.
// Die Pruefung selbst liegt in sensors.cpp; hier hangen nur die Folgen daran.
#include "app_state.h"
#include "ntfy.h"
#include "../sensors/sensors.h"

// Gegen Flattern: ein Sensor meldet aufs Handy hoechstens alle 30 Minuten einen neuen Alarm
static const uint32_t NTFY_MIN_ABSTAND_MS = 30UL * 60UL * 1000UL;
struct Merker { uint32_t idHash; uint32_t letzterMs; bool gesendet; };
static Merker s_merker[SENSOR_MAX];

static uint32_t hashId(const char* s) { uint32_t h = 2166136261u; for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; } return h ? h : 1; }

static Merker* merkerFuer(const char* id) {
  uint32_t h = hashId(id);
  Merker* frei = nullptr;
  for (auto& m : s_merker) { if (m.idHash == h) return &m; if (!m.idHash && !frei) frei = &m; }
  if (!frei) frei = &s_merker[0];
  *frei = {h, 0, false};
  return frei;
}

// "Küche Luftfeuchtigkeit" (Raum und Name) bzw. nur der Name
void sensorBezeichnung(const SensorCfg& c, char* out, size_t n) {
  if (c.raum[0]) snprintf(out, n, "%s %s", c.raum, c.name); else snprintf(out, n, "%s", c.name);
}

static void ereignis(const SensorCfg& c, const SensorWert& w, bool ein) {
  char wer[48]; sensorBezeichnung(c, wer, sizeof(wer));
  char wert[24]; snprintf(wert, sizeof(wert), "%.*f %s", c.dezimal, w.wert, c.einheit);
  char grenze[40];
  if (w.alarm == 2 || (!ein && (c.grenze_flags & 2) && !(c.grenze_flags & 1))) snprintf(grenze, sizeof(grenze), "über %.*f %s", c.dezimal, c.grenze_max, c.einheit);
  else                                                                          snprintf(grenze, sizeof(grenze), "unter %.*f %s", c.dezimal, c.grenze_min, c.einheit);
  Merker* m = merkerFuer(c.id);
  if (ein) logDiag("[Sensor] Alarm: %s %s (Grenze %s)", wer, wert, grenze);
  else     logDiag("[Sensor] Entwarnung: %s wieder %s", wer, wert);
  aktualisiereHinweise();                                   // Hinweis-Kachel sofort anpassen

  if (!ntfyQuelleAktiv(NTFY_Q_SENSOR)) return;
  char titel[72], text[200];
  uint32_t jetzt = (uint32_t)(esp_timer_get_time() / 1000);
  if (ein) {
    if (m->letzterMs && jetzt - m->letzterMs < NTFY_MIN_ABSTAND_MS) { m->gesendet = false; return; }   // zu kurz nach dem letzten Alarm
    snprintf(titel, sizeof(titel), "Sensor-Alarm: %s", wer);
    snprintf(text, sizeof(text), "%s: %s (Grenze: %s)", c.name, wert, grenze);
    ntfySenden(titel, text, 4, "warning");
    m->letzterMs = jetzt ? jetzt : 1; m->gesendet = true;
  } else if (cfg.ntfy_entwarnung && m->gesendet) {
    snprintf(titel, sizeof(titel), "Entwarnung: %s", wer);
    snprintf(text, sizeof(text), "%s ist wieder im normalen Bereich: %s", c.name, wert);
    ntfySenden(titel, text, 2, "white_check_mark");
    m->gesendet = false;
  }
}

void sensorAlarmInit() { sensorenSetzeAlarmHook(ereignis); }

// Text fuer die Hinweis-Kachel: erster Sensor mit Alarm, bei mehreren "(+n)"
#if WC_P4
// ── Nur P4: stille Sensoren und gemessener Frost (einmal je Minute) ──────────
// Still: ein MQTT-Sensor hat seit cfg.sensor_still_min Minuten keinen Wert geliefert (oder seit dem Start noch nie).
struct Still { uint32_t h; bool still; bool gemeldet; };
static Still s_still[SENSOR_MAX];
static int s_stillIdx[SENSOR_MAX];
static int s_stillN = 0;
static bool s_frostGemessen = false; static float s_frostT = 0; static char s_frostName[24] = "";

static void stillPruefen() {
  const int64_t jetzt = esp_timer_get_time() / 1000;
  const int64_t grenze = (int64_t)cfg.sensor_still_min * 60000;
  Still neu[SENSOR_MAX] = {}; int nNeu = 0, nl = 0; int liste[SENSOR_MAX];
  bool geaendert = false;
  for (int i = 0; i < sensorenAnzahl() && nNeu < SENSOR_MAX; i++) {
    SensorCfg c; SensorWert w;
    if (!sensorenHole(i, &c, &w) || c.quelle != SQ_MQTT) continue;   // nur MQTT (der Fusion liest Home Assistant nicht direkt)
    const uint32_t h = hashId(c.id);
    const bool still = cfg.sensor_still_min > 0 && (w.gueltig ? jetzt - w.zuletzt_ms > grenze : jetzt > grenze);
    Still alt = {h, false, false};
    for (const auto& s : s_still) if (s.h == h) { alt = s; break; }
    Still& n = neu[nNeu++]; n = alt;
    if (still) liste[nl++] = i;
    if (still == alt.still) continue;
    n.still = still; geaendert = true;
    char wer[48]; sensorBezeichnung(c, wer, sizeof(wer));
    char titel[72], text[200];
    if (still) {
      if (w.gueltig) snprintf(text, sizeof(text), "%s liefert seit %d Minuten keinen Wert.", wer, (int)((jetzt - w.zuletzt_ms) / 60000));
      else           snprintf(text, sizeof(text), "%s hat seit dem Start noch keinen Wert geliefert.", wer);
      logDiag("[Sensor] Still: %s", text);
      if (ntfyQuelleAktiv(NTFY_Q_STILL)) { snprintf(titel, sizeof(titel), "Sensor still: %s", wer); ntfySenden(titel, text, 3, "electric_plug"); n.gemeldet = true; }
    } else {
      logDiag("[Sensor] %s liefert wieder Werte", wer);
      if (n.gemeldet && cfg.ntfy_entwarnung && ntfyQuelleAktiv(NTFY_Q_STILL)) { snprintf(titel, sizeof(titel), "Sensor wieder da: %s", wer); snprintf(text, sizeof(text), "%s liefert wieder Werte.", wer); ntfySenden(titel, text, 2, "white_check_mark"); }
      n.gemeldet = false;
    }
  }
  if (nl != s_stillN) geaendert = true;
  memcpy(s_still, neu, sizeof(s_still));
  memcpy(s_stillIdx, liste, sizeof(int) * nl); s_stillN = nl;
  if (geaendert) aktualisiereHinweise();
}

// Gemessener Frost: Aussensensor bei hoechstens 0,5 Grad. Hinweis-Kachel, abends und nachts einmal je Nacht aufs Handy (Quelle Frost).
static void frostMessungPruefen() {
  const bool vorher = s_frostGemessen;
  float t; char nm[24]; struct tm l;
  s_frostGemessen = cfg.frost_sensor && sensorenAussenTemp(&t, nm, sizeof(nm), nullptr, 0, 60) && t <= 0.5f && getLocalTime(&l, 0);
  if (s_frostGemessen) {
    s_frostT = t; strlcpy(s_frostName, nm, sizeof(s_frostName));
    static int s_nacht = -1;
    const int nacht = l.tm_year * 400 + l.tm_yday - (l.tm_hour < 12 ? 1 : 0);
    if ((l.tm_hour >= 16 || l.tm_hour < 9) && nacht != s_nacht) {
      s_nacht = nacht;
      logDiag("[Frost] Gemessen: %s %.1f Grad", nm, t);
      if (ntfyQuelleAktiv(NTFY_Q_FROST)) {
        char text[200]; snprintf(text, sizeof(text), "%s misst %.1f °C. Sommerblumen, Gemüse und Kübelpflanzen jetzt reinholen oder abdecken.", nm, t);
        for (char* p = text; *p; p++) if (*p == '.' && p[1] >= '0' && p[1] <= '9') *p = ',';
        ntfySenden("Frost gemessen: Pflanzen rein", text, 4, "snowflake");
      }
    }
  }
  if (s_frostGemessen != vorher) aktualisiereHinweise();
}

void sensorUeberwachungTick() {
  static unsigned long t = 0;
  if (t && millis() - t < 60000UL) return;
  t = millis() ? millis() : 1;
  stillPruefen();
  frostMessungPruefen();
}

bool hinweisFrostGemessen(char* buf, size_t n) {
  if (!s_frostGemessen) return false;
  snprintf(buf, n, "%.30s %.1f °C:\nPflanzen rein", s_frostName, s_frostT);
  for (char* p = buf; *p; p++) if (*p == '.') *p = ',';
  return true;
}

static bool stillText(char* buf, size_t n) {   // kein Grenzwert-Alarm, aber ein Sensor schweigt
  if (s_stillN <= 0) return false;
  SensorCfg c; SensorWert w;
  if (!sensorenHole(s_stillIdx[0], &c, &w)) return false;
  char wer[48]; sensorBezeichnung(c, wer, sizeof(wer));
  if (s_stillN > 1) snprintf(buf, n, "Sensor still:\n%.40s (+%d)", wer, s_stillN - 1);
  else              snprintf(buf, n, "Sensor still:\n%.40s", wer);
  return true;
}
#else
void sensorUeberwachungTick() {}
bool hinweisFrostGemessen(char*, size_t) { return false; }
static bool stillText(char*, size_t) { return false; }
#endif

bool hinweisSensorText(char* buf, size_t n) {
  int idx[SENSOR_MAX]; int k = sensorenAlarme(idx, SENSOR_MAX);
  if (k <= 0) return stillText(buf, n);
  SensorCfg c; SensorWert w;
  if (!sensorenHole(idx[0], &c, &w)) return false;
  char wer[48]; sensorBezeichnung(c, wer, sizeof(wer));
  // Name in der ersten, Wert in einer eigenen Zeile: so bricht die Kachel nie mitten in der Zahl um ("62." / "5 %")
  if (k > 1) snprintf(buf, n, "%.40s\n%.*f %.4s (+%d)", wer, c.dezimal, w.wert, c.einheit, k - 1);
  else       snprintf(buf, n, "%.40s\n%.*f %.4s", wer, c.dezimal, w.wert, c.einheit);
  return true;
}
