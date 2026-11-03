// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Display-Seite des Schimmelwaechters (nur ESP32-P4): bis zu vier Raeume je Seite, je Raum Stufe, Raumwerte, geschaetzte Wandfeuchte als Balken,
// Rat und 7-Tage-Verlauf. Rahmen, Karten und Beschriftungen kommen aus PicoPixel (ScreenSchimmel), hier werden Texte, Farben, Balken und die
// Verlaufsfelder gesetzt. Mehr als vier Raeume: Blaettertasten in der Fussleiste. Alle 2 Sekunden neu, solange die Seite offen ist.
#include "schimmel.h"
#if SCHIMMEL_VERFUEGBAR
#include "app_state.h"
#include "navigation.h"
#include <ArduinoJson.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../ui/screens.h"
#include "../ui/fonts/fonts.h"

namespace {
const uint32_t GRUEN = 0x0FA64A, AMBER = 0xD9930D, ROT = 0xCE4B11, DUNKELROT = 0x8B1D1D, GRAU = 0x6B7C8D;
uint32_t stufeFarbe(int s) { return s <= 0 ? GRUEN : s == 1 ? AMBER : s == 2 ? ROT : DUNKELROT; }
const char* const STUFE[4] = {"in Ordnung", "erhöht", "Schimmelgefahr", "akut"};
const int PRO_SEITE = 4;

bool       s_bereit = false;
int        s_seite = 0;
lv_obj_t*  s_zurueck = nullptr, *s_weiter = nullptr, *s_zaehler = nullptr;
lv_timer_t* s_timer = nullptr;
lv_obj_t*  s_feld[4][7] = {};
lv_obj_t*  s_feldText[4][7] = {};

struct Karte { lv_obj_t *box, *raum, *stufe, *werte, *wand, *balken, *tipp, *verlauf; };
Karte karte(int i) {
  switch (i) {
    case 0: return {objects.containerschimmelraum0, objects.labelschimmelraum0, objects.labelschimmelstufe0, objects.labelschimmelwerte0, objects.labelschimmelwand0, objects.barschimmelwand0, objects.labelschimmeltipp0, objects.containerschimmelverlauf0};
    case 1: return {objects.containerschimmelraum1, objects.labelschimmelraum1, objects.labelschimmelstufe1, objects.labelschimmelwerte1, objects.labelschimmelwand1, objects.barschimmelwand1, objects.labelschimmeltipp1, objects.containerschimmelverlauf1};
    case 2: return {objects.containerschimmelraum2, objects.labelschimmelraum2, objects.labelschimmelstufe2, objects.labelschimmelwerte2, objects.labelschimmelwand2, objects.barschimmelwand2, objects.labelschimmeltipp2, objects.containerschimmelverlauf2};
    default: return {objects.containerschimmelraum3, objects.labelschimmelraum3, objects.labelschimmelstufe3, objects.labelschimmelwerte3, objects.labelschimmelwand3, objects.barschimmelwand3, objects.labelschimmeltipp3, objects.containerschimmelverlauf3};
  }
}

void text(lv_obj_t* o, const char* t) { if (o) lv_label_set_text(o, t); }
void textf(lv_obj_t* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void textf(lv_obj_t* o, const char* fmt, ...) { if (!o) return; char b[200]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a); lv_label_set_text(o, b); }
void zeigen(lv_obj_t* o, bool an) { if (!o) return; if (an) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }

void schrift(lv_obj_t* l, const lv_font_t* f, int breite) {
  if (!l) return;
  lv_obj_set_style_text_font(l, f, 0);
  if (breite > 0) { lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_width(l, breite); }
}

// Textfarbe nach dem Design (hell: schwarz, dunkel und grau: hell); die Stufen-Pillen behalten Weiss
void textfarben() {
  const lv_color_t c = designDunkel() ? lv_color_hex(0xE2E8F0) : lv_color_black();
  lv_obj_t* l[] = {objects.labelschimmeltitel, objects.labelschimmelaussen, s_zaehler};
  for (lv_obj_t* o : l) if (o) lv_obj_set_style_text_color(o, c, 0);
  for (int i = 0; i < 4; i++) { Karte k = karte(i); lv_obj_t* t[] = {k.raum, k.werte, k.wand, k.tipp}; for (lv_obj_t* o : t) if (o) lv_obj_set_style_text_color(o, c, 0); }
}

// Verlauf der letzten 7 Tage: Feld je Tag mit Wochentag und hoechster Wandfeuchte, Farbe nach der Stufe des Tages (rechts heute)
void verlauf(int i, lv_obj_t* box, JsonArrayConst v) {
  if (!box) return;
  if (!s_feld[i][0]) {
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(box);                                       // Groesse steht sonst beim ersten Aufruf noch nicht fest
    int w = lv_obj_get_width(box) / 7, h = lv_obj_get_height(box);
    if (w < 20 || h < 20) { w = 452 / 7; h = 44; }
    for (int k = 0; k < 7; k++) {
      lv_obj_t* f = lv_obj_create(box); lv_obj_remove_style_all(f); lv_obj_set_size(f, w - 6, h); lv_obj_set_pos(f, k * w, 0);
      lv_obj_set_style_radius(f, 8, 0); lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0); lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t* l = lv_label_create(f); lv_obj_set_style_text_font(l, &font_montserrat_latin_ext_16, 0); lv_obj_set_style_text_color(l, lv_color_white(), 0);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_line_space(l, -3, 0); lv_obj_center(l);
      s_feld[i][k] = f; s_feldText[i][k] = l;
    }
  }
  static const char* const WT[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  const int n = (int)v.size();
  for (int k = 0; k < 7; k++) {
    const int ix = n - 7 + k;
    if (ix < 0) { lv_obj_set_style_bg_color(s_feld[i][k], lv_color_hex(GRAU), 0); lv_obj_set_style_bg_opa(s_feld[i][k], LV_OPA_30, 0); text(s_feldText[i][k], ""); continue; }
    JsonObjectConst e = v[ix]; const long dt = e["d"] | 0L; const double h = e["h"] | 0.0;
    struct tm tm = {}; tm.tm_year = (int)(dt / 10000) - 1900; tm.tm_mon = (int)(dt / 100 % 100) - 1; tm.tm_mday = (int)(dt % 100); tm.tm_hour = 12; mktime(&tm);
    const int st = h >= 90 ? 3 : h >= 80 ? 2 : h >= 70 ? 1 : 0;
    lv_obj_set_style_bg_color(s_feld[i][k], lv_color_hex(h > 0 ? stufeFarbe(st) : GRAU), 0); lv_obj_set_style_bg_opa(s_feld[i][k], LV_OPA_COVER, 0);
    const char* tag = k == 6 ? "heute" : WT[tm.tm_wday < 0 || tm.tm_wday > 6 ? 0 : tm.tm_wday];
    if (h > 0) textf(s_feldText[i][k], "%s\n%.0f %%", tag, h); else text(s_feldText[i][k], tag);
    lv_obj_center(s_feldText[i][k]);
  }
}

lv_obj_t* fussTaste(const char* sym, int x) {
  lv_obj_t* b = lv_btn_create(objects.screenschimmel);
  lv_obj_set_pos(b, x, 548); lv_obj_set_size(b, 80, 40);
  lv_obj_set_style_radius(b, 20, 0); lv_obj_set_style_bg_color(b, lv_color_hex(0x2B7FA3), 0); lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, sym); lv_obj_set_style_text_font(l, &font_montserrat_22, 0); lv_obj_set_style_text_color(l, lv_color_white(), 0); lv_obj_center(l);
  return b;
}
void blaettern(lv_event_t* e) { s_seite += (int)(intptr_t)lv_event_get_user_data(e); aktualisiereSchimmel(); }
void takt(lv_timer_t*) { if (lv_scr_act() == objects.screenschimmel) aktualisiereSchimmel(); }
void beiAnzeige(lv_event_t*) { s_seite = 0; aktualisiereSchimmel(); if (s_timer) lv_timer_resume(s_timer); }
void beiVerlassen(lv_event_t*) { if (s_timer) lv_timer_pause(s_timer); }
}  // namespace

