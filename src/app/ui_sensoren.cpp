// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Sensor-Seite: baut im Container des PicoPixel-Screens eine Kachel je sichtbarem Sensor (Icon, Name, Raum, Wert, Status)
// Die Anzahl der Kacheln ist einstellbar, deshalb entsteht der Inhalt zur Laufzeit; Icons kommen aus dem Code (icons.h).
#include "app_state.h"
#include <math.h>
#include "../ui/screens.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"
#include "../icons/icons.h"
#include "../sensors/sensors.h"
#include "../sensors/schalter.h"
#include "lueften.h"

// Eine Kachel zeigt einen Sensor (Icon, Name, Raum, grosser Wert) oder mehrere Sensoren mit gleichem Kacheln-Namen (je eine Zeile mit Icon, Name, Wert)
struct Zeile  { lv_obj_t* wert; int sensorIndex; };
struct Kachel { lv_obj_t* card; lv_obj_t* punkt; int n; Zeile z[SENSOR_MAX]; };
static Kachel  s_k[SENSOR_MAX];
static int     s_n = 0;
// Schalter-Kacheln (MQTT): Name, Raum, Schalter-Symbol und Zustandstext; Antippen schaltet um
struct SKachel { lv_obj_t* card; lv_obj_t* sw; lv_obj_t* text; int idx; };
static SKachel s_sk[SCHALTER_MAX];
static int     s_nsk = 0;
static uint32_t s_sig = 0;

static uint32_t thema(int farbe) { return theme_colors[active_theme_index][farbe] & 0xFFFFFF; }
// Farbe der Unterzeilen (Raum, Sensorname): gut lesbar, aber klar schwaecher als der Wert
static uint32_t unterzeile() { return active_theme_index == THEME_DARK ? 0xE2E8F0 : 0x334155; }

static lv_obj_t* label(lv_obj_t* eltern, const lv_font_t* font, uint32_t farbe, int x, int y, int breite, lv_text_align_t ausr) {
  lv_obj_t* l = lv_label_create(eltern);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(farbe), 0);
  lv_obj_set_style_text_align(l, ausr, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, breite);
  lv_obj_set_pos(l, x, y);
  return l;
}

static lv_obj_t* karteAnlegen(lv_obj_t* eltern, int x, int y, int w, int h) {
  lv_obj_t* c = lv_obj_create(eltern);
  lv_obj_set_pos(c, x, y); lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, lv_color_hex(thema(THEME_COLOR_BACKGR_CONT)), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 16, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

// Antippen einer Kachel (oder einer Zeile in einer Gruppen-Kachel): Verlauf dieses Sensors
static void sensorTippen(lv_event_t* e) {
  SensorCfg c;
  if (!sensorenHole((int)(intptr_t)lv_event_get_user_data(e), &c, nullptr)) return;
  char t[64];
  if (c.raum[0]) snprintf(t, sizeof(t), "%s (%s)", c.name, c.raum); else strlcpy(t, c.name, sizeof(t));
  uiHistorieOeffnen(c.id, t, c.einheit, c.dezimal);
}

// ── Aufbau ───────────────────────────────────────────────────────────────────
struct Gruppe { int mem[SENSOR_MAX]; int n; char name[20]; };

static uint32_t signatur(const int* idx, int anzahl) {
  uint32_t h = 2166136261u;
  auto mix = [&](const char* s) { for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; } h ^= 0xFF; h *= 16777619u; };
  mix(active_theme_index == THEME_DARK ? "d" : "h");
  mix(cfg.kacheln_seite <= 8 ? "8" : "12");
  mix(cfg.lueft_ampel ? "L" : "l");    // Streifen an/aus aendert die Hoehe der Kacheln
  for (int i = 0; i < anzahl; i++) {
    SensorCfg c; if (!sensorenHole(idx[i], &c, nullptr)) continue;
    mix(c.id); mix(c.raum); mix(c.name); mix(c.einheit); mix(c.gruppe); mix(sensorenIconName(c));
  }
  int si[SCHALTER_MAX]; const int sn = schalterSichtbar(si, SCHALTER_MAX);
  for (int i = 0; i < sn; i++) { SchalterCfg c; if (schalterHole(si[i], &c, nullptr)) { mix("S"); mix(c.id); mix(c.name); mix(c.raum); } }
  return h;
}

