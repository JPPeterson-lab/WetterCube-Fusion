// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Allergie-Tagebuch: drei Tasten "Keine / Leichte / Starke Beschwerden" tragen den heutigen Tag ein, darunter zeigen 30 Balken die Beschwerden
// der letzten Tage (gruen / orange / rot) mit der Pollenbelastung als blaue Linie. Rahmen, Tasten und Beschriftungen kommen aus PicoPixel
// (ScreenTagebuch), die Balken entstehen zur Laufzeit.
#include "app_state.h"
#include "tagebuch.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../ui/screens.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"

static lv_obj_t* s_plot = nullptr;                      // Zeichenflaeche der Balken (Kind des Diagrammkastens)
static lv_obj_t* s_von = nullptr, *s_mitte = nullptr, *s_bis = nullptr;
static bool      s_bereit = false;

static const int TAGE = 30, PLOT_B = 888, PLOT_H = 150, SPALTE = 29, BALKEN_B = 24;

static lv_obj_t* beschriftung(lv_obj_t* eltern, int x, int y, int b, lv_text_align_t ausr) {
  lv_obj_t* l = lv_label_create(eltern);
  lv_obj_set_style_text_font(l, &font_montserrat_latin_ext_16, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0x475569), 0);
  lv_obj_set_style_text_align(l, ausr, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(l, b, 20);
  lv_obj_set_pos(l, x, y);
  return l;
}

static void datumText(char* o, size_t n, int32_t d) { snprintf(o, n, "%02ld.%02ld.", (long)d % 100, (long)(d / 100) % 100); }

static void markiere(int heute) {
  lv_obj_t* b[3] = {objects.buttontagebuchgut, objects.buttontagebuchleicht, objects.buttontagebuchstark};
  for (int i = 0; i < 3; i++) {
    if (!b[i]) continue;
    lv_obj_set_style_border_width(b[i], i == heute ? 6 : 0, 0);
    lv_obj_set_style_border_color(b[i], lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_opa(b[i], LV_OPA_COVER, 0);
  }
}

static void balken(lv_obj_t* eltern, int x, int h, uint32_t farbe) {
  lv_obj_t* o = lv_obj_create(eltern);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, PLOT_H - h);
  lv_obj_set_size(o, BALKEN_B, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(farbe), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, 4, 0);
}

// Texte auf dem Rahmen folgen dem Design (hell: schwarz, dunkel: hell); die Beschriftung im weissen Diagrammkasten bleibt dunkel
static void textfarben() {
  const lv_color_t c = active_theme_index == THEME_DARK ? lv_color_hex(0xE2E8F0) : lv_color_black();
  lv_obj_t* l[] = {objects.labeltagebuchtitel, objects.labeltagebuchfrage, objects.labeltagebuchstatus, objects.labeltagebuchlegende};
  for (lv_obj_t* o : l) if (o) lv_obj_set_style_text_color(o, c, 0);
}

void aktualisiereTagebuch() {
  if (!s_bereit) return;
  textfarben();
  // Status
  const int heute = tagebuchHeute();
  static const char* const NAME[3] = {"keine Beschwerden", "leichte Beschwerden", "starke Beschwerden"};
  char z[160];
  if (!tagebuchDatumHeute()) snprintf(z, sizeof(z), "Die Uhrzeit ist noch nicht bekannt, Einträge sind erst danach möglich.");
  else if (heute < 0) snprintf(z, sizeof(z), "Heute: noch kein Eintrag");
  else snprintf(z, sizeof(z), "Heute eingetragen: %s", NAME[heute]);
  if (objects.labeltagebuchstatus) lv_label_set_text(objects.labeltagebuchstatus, z);
  markiere(heute);

  // Balken der letzten 30 Tage
  lv_obj_clean(s_plot);
  const int32_t ab = tagebuchDatumHeute(TAGE - 1);
  TagebuchEintrag e[TAGE + 2];
  const int n = ab ? tagebuchAbDatum(ab, e, TAGE + 2) : 0;
  lv_obj_t* grund = lv_obj_create(s_plot);                // Grundlinie
  lv_obj_remove_style_all(grund);
  lv_obj_set_pos(grund, 0, PLOT_H); lv_obj_set_size(grund, PLOT_B, 2);
  lv_obj_set_style_bg_color(grund, lv_color_hex(0xCBD5E1), 0); lv_obj_set_style_bg_opa(grund, LV_OPA_COVER, 0);
  static lv_point_t pts[TAGE];
  int np = 0;
  for (int k = 0; k < TAGE; k++) {
    const int32_t d = tagebuchDatumHeute(TAGE - 1 - k);
    const TagebuchEintrag* t = nullptr;
    for (int i = 0; i < n; i++) if (e[i].datum == d) { t = &e[i]; break; }
    const int x = k * SPALTE + 2;
    if (!t) { balken(s_plot, x, 3, 0xCBD5E1); continue; }          // kein Eintrag: kleiner grauer Strich
    if (t->stufe == 0) balken(s_plot, x, 8, 0x16A34A);
    else if (t->stufe == 1) balken(s_plot, x, 78, 0xD97706);
    else balken(s_plot, x, PLOT_H, 0xDC2626);
    if (t->pollen10 != 255 && np < TAGE) { pts[np].x = x + BALKEN_B / 2; pts[np].y = PLOT_H - 4 - (t->pollen10 * (PLOT_H - 12)) / 30; np++; }
  }
  if (np >= 2) {
    lv_obj_t* l = lv_line_create(s_plot);
    lv_line_set_points(l, pts, np);
    lv_obj_set_style_line_width(l, 4, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(0x0A5CE0), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
  }
  for (int i = 0; i < np; i++) {                           // Punkte auf der Linie
    lv_obj_t* p = lv_obj_create(s_plot);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, 10, 10); lv_obj_set_pos(p, pts[i].x - 5, pts[i].y - 5);
    lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x0A5CE0), 0); lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  }
  // Datumsmarken
  char dz[12];
  if (ab) { datumText(dz, sizeof(dz), ab); lv_label_set_text(s_von, dz); datumText(dz, sizeof(dz), tagebuchDatumHeute(TAGE / 2 - 1)); lv_label_set_text(s_mitte, dz); }
  lv_label_set_text(s_bis, "heute");
}

