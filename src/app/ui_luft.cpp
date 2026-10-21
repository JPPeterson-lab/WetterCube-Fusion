// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Luftqualitaet-Seite, zusaetzlich zu den PicoPixel-Elementen (AQI-Bogen, PM2.5, PM10, NO2, Ozon):
//  - Verlauf des europaeischen AQI fuer die naechsten 24 Stunden (Balken), die beste Zeit zum Lueften und fuer Sport umrandet
//  - "Weitere Werte": Schwefeldioxid, Kohlenmonoxid, Staub
//  - "Tipp": Leitschadstoff (welcher Stoff bestimmt den AQI) und beste Zeit
// Die drei Rahmen sind Container aus PicoPixel (ContainerAQIVerlauf, ContainerAQIWeitere, ContainerAQITipp); der Inhalt entsteht zur
// Laufzeit, weil er von den Daten abhaengt.
#include "app_state.h"
#include <math.h>
#include "../ui/screens.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"

static uint32_t s_sig = 0;

static uint32_t thema(int farbe) { return theme_colors[active_theme_index][farbe] & 0xFFFFFF; }
static uint32_t unterzeile() { return active_theme_index == THEME_DARK ? 0xE2E8F0 : 0x334155; }

static lv_obj_t* beschriftung(lv_obj_t* eltern, const lv_font_t* font, uint32_t farbe, int x, int y, int breite, lv_text_align_t ausr, const char* text) {
  lv_obj_t* l = lv_label_create(eltern);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(farbe), 0);
  lv_obj_set_style_text_align(l, ausr, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, breite);
  lv_obj_set_height(l, lv_font_get_line_height(font));          // eine Zeile
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, text);
  return l;
}

static int textBreite(const lv_font_t* f, const char* t) { lv_point_t p; lv_txt_get_size(&p, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); return p.x; }

static void leeren(lv_obj_t* o) { if (!o) return; lv_obj_clean(o); lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE); }

// ── beste Zeit: 2 Stunden am Tag (6 bis 22 Uhr) mit dem kleinsten Mittel aus AQI und Pollen-Zuschlag ──
// Rueckgabe false, wenn die Daten fehlen. start/ende sind Uhrzeiten (ende = letzte volle Stunde + 1), i0 der Index des ersten Balkens.
static bool besteZeit(int* i0, int* start, int* ende, int* aqiMittel) {
  if (pollen.aqi_h0 < 0) return false;
  float besser = 1e9f; int bi = -1;
  for (int i = 0; i + 1 < 24; i++) {
    int h = (pollen.aqi_h0 + i) % 24;
    if (h < 6 || h > 20) continue;                                // beide Stunden zwischen 6 und 22 Uhr
    if (pollen.aqi_h[i] < 0 || pollen.aqi_h[i + 1] < 0) continue;
    float z = 0;
    for (int k = 0; k < 2; k++) {
      float p = pollen.pollen_h[i + k] > 0 ? fminf(30.0f, pollen.pollen_h[i + k] / 2.0f) : 0;   // je 2 Koerner/m3 ein Punkt, hoechstens 30
      z += pollen.aqi_h[i + k] + p;
    }
    z /= 2;
    if (z < besser) { besser = z; bi = i; }
  }
  if (bi < 0) return false;
  *i0 = bi; *start = (pollen.aqi_h0 + bi) % 24; *ende = *start + 2;
  *aqiMittel = (pollen.aqi_h[bi] + pollen.aqi_h[bi + 1]) / 2;
  return true;
}

static const char* LEIT_NAME[5] = {"Feinstaub (PM2.5)", "Feinstaub (PM10)", "Stickstoffdioxid", "Ozon (O3)", "Schwefeldioxid"};

static uint32_t signatur() {
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h ^= v; h *= 16777619u; };
  mix(active_theme_index == THEME_DARK ? 1 : 2);
  mix((uint32_t)pollen.aqi_h0 + 1);
  for (int i = 0; i < 24; i++) { mix((uint32_t)(pollen.aqi_h[i] + 2)); mix((uint32_t)(int)(pollen.pollen_h[i] + 2)); }
  for (int k = 0; k < 5; k++) mix((uint32_t)(pollen.idx[k] + 2));
  mix((uint32_t)(int)(pollen.so2 + 2)); mix((uint32_t)(int)(pollen.co + 2)); mix((uint32_t)(int)(pollen.staub + 2));
  return h;
}