// Einzelner Sensor: Icon, Name, Raum und grosser Wert
static void einzelKachel(lv_obj_t* card, Kachel& k, int sensorIdx, int w, int h, bool kompakt, uint32_t txt) {
  SensorCfg c; if (!sensorenHole(sensorIdx, &c, nullptr)) return;
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, sensorTippen, LV_EVENT_CLICKED, (void*)(intptr_t)sensorIdx);
  const int iconPx = kompakt ? 44 : 64;
  lv_obj_t* ic = lv_img_create(card); lv_obj_set_pos(ic, 14, 12); iconSetzen(ic, sensorenIconName(c), iconPx);
  lv_obj_t* name = label(card, &font_montserrat_22, txt, 14, 12 + iconPx + 6, w - 28, LV_TEXT_ALIGN_LEFT); lv_label_set_text(name, c.name);
  lv_obj_t* raum = label(card, &font_montserrat_22, unterzeile(), 14, 12 + iconPx + 6 + 28, w - 28, LV_TEXT_ALIGN_LEFT); lv_label_set_text(raum, c.raum);
  lv_obj_t* wert = label(card, kompakt ? &font_montserrat_32 : &font_montserrat_48, txt, 14, h - (kompakt ? 46 : 66), w - 28, LV_TEXT_ALIGN_LEFT);
  k.z[k.n++] = {wert, sensorIdx};
}

// Breite eines Textes in Pixeln
static int textBreite(const lv_font_t* f, const char* t) { lv_point_t p; lv_txt_get_size(&p, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); return p.x; }