void aktualisiereSchimmel() {
  if (!s_bereit) return;
  textfarben();
  JsonDocument d; if (deserializeJson(d, schimmelJson()) != DeserializationError::Ok) return;
  const bool an = d["an"] | false, aussen = d["aussenBekannt"] | false;
  JsonArrayConst r = d["raeume"].as<JsonArrayConst>();
  const int gesamt = an ? (int)r.size() : 0;
  if (!an) text(objects.labelschimmelaussen, "Schimmelwächter ist aus (Weboberfläche: Schimmel, einschalten)");
  else if (!aussen) text(objects.labelschimmelaussen, "Noch keine Außentemperatur");
  else if (gesamt == 0) text(objects.labelschimmelaussen, (d["reiheGesetzt"] | false) ? "Keiner der gewählten Räume liefert gerade Werte (Weboberfläche: Schimmel)" : "Kein Raum mit Temperatur- und Feuchtesensor gefunden (Weboberfläche: Sensoren)");
  else textf(objects.labelschimmelaussen, "Außen: %.1f °C, %.0f %% Feuchte", (double)(d["aussen"] | 0.0f), (double)(d["aussenFeuchte"] | 0.0f));
  // Seiten
  const int seiten = gesamt > 0 ? (gesamt + PRO_SEITE - 1) / PRO_SEITE : 1;
  if (s_seite >= seiten) s_seite = seiten - 1;
  if (s_seite < 0) s_seite = 0;
  const bool blaettern = seiten > 1;
  zeigen(s_zurueck, blaettern); zeigen(s_weiter, blaettern); zeigen(s_zaehler, blaettern);
  if (blaettern) textf(s_zaehler, "Seite %d von %d", s_seite + 1, seiten);
  for (int i = 0; i < 4; i++) {
    Karte k = karte(i);
    const int ix = s_seite * PRO_SEITE + i;
    const bool da = ix < gesamt; zeigen(k.box, da); if (!da) continue;
    JsonObjectConst o = r[ix]; int s = o["stufe"] | 0; if (s < 0) s = 0; if (s > 3) s = 3;
    text(k.raum, (const char*)(o["raum"] | ""));
    if (k.stufe) { lv_label_set_text(k.stufe, STUFE[s]); lv_obj_set_style_bg_color(k.stufe, lv_color_hex(stufeFarbe(s)), 0); lv_obj_set_style_bg_opa(k.stufe, LV_OPA_COVER, 0); lv_obj_align(k.stufe, LV_ALIGN_TOP_RIGHT, -12, 8); }
    textf(k.werte, "%.1f °C  |  %.0f %%   Taupunkt %.1f °C", (double)(o["temp"] | 0.0f), (double)(o["feuchte"] | 0.0f), (double)(o["taupunkt"] | 0.0f));
    textf(k.wand, "Wand ca. %.1f °C, dort %.0f %% Feuchte", (double)(o["wandTemp"] | 0.0f), (double)(o["wandFeuchte"] | 0.0f));
    if (k.balken) { lv_bar_set_value(k.balken, (int)(o["wandFeuchte"] | 0.0f), LV_ANIM_OFF); lv_obj_set_style_bg_color(k.balken, lv_color_hex(stufeFarbe(s)), LV_PART_INDICATOR); }
    const int tk = o["tageKritisch7"] | 0;
    textf(k.tipp, "Raumfeuchte unter %.0f %% halten. 7 Tage: %.0f h kritisch%s", (double)(o["maxFeuchte"] | 0.0f), (double)(o["kritisch7"] | 0.0f), tk >= 3 ? ", dauerhaft feucht" : "");
    verlauf(i, k.verlauf, o["verlauf"].as<JsonArrayConst>());
  }
}

