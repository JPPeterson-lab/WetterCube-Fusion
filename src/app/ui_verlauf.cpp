// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Temperaturverlauf: Liniendiagramm der nächsten 24 Stunden und (wählbar) Regenbalken je Stunde.
// Die Widgets stammen aus PicoPixel (ScreenVerlauf), die Datenreihen, Achsen und Farben setzt dieser Code.
// Regel für LVGL-Charts: die Zahl der Gitterlinien nie auf 1 setzen, LVGL teilt beim Zeichnen durch (Anzahl - 1).
#include "app_state.h"
#include <math.h>
#include "../ui/screens.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"
#include "../icons/icons.h"

static lv_chart_series_t* s_serT = nullptr;
static lv_chart_series_t* s_serR = nullptr;
static lv_coord_t s_t[25];
static lv_coord_t s_r[25];
static bool s_bereit = false;

static int rundeInt(float v) { return (int)floorf(v + 0.5f); }

// Beschriftung der Achsen: Temperatur und Regen sind mit 10 multipliziert (Zehntel), die Zeitachse zeigt Uhrzeiten
static void achsenBeschriftung(lv_event_t* e) {
  lv_obj_draw_part_dsc_t* d = (lv_obj_draw_part_dsc_t*)lv_event_get_param(e);
  if (!lv_obj_draw_part_check_type(d, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) return;
  if (!d->text) return;
  if (d->id == LV_CHART_AXIS_PRIMARY_X) {
    lv_snprintf(d->text, d->text_length, "%02d", (wetter.vl_h0 + (int)d->value * 4) % 24);   // Marke alle 4 Stunden
  } else if (d->id == LV_CHART_AXIS_PRIMARY_Y) {
    int v = (int)d->value;
    lv_snprintf(d->text, d->text_length, "%d", (v < 0 ? v - 5 : v + 5) / 10);
  }
}

static void achse(lv_obj_t* ch, bool mitZeit, int yMarken) {
  lv_chart_set_axis_tick(ch, LV_CHART_AXIS_PRIMARY_Y, 4, 0, yMarken, 1, true, 40);
  if (mitZeit) lv_chart_set_axis_tick(ch, LV_CHART_AXIS_PRIMARY_X, 4, 0, 7, 1, true, 26);
  else         lv_chart_set_axis_tick(ch, LV_CHART_AXIS_PRIMARY_X, 0, 0, 0, 0, false, 0);
}

void uiVerlaufInit() {
  lv_obj_t* tc = objects.chartverlauf;
  lv_obj_t* rc = objects.chartregen;
  if (!tc || !rc || s_bereit) return;
  s_bereit = true;
  for (int i = 0; i < 25; i++) { s_t[i] = LV_CHART_POINT_NONE; s_r[i] = LV_CHART_POINT_NONE; }
  lv_chart_set_type(tc, LV_CHART_TYPE_LINE);
  lv_chart_set_type(rc, LV_CHART_TYPE_BAR);
  s_serT = lv_chart_add_series(tc, lv_color_hex(0xE5572B), LV_CHART_AXIS_PRIMARY_Y);
  s_serR = lv_chart_add_series(rc, lv_color_hex(0x0A5CE0), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_point_count(tc, 25);
  lv_chart_set_point_count(rc, 25);
  lv_chart_set_ext_y_array(tc, s_serT, s_t);
  lv_chart_set_ext_y_array(rc, s_serR, s_r);
  lv_obj_t* cs[2] = {tc, rc};
  for (lv_obj_t* c : cs) {
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(c, 8, LV_PART_MAIN);       // halber Balkenabstand: die Linienpunkte stehen mittig über den Balken
    lv_obj_set_style_pad_right(c, 8, LV_PART_MAIN);
    lv_obj_set_style_line_width(c, 1, LV_PART_MAIN);
    lv_obj_set_style_size(c, 0, LV_PART_INDICATOR);       // keine Punktmarken
    lv_obj_set_style_text_font(c, &font_montserrat_18, LV_PART_TICKS);
    lv_obj_add_event_cb(c, achsenBeschriftung, LV_EVENT_DRAW_PART_BEGIN, nullptr);
  }
  lv_obj_set_style_line_width(tc, 4, LV_PART_ITEMS);
  lv_obj_set_style_pad_column(rc, 1, LV_PART_ITEMS);
  lv_obj_set_style_radius(rc, 2, LV_PART_ITEMS);

  // Layout feinjustieren: Platz links für die Beschriftung der Temperaturachse, Titel und Min/Max überlappen nicht
  lv_obj_t* diagramme[2] = {tc, rc};
  for (lv_obj_t* c : diagramme) { lv_obj_set_x(c, 62); lv_obj_set_width(c, 914); }
  if (objects.labelverlauftitel) {
    lv_obj_set_style_text_font(objects.labelverlauftitel, &font_montserrat_32, 0);
    lv_obj_set_width(objects.labelverlauftitel, 470);
    lv_label_set_long_mode(objects.labelverlauftitel, LV_LABEL_LONG_CLIP);
    lv_label_set_text(objects.labelverlauftitel, "Temperaturverlauf 24 h");
  }
  if (objects.labelverlaufminmax) {
    lv_obj_set_style_text_font(objects.labelverlaufminmax, &font_montserrat_32, 0);
    lv_obj_set_style_text_align(objects.labelverlaufminmax, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_x(objects.labelverlaufminmax, 520); lv_obj_set_width(objects.labelverlaufminmax, 456);
    lv_label_set_long_mode(objects.labelverlaufminmax, LV_LABEL_LONG_CLIP);
  }
  if (objects.labelverlaufregen) lv_obj_set_style_text_font(objects.labelverlaufregen, &font_montserrat_22, 0);
  if (objects.imageverlauftemp) iconSetzen(objects.imageverlauftemp, "thermometer", 44);
  if (objects.imageverlaufregen) iconSetzen(objects.imageverlaufregen, "raindrop", 36);
}

void aktualisiereVerlauf() {
  spur("UI: Temperaturverlauf");
  uiVerlaufInit();
  lv_obj_t* tc = objects.chartverlauf;
  lv_obj_t* rc = objects.chartregen;
  if (!tc || !rc || !s_bereit) return;
  const uint32_t txtFarbe = theme_colors[active_theme_index][THEME_COLOR_TEXT] & 0xFFFFFF;
  lv_color_t txt = lv_color_hex(txtFarbe);
  lv_color_t gitter = lv_color_mix(txt, lv_color_hex(theme_colors[active_theme_index][THEME_COLOR_BACKGR_CONT] & 0xFFFFFF), 40);
  const bool dunkel = (active_theme_index == THEME_DARK);
  lv_obj_t* cs[2] = {tc, rc};
  for (lv_obj_t* c : cs) {
    lv_obj_set_style_line_color(c, gitter, LV_PART_MAIN);
    lv_obj_set_style_text_color(c, txt, LV_PART_TICKS);
    lv_obj_set_style_line_color(c, txt, LV_PART_TICKS);
  }
  s_serR->color = lv_color_hex(dunkel ? 0x3FA0FF : 0x0A5CE0);
  s_serT->color = lv_color_hex(dunkel ? 0xFF7A4D : 0xE5572B);
  if (objects.labelverlauftitel) lv_obj_set_style_text_color(objects.labelverlauftitel, txt, 0);
  if (objects.labelverlaufminmax) lv_obj_set_style_text_color(objects.labelverlaufminmax, txt, 0);
  if (objects.labelverlaufregen) lv_obj_set_style_text_color(objects.labelverlaufregen, txt, 0);

  // Layout: mit Regenbalken ist das Temperaturdiagramm kürzer, die Zeitachse hängt unter den Balken
  const bool mitRegen = cfg.verlauf_regen;
  lv_obj_t* regenTeile[3] = {rc, objects.labelverlaufregen, objects.imageverlaufregen};
  for (lv_obj_t* o : regenTeile) {
    if (!o) continue;
    if (mitRegen) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_height(tc, mitRegen ? 230 : 340);
  achse(tc, !mitRegen, 5);
  achse(rc, mitRegen, 2);

  const int n = wetter.vl_n;
  if (!wetter.geladen || n < 2) {
    for (int i = 0; i < 25; i++) { s_t[i] = LV_CHART_POINT_NONE; s_r[i] = LV_CHART_POINT_NONE; }
    if (objects.labelverlaufminmax) lv_label_set_text(objects.labelverlaufminmax, "keine Daten");
    if (objects.labelverlaufregen) lv_label_set_text(objects.labelverlaufregen, "");
    lv_chart_refresh(tc); lv_chart_refresh(rc);
    return;
  }
  float tmin = wetter.vl_temp[0], tmax = wetter.vl_temp[0], rmax = 0, rsum = 0;
  for (int i = 0; i < n; i++) {
    if (wetter.vl_temp[i] < tmin) tmin = wetter.vl_temp[i];
    if (wetter.vl_temp[i] > tmax) tmax = wetter.vl_temp[i];
    if (wetter.vl_regen[i] > rmax) rmax = wetter.vl_regen[i];
    rsum += wetter.vl_regen[i];
  }
  // Temperaturskala in ganzen Grad, Spanne durch 4 teilbar (5 Beschriftungen mit ganzen Zahlen)
  int lo = (int)floorf(tmin) - 1;
  int hi = (int)ceilf(tmax) + 1;
  int span = hi - lo; if (span < 4) span = 4;
  span = ((span + 3) / 4) * 4;
  hi = lo + span;
  lv_chart_set_range(tc, LV_CHART_AXIS_PRIMARY_Y, lo * 10, hi * 10);
  lv_chart_set_div_line_count(tc, 3, 5);
  int rtop = (int)ceilf(rmax); if (rtop < 1) rtop = 1;
  lv_chart_set_range(rc, LV_CHART_AXIS_PRIMARY_Y, 0, rtop * 10);
  lv_chart_set_div_line_count(rc, 2, 5);                 // mindestens 2 waagerechte Linien (Division durch null in LVGL bei 1)
  for (int i = 0; i < 25; i++) {
    if (i < n) { s_t[i] = (lv_coord_t)rundeInt(wetter.vl_temp[i] * 10.0f); s_r[i] = (lv_coord_t)rundeInt(wetter.vl_regen[i] * 10.0f); }
    else       { s_t[i] = LV_CHART_POINT_NONE; s_r[i] = LV_CHART_POINT_NONE; }
  }
  lv_chart_refresh(tc);
  lv_chart_refresh(rc);
  char buf[64];
  if (objects.labelverlaufminmax) {
    snprintf(buf, sizeof(buf), "Min %d °C   Max %d °C", rundeInt(tmin), rundeInt(tmax));
    lv_label_set_text(objects.labelverlaufminmax, buf);
  }
  if (objects.labelverlaufregen) {
    if (rsum < 0.05f) snprintf(buf, sizeof(buf), "Regen: keiner erwartet");
    else              snprintf(buf, sizeof(buf), "Regen: bis %.1f mm/h, zusammen %.1f mm", rmax, rsum);
    lv_label_set_text(objects.labelverlaufregen, buf);
  }
}