static void beiKlick(lv_event_t* ev) {
  const int stufe = (int)(intptr_t)lv_event_get_user_data(ev);
  tagebuchHeuteEintragen(stufe);
  aktualisiereTagebuch();
}
static void beiAnzeige(lv_event_t*) { aktualisiereTagebuch(); }
static void beiVerlassen(lv_event_t*) { if (s_plot) lv_obj_clean(s_plot); }   // Balken wieder freigeben (knapper Speicher beim S3)

void uiTagebuchInit() {
  if (s_bereit || !objects.containertagebuch || !objects.containertagebuchdiagramm) return;
  lv_obj_t* c = objects.containertagebuch;
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  if (objects.labeltagebuchtitel) {
    lv_obj_set_style_text_font(objects.labeltagebuchtitel, &font_montserrat_32, 0);
    lv_obj_set_style_text_color(objects.labeltagebuchtitel, lv_color_black(), 0);
    lv_obj_set_width(objects.labeltagebuchtitel, 700);
    lv_label_set_long_mode(objects.labeltagebuchtitel, LV_LABEL_LONG_CLIP);
    lv_label_set_text(objects.labeltagebuchtitel, "Allergie-Tagebuch");
    lv_obj_set_x(objects.labeltagebuchtitel, 40);
  }
  struct { lv_obj_t* o; const char* t; } l[] = {
    {objects.labeltagebuchfrage, "Wie geht es dir heute?"}, {objects.labeltagebuchstatus, ""},
    {objects.labeltagebuchdiagrammtitel, "Letzte 30 Tage"},
    {objects.labeltagebuchlegende, "Balken: Beschwerden (grün keine, orange leicht, rot stark)  |  blaue Linie: Pollenbelastung"},
  };
  if (objects.labeltagebuchfrage) lv_obj_set_x(objects.labeltagebuchfrage, 40);
  for (auto& e : l) if (e.o) {
    lv_obj_set_style_text_font(e.o, &font_montserrat_20, 0);
    lv_obj_set_style_text_color(e.o, lv_color_black(), 0);
    lv_label_set_long_mode(e.o, LV_LABEL_LONG_CLIP);
    lv_label_set_text(e.o, e.t);
  }
  lv_obj_t* kasten = objects.containertagebuchdiagramm;
  lv_obj_clear_flag(kasten, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(kasten, lv_color_white(), 0); lv_obj_set_style_bg_opa(kasten, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(kasten, 0, 0);
  s_plot = lv_obj_create(kasten);
  lv_obj_remove_style_all(s_plot);
  lv_obj_set_pos(s_plot, 14, 38); lv_obj_set_size(s_plot, PLOT_B, PLOT_H + 4);
  lv_obj_clear_flag(s_plot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  s_von = beschriftung(kasten, 14, 196, 120, LV_TEXT_ALIGN_LEFT);
  s_mitte = beschriftung(kasten, 14 + PLOT_B / 2 - 60, 196, 120, LV_TEXT_ALIGN_CENTER);
  s_bis = beschriftung(kasten, 14 + PLOT_B - 120, 196, 120, LV_TEXT_ALIGN_RIGHT);

  struct { lv_obj_t* b; lv_obj_t* lbl; int stufe; } t[] = {
    {objects.buttontagebuchgut, objects.buttontagebuchgut_label, 0},
    {objects.buttontagebuchleicht, objects.buttontagebuchleicht_label, 1},
    {objects.buttontagebuchstark, objects.buttontagebuchstark_label, 2},
  };
  for (auto& e : t) {
    if (!e.b) continue;
    lv_obj_add_flag(e.b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(e.b, beiKlick, LV_EVENT_CLICKED, (void*)(intptr_t)e.stufe);
    if (e.lbl) { lv_obj_set_style_text_font(e.lbl, &font_montserrat_22, 0); lv_obj_set_style_text_color(e.lbl, lv_color_white(), 0); }
  }
  if (objects.screentagebuch) {
    lv_obj_add_event_cb(objects.screentagebuch, beiAnzeige, LV_EVENT_SCREEN_LOAD_START, nullptr);
    lv_obj_add_event_cb(objects.screentagebuch, beiVerlassen, LV_EVENT_SCREEN_UNLOADED, nullptr);
  }
  s_bereit = true;
}
