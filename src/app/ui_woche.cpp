// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// 7-Tage-Vorschau: sieben Tageskacheln nebeneinander (Wochentag, Datum, Symbol, Hoechst- und Tiefstwert, Regen, Wind).
// Der Rahmen mit Titel und den drei Tasten kommt aus PicoPixel (ScreenWoche, ContainerWoche, LabelWocheTitel), die Kacheln entstehen
// zur Laufzeit, weil sie von den Daten und vom Farbthema abhaengen.
#include "app_state.h"
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "../ui/screens.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"

static lv_obj_t* s_tage = nullptr;      // Flaeche fuer die sieben Kacheln (Kind von ContainerWoche)
static uint32_t  s_sig = 0;
static bool      s_bereit = false;

static const int KACHEL_B = 132, KACHEL_H = 436, ABSTAND = 8, RAND = 12, OBEN = 64;

static uint32_t thema(int farbe) { return theme_colors[active_theme_index][farbe] & 0xFFFFFF; }
static uint32_t unterzeile() { return active_theme_index == THEME_DARK ? 0xE2E8F0 : 0x334155; }
static uint32_t regenFarbe() { return active_theme_index == THEME_DARK ? 0x3FA0FF : 0x0A5CE0; }

static lv_obj_t* beschriftung(lv_obj_t* eltern, const lv_font_t* font, lv_color_t farbe, int y, const char* text) {
  lv_obj_t* l = lv_label_create(eltern);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, farbe, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(l, KACHEL_B);
  lv_obj_set_height(l, lv_font_get_line_height(font));
  lv_obj_set_pos(l, 0, y);
  lv_label_set_text(l, text);
  return l;
}

// Wochentag zu einem Datum "JJJJ-MM-TT": 0 = Sonntag, -1 = Fehler
static int wochentagVon(const char* datum) {
  int j, m, t;
  if (sscanf(datum, "%d-%d-%d", &j, &m, &t) != 3) return -1;
  struct tm tm = {};
  tm.tm_year = j - 1900; tm.tm_mon = m - 1; tm.tm_mday = t; tm.tm_hour = 12;
  mktime(&tm);
  return tm.tm_wday;
}

static int runde(float v) { return (int)floorf(v + 0.5f); }

