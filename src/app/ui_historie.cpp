// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Verlauf einer Messreihe (Temperatur, Druck, Feuchte, jeder Sensor): öffnet sich beim Antippen einer Kachel.
// Der Screen entsteht zur Laufzeit (ein Diagramm für alle Reihen), die Daten kommen aus historie.cpp.
// Regel für LVGL-Charts: die Zahl der Gitterlinien nie auf 1 setzen, LVGL teilt beim Zeichnen durch (Anzahl - 1).
#include "app_state.h"
#include "historie.h"
#include <math.h>
#include "../ui/screens.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"

static lv_obj_t *s_scr = nullptr, *s_titel = nullptr, *s_info = nullptr, *s_chart = nullptr, *s_hinweis = nullptr, *s_punkt = nullptr;
static lv_obj_t *s_btn[3] = {nullptr, nullptr, nullptr};
static lv_chart_series_t* s_ser = nullptr;
static lv_obj_t* s_zurueck = nullptr;                 // Screen, von dem aus geöffnet wurde

static char s_id[24] = "", s_name[56] = "", s_einheit[8] = "";
static int  s_dez = 1;
static int  s_bereich = HB_24H;                      // gewählter Zeitraum
static int  s_gezeigt = HB_24H;                      // tatsächlich gezeigter Zeitraum (30 Tage fällt ohne Daten auf 7 Tage zurück)
static lv_obj_t* s_zbtn = nullptr;                    // Taste "Zurück"
static float      s_f[HIST_MAX_PUNKTE];
static lv_coord_t s_y[HIST_MAX_PUNKTE];
static int    s_n = 0;
static time_t s_t0 = 0;
static int    s_schritt = 600;
static int    s_yDez = 0;                             // Nachkommastellen der Achsenbeschriftung (0 oder 1)
static uint32_t s_sig = 0;

static uint32_t thema(int farbe) { return theme_colors[active_theme_index][farbe] & 0xFFFFFF; }
static const char* WT[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

// Zehntelwert als Text ("1013.5", "-3.0")
static void zehntel(char* out, size_t n, int v10, int dez) {
  const char* vz = v10 < 0 ? "-" : "";
  const int a = v10 < 0 ? -v10 : v10;
  if (dez) lv_snprintf(out, n, "%s%d.%d", vz, a / 10, a % 10); else lv_snprintf(out, n, "%s%d", vz, (a + 5) / 10);
}

static void achsenBeschriftung(lv_event_t* e) {
  lv_obj_draw_part_dsc_t* d = (lv_obj_draw_part_dsc_t*)lv_event_get_param(e);
  if (!lv_obj_draw_part_check_type(d, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) return;
  if (!d->text) return;
  if (d->id == LV_CHART_AXIS_PRIMARY_Y) { zehntel(d->text, d->text_length, (int)d->value, s_yDez); return; }
  if (d->id != LV_CHART_AXIS_PRIMARY_X) return;
  time_t t = s_t0 + (time_t)d->value * s_schritt;
  struct tm ti;
  if (s_gezeigt == HB_24H) { t += 1800; localtime_r(&t, &ti); lv_snprintf(d->text, d->text_length, "%02d:00", ti.tm_hour); }   // auf volle Stunden gerundet
  else if (s_gezeigt == HB_7D) { localtime_r(&t, &ti); lv_snprintf(d->text, d->text_length, "%s %d.", WT[ti.tm_wday], ti.tm_mday); }
  else { localtime_r(&t, &ti); lv_snprintf(d->text, d->text_length, "%d.%d.", ti.tm_mday, ti.tm_mon + 1); }
}

// Tippen oder Wischen im Diagramm: Wert und Zeitpunkt anzeigen
static void diagrammBeruehrt(lv_event_t*) {
  if (!s_chart || s_n < 2) return;
  uint32_t i = lv_chart_get_pressed_point(s_chart);
  if (i == LV_CHART_POINT_NONE || (int)i >= s_n || isnan(s_f[i])) return;
  time_t t = s_t0 + (time_t)i * s_schritt;
  struct tm ti; localtime_r(&t, &ti);
  char b[96];
  if (s_gezeigt == HB_24H) snprintf(b, sizeof(b), "%s %02d:%02d Uhr:  %.*f %s", WT[ti.tm_wday], ti.tm_hour, ti.tm_min, s_dez, s_f[i], s_einheit);
  else snprintf(b, sizeof(b), "%s %d.%d. %02d Uhr:  %.*f %s", WT[ti.tm_wday], ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, s_dez, s_f[i], s_einheit);
  lv_label_set_text(s_punkt, b);
}

static void zeichnen();

static void bereichTippen(lv_event_t* e) {
  s_bereich = (int)(intptr_t)lv_event_get_user_data(e);
  if (s_bereich == HB_30D) historie30Anfordern(s_id);
  s_sig = 0;
  zeichnen();
}

static void zurueckTippen(lv_event_t*) {
  lv_obj_t* z = s_zurueck && s_zurueck != s_scr ? s_zurueck : objects.screen_1;
  if (z) lv_scr_load(z);
}

static lv_obj_t* taste(const char* text, int x, int w, lv_event_cb_t cb, intptr_t nutzer) {
  lv_obj_t* b = lv_btn_create(s_scr);
  lv_obj_set_pos(b, x, 538); lv_obj_set_size(b, w, 50);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)nutzer);
  lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &font_montserrat_22, 0);
  lv_obj_center(l);
  return b;
}

