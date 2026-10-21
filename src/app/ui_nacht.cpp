// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Nacht-Uhr: Im Nachtmodus (wenn eingeschaltet) zeigt das Display nur noch die grosse, gedimmte Uhr mit Datum, Wettersymbol und
// Aussentemperatur. Antippen zeigt den normalen Hauptscreen; nach 45 Sekunden ohne Beruehrung kehrt die Nacht-Uhr zurueck.
// Der Bildschirm (ScreenNachtUhr, schwarz) kommt aus PicoPixel, Schriften und Farben setzt der Code.
#include "app_state.h"
#include "navigation.h"
#include <time.h>
#include "../ui/screens.h"
#include "../ui/fonts/fonts.h"
#include "wetteranim.h"

static bool s_bereit = false;
static lv_obj_t* s_gefuehlt = nullptr;   // "gefühlt X °C" unter der Wetterzeile (im Code angelegt, die Nacht-Uhr wird ohnehin im Code angeordnet)
// Senkrechte Aufteilung (600 px): Uhrzeit 115 px, Datum, Wetterzeile
static const int ZEILE_ZEIT = 140, ZEILE_DATUM = 300, ZEILE_WETTER = 390, SYMBOL_PX = 64, LUECKE_PX = 20;
static const uint32_t FARBE_ZEIT = 0xA3AAB4, FARBE_NEBEN = 0x7B828C;   // gedimmtes Grau auf Schwarz