// ── Verlauf ───────────────────────────────────────────────────────────────────
static void baueVerlauf(lv_obj_t* host, int bestI0, bool hatBeste) {
  leeren(host);
  if (!host) return;
  const uint32_t txt = thema(THEME_COLOR_TEXT);
  beschriftung(host, &font_montserrat_22, txt, 14, 6, 700, LV_TEXT_ALIGN_LEFT, "Luftqualität in den nächsten 24 Stunden (AQI)");
  if (pollen.aqi_h0 < 0) { beschriftung(host, &font_montserrat_22, unterzeile(), 14, 100, 968, LV_TEXT_ALIGN_CENTER, "Noch keine Daten"); return; }

  const int slot = 40, breite = 32, x0 = (996 - 24 * slot) / 2, basis = 176, maxH = 118;
  int skala = 50;                                                 // Hoehe der Balken: Tageshoechstwert, mindestens AQI 50 (sonst sind niedrige Werte kaum zu sehen)
  for (int i = 0; i < 24; i++) if (pollen.aqi_h[i] > skala) skala = pollen.aqi_h[i];
  for (int i = 0; i < 24; i++) {
    const int a = pollen.aqi_h[i];
    const int x = x0 + i * slot;
    if (a >= 0) {
      int hoehe = (int)lroundf((float)a * maxH / skala); if (hoehe < 6) hoehe = 6;
      lv_obj_t* b = lv_obj_create(host);
      lv_obj_set_pos(b, x + (slot - breite) / 2, basis - hoehe); lv_obj_set_size(b, breite, hoehe);
      lv_obj_set_style_bg_color(b, aqiColor(a), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(b, 4, 0); lv_obj_set_style_border_width(b, 0, 0); lv_obj_set_style_pad_all(b, 0, 0);
      lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
      if (hatBeste && (i == bestI0 || i == bestI0 + 1)) {           // beste Zeit: dicker Rand
        lv_obj_set_style_outline_width(b, 3, 0); lv_obj_set_style_outline_color(b, lv_color_hex(txt), 0); lv_obj_set_style_outline_opa(b, LV_OPA_COVER, 0);
      }
      char buf[8]; snprintf(buf, sizeof(buf), "%d", a);
      beschriftung(host, &font_montserrat_18, txt, x, basis - hoehe - 24, slot, LV_TEXT_ALIGN_CENTER, buf);
    }
    char hb[8];
    if (i == 0) snprintf(hb, sizeof(hb), "jetzt"); else snprintf(hb, sizeof(hb), "%02d", (pollen.aqi_h0 + i) % 24);
    beschriftung(host, &font_montserrat_18, unterzeile(), x, basis + 4, slot, LV_TEXT_ALIGN_CENTER, hb);
  }

  // Legende
  static const struct { int von; const char* text; } L[5] = {{10, "Gut"}, {30, "Mittel"}, {50, "Mäßig"}, {70, "Schlecht"}, {90, "Sehr schlecht"}};
  int x = 14;
  for (int k = 0; k < 5; k++) {
    lv_obj_t* q = lv_obj_create(host);
    lv_obj_set_pos(q, x, 212); lv_obj_set_size(q, 14, 14);
    lv_obj_set_style_bg_color(q, aqiColor(L[k].von), 0); lv_obj_set_style_bg_opa(q, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(q, 3, 0); lv_obj_set_style_border_width(q, 0, 0); lv_obj_clear_flag(q, LV_OBJ_FLAG_SCROLLABLE);
    int w = textBreite(&font_montserrat_18, L[k].text);
    beschriftung(host, &font_montserrat_18, unterzeile(), x + 20, 208, w + 4, LV_TEXT_ALIGN_LEFT, L[k].text);
    x += 20 + w + 28;
  }
  if (hatBeste) beschriftung(host, &font_montserrat_18, unterzeile(), 600, 208, 382, LV_TEXT_ALIGN_RIGHT, "Dick umrandet: beste Zeit zum Lüften");
}

// ── Weitere Werte ─────────────────────────────────────────────────────────────
static void baueWeitere(lv_obj_t* host) {
  leeren(host);
  if (!host) return;
  const uint32_t txt = thema(THEME_COLOR_TEXT);
  beschriftung(host, &font_montserrat_22, txt, 14, 8, 228, LV_TEXT_ALIGN_LEFT, "Weitere Werte");
  struct Zeile { const char* name; float wert; lv_color_t farbe; } z[3] = {
    {"Schwefeldioxid (SO2)", pollen.so2, so2Color(pollen.so2)}, {"Kohlenmonoxid (CO)", pollen.co, coColor(pollen.co)}, {"Staub", pollen.staub, staubColor(pollen.staub)}};
  for (int r = 0; r < 3; r++) {
    const int y = 50 + r * 70;
    beschriftung(host, &font_montserrat_18, unterzeile(), 14, y, 228, LV_TEXT_ALIGN_LEFT, z[r].name);
    char buf[16]; if (z[r].wert >= 0) snprintf(buf, sizeof(buf), "%.0f", z[r].wert); else snprintf(buf, sizeof(buf), "--");
    lv_obj_t* v = beschriftung(host, &font_montserrat_32, txt, 14, y + 24, 140, LV_TEXT_ALIGN_LEFT, buf);
    lv_obj_set_style_text_color(v, z[r].farbe, 0);
    beschriftung(host, &font_montserrat_18, unterzeile(), 14 + textBreite(&font_montserrat_32, buf) + 8, y + 36, 100, LV_TEXT_ALIGN_LEFT, "ug/m3");
  }
}

// ── Tipp: Leitschadstoff und beste Zeit ───────────────────────────────────────
static void baueTipp(lv_obj_t* host, int start, int ende, int aqiMittel, bool hatBeste, bool jetztGut) {
  leeren(host);
  if (!host) return;
  const uint32_t txt = thema(THEME_COLOR_TEXT);
  beschriftung(host, &font_montserrat_22, txt, 14, 8, 230, LV_TEXT_ALIGN_LEFT, "Tipp");

  // Leitschadstoff: der Stoff mit dem hoechsten Einzel-AQI
  int best = -1, bestIdx = -1;
  for (int k = 0; k < 5; k++) if (pollen.idx[k] > bestIdx) { bestIdx = pollen.idx[k]; best = k; }
  beschriftung(host, &font_montserrat_18, unterzeile(), 14, 44, 230, LV_TEXT_ALIGN_LEFT, "Hauptbelastung:");
  char buf[40];
  if (best < 0) { snprintf(buf, sizeof(buf), "--"); }
  else if (bestIdx <= 20) { snprintf(buf, sizeof(buf), "Alles gut"); }
  else snprintf(buf, sizeof(buf), "%s", LEIT_NAME[best]);
  const lv_font_t* f = textBreite(&font_montserrat_32, buf) <= 230 ? &font_montserrat_32 : &font_montserrat_22;
  lv_obj_t* l = beschriftung(host, f, txt, 14, f == &font_montserrat_32 ? 68 : 74, 230, LV_TEXT_ALIGN_LEFT, buf);
  lv_obj_set_style_text_color(l, aqiColor(bestIdx), 0);
  if (best >= 0 && bestIdx > 20) { snprintf(buf, sizeof(buf), "Index %d (%s)", bestIdx, aqiStatusText(bestIdx)); beschriftung(host, &font_montserrat_18, unterzeile(), 14, 108, 230, LV_TEXT_ALIGN_LEFT, buf); }

  lv_obj_t* t = lv_obj_create(host);     // Trennlinie
  lv_obj_set_pos(t, 14, 140); lv_obj_set_size(t, 230, 1);
  lv_obj_set_style_bg_color(t, lv_color_hex(0x94A3B8), 0); lv_obj_set_style_bg_opa(t, LV_OPA_50, 0); lv_obj_set_style_border_width(t, 0, 0); lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

  beschriftung(host, &font_montserrat_18, unterzeile(), 14, 152, 230, LV_TEXT_ALIGN_LEFT, "Lüften und Sport:");
  if (!hatBeste) { beschriftung(host, &font_montserrat_32, txt, 14, 176, 230, LV_TEXT_ALIGN_LEFT, "--"); return; }
  snprintf(buf, sizeof(buf), "%d bis %d Uhr", start, ende);
  beschriftung(host, &font_montserrat_32, txt, 14, 176, 230, LV_TEXT_ALIGN_LEFT, buf);
  if (jetztGut) snprintf(buf, sizeof(buf), "Jetzt, AQI %d (%s)", aqiMittel, aqiStatusText(aqiMittel));
  else snprintf(buf, sizeof(buf), "AQI %d (%s)", aqiMittel, aqiStatusText(aqiMittel));
  lv_obj_t* s = beschriftung(host, &font_montserrat_18, txt, 14, 220, 230, LV_TEXT_ALIGN_LEFT, buf);
  lv_obj_set_style_text_color(s, aqiColor(aqiMittel), 0);
}

static void aufbauen() {
  int i0 = 0, start = 0, ende = 0, aqiM = 0;
  bool hat = besteZeit(&i0, &start, &ende, &aqiM);
  baueVerlauf(objects.containeraqiverlauf, i0, hat);
  baueWeitere(objects.containeraqiweitere);
  baueTipp(objects.containeraqitipp, start, ende, aqiM, hat, hat && i0 == 0);
}

static void aktualisierenIntern(bool erzwingen) {
  if (!objects.screenairquality) return;
  if (!erzwingen && lv_scr_act() != objects.screenairquality) return;   // nur arbeiten, wenn die Seite sichtbar ist
  uint32_t sig = signatur();
  if (sig == s_sig) return;
  s_sig = sig;
  aufbauen();
}

void uiLuftErzwingen() { s_sig = 0; aktualisierenIntern(true); }

static void zeitgeber(lv_timer_t*) { aktualisierenIntern(false); }
static void seiteGeladen(lv_event_t*) { s_sig = 0; aktualisierenIntern(false); }

void uiLuftInit() {
  if (!objects.screenairquality) return;
  lv_obj_add_event_cb(objects.screenairquality, seiteGeladen, LV_EVENT_SCREEN_LOADED, nullptr);
  lv_timer_create(zeitgeber, 2000, nullptr);
}