void uiSchimmelInit() {
  if (s_bereit || !objects.screenschimmel || !objects.containerschimmel) return;
  lv_obj_clear_flag(objects.containerschimmel, LV_OBJ_FLAG_SCROLLABLE);
  if (objects.labelschimmeltitel) {
    schrift(objects.labelschimmeltitel, &font_montserrat_32, 700);
    lv_label_set_text(objects.labelschimmeltitel, "Schimmelwächter");
    lv_obj_set_pos(objects.labelschimmeltitel, 22, 12);
  }
  schrift(objects.labelschimmelaussen, &font_montserrat_20, 950);
  for (int i = 0; i < 4; i++) {
    Karte k = karte(i);
    if (k.box) lv_obj_clear_flag(k.box, LV_OBJ_FLAG_SCROLLABLE);
    schrift(k.raum, &font_montserrat_22, 300);
    schrift(k.stufe, &font_montserrat_18, 0);
    schrift(k.werte, &font_montserrat_20, 446);
    schrift(k.wand, &font_montserrat_20, 446);
    schrift(k.tipp, &font_montserrat_latin_ext_16, 446);
    if (k.tipp) lv_obj_set_height(k.tipp, 20);                       // eine Zeile, zu langer Text endet mit ...
    if (k.balken) lv_bar_set_range(k.balken, 0, 100);
  }
  // Blaettertasten und Seitenzaehler in der Fussleiste (nur sichtbar, wenn mehr als vier Raeume)
  s_zurueck = fussTaste(LV_SYMBOL_LEFT, 345);              // Home sitzt bei x 465 bis 565: je 40 px Abstand
  s_weiter = fussTaste(LV_SYMBOL_RIGHT, 605);
  lv_obj_add_event_cb(s_zurueck, blaettern, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
  lv_obj_add_event_cb(s_weiter, blaettern, LV_EVENT_CLICKED, (void*)(intptr_t)1);
  s_zaehler = lv_label_create(objects.screenschimmel);
  lv_obj_set_style_text_font(s_zaehler, &font_montserrat_18, 0); lv_obj_set_pos(s_zaehler, 705, 557);
  zeigen(s_zurueck, false); zeigen(s_weiter, false); zeigen(s_zaehler, false);
  lv_obj_add_event_cb(objects.screenschimmel, beiAnzeige, LV_EVENT_SCREEN_LOAD_START, nullptr);
  lv_obj_add_event_cb(objects.screenschimmel, beiVerlassen, LV_EVENT_SCREEN_UNLOADED, nullptr);
  s_timer = lv_timer_create(takt, 2000, nullptr); lv_timer_pause(s_timer);
  s_bereit = true;
}

#else  // S3: keine Schimmel-Seite
#include "../ui/screens.h"
// Die Seite steht im gemeinsamen PicoPixel-Entwurf, wird beim S3 aber gleich wieder freigegeben (Speicher)
void uiSchimmelInit() {
  if (objects.screenschimmel) { lv_obj_del(objects.screenschimmel); objects.screenschimmel = nullptr; }
}
void aktualisiereSchimmel() {}
#endif
