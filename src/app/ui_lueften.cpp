// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Warnbildschirm "Lueften / Schimmelgefahr" (ScreenWarnungLueften aus PicoPixel, Duplikat des Pollen-Warnbildschirms):
// erscheint einmal je Warnphase, blinkt, Antippen bestaetigt. Der Inhalt kommt aus lueften.cpp.
#include "app_state.h"
#include "lueften.h"
#include "navigation.h"
#include "../ui/screens.h"
#include "../ui/fonts/fonts.h"
#include "../icons/icons.h"

static bool     s_episode = false, s_gezeigt = false, s_bestaetigt = false;
static uint32_t s_gezeigtMs = 0, s_inaktivSeit = 0;
static lv_obj_t* s_bestaetigeLabel = nullptr;
static bool     s_blink = false;

static lv_obj_t* feldFormatieren(lv_obj_t* l, const lv_font_t* f, int y, int breite) {
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, breite);
  lv_obj_set_pos(l, (1024 - breite) / 2, y);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
  return l;
}

// Texte des Warnbildschirms aus dem aktuellen Stand setzen (Aufrufer haelt den LVGL-Lock)
void aktualisiereLueftenWarnScreen() {
  if (!objects.screenwarnunglueften) return;
  LueftenStatus st = lueftenKopie();
  if (objects.imagealertlueften) iconSetzen(objects.imagealertlueften, "humidity", 112);
  if (objects.labellueftenwarntitel) {
    // Titel mittig: volle Breite, zentrierter Text (Schrift und Hoehe bleiben wie in PicoPixel)
    lv_obj_set_width(objects.labellueftenwarntitel, 820);
    lv_obj_set_x(objects.labellueftenwarntitel, (1024 - 820) / 2);
    lv_obj_set_style_text_align(objects.labellueftenwarntitel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(objects.labellueftenwarntitel, st.warnTitel[0] ? st.warnTitel : "Zu feucht !");
  }
  if (objects.labellueftenwarnraum) {
    feldFormatieren(objects.labellueftenwarnraum, &font_montserrat_32, 318, 820);
    lv_label_set_text(objects.labellueftenwarnraum, st.warnRaeume);
  }
  if (objects.labellueftenwarnhint) {
    feldFormatieren(objects.labellueftenwarnhint, &font_montserrat_22, 452, 820);
    lv_label_set_text(objects.labellueftenwarnhint, st.warnHinweis);
  }
  if (!s_bestaetigeLabel) {
    s_bestaetigeLabel = lv_label_create(objects.screenwarnunglueften);
    feldFormatieren(s_bestaetigeLabel, &font_montserrat_22, 546, 600);
    lv_obj_set_style_text_opa(s_bestaetigeLabel, LV_OPA_70, 0);
    lv_label_set_text(s_bestaetigeLabel, "Tippen zum Bestätigen");
  }
}

static void blinken(lv_timer_t*) {
  s_blink = !s_blink;
  if (lv_scr_act() == objects.screenwarnunglueften)
    lv_obj_set_style_bg_color(objects.screenwarnunglueften, s_blink ? lv_color_hex(0x0E7490) : lv_color_hex(0x073A48), LV_PART_MAIN);
}

static void tippen(lv_event_t*) {
  s_bestaetigt = true;
  nav_goto(SCREEN_ID_SCREEN_1);
}

// Aus uiWarnenPruefen() alle 500 ms (mit LVGL-Lock): Warnbildschirm zeigen oder schliessen
void uiLueftenPruefen() {
  if (!objects.screenwarnunglueften) return;
  lv_obj_t* aktiv = lv_scr_act();
  if (!cfg.lueft_warnung) { s_episode = false; if (aktiv == objects.screenwarnunglueften) nav_goto(SCREEN_ID_SCREEN_1); return; }
  LueftenStatus st = lueftenKopie();
  const uint32_t jetzt = millis();
  if (warnRuheAktiv()) {                     // Ruhezeit: nichts zeigen, der Warnbildschirm kommt danach, falls die Warnung noch gilt
    if (aktiv == objects.screenwarnunglueften) nav_goto(SCREEN_ID_SCREEN_1);
    return;
  }
  if (st.warnAktiv) {
    s_inaktivSeit = 0;
    if (!s_episode) { s_episode = true; s_gezeigt = false; s_bestaetigt = false; }
    else if (s_bestaetigt && jetzt - s_gezeigtMs >= 6UL * 3600UL * 1000UL) { s_gezeigt = false; s_bestaetigt = false; }   // nach 6 Stunden erneut
    if (!s_gezeigt && !s_bestaetigt && aktiv != objects.screenboot && aktiv != objects.screenwarnungpollen && aktiv != objects.screenwarnunglueften) {
      s_gezeigt = true; s_gezeigtMs = jetzt ? jetzt : 1;
      aktualisiereLueftenWarnScreen();
      nav_goto(SCREEN_ID_SCREENWARNUNGLUEFTEN);
    }
  } else {
    if (s_episode) { if (!s_inaktivSeit) s_inaktivSeit = jetzt ? jetzt : 1; else if (jetzt - s_inaktivSeit >= 20UL * 60UL * 1000UL) s_episode = false; }
    if (aktiv == objects.screenwarnunglueften) nav_goto(SCREEN_ID_SCREEN_1);     // Bedingung weggefallen: Warnbildschirm schliessen
  }
}

void uiLueftenInit() {
  LvLock lock;
  if (!objects.screenwarnunglueften) return;
  lv_obj_add_event_cb(objects.screenwarnunglueften, tippen, LV_EVENT_CLICKED, nullptr);
  lv_timer_create(blinken, 500, nullptr);
  aktualisiereLueftenWarnScreen();
}
