// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Module wie beim Apex: Allergie-Tagebuch und Schimmelwaechter (nur P4) stehen nicht in der Reihe der Bildschirme, sondern
//  - in der Modul-Leiste im Menue (zwischen den oberen Kacheln und der Quellenangabe; ausgeschaltete Module grau, nicht antippbar),
//  - als Pille in der Fussleiste: "Allergie-Tagebuch" auf der Allergene-Seite, "Schimmelwaechter" auf der Luftqualitaet-Seite
//    (nur sichtbar, wenn das Modul an ist; der Punkt zeigt den heutigen Eintrag bzw. den kritischsten Raum).
// Ein- und ausschalten in der Weboberflaeche (Reiter Anzeige, Karte Module). Die Zurueck-Taste eines Moduls fuehrt dorthin, woher man kam.
// Leiste und Pillen entstehen im Code, der PicoPixel-Entwurf bleibt unberuehrt.
#include "app_state.h"
#include "navigation.h"
#include "tagebuch.h"
#include "schimmel.h"
#include <ArduinoJson.h>
#include "../ui/screens.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"

namespace {
const uint32_t GRUEN = 0x0FA64A, AMBER = 0xD9930D, ROT = 0xCE4B11, DUNKELROT = 0x8B1D1D, MARINE = 0x0B2545, GRAU = 0x6B7C8D;

void zeigen(lv_obj_t* o, bool an) { if (!o) return; if (an) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }

// ── Herkunft: Zurueck fuehrt auf die Seite, von der aus das Modul geoeffnet wurde ──
lv_obj_t* s_herkunft = nullptr;
void oeffnen(enum ScreensEnum ziel) {
  lv_obj_t* a = lv_scr_act();
  if (a != objects.screentagebuch && a != objects.screenschimmel) s_herkunft = a;
  nav_goto(ziel);
}
void zurueck(lv_event_t*) {
  if (updateLaeuft) return;
  if (s_herkunft) lv_scr_load(s_herkunft); else nav_goto(SCREEN_ID_SCREENMENU);
}
void zuTagebuch() { oeffnen(SCREEN_ID_SCREENTAGEBUCH); }
#if SCHIMMEL_VERFUEGBAR
void zuSchimmel() { oeffnen(SCREEN_ID_SCREENSCHIMMEL); }
#endif
bool tagebuchModulAn() { return cfg.tagebuch_an; }

// ── Modul-Leiste im Menue ───────────────────────────────────────────────────────
struct ModulEintrag { const char* name; bool (*an)(); void (*oeffnen)(); };
const ModulEintrag MODULE[] = {
  {"Allergie-Tagebuch", tagebuchModulAn, zuTagebuch},
#if SCHIMMEL_VERFUEGBAR
  {"Schimmelwächter", schimmelAn, zuSchimmel},
#endif
};
const int N_MODULE = sizeof(MODULE) / sizeof(MODULE[0]);
lv_obj_t* m_leiste = nullptr; lv_obj_t* m_titel = nullptr;
lv_obj_t* m_knopf[4] = {}; lv_obj_t* m_text[4] = {};

void modulTippen(lv_event_t* e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= 0 && i < N_MODULE && MODULE[i].an()) MODULE[i].oeffnen();
}
void leisteAktualisieren(lv_event_t* = nullptr) {
  if (!m_leiste) return;
  const uint32_t kachel = theme_colors[active_theme_index][THEME_COLOR_BACKGR_CONT];
  lv_obj_set_style_bg_color(m_leiste, lv_color_hex(kachel & 0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(m_leiste, (lv_opa_t)(kachel >> 24), 0);
  lv_obj_set_style_text_color(m_titel, designDunkel() ? lv_color_hex(0xE2E8F0) : lv_color_black(), 0);
  for (int i = 0; i < N_MODULE; i++) {
    const bool an = MODULE[i].an();
    lv_obj_set_style_bg_color(m_knopf[i], lv_color_hex(an ? MARINE : GRAU), 0);
    lv_obj_set_style_bg_opa(m_knopf[i], an ? LV_OPA_COVER : LV_OPA_60, 0);
    if (an) lv_label_set_text(m_text[i], MODULE[i].name); else lv_label_set_text_fmt(m_text[i], "%s\n(ausgeschaltet)", MODULE[i].name);
  }
}
void menueLeiste() {
  if (!objects.screenmenu) return;
  lv_obj_t* c = lv_obj_create(objects.screenmenu); lv_obj_set_pos(c, 14, 284); lv_obj_set_size(c, 996, 158); lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(c, 0, 0); lv_obj_set_style_radius(c, 5, 0); lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_outline_width(c, 1, 0); lv_obj_set_style_outline_pad(c, 0, 0); lv_obj_set_style_outline_color(c, lv_color_black(), 0);
  m_leiste = c;
  m_titel = lv_label_create(c); lv_obj_set_pos(m_titel, 14, 8); lv_label_set_text(m_titel, "Module"); lv_obj_set_style_text_font(m_titel, &font_montserrat_22, 0);
  for (int i = 0; i < N_MODULE && i < 4; i++) {
    lv_obj_t* b = lv_btn_create(c); lv_obj_set_size(b, 300, 92); lv_obj_set_pos(b, 14 + i * 324, 50);
    lv_obj_set_style_radius(b, 22, 0); lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t* l = lv_label_create(b); lv_obj_set_style_text_font(l, &font_montserrat_22, 0); lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(l);
    lv_obj_add_event_cb(b, modulTippen, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    m_knopf[i] = b; m_text[i] = l;
  }
  leisteAktualisieren();
  lv_obj_add_event_cb(objects.screenmenu, leisteAktualisieren, LV_EVENT_SCREEN_LOAD_START, nullptr);
}

// ── Pillen in der Fussleiste ────────────────────────────────────────────────────
// Home sitzt auf allen Seiten bei x 465 bis 565; die Pillen halten 40 px Abstand: links 165 bis 425, rechts 605 bis 865.
const int PILLE_LINKS = 165, PILLE_RECHTS = 605;
lv_obj_t* pille(lv_obj_t* seite, int x, const char* text, lv_obj_t** punkt, void (*klick)()) {
  lv_obj_t* b = lv_btn_create(seite); lv_obj_set_size(b, 260, 40); lv_obj_set_pos(b, x, 548);          // wie die Fusstasten: y 548, 40 hoch
  lv_obj_set_ext_click_area(b, 15);
  lv_obj_set_style_radius(b, 20, 0); lv_obj_set_style_bg_color(b, lv_color_hex(MARINE), 0); lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* p = lv_obj_create(b); lv_obj_remove_style_all(p); lv_obj_set_size(p, 16, 16); lv_obj_align(p, LV_ALIGN_LEFT_MID, 6, 0);
  lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_border_width(p, 2, 0); lv_obj_set_style_border_color(p, lv_color_white(), 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, text); lv_obj_set_style_text_font(l, &font_montserrat_20, 0); lv_obj_set_style_text_color(l, lv_color_white(), 0); lv_obj_align(l, LV_ALIGN_CENTER, 10, 0);
  lv_obj_add_event_cb(b, [](lv_event_t* e) { ((void (*)())lv_event_get_user_data(e))(); }, LV_EVENT_CLICKED, (void*)klick);
  *punkt = p;
  return b;
}

lv_obj_t* t_pille = nullptr; lv_obj_t* t_punkt = nullptr;
void tagebuchPilleAktualisieren(lv_event_t* = nullptr) {
  if (!t_pille) return;
  zeigen(t_pille, cfg.tagebuch_an);
  const int st = cfg.tagebuch_an ? tagebuchHeute() : -1;     // -1 noch kein Eintrag
  lv_obj_set_style_bg_color(t_punkt, lv_color_hex(st == 0 ? GRUEN : st == 1 ? AMBER : st >= 2 ? ROT : 0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(t_punkt, st < 0 ? LV_OPA_20 : LV_OPA_COVER, 0);
}

#if SCHIMMEL_VERFUEGBAR
lv_obj_t* s_pille = nullptr; lv_obj_t* s_punkt = nullptr;
void schimmelPilleAktualisieren(lv_event_t* = nullptr) {
  if (!s_pille) return;
  zeigen(s_pille, schimmelAn());
  if (!schimmelAn()) return;
  int st = -1;
  JsonDocument d;
  if (deserializeJson(d, schimmelJson()) == DeserializationError::Ok) for (JsonObjectConst r : d["raeume"].as<JsonArrayConst>()) { const int x = r["stufe"] | 0; if (x > st) st = x; }
  const uint32_t f = st <= 0 ? GRUEN : st == 1 ? AMBER : st == 2 ? ROT : DUNKELROT;
  lv_obj_set_style_bg_color(s_punkt, lv_color_hex(st < 0 ? 0xFFFFFF : f), 0);
  lv_obj_set_style_bg_opa(s_punkt, st < 0 ? LV_OPA_20 : LV_OPA_COVER, 0);
}
#endif
}  // namespace

void uiModuleInit() {
  menueLeiste();
  // Allergie-Tagebuch: Pille links neben Home (rechts steht die Quellenangabe); auf der Tagebuch-Seite fuehrt Zurueck zur Herkunft, Weiter entfaellt
  if (objects.screenallergen) {
    t_pille = pille(objects.screenallergen, PILLE_LINKS, "Allergie-Tagebuch", &t_punkt, zuTagebuch);
    tagebuchPilleAktualisieren();
    lv_obj_add_event_cb(objects.screenallergen, tagebuchPilleAktualisieren, LV_EVENT_SCREEN_LOAD_START, nullptr);
  }
  if (objects.buttontagebuchzurueck) { lv_obj_add_flag(objects.buttontagebuchzurueck, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(objects.buttontagebuchzurueck, zurueck, LV_EVENT_CLICKED, nullptr); }
  zeigen(objects.buttontagebuchweiter, false);
#if SCHIMMEL_VERFUEGBAR
  // Schimmelwaechter: Pille rechts neben Home auf der Luftqualitaet-Seite
  if (objects.screenairquality && objects.screenschimmel) {
    s_pille = pille(objects.screenairquality, PILLE_RECHTS, "Schimmelwächter", &s_punkt, zuSchimmel);
    schimmelPilleAktualisieren();
    lv_obj_add_event_cb(objects.screenairquality, schimmelPilleAktualisieren, LV_EVENT_SCREEN_LOAD_START, nullptr);
  }
  if (objects.buttonschimmelzurueck) { lv_obj_add_flag(objects.buttonschimmelzurueck, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(objects.buttonschimmelzurueck, zurueck, LV_EVENT_CLICKED, nullptr); }
  if (objects.buttonschimmelhome) { lv_obj_add_flag(objects.buttonschimmelhome, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(objects.buttonschimmelhome, [](lv_event_t*) { nav_goto(SCREEN_ID_SCREEN_1); }, LV_EVENT_CLICKED, nullptr); }
#endif
}
