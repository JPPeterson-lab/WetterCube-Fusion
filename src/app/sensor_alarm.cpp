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
bool hinweisSensorText(char* buf, size_t n) {
  int idx[SENSOR_MAX]; int k = sensorenAlarme(idx, SENSOR_MAX);
  if (k <= 0) return false;
  SensorCfg c; SensorWert w;
  if (!sensorenHole(idx[0], &c, &w)) return false;
  char wer[48]; sensorBezeichnung(c, wer, sizeof(wer));
  // Name in der ersten, Wert in einer eigenen Zeile: so bricht die Kachel nie mitten in der Zahl um ("62." / "5 %")
  if (k > 1) snprintf(buf, n, "%.40s\n%.*f %.4s (+%d)", wer, c.dezimal, w.wert, c.einheit, k - 1);
  else       snprintf(buf, n, "%.40s\n%.*f %.4s", wer, c.dezimal, w.wert, c.einheit);
  return true;
}