// Farben und Hintergrund: werden bei jedem Aktualisieren neu gesetzt, weil der Wechsel des Designs (auch das dunkle Design nachts)
// die Farben aller Elemente auf die Werte des Designs zuruecksetzt
static void farbenSetzen() {
  if (objects.labelnachtzeit)  lv_obj_set_style_text_color(objects.labelnachtzeit,  lv_color_hex(FARBE_ZEIT),  0);
  if (objects.labelnachtdatum) lv_obj_set_style_text_color(objects.labelnachtdatum, lv_color_hex(FARBE_NEBEN), 0);
  if (objects.labelnachttemp)  lv_obj_set_style_text_color(objects.labelnachttemp,  lv_color_hex(FARBE_NEBEN), 0);
  if (s_gefuehlt)              lv_obj_set_style_text_color(s_gefuehlt,              lv_color_hex(FARBE_NEBEN), 0);
  lv_obj_set_style_bg_color(objects.screennachtuhr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(objects.screennachtuhr, LV_OPA_COVER, 0);
}

static void einrichten() {
  if (s_bereit || !objects.screennachtuhr) return;
  s_bereit = true;
  auto stil = [](lv_obj_t* l, const lv_font_t* f, lv_text_align_t a, int x, int y, int w) {
    if (!l) return;
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(l, w); lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_align(l, a, 0);
  };
  // Ganze Breite, Text zentriert: so sitzt jede Zeile mittig, egal was PicoPixel an Position und Breite exportiert hat.
  // Die Uhrzeit nutzt die grosse Schrift nur mit Ziffern (font_montserrat_160_160, 115 px hoch)
  stil(objects.labelnachtzeit,  &font_montserrat_160_160, LV_TEXT_ALIGN_CENTER, 0, ZEILE_ZEIT,  1024);
  stil(objects.labelnachtdatum, &font_montserrat_32,      LV_TEXT_ALIGN_CENTER, 0, ZEILE_DATUM, 1024);
  stil(objects.labelnachttemp,  &font_montserrat_48,      LV_TEXT_ALIGN_LEFT,   0, ZEILE_WETTER, 300);
  if (objects.labelnachttemp) lv_obj_set_width(objects.labelnachttemp, LV_SIZE_CONTENT);   // Breite folgt dem Text, damit Symbol und Temperatur als Paar mittig stehen
  s_gefuehlt = lv_label_create(objects.screennachtuhr);
  lv_label_set_long_mode(s_gefuehlt, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(s_gefuehlt, 1024);
  lv_obj_set_pos(s_gefuehlt, 0, ZEILE_WETTER + SYMBOL_PX + 14);
  lv_obj_set_style_text_font(s_gefuehlt, &font_montserrat_32, 0);
  lv_obj_set_style_text_align(s_gefuehlt, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_gefuehlt, lv_color_hex(FARBE_NEBEN), 0);
  lv_label_set_text(s_gefuehlt, "");
}

// Symbol (64 px) und Temperatur als Paar waagerecht mittig, senkrecht aufeinander ausgerichtet
static void wetterZeileSetzen(bool symbolNeu, const lv_img_dsc_t* d, const char* animName) {
  if (!objects.labelnachttemp) return;
  lv_obj_update_layout(objects.labelnachttemp);
  const int tw = lv_obj_get_width(objects.labelnachttemp), th = lv_obj_get_height(objects.labelnachttemp);
  const int x0 = (1024 - (SYMBOL_PX + LUECKE_PX + tw)) / 2;
  lv_obj_set_pos(objects.labelnachttemp, x0 + SYMBOL_PX + LUECKE_PX, ZEILE_WETTER + (SYMBOL_PX - th) / 2);
  static int letztesX = -1;
  if (!objects.imagenachtwetter || !d) return;
  if (cfg.anim_symbole && animName && wetterAnimSetzen(objects.imagenachtwetter, animName, SYMBOL_PX, x0, ZEILE_WETTER)) { letztesX = x0; return; }   // animiert (P4)
  wetterAnimStoppen(objects.imagenachtwetter);
  if (symbolNeu || x0 != letztesX) { wetterIconZeichnen(objects.imagenachtwetter, d, SYMBOL_PX, x0, ZEILE_WETTER); letztesX = x0; }
}

static void aktualisieren() {
  einrichten();
  farbenSetzen();
  struct tm ti;
  if (getLocalTime(&ti, 0)) {
    char b[24]; static const char* wt[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
    strftime(b, sizeof(b), "%H:%M", &ti);
    if (objects.labelnachtzeit && strcmp(lv_label_get_text(objects.labelnachtzeit), b) != 0) lv_label_set_text(objects.labelnachtzeit, b);
    snprintf(b, sizeof(b), "%s. %02d.%02d.%04d", wt[ti.tm_wday], ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    if (objects.labelnachtdatum && strcmp(lv_label_get_text(objects.labelnachtdatum), b) != 0) lv_label_set_text(objects.labelnachtdatum, b);
  }
  if (objects.labelnachttemp) {
    char b[24]; if (wetter.geladen) snprintf(b, sizeof(b), "%.0f °C", wetter.temp); else snprintf(b, sizeof(b), "-- °C");
    if (strcmp(lv_label_get_text(objects.labelnachttemp), b) != 0) lv_label_set_text(objects.labelnachttemp, b);
  }
  if (s_gefuehlt) {
    char b[40]; if (wetter.geladen) snprintf(b, sizeof(b), "gefühlt %.0f °C", wetter.feels_like); else b[0] = 0;
    if (strcmp(lv_label_get_text(s_gefuehlt), b) != 0) lv_label_set_text(s_gefuehlt, b);
  }
  if (objects.imagenachtwetter && wetter.geladen) {
    const lv_img_dsc_t* d = wmoZuImage(wetter.wmo_code, wetter.is_day);
    if (d) wetterZeileSetzen(lv_img_get_src(objects.imagenachtwetter) != (const void*)d, d, wmoZuAnimName(wetter.wmo_code, wetter.is_day));
    lv_obj_set_style_img_opa(objects.imagenachtwetter, LV_OPA_60, 0);   // gedimmt wie die Schrift
  } else wetterZeileSetzen(false, nullptr, nullptr);
}

void uiNachtUhrErzwingen() { aktualisieren(); }

static void antippen(lv_event_t*) { nav_goto(SCREEN_ID_SCREEN_1); }

// Aus helligkeitPruefen() alle 500 ms: nacht = Nachtmodus gerade aktiv
void uiNachtUhrPruefen(bool nacht) {
  if (!objects.screennachtuhr) return;
  LvLock lock;
  lv_obj_t* aktiv = lv_scr_act();
  const bool soll = nacht && cfg.night_uhr;
  if (aktiv == objects.screennachtuhr) {
    if (!soll) nav_goto(SCREEN_ID_SCREEN_1); else aktualisieren();
    return;
  }
  if (!soll) return;
  if (aktiv == objects.screenboot || aktiv == objects.screenwarnungpollen || aktiv == objects.screenwarnunglueften) return;   // Warnungen nicht verdecken
  if (lv_disp_get_inactive_time(NULL) >= 45000) { aktualisieren(); nav_goto(SCREEN_ID_SCREENNACHTUHR); }
}

void uiNachtInit() {
  LvLock lock;
  if (!objects.screennachtuhr) return;
  einrichten();
  lv_obj_add_event_cb(objects.screennachtuhr, antippen, LV_EVENT_CLICKED, nullptr);
}
