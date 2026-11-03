// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Prognose-Check, siehe prognose.h. Datei klima/prognose.csv: Zeilen "Abruftag,Zieltag,Vorlauf,tmax,tmin" (JJJJ-MM-TT, Vorlauf 1 bis 3 Tage).
// Tatsaechliche Werte: Tagesminimum und -maximum aus den Verlaufsdateien (alle 10 Minuten ein Wert). Gewertet wird ein Tag nur mit mindestens
// 60 Werten (10 Stunden). Bei mehreren Zeilen fuer denselben Zieltag und Vorlauf gilt die letzte. Gelesene Tage bleiben im Zwischenspeicher
// (PSRAM), so liest die stuendliche Auswertung nur neue Tage.
#include "prognose.h"
#include "app_state.h"
#if WC_P4
#include "../sensors/sensors.h"
#include "../storage/storage.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"

#define RUECKBLICK   30      // Tage, die in die Auswertung eingehen
#define MIN_WERTE    60
#define DATEI        "klima/prognose.csv"

static long tagNr(int J, int M, int D) {   // Tage seit 1970 (nach Howard Hinnant)
  J -= M <= 2;
  const long era = (J >= 0 ? J : J - 399) / 400, yoe = J - era * 400;
  const long doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

static PrognoseErgebnis s_erg;
static bool s_hat = false;
static long s_letzterAbruf = -2;         // letzter abgelegter Abruftag (-2 = Datei noch nicht gelesen)
static unsigned long s_tAuswertung = 0;
static bool s_neu = true;                // Auswertung faellig

struct Prog { long ziel; int8_t vorl; float tmax, tmin; };
struct Lauf { Prog* p; int np; int pMax; long ab; long heute; long maxAbruf; };

// Zwischenspeicher der Tageswerte: je Tag Sensor (s) und Wetterdaten (a)
struct TagWert { long tag; bool gelesen; float sMn, sMx; int sN; float aMn, aMx; int aN; };
static TagWert* s_tage = nullptr;        // RUECKBLICK + 2 Eintraege, PSRAM
static char s_cacheId[24] = "";

static void cbProg(const char* z, void* ctx) {
  Lauf* l = (Lauf*)ctx;
  int J1, M1, D1, J2, M2, D2, v; float mx, mn;
  if (sscanf(z, "%d-%d-%d,%d-%d-%d,%d,%f,%f", &J1, &M1, &D1, &J2, &M2, &D2, &v, &mx, &mn) != 9 || v < 1 || v > 3) return;
  const long ab = tagNr(J1, M1, D1), ziel = tagNr(J2, M2, D2);
  if (ab > l->maxAbruf) l->maxAbruf = ab;
  if (ziel < l->ab || ziel >= l->heute) return;
  for (int k = 0; k < l->np; k++) if (l->p[k].ziel == ziel && l->p[k].vorl == v) { l->p[k].tmax = mx; l->p[k].tmin = mn; return; }   // spaetere Zeile ersetzt
  if (l->np < l->pMax) l->p[l->np++] = {ziel, (int8_t)v, mx, mn};
}

struct TagLesen { TagWert* t; const char* sid; };
static void cbVerlauf(time_t, const char* id, float w, void* ctx) {
  TagLesen* l = (TagLesen*)ctx; TagWert& t = *l->t;
  if (l->sid[0] && !strcmp(id, l->sid)) { if (!t.sN || w < t.sMn) t.sMn = w; if (!t.sN || w > t.sMx) t.sMx = w; t.sN++; }
  else if (!strcmp(id, "aussen_temp")) { if (!t.aN || w < t.aMn) t.aMn = w; if (!t.aN || w > t.aMx) t.aMx = w; t.aN++; }
}

// Tageswerte eines Tages (Tagesnummer) aus dem Zwischenspeicher oder von der Karte
static const TagWert* tagWert(long tag, const char* sid) {
  const int N = RUECKBLICK + 2;
  int frei = -1, aeltester = 0;
  for (int i = 0; i < N; i++) {
    if (s_tage[i].gelesen && s_tage[i].tag == tag) return &s_tage[i];
    if (!s_tage[i].gelesen && frei < 0) frei = i;
    if (s_tage[i].tag < s_tage[aeltester].tag) aeltester = i;
  }
  TagWert& t = s_tage[frei >= 0 ? frei : aeltester];
  t = TagWert(); t.tag = tag; t.gelesen = true;
  // Tagesnummer in ein Datum zuruecksetzen (Mittag, damit die Sommerzeit nicht stoert)
  const time_t z = (time_t)tag * 86400 + 12 * 3600; struct tm d; gmtime_r(&z, &d);
  TagLesen l{&t, sid};
  speicherVerlaufTag(d.tm_year + 1900, d.tm_mon + 1, d.tm_mday, cbVerlauf, &l);
  return &t;
}

static bool heuteLokal(long* nr, char* text, size_t n) {
  time_t t = time(nullptr);
  if (t < 1700000000) return false;
  struct tm l; localtime_r(&t, &l);
  *nr = tagNr(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
  if (text) snprintf(text, n, "%04d-%02d-%02d", l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
  return true;
}

// Vorhersage des Tages ablegen (erste des Tages) und merken, welcher Tag zuletzt abgelegt wurde
static void ablegen(long heute, const char* heuteText) {
  WocheTag t[4];
  { LvLock lock; if (!woche.geladen || woche.n < 4 || strcmp(woche.tag[0].datum, heuteText) != 0) return; for (int i = 0; i < 4; i++) t[i] = woche.tag[i]; }   // 7-Tage-Daten von heute
  char z[96], text[400] = "";
  for (int v = 1; v <= 3; v++) { snprintf(z, sizeof(z), "%s,%s,%d,%.1f,%.1f\n", heuteText, t[v].datum, v, t[v].tmax, t[v].tmin); strlcat(text, z, sizeof(text)); }
  if (speicherTextAnhaengen(DATEI, text)) { s_letzterAbruf = heute; s_neu = true; logDiag("[Prognose] Vorhersage vom %s abgelegt", heuteText); }
}

// Eine Auswertung mit den Tageswerten des Sensors (mitSensor) oder der Wetterdaten
static void rechne(long heute, const Lauf& l, bool mitSensor, const char* sid, PrognoseErgebnis& e) {
  float sMax[3] = {}, sMin[3] = {}; int nt[3] = {}, tr[3] = {};
  for (int k = 0; k < l.np; k++) {
    const Prog& p = l.p[k];
    if (p.ziel < heute - RUECKBLICK || p.ziel >= heute) continue;
    const TagWert* t = tagWert(p.ziel, sid);
    const int n = mitSensor ? t->sN : t->aN;
    if (n < MIN_WERTE) continue;
    const float mn = mitSensor ? t->sMn : t->aMn, mx = mitSensor ? t->sMx : t->aMx;
    if (p.vorl == 1 && p.ziel == heute - 1) { e.gestern = true; e.gFcMax = p.tmax; e.gFcMin = p.tmin; e.gIstMax = mx; e.gIstMin = mn; }
    const int v = p.vorl - 1;
    const float dMax = fabsf(p.tmax - mx), dMin = fabsf(p.tmin - mn);
    sMax[v] += dMax; sMin[v] += dMin; nt[v]++; if (dMax <= 2.0f) tr[v]++;
  }
  for (int v = 0; v < 3; v++) if (nt[v]) { e.n[v] = nt[v]; e.abwMax[v] = sMax[v] / nt[v]; e.abwMin[v] = sMin[v] / nt[v]; e.trefferMax[v] = (int)lroundf(100.0f * tr[v] / nt[v]); }
}

static void auswerten(long heute) {
  PrognoseErgebnis e;
  e.sdFehlt = !speicherBereit();
  if (!s_tage) {
    s_tage = (TagWert*)heap_caps_calloc(RUECKBLICK + 2, sizeof(TagWert), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_tage) return;
  }
  Lauf l{};
  l.pMax = 400; l.ab = heute - RUECKBLICK; l.heute = heute; l.maxAbruf = -2;
  l.p = (Prog*)heap_caps_malloc(sizeof(Prog) * l.pMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!l.p) return;
  if (!e.sdFehlt) {
    speicherTextZeilen(DATEI, cbProg, &l);
    s_letzterAbruf = l.maxAbruf < 0 ? -1 : l.maxAbruf;
    char sid[24] = "", sname[24] = "";
    const bool mitSensor = cfg.prognose_sensor && sensorenAussenTemp(nullptr, sname, sizeof(sname), sid, sizeof(sid), 0);
    if (strcmp(sid, s_cacheId) != 0) { memset(s_tage, 0, sizeof(TagWert) * (RUECKBLICK + 2)); strlcpy(s_cacheId, sid, sizeof(s_cacheId)); }   // anderer Sensor: neu lesen
    bool fertig = false;
    if (mitSensor) {
      rechne(heute, l, true, sid, e);
      if (e.n[0] >= 3) { strlcpy(e.quelle, sname, sizeof(e.quelle)); fertig = true; }
      else { const int tage = e.n[0]; e = PrognoseErgebnis(); e.sensorTage = tage; }   // noch zu wenig Tage: Wetterdaten
    }
    if (!fertig) { const int tage = e.sensorTage; rechne(heute, l, false, sid, e); e.sensorTage = tage; }
  }
  heap_caps_free(l.p);
  e.fertig = true;
  { LvLock lock; s_erg = e; s_hat = true; }
  logDiag("[Prognose] %d Tage ausgewertet (%s), morgen: Hoechstwert im Mittel %.1f Grad daneben, %d %% bis 2 Grad", e.n[0], e.quelle[0] ? e.quelle : "Wetterdaten", e.abwMax[0], e.trefferMax[0]);
  if (cfg.zf_prognose) aktualisiereUI();
}

void prognoseTick() {
  long heute; char heuteText[12];
  if (!heuteLokal(&heute, heuteText, sizeof(heuteText)) || !speicherBereit()) return;   // ohne Uhrzeit oder Karte gibt es nichts zu tun
  if (s_letzterAbruf == -2) { auswerten(heute); s_tAuswertung = millis(); s_neu = false; }   // erster Aufruf nach dem Start liest auch den letzten Abruftag
  static unsigned long tAblegen = 0;
  if (s_letzterAbruf != heute && millis() - tAblegen >= 60000UL) { tAblegen = millis(); ablegen(heute, heuteText); }   // bei Fehlern nicht jede Runde neu versuchen
  if (s_neu || millis() - s_tAuswertung >= 3600UL * 1000) { auswerten(heute); s_tAuswertung = millis(); s_neu = false; }
}

bool prognoseHole(PrognoseErgebnis* out) {
  LvLock lock;
  if (!s_hat) { *out = PrognoseErgebnis(); return false; }
  *out = s_erg;
  return true;
}
#else
void prognoseTick() {}
bool prognoseHole(PrognoseErgebnis* out) { *out = PrognoseErgebnis(); return false; }
#endif