static void kachel(int i, const WocheTag& t) {
  static const char* WT[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  lv_obj_t* k = lv_obj_create(s_tage);
  lv_obj_set_size(k, KACHEL_B, KACHEL_H);
  lv_obj_set_pos(k, i * (KACHEL_B + ABSTAND), 0);
  lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(k, 0, 0);
  lv_obj_set_style_radius(k, 14, 0);
  lv_obj_set_style_border_width(k, 1, 0);
  lv_obj_set_style_border_color(k, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_bg_color(k, lv_color_hex(thema(THEME_COLOR_BACKGR_CONT)), 0);
  lv_obj_set_style_bg_opa(k, (lv_opa_t)(theme_colors[active_theme_index][THEME_COLOR_BACKGR_CONT] >> 24), 0);

  const lv_color_t txt   = lv_color_hex(thema(THEME_COLOR_TEXT));
  const lv_color_t zweit = lv_color_hex(unterzeile());
  // Wochentag in der Farbe der Uhrzeiten der Stundenvorhersage (kommt aus dem Entwurf, passt sich dem Thema an)
  lv_color_t kopf = objects.labelh1zeit ? lv_obj_get_style_text_color(objects.labelh1zeit, LV_PART_MAIN) : lv_color_hex(0xCE4B11);

  char buf[24];
  int wt = wochentagVon(t.datum);
  const char* name = i == 0 ? "Heute" : i == 1 ? "Morgen" : (wt >= 0 ? WT[wt] : "--");
  beschriftung(k, &font_montserrat_22, kopf, 10, name);
  int j, m, tag;
  if (sscanf(t.datum, "%d-%d-%d", &j, &m, &tag) == 3) snprintf(buf, sizeof(buf), "%d.%d.", tag, m); else buf[0] = 0;
  beschriftung(k, &font_montserrat_18, zweit, 40, buf);

  // Wettersymbol (112 px Quelle, 96 px angezeigt), mittig zwischen Datum und Temperaturen
  const lv_img_dsc_t* ic = wmoZuImage(t.wmo, true);
  if (ic) {
    lv_obj_t* im = lv_img_create(k);
    lv_img_set_src(im, ic);
    lv_img_set_antialias(im, true);
    lv_img_set_zoom(im, (uint16_t)(256 * 96 / ic->header.w));
    lv_obj_set_pos(im, (KACHEL_B - ic->header.w) / 2, 66 + (112 - ic->header.h) / 2);
  }

  snprintf(buf, sizeof(buf), "%d°", runde(t.tmax));
  beschriftung(k, &font_montserrat_48, tempColor(t.tmax), 186, buf);
  snprintf(buf, sizeof(buf), "%d°", runde(t.tmin));
  beschriftung(k, &font_montserrat_32, tempColor(t.tmin), 240, buf);

  // Regen: Menge und Wahrscheinlichkeit
  const lv_color_t blau = lv_color_hex(regenFarbe());
  if (t.regen_mm >= 0.1f) {
    snprintf(buf, sizeof(buf), "%.1f mm", t.regen_mm);
    for (char* c = buf; *c; c++) if (*c == '.') *c = ',';
    beschriftung(k, &font_montserrat_22, blau, 296, buf);
  } else {
    beschriftung(k, &font_montserrat_22, zweit, 296, "trocken");
  }
  if (t.regen_p >= 0) { snprintf(buf, sizeof(buf), "%d %%", t.regen_p); beschriftung(k, &font_montserrat_18, blau, 328, buf); }

  // Wind
  snprintf(buf, sizeof(buf), "%d km/h", runde(t.wind_max));
  beschriftung(k, &font_montserrat_22, txt, 378, buf);
  beschriftung(k, &font_montserrat_18, zweit, 404, "Wind");
}

static uint32_t signatur() {
  uint32_t h = 2166136261u;
  auto mische = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  mische(active_theme_index); mische(woche.geladen); mische(woche.n);
  for (int i = 0; i < woche.n; i++) {
    const WocheTag& t = woche.tag[i];
    for (const char* c = t.datum; *c; c++) mische((uint8_t)*c);
    mische(t.wmo); mische((uint32_t)runde(t.tmax * 10)); mische((uint32_t)runde(t.tmin * 10));
    mische((uint32_t)runde(t.regen_mm * 10)); mische((uint32_t)t.regen_p); mische((uint32_t)runde(t.wind_max));
  }
  return h;
}

static void beiAnzeige(lv_event_t*) { aktualisiereWoche(); }   // Farben und Daten beim Oeffnen der Seite auffrischen

void uiWocheInit() {
  if (s_bereit || !objects.containerwoche) return;
  s_bereit = true;
  lv_obj_t* c = objects.containerwoche;
  s_tage = lv_obj_create(c);
  lv_obj_remove_style_all(s_tage);
  lv_obj_set_size(s_tage, 7 * KACHEL_B + 6 * ABSTAND, KACHEL_H);
  lv_obj_set_pos(s_tage, RAND, OBEN);
  lv_obj_clear_flag(s_tage, LV_OBJ_FLAG_SCROLLABLE);
  if (objects.labelwochetitel) {
    lv_obj_set_style_text_font(objects.labelwochetitel, &font_montserrat_32, 0);
    lv_obj_set_style_text_color(objects.labelwochetitel, lv_color_black(), 0);   // Rahmen ist in beiden Themen weiss
    lv_obj_set_width(objects.labelwochetitel, 700);
    lv_label_set_long_mode(objects.labelwochetitel, LV_LABEL_LONG_CLIP);
    lv_label_set_text(objects.labelwochetitel, "7-Tage-Vorschau");
  }
  if (objects.screenwoche) lv_obj_add_event_cb(objects.screenwoche, beiAnzeige, LV_EVENT_SCREEN_LOAD_START, nullptr);
}

void aktualisiereWoche() {
  uiWocheInit();
  if (!s_tage) return;
  uint32_t sig = signatur();
  if (sig == s_sig) return;
  s_sig = sig;
  lv_obj_clean(s_tage);
  if (!woche.geladen || woche.n == 0) {
    lv_obj_t* l = lv_label_create(s_tage);
    lv_obj_set_style_text_font(l, &font_montserrat_32, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(unterzeile()), 0);
    lv_label_set_text(l, "Noch keine Daten");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 120);
    return;
  }
  for (int i = 0; i < woche.n; i++) kachel(i, woche.tag[i]);
}
