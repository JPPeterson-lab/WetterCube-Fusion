// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Standort-Umschalter auf dem Hauptscreen: oben links eine kleine Pille mit zwei Punkten und dem Ortsnamen. Ein Tipp darauf wechselt
// zwischen Standort 1 und 2 (nur sichtbar, wenn ein zweiter Standort eingerichtet ist). Die Pille entsteht im Code, weil sie nur mit
// zweitem Standort erscheint; Lage und Aussehen koennen spaeter in PicoPixel entworfen werden.
#include "app_state.h"
#include "../ui/screens.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"

static lv_obj_t* s_pille = nullptr;
static lv_obj_t* s_punkt[2] = {nullptr, nullptr};
static lv_obj_t* s_name = nullptr;
static const int PILLE_X = 12, PILLE_Y = 12, PILLE_H = 40, NAME_MAX_B = 200;

static void beiTipp(lv_event_t*) { standortWechseln(standortAktiv == 0 ? 1 : 0); }

void uiStandortInit() {
  if (s_pille || !objects.screen_1) return;
  s_pille = lv_obj_create(objects.screen_1);
  lv_obj_remove_style_all(s_pille);
  lv_obj_set_pos(s_pille, PILLE_X, PILLE_Y);
  lv_obj_set_size(s_pille, LV_SIZE_CONTENT, PILLE_H);
  lv_obj_set_flex_flow(s_pille, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_pille, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_left(s_pille, 14, 0);
  lv_obj_set_style_pad_right(s_pille, 14, 0);
  lv_obj_set_style_pad_column(s_pille, 8, 0);
  lv_obj_set_style_radius(s_pille, PILLE_H / 2, 0);
  lv_obj_set_style_border_width(s_pille, 1, 0);
  lv_obj_set_style_border_color(s_pille, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_bg_opa(s_pille, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_pille, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_pille, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_pille, beiTipp, LV_EVENT_CLICKED, nullptr);
  for (int i = 0; i < 2; i++) {
    s_punkt[i] = lv_obj_create(s_pille);
    lv_obj_remove_style_all(s_punkt[i]);
    lv_obj_set_size(s_punkt[i], 12, 12);
    lv_obj_set_style_radius(s_punkt[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_punkt[i], 2, 0);
    lv_obj_set_style_bg_opa(s_punkt[i], LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_punkt[i], LV_OBJ_FLAG_CLICKABLE);
  }
  s_name = lv_label_create(s_pille);
  lv_obj_set_style_text_font(s_name, &font_montserrat_22, 0);
  lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
  lv_obj_clear_flag(s_name, LV_OBJ_FLAG_CLICKABLE);
}

void aktualisiereStandort() {
  LvLock lock;
  uiStandortInit();
  if (!s_pille) return;
  if (!hatStandort2()) { lv_obj_add_flag(s_pille, LV_OBJ_FLAG_HIDDEN); return; }
  lv_obj_clear_flag(s_pille, LV_OBJ_FLAG_HIDDEN);
  const uint32_t thema = active_theme_index;
  lv_obj_set_style_bg_color(s_pille, lv_color_hex(theme_colors[thema][THEME_COLOR_BACKGR_CONT] & 0xFFFFFF), 0);
  const lv_color_t text = lv_color_hex(theme_colors[thema][THEME_COLOR_TEXT] & 0xFFFFFF);
  const lv_color_t akzent = lv_color_hex(thema == THEME_DARK ? 0x3FA0FF : 0x0A5CE0);
  for (int i = 0; i < 2; i++) {
    bool an = (i == standortAktiv);
    lv_obj_set_style_bg_color(s_punkt[i], an ? akzent : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_punkt[i], an ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_punkt[i], an ? akzent : lv_color_hex(0x94A3B8), 0);
  }
  lv_obj_set_style_text_color(s_name, text, 0);
  const String& name = stdName();
  lv_point_t groesse;
  lv_txt_get_size(&groesse, name.c_str(), &font_montserrat_22, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_obj_set_width(s_name, groesse.x > NAME_MAX_B ? NAME_MAX_B : groesse.x + 2);
  lv_label_set_text(s_name, name.c_str());
}