static void bauen() {
  s_scr = lv_obj_create(nullptr);
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_scr, 0, 0);
  s_titel = lv_label_create(s_scr);
  lv_obj_set_style_text_font(s_titel, &font_montserrat_32, 0);
  lv_label_set_long_mode(s_titel, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s_titel, 24, 12); lv_obj_set_width(s_titel, 976);
  s_info = lv_label_create(s_scr);
  lv_obj_set_style_text_font(s_info, &font_montserrat_22, 0);
  lv_label_set_long_mode(s_info, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s_info, 24, 56); lv_obj_set_width(s_info, 976);

  s_chart = lv_chart_create(s_scr);
  lv_obj_set_pos(s_chart, 104, 104); lv_obj_set_size(s_chart, 892, 360);
  lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_chart, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(s_chart, 4, LV_PART_MAIN); lv_obj_set_style_pad_right(s_chart, 10, LV_PART_MAIN);
  lv_obj_set_style_line_width(s_chart, 1, LV_PART_MAIN);
  lv_obj_set_style_size(s_chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(s_chart, 4, LV_PART_ITEMS);
  lv_obj_set_style_text_font(s_chart, &font_montserrat_18, LV_PART_TICKS);
  lv_obj_add_flag(s_chart, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_chart, achsenBeschriftung, LV_EVENT_DRAW_PART_BEGIN, nullptr);
  lv_obj_add_event_cb(s_chart, diagrammBeruehrt, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(s_chart, diagrammBeruehrt, LV_EVENT_PRESSED, nullptr);
  s_ser = lv_chart_add_series(s_chart, lv_color_hex(0x0A5CE0), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_ext_y_array(s_chart, s_ser, s_y);

  s_hinweis = lv_label_create(s_scr);
  lv_obj_set_style_text_font(s_hinweis, &font_montserrat_22, 0);
  lv_obj_set_style_text_align(s_hinweis, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_hinweis, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(s_hinweis, 104, 250); lv_obj_set_width(s_hinweis, 892);
  s_punkt = lv_label_create(s_scr);
  lv_obj_set_style_text_font(s_punkt, &font_montserrat_22, 0);
  lv_obj_set_style_text_align(s_punkt, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s_punkt, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s_punkt, 24, 480); lv_obj_set_width(s_punkt, 976);

  s_btn[0] = taste("24 Stunden", 24, 170, bereichTippen, HB_24H);
  s_btn[1] = taste("7 Tage", 206, 140, bereichTippen, HB_7D);
  s_btn[2] = taste("30 Tage", 358, 150, bereichTippen, HB_30D);
  s_zbtn = taste("Zurück", 836, 164, zurueckTippen, 0);
}

// Achsenteilung: kleinste "schöne" Schrittweite, bei der höchstens 6 Abschnitte nötig sind
static void yBereich(float mn, float mx, int* lo10, int* hi10, int* marken) {
  static const float stufen[] = {0.5f, 1, 2, 5, 10, 20, 50, 100, 200, 500};
  float schritt = 500, lo = 0, hi = 0;
  for (float st : stufen) {
    lo = floorf(mn / st) * st; hi = ceilf(mx / st) * st;
    if ((hi - lo) / st <= 6.01f) { schritt = st; break; }
  }
  if (hi - lo < 2 * schritt) { if (hi - lo < schritt) hi = lo + schritt; hi += schritt; }
  s_yDez = schritt < 1 ? 1 : 0;
  *lo10 = (int)lroundf(lo * 10); *hi10 = (int)lroundf(hi * 10);
  *marken = (int)lroundf((hi - lo) / schritt) + 1;
}

static void zeichnen() {
  if (!s_scr) return;
  const uint32_t txt = thema(THEME_COLOR_TEXT), karte = thema(THEME_COLOR_BACKGR_CONT), akzent = thema(THEME_COLOR_HIGHLITE);
  lv_obj_set_style_bg_color(s_scr, lv_color_hex(thema(THEME_COLOR_SCREEN)), 0);
  lv_label_set_text(s_titel, s_name);
  lv_obj_set_style_text_color(s_titel, lv_color_hex(txt), 0);
  lv_obj_set_style_text_color(s_info, lv_color_hex(txt), 0);
  lv_obj_set_style_text_color(s_hinweis, lv_color_hex(txt), 0);
  lv_obj_set_style_text_color(s_punkt, lv_color_hex(txt), 0);
  for (int i = 0; i < 3; i++) {
    const bool an = i == s_bereich;
    lv_obj_set_style_bg_color(s_btn[i], lv_color_hex(an ? akzent : karte), 0);
    lv_obj_set_style_border_width(s_btn[i], an ? 0 : 1, 0);
    lv_obj_set_style_border_color(s_btn[i], lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_color(s_btn[i], lv_color_hex(an ? 0xFFFFFF : txt), 0);
  }
  lv_obj_set_style_bg_color(s_zbtn, lv_color_hex(karte), 0);
  lv_obj_set_style_border_width(s_zbtn, 1, 0);
  lv_obj_set_style_border_color(s_zbtn, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_color(s_zbtn, lv_color_hex(txt), 0);

  const bool dunkel = active_theme_index == THEME_DARK;
  uint32_t farbe = akzent;
  if (!strcmp(s_einheit, "°C")) farbe = dunkel ? 0xFF7A4D : 0xE5572B;
  else if (!strcmp(s_einheit, "hPa")) farbe = dunkel ? 0x3FA0FF : 0x0A5CE0;
  else if (!strcmp(s_einheit, "%")) farbe = dunkel ? 0x2DD4BF : 0x0E7490;
  s_ser->color = lv_color_hex(farbe);
  const lv_color_t gitter = lv_color_mix(lv_color_hex(txt), lv_color_hex(thema(THEME_COLOR_SCREEN)), 40);
  lv_obj_set_style_line_color(s_chart, gitter, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_chart, lv_color_hex(txt), LV_PART_TICKS);
  lv_obj_set_style_line_color(s_chart, lv_color_hex(txt), LV_PART_TICKS);

  // Daten holen; 30 Tage kommen im Hintergrund, bis dahin (oder ohne Speicherkarte) gibt es einen Hinweis
  int bereich = s_bereich;
  const char* hinweis = "";
  if (bereich == HB_30D) {
    const int st = historie30Status();
    if (st == H30_LAEDT) hinweis = "Lade 30 Tage ...";
    else if (st == H30_FEHLT) { hinweis = "30 Tage: nicht verfügbar (für Sensoren wird eine Speicherkarte gebraucht). Gezeigt werden die letzten 7 Tage."; bereich = HB_7D; }
  }
  s_n = hinweis[0] && bereich == HB_30D ? 0 : historieHole(s_id, bereich, s_f, HIST_MAX_PUNKTE, &s_t0, &s_schritt);
  const int gezeigt = bereich;
  s_gezeigt = gezeigt;
  s_sig = (uint32_t)bereich + 7u * (uint32_t)historie30Status();

  if (s_n < 2) {
    lv_obj_add_flag(s_chart, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_hinweis, hinweis[0] ? hinweis : "Noch keine Daten. Der Verlauf füllt sich alle 10 Minuten.");
    lv_obj_clear_flag(s_hinweis, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_info, "");
    lv_label_set_text(s_punkt, "");
    return;
  }
  lv_obj_clear_flag(s_chart, LV_OBJ_FLAG_HIDDEN);
  if (hinweis[0]) { lv_label_set_text(s_hinweis, hinweis); lv_obj_set_pos(s_hinweis, 104, 466); lv_obj_set_style_text_font(s_hinweis, &font_montserrat_18, 0); lv_obj_clear_flag(s_hinweis, LV_OBJ_FLAG_HIDDEN); }
  else lv_obj_add_flag(s_hinweis, LV_OBJ_FLAG_HIDDEN);

  float mn = 1e9f, mx = -1e9f, jetzt = NAN; int imn = 0, imx = 0;
  for (int i = 0; i < s_n; i++) {
    if (isnan(s_f[i])) { s_y[i] = LV_CHART_POINT_NONE; continue; }
    if (s_f[i] < mn) { mn = s_f[i]; imn = i; }
    if (s_f[i] > mx) { mx = s_f[i]; imx = i; }
    jetzt = s_f[i];
    s_y[i] = (lv_coord_t)lroundf(s_f[i] * 10.0f);
  }
  int lo10, hi10, marken;
  yBereich(mn, mx, &lo10, &hi10, &marken);
  lv_chart_set_point_count(s_chart, s_n);
  lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, lo10, hi10);
  const int xm = gezeigt == HB_30D ? 7 : gezeigt == HB_7D ? 8 : 7;
  lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_PRIMARY_Y, 8, 4, marken, 1, true, 76);
  lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_PRIMARY_X, 8, 4, xm, 1, true, 30);
  lv_chart_set_div_line_count(s_chart, marken, xm);
  lv_chart_refresh(s_chart);

  // 24 Stunden und 7 Tage zeigen immer die jeweils richtige Zeitachse, auch wenn "30 Tage" auf 7 Tage zurückgefallen ist
  char b[160]; struct tm ta, ti2;
  time_t tmn = s_t0 + (time_t)imn * s_schritt, tmx = s_t0 + (time_t)imx * s_schritt;
  localtime_r(&tmn, &ta); localtime_r(&tmx, &ti2);
  if (gezeigt == HB_24H) snprintf(b, sizeof(b), "Jetzt %.*f %s     Tiefstwert %.*f (%02d:%02d)     Höchstwert %.*f (%02d:%02d)", s_dez, jetzt, s_einheit, s_dez, mn, ta.tm_hour, ta.tm_min, s_dez, mx, ti2.tm_hour, ti2.tm_min);
  else snprintf(b, sizeof(b), "Jetzt %.*f %s     Tiefstwert %.*f (%d.%d.)     Höchstwert %.*f (%d.%d.)", s_dez, jetzt, s_einheit, s_dez, mn, ta.tm_mday, ta.tm_mon + 1, s_dez, mx, ti2.tm_mday, ti2.tm_mon + 1);
  lv_label_set_text(s_info, b);
  lv_label_set_text(s_punkt, "Diagramm berühren: Wert und Zeitpunkt");
}

// Alle 2 Sekunden prüfen, ob die 30 Tage fertig sind; jede Minute wegen neuer Messwerte neu zeichnen
static void takt(lv_timer_t*) {
  if (!s_scr || lv_scr_act() != s_scr) return;
  static unsigned long tZeichnet = 0;
  const uint32_t sig = (uint32_t)s_bereich + 7u * (uint32_t)historie30Status();
  if (sig != s_sig || millis() - tZeichnet >= 60000UL) { tZeichnet = millis(); zeichnen(); }
}

void uiHistorieOeffnen(const char* id, const char* titel, const char* einheit, int dezimal) {
  if (!id || !id[0]) return;
  if (!s_scr) { bauen(); lv_timer_create(takt, 1000, nullptr); }
  strlcpy(s_id, id, sizeof(s_id)); strlcpy(s_name, titel ? titel : id, sizeof(s_name)); strlcpy(s_einheit, einheit ? einheit : "", sizeof(s_einheit));
  s_dez = dezimal < 0 ? 0 : dezimal > 2 ? 2 : dezimal;
  s_bereich = HB_24H;
  lv_obj_t* a = lv_scr_act();
  if (a != s_scr) s_zurueck = a;
  lv_obj_set_style_text_font(s_hinweis, &font_montserrat_22, 0); lv_obj_set_pos(s_hinweis, 104, 250);
  zeichnen();
  lv_scr_load(s_scr);
}

// ── Hauptscreen: Temperatur, Luftfeuchte und Luftdruck antippen ──────────────
static void aussenTippen(lv_event_t* e) {
  switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case 0: uiHistorieOeffnen("aussen_temp", "Temperatur außen", "°C", 1); break;
    case 1: uiHistorieOeffnen("aussen_feuchte", "Luftfeuchte außen", "%", 0); break;
    default: uiHistorieOeffnen("aussen_druck", "Luftdruck außen", "hPa", 0); break;
  }
}

static void antippbar(lv_obj_t* o, int art, int rand) {
  if (!o) return;
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  if (rand) lv_obj_set_ext_click_area(o, rand);
  lv_obj_add_event_cb(o, aussenTippen, LV_EVENT_CLICKED, (void*)(intptr_t)art);
}

void uiHistorieInit() {
  antippbar(objects.labeltemp, 0, 24);
  antippbar(objects.container_30, 2, 0);                // Kachel mit Druck und Feuchte: Druck
  antippbar(objects.labelhumidity, 1, 8);               // Feuchte-Wert und Symbol: Feuchte
  antippbar(objects.imagehumidityicon, 1, 8);
}