// Mehrere Sensoren in einer Kachel: Titel (Kachel-Name), darunter je Sensor eine Zeile
static void gruppenKachel(lv_obj_t* card, Kachel& k, const Gruppe& g, int w, int h, uint32_t txt) {
  lv_obj_t* titel = label(card, &font_montserrat_22, txt, 14, 10, w - 56, LV_TEXT_ALIGN_LEFT); lv_label_set_text(titel, g.name);
  const int top = 44, rh = (h - top - 8) / g.n;
  const int iconPx = rh - 18 < 28 ? 28 : (rh - 18 > 56 ? 56 : rh - 18);
  const bool mitName = rh >= 70;
  const int x = 14 + iconPx + 12, bw = w - x - 10;

  // Groesste Schrift, in die der breiteste Wert der Kachel (mit einer Stelle Reserve) noch hineinpasst und die in die Zeilenhoehe passt
  const lv_font_t* fonts[3] = { &font_montserrat_48, &font_montserrat_32, &font_montserrat_22 };
  const int minH[3] = { mitName ? 92 : 62, mitName ? 64 : 40, 0 };
  int breit[3] = { 0, 0, 0 };
  for (int r = 0; r < g.n; r++) {
    SensorCfg c; SensorWert v; if (!sensorenHole(g.mem[r], &c, &v)) continue;
    char buf[40]; snprintf(buf, sizeof(buf), "%.*f %s0", c.dezimal, v.gueltig ? v.wert : 8.8f, c.einheit);
    for (int f = 0; f < 3; f++) { int b = textBreite(fonts[f], buf); if (b > breit[f]) breit[f] = b; }
  }
  int fi = 2;
  for (int f = 0; f < 3; f++) if (rh >= minH[f] && breit[f] <= bw) { fi = f; break; }
  const lv_font_t* fw = fonts[fi];
  const int zh = lv_font_get_line_height(fw);
  int namenBreit = 0;
  for (int r = 0; r < g.n; r++) { SensorCfg c; if (sensorenHole(g.mem[r], &c, nullptr)) { int b = textBreite(&font_montserrat_22, c.name); if (b > namenBreit) namenBreit = b; } }
  const lv_font_t* fn = (namenBreit <= bw && rh >= 80) ? &font_montserrat_22 : &font_montserrat_18;

  for (int r = 0; r < g.n; r++) {
    SensorCfg c; if (!sensorenHole(g.mem[r], &c, nullptr)) continue;
    const int y = top + r * rh;
    if (r > 0) { lv_obj_t* l = lv_obj_create(card); lv_obj_set_pos(l, 14, y - 2); lv_obj_set_size(l, w - 28, 1);
      lv_obj_set_style_bg_color(l, lv_color_hex(0x94A3B8), 0); lv_obj_set_style_bg_opa(l, LV_OPA_40, 0); lv_obj_set_style_border_width(l, 0, 0);
      lv_obj_set_style_pad_all(l, 0, 0); lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE); }
    lv_obj_t* ic = lv_img_create(card); lv_obj_set_pos(ic, 14, y + (rh - iconPx) / 2); iconSetzen(ic, sensorenIconName(c), iconPx);
    int wy = y + (rh - zh) / 2;
    if (mitName) {
      lv_obj_t* nm = label(card, fn, unterzeile(), x, y + 4, bw, LV_TEXT_ALIGN_LEFT); lv_label_set_text(nm, c.name);
      lv_obj_set_height(nm, lv_font_get_line_height(fn));
      wy = y + 4 + lv_font_get_line_height(fn) + (rh - 4 - lv_font_get_line_height(fn) - zh) / 2;
    }
    lv_obj_t* wert = label(card, fw, txt, x, wy, bw, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_height(wert, zh);                                  // eine Zeile: zu langer Text wird mit ... gekuerzt statt umzubrechen
    k.z[k.n++] = {wert, g.mem[r]};
    lv_obj_t* tipp = lv_obj_create(card);                         // unsichtbare Tippfläche über der Zeile: öffnet den Verlauf dieses Sensors
    lv_obj_set_pos(tipp, 0, y); lv_obj_set_size(tipp, w, rh);
    lv_obj_set_style_bg_opa(tipp, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(tipp, 0, 0); lv_obj_set_style_pad_all(tipp, 0, 0);
    lv_obj_set_style_shadow_width(tipp, 0, 0); lv_obj_clear_flag(tipp, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(tipp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tipp, sensorTippen, LV_EVENT_CLICKED, (void*)(intptr_t)g.mem[r]);
  }
}

static void werteAktualisieren();
static void aktualisierenIntern(bool erzwingen);

// ── Seiten: mehr Kacheln als auf eine Seite passen (Einstellung 8 oder 12 je Seite) ─────────────────────────────
static int s_seite = 0, s_seiten = 1;
static int s_sensorSeite[SENSOR_MAX];                                 // Seite, auf der die Kachel des Sensors steht (-1 = nicht angezeigt)
static lv_obj_t *s_pl = nullptr, *s_pr = nullptr, *s_pt = nullptr, *s_pd = nullptr;   // zurueck, weiter, Text "1 / 2", roter Punkt (Alarm auf anderer Seite)

static void seiteWechseln(int delta) {
  if (s_seiten <= 1) return;
  s_seite = (s_seite + delta + s_seiten) % s_seiten;
  s_sig = 0;                                                            // neu aufbauen
  aktualisierenIntern(true);
}
static void seiteTippen(lv_event_t* e) { seiteWechseln((int)(intptr_t)lv_event_get_user_data(e)); }
static void seiteWischen(lv_event_t*) {
  lv_indev_t* in = lv_indev_get_act(); if (!in) return;
  const lv_dir_t d = lv_indev_get_gesture_dir(in);
  if (d == LV_DIR_LEFT) seiteWechseln(1); else if (d == LV_DIR_RIGHT) seiteWechseln(-1);
}

// Anzeige "<  1 / 2  >" unten zwischen Home- und Weiter-Taste; nur bei mehr als einer Seite sichtbar
static void seitenAnzeigeAktualisieren() {
  lv_obj_t* scr = objects.screensensoren; if (!scr) return;
  if (!s_pt) {
    auto knopf = [&](const char* t, int x, int delta) {
      lv_obj_t* l = lv_label_create(scr); lv_label_set_text(l, t);
      lv_obj_set_style_text_font(l, &font_montserrat_32, 0);
      lv_obj_set_pos(l, x, 552); lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(l, 22);
      lv_obj_add_event_cb(l, seiteTippen, LV_EVENT_CLICKED, (void*)(intptr_t)delta);
      return l;
    };
    s_pl = knopf("<", 606, -1);
    s_pt = lv_label_create(scr); lv_obj_set_style_text_font(s_pt, &font_montserrat_22, 0); lv_obj_set_style_text_align(s_pt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_pt, 120); lv_obj_set_pos(s_pt, 654, 557);
    s_pr = knopf(">", 794, 1);
    s_pd = lv_obj_create(scr); lv_obj_set_size(s_pd, 14, 14); lv_obj_set_pos(s_pd, 842, 556);
    lv_obj_set_style_radius(s_pd, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_border_width(s_pd, 0, 0); lv_obj_clear_flag(s_pd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_pd, lv_color_hex(0xE01E00), 0); lv_obj_set_style_bg_opa(s_pd, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, seiteWischen, LV_EVENT_GESTURE, nullptr);          // Wischen nach links/rechts blaettert ebenfalls
  }
  const bool mehr = s_seiten > 1;
  lv_obj_t* teile[3] = {s_pl, s_pt, s_pr};
  for (lv_obj_t* o : teile) { if (mehr) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }
  if (!mehr) { lv_obj_add_flag(s_pd, LV_OBJ_FLAG_HIDDEN); return; }
  char b[24]; snprintf(b, sizeof(b), "%d / %d", s_seite + 1, s_seiten);
  if (strcmp(lv_label_get_text(s_pt), b) != 0) lv_label_set_text(s_pt, b);
  const lv_color_t c = lv_color_hex(thema(THEME_COLOR_TEXT));
  for (lv_obj_t* o : teile) lv_obj_set_style_text_color(o, c, 0);
}

static void schalterTippen(lv_event_t* e) {
  const int k = (int)(intptr_t)lv_event_get_user_data(e);
  if (k < 0 || k >= s_nsk) return;
  if (!schalterUmschalten(s_sk[k].idx)) { lv_label_set_text(s_sk[k].text, "kein MQTT"); return; }   // nicht verbunden: Hinweis, der naechste Takt stellt den Text wieder her
  werteAktualisieren();
}

static void schalterKachel(lv_obj_t* host, int k, int regIdx, int x, int y, int w, int h, uint32_t txt) {
  SchalterCfg c; if (!schalterHole(regIdx, &c, nullptr)) return;
  lv_obj_t* card = karteAnlegen(host, x, y, w, h);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(card, lv_color_mix(lv_color_hex(0x22C55E), lv_color_hex(thema(THEME_COLOR_BACKGR_CONT)), 60), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_event_cb(card, schalterTippen, LV_EVENT_CLICKED, (void*)(intptr_t)k);
  lv_obj_t* name = label(card, &font_montserrat_22, txt, 14, 12, w - 28, LV_TEXT_ALIGN_LEFT); lv_label_set_text(name, c.name);
  lv_obj_t* raum = label(card, &font_montserrat_22, unterzeile(), 14, 40, w - 28, LV_TEXT_ALIGN_LEFT); lv_label_set_text(raum, c.raum);
  const int sh = h >= 140 ? 48 : 38, sw_ = sh * 2;
  lv_obj_t* sw = lv_switch_create(card);
  lv_obj_set_size(sw, sw_, sh); lv_obj_set_pos(sw, 14, h - sh - 14);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);                     // der Fingertipp gilt der ganzen Kachel
  lv_obj_set_style_bg_color(sw, lv_color_hex(0x94A3B8), LV_PART_MAIN);
  lv_obj_set_style_bg_color(sw, lv_color_hex(0x22C55E), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_t* tx = label(card, &font_montserrat_32, txt, 14 + sw_ + 14, h - sh - 14 + (sh - lv_font_get_line_height(&font_montserrat_32)) / 2, w - (14 + sw_ + 14) - 10, LV_TEXT_ALIGN_LEFT);
  lv_label_set_text(tx, "?");
  s_sk[k] = {card, sw, tx, regIdx};
}

static void aufbauen(const int* idx, int anzahl) {
  lv_obj_t* host = objects.containersensoren;
  if (!host) return;
  lv_obj_clean(host);
  s_n = 0; s_nsk = 0;

  // Sichtbare Sensoren zu Kacheln zusammenfassen: gleicher Kacheln-Name = eine Kachel, leer = eigene Kachel (Reihenfolge nach dem ersten Auftreten)
  static Gruppe g[SENSOR_MAX];
  int gn = 0, maxZeilen = 1;
  for (int i = 0; i < anzahl; i++) {
    SensorCfg c; if (!sensorenHole(idx[i], &c, nullptr)) continue;
    int t = -1;
    if (c.gruppe[0]) for (int q = 0; q < gn; q++) if (strcmp(g[q].name, c.gruppe) == 0) { t = q; break; }
    if (t < 0) { t = gn++; g[t].n = 0; strlcpy(g[t].name, c.gruppe, sizeof(g[t].name)); }
    g[t].mem[g[t].n++] = idx[i];
  }

  int swIdx[SCHALTER_MAX]; const int sn = schalterSichtbar(swIdx, SCHALTER_MAX);
  const int gesamt = gn + sn;                          // Sensor-Kacheln, dahinter die Schalter-Kacheln
  const int N = cfg.kacheln_seite <= 8 ? 8 : 12;       // Kacheln je Seite (Einstellung: 8 gross oder 12 kompakt)
  s_seiten = gesamt > N ? (gesamt + N - 1) / N : 1;
  if (s_seite >= s_seiten) s_seite = s_seiten - 1;
  if (s_seite < 0) s_seite = 0;
  for (int i = 0; i < SENSOR_MAX; i++) s_sensorSeite[i] = -1;
  for (int t = 0; t < gn; t++) for (int m = 0; m < g[t].n; m++) s_sensorSeite[g[t].mem[m]] = t / N;   // fuer den Hinweis "Alarm auf anderer Seite"
  seitenAnzeigeAktualisieren();
  if (gesamt == 0) {
    lv_obj_t* l = label(host, &font_montserrat_22, thema(THEME_COLOR_TEXT), 0, 200, 996, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, "Keine Sensoren oder Schalter eingerichtet.\nIn der Web-Oberfläche unter \"Sensoren\" bzw. \"Schalter\" auswählen.");
    return;
  }
  const int start = s_seite * N, ende = fminf((float)gesamt, (float)(start + N));
  const int tp = ende - start;                         // Kacheln auf dieser Seite
  for (int t = start; t < ende && t < gn; t++) if (g[t].n > maxZeilen) maxZeilen = g[t].n;
  const int lay = s_seiten > 1 ? N : tp;               // mehrere Seiten: auf allen Seiten dasselbe Raster
  const int cols = lay <= 1 ? 1 : lay == 2 ? 2 : lay == 3 ? 3 : 4;
  const int rows = (lay + cols - 1) / cols;
  const int gap = 12;
  const int w = (996 - (cols - 1) * gap) / cols;
  const float maxH = maxZeilen >= 3 ? 330.0f : 250.0f;     // Kacheln mit mehreren Werten duerfen hoeher sein
  const float hostH = cfg.lueft_ampel ? 472.0f : 520.0f;  // mit Lueften-Streifen oben ist weniger Platz
  const int h = fminf(maxH, (hostH - (rows - 1) * gap) / rows);
  const bool kompakt = rows >= 3;
  const uint32_t txt = thema(THEME_COLOR_TEXT);

  for (int t = start; t < ende; t++) {
    const int pi = t - start, x = (pi % cols) * (w + gap), y = (pi / cols) * (h + gap);
    if (t < gn) {
      lv_obj_t* card = karteAnlegen(host, x, y, w, h);
      lv_obj_t* punkt = lv_obj_create(card); lv_obj_set_size(punkt, 14, 14); lv_obj_set_pos(punkt, w - 30, 14);
      lv_obj_set_style_radius(punkt, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_border_width(punkt, 0, 0); lv_obj_clear_flag(punkt, LV_OBJ_FLAG_SCROLLABLE);
      Kachel& k = s_k[s_n]; k.card = card; k.punkt = punkt; k.n = 0;
      if (g[t].n == 1) einzelKachel(card, k, g[t].mem[0], w, h, kompakt, txt);
      else             gruppenKachel(card, k, g[t], w, h, txt);
      s_n++;
    } else {
      schalterKachel(host, s_nsk, swIdx[t - gn], x, y, w, h, txt);
      s_nsk++;
    }
  }
}

static void werteAktualisieren() {
  const uint32_t txt = thema(THEME_COLOR_TEXT);
  for (int i = 0; i < s_n; i++) {
    int online = 0, gueltig = 0, gesamt = 0; bool alarm = false;
    for (int r = 0; r < s_k[i].n; r++) {
      SensorCfg c; SensorWert v;
      if (!sensorenHole(s_k[i].z[r].sensorIndex, &c, &v)) continue;
      char buf[40];
      if (v.gueltig) snprintf(buf, sizeof(buf), "%.*f %s", c.dezimal, v.wert, c.einheit); else strlcpy(buf, "--", sizeof(buf));
      if (strcmp(lv_label_get_text(s_k[i].z[r].wert), buf) != 0) lv_label_set_text(s_k[i].z[r].wert, buf);
      int64_t alterMs = v.gueltig ? (esp_timer_get_time() / 1000 - v.zuletzt_ms) : 0;
      bool ok = v.gueltig && v.online && alterMs < 15 * 60 * 1000;
      gesamt++; if (v.gueltig) gueltig++; if (ok) online++;
      const bool al = v.alarm && ok;
      if (al) alarm = true;
      lv_obj_set_style_text_color(s_k[i].z[r].wert, lv_color_hex(al ? 0xE01E00 : txt), 0);   // Grenzwert verletzt: Wert in Rot
      lv_obj_set_style_text_opa(s_k[i].z[r].wert, (v.gueltig && !ok) ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
    // Statuspunkt: gruen = alle Werte der Kachel aktuell, rot = mindestens einer schweigt, grau = noch nichts empfangen
    lv_obj_set_style_bg_color(s_k[i].punkt, lv_color_hex(gueltig == 0 ? 0x9CA3AF : online == gesamt ? 0x22C55E : 0xEF4444), 0);
    // Alarm: Kachel mit dickem rotem Rand
    lv_obj_set_style_border_width(s_k[i].card, alarm ? 4 : 1, 0);
    lv_obj_set_style_border_color(s_k[i].card, lv_color_hex(alarm ? 0xE01E00 : 0x94A3B8), 0);
  }
  // Alarm auf einer anderen Seite: roter Punkt an der Seitenanzeige
  if (s_pd) {
    int al[SENSOR_MAX]; const int na = sensorenAlarme(al, SENSOR_MAX); bool andere = false;
    for (int i = 0; i < na; i++) if (s_sensorSeite[al[i]] >= 0 && s_sensorSeite[al[i]] != s_seite) andere = true;
    if (andere && s_seiten > 1) lv_obj_clear_flag(s_pd, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_pd, LV_OBJ_FLAG_HIDDEN);
  }
  // Schalter-Kacheln: Schalter und Text nach dem Zustand, gruener Rand bei "an", gedimmt bei unbekanntem Zustand
  for (int i = 0; i < s_nsk; i++) {
    SchalterCfg c; uint8_t z = SZ_UNBEKANNT;
    if (!schalterHole(s_sk[i].idx, &c, &z)) continue;
    const bool an = z == SZ_AN;
    if (an) lv_obj_add_state(s_sk[i].sw, LV_STATE_CHECKED); else lv_obj_clear_state(s_sk[i].sw, LV_STATE_CHECKED);
    lv_obj_set_style_opa(s_sk[i].sw, z == SZ_UNBEKANNT ? LV_OPA_50 : LV_OPA_COVER, 0);
    const char* t = z == SZ_AN ? "An" : z == SZ_AUS ? "Aus" : "?";
    if (strcmp(lv_label_get_text(s_sk[i].text), t) != 0) lv_label_set_text(s_sk[i].text, t);
    lv_obj_set_style_text_color(s_sk[i].text, lv_color_hex(an ? 0x22C55E : txt), 0);
    lv_obj_set_style_border_width(s_sk[i].card, an ? 3 : 1, 0);
    lv_obj_set_style_border_color(s_sk[i].card, lv_color_hex(an ? 0x22C55E : 0x94A3B8), 0);
  }
}

// ── Streifen "Lüften" oben: Ampelpunkt, Überschrift und Gründe (Daten aus lueften.cpp) ──
static lv_obj_t *s_ldot = nullptr, *s_lkopf = nullptr, *s_ldetail = nullptr;
static char s_lkopfText[48] = "", s_ldetailText[128] = "";
static int s_lastAmpel = -1;

static int streifenTextBreite(const lv_font_t* f, const char* t) { lv_point_t p; lv_txt_get_size(&p, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); return p.x; }

static void streifenAktualisieren() {
  lv_obj_t* st = objects.containerlueften; lv_obj_t* host = objects.containersensoren;
  if (!st || !host) return;
  if (s_lastAmpel != (cfg.lueft_ampel ? 1 : 0)) {                      // Streifen ein- oder ausgeblendet: Kachelflaeche anpassen
    s_lastAmpel = cfg.lueft_ampel ? 1 : 0;
    if (cfg.lueft_ampel) { lv_obj_clear_flag(st, LV_OBJ_FLAG_HIDDEN); lv_obj_set_pos(host, 14, 62); lv_obj_set_size(host, 996, 472); }
    else                 { lv_obj_add_flag(st, LV_OBJ_FLAG_HIDDEN);   lv_obj_set_pos(host, 14, 14); lv_obj_set_size(host, 996, 520); }
  }
  if (!cfg.lueft_ampel) return;
  if (!s_ldot) {
    lv_obj_clear_flag(st, LV_OBJ_FLAG_SCROLLABLE);
    s_ldot = lv_obj_create(st); lv_obj_set_size(s_ldot, 18, 18); lv_obj_set_pos(s_ldot, 14, 12);
    lv_obj_set_style_radius(s_ldot, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_border_width(s_ldot, 0, 0); lv_obj_clear_flag(s_ldot, LV_OBJ_FLAG_SCROLLABLE);
    s_lkopf = label(st, &font_montserrat_22, 0, 42, 7, 330, LV_TEXT_ALIGN_LEFT);
    s_ldetail = label(st, &font_montserrat_18, 0, 386, 10, 596, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_height(s_lkopf, lv_font_get_line_height(&font_montserrat_22)); lv_obj_set_height(s_ldetail, lv_font_get_line_height(&font_montserrat_18));
    s_lkopfText[0] = 0; s_ldetailText[0] = 0;
  }
  LueftenStatus ls = lueftenKopie();
  lv_obj_set_style_bg_color(s_ldot, lv_color_hex(ls.farbe), 0); lv_obj_set_style_bg_opa(s_ldot, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s_lkopf, lv_color_hex(thema(THEME_COLOR_TEXT)), 0);
  lv_obj_set_style_text_color(s_ldetail, lv_color_hex(unterzeile()), 0);
  const char* kopf = ls.kopf[0] ? ls.kopf : "Lüften: wird berechnet ...";
  if (strcmp(s_lkopfText, kopf) != 0 || strcmp(s_ldetailText, ls.detail) != 0) {
    strlcpy(s_lkopfText, kopf, sizeof(s_lkopfText)); strlcpy(s_ldetailText, ls.detail, sizeof(s_ldetailText));
    // Ueberschrift in ihrer echten Breite, die Zeile dahinter bekommt den Rest; passt sie nicht, nimmt sie die kleinere Schrift
    int kw = streifenTextBreite(&font_montserrat_22, kopf) + 6; if (kw < 120) kw = 120; if (kw > 560) kw = 560;
    const int dx = 42 + kw + 16, dw = 984 - dx;
    const lv_font_t* df = streifenTextBreite(&font_montserrat_18, ls.detail) <= dw ? &font_montserrat_18 : &font_montserrat_latin_ext_16;
    lv_obj_set_width(s_lkopf, kw);
    lv_obj_set_style_text_font(s_ldetail, df, 0);
    lv_obj_set_height(s_ldetail, lv_font_get_line_height(df));
    lv_obj_set_pos(s_ldetail, dx, df == &font_montserrat_18 ? 10 : 12);
    lv_obj_set_width(s_ldetail, dw);
    lv_label_set_text(s_lkopf, kopf); lv_label_set_text(s_ldetail, ls.detail);
  }
}

static void aktualisierenIntern(bool erzwingen) {
  if (!objects.screensensoren) return;
  if (!erzwingen && lv_scr_act() != objects.screensensoren) return;     // nur arbeiten, wenn die Seite sichtbar ist
  streifenAktualisieren();
  int idx[SENSOR_MAX]; int m = sensorenSichtbar(idx, SENSOR_MAX);
  uint32_t sig = signatur(idx, m);
  if (sig != s_sig) { aufbauen(idx, m); s_sig = sig; }
  werteAktualisieren();
}

static void aktualisieren(lv_timer_t*) { aktualisierenIntern(false); }

// Für /screenshot.bmp?s=sensoren: Kacheln auch aufbauen, wenn die Seite gerade nicht angezeigt wird (Aufrufer hält den LVGL-Lock)
void uiSensorenErzwingen() { s_sig = 0; aktualisierenIntern(true); }

static void seiteGeladen(lv_event_t*) { s_sig = 0; aktualisieren(nullptr); }

void uiSensorenInit() {
  if (!objects.screensensoren) return;
  // PicoPixel legt in jedem Button ein Label mit dem Platzhaltertext "Button" an, die Pfeile stehen als Hintergrundbild darunter
  lv_obj_t* tasten[] = {objects.buttonsensorenzurueck, objects.buttonsensorenhome, objects.buttonsensorenweiter};
  for (lv_obj_t* t : tasten) if (t && lv_obj_get_child_cnt(t) > 0) lv_label_set_text(lv_obj_get_child(t, 0), "");
  lv_obj_add_event_cb(objects.screensensoren, seiteGeladen, LV_EVENT_SCREEN_LOADED, nullptr);
  lv_timer_create(aktualisieren, 1000, nullptr);
}
