// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Menü (Helligkeit, Warn-Schalter, Theme) und Helligkeitssteuerung (Nachtmodus, Dimm-Timeout)
#include "app_state.h"
#include <time.h>
#include "../board/board.h"
#include "../ui/screens.h"
#include "../ui/colors.h"

// ── Helligkeit ───────────────────────────────────────────────────────────────
static bool dimmed = false;

void setBrightness(int prozent) {
  LvLock lock;   // der I²C-Bus wird auch vom Touch-Treiber im LVGL-Task benutzt
  boardHelligkeit(constrain(prozent, 3, 100));
}

// "HH:MM" -> Minuten seit Mitternacht
static bool zeitText(const String& t, int* min) {
  if (t.length() < 5 || t[2] != ':') return false;
  *min = t.substring(0, 2).toInt() * 60 + t.substring(3, 5).toInt();
  return true;
}
static int tagesminute(int m) { return ((m % 1440) + 1440) % 1440; }

bool nachtGrenzen(int* von, int* bis) {
  int sr, ss;
  if (cfg.night_modus == 2 && zeitText(wetter.sunrise, &sr) && zeitText(wetter.sunset, &ss)) {
    *von = tagesminute(ss + cfg.night_sonne_ab);
    *bis = tagesminute(sr + cfg.night_sonne_bis);
    return true;
  }
  *von = cfg.night_from; *bis = cfg.night_to;   // feste Uhrzeiten, auch als Ersatz, solange die Sonnenzeiten fehlen
  return false;
}

// true, wenn der Nachtmodus der Fusion gerade gilt (wie in helligkeitPruefen; auch fuer WetterCube Next)
bool nachtJetzt() {
  if (cfg.night_modus == 0) return false;
  struct tm ti;
  if (!getLocalTime(&ti, 0)) return false;
  int von, bis;
  nachtGrenzen(&von, &bis);
  const int jetzt = ti.tm_hour * 60 + ti.tm_min;
  return (von < bis) ? (jetzt >= von && jetzt < bis) : (jetzt >= von || jetzt < bis);
}

// Ruhezeit: keine Warnungen auf dem Display (feste Uhrzeiten oder wie der Nachtmodus)
bool warnRuheAktiv() {
  if (cfg.ruhe_modus == 0) return false;
  struct tm ti;
  if (!getLocalTime(&ti, 0)) return false;
  int von, bis;
  if (cfg.ruhe_modus == 2) { if (cfg.night_modus == 0) return false; nachtGrenzen(&von, &bis); }
  else { von = cfg.ruhe_von; bis = cfg.ruhe_bis; }
  if (von == bis) return false;
  const int jetzt = ti.tm_hour * 60 + ti.tm_min;
  return (von < bis) ? (jetzt >= von && jetzt < bis) : (jetzt >= von || jetzt < bis);   // auch ueber Mitternacht
}

// Dunkles Design in der Nacht, danach wieder das Design, das vorher eingestellt war (nur bei den Uebergaengen, damit eine
// Umstellung von Hand nicht sofort ueberschrieben wird)
static bool nachtDesign = false;
static uint32_t tagDesign = THEME_LIGHT;
static void nachtDesignPruefen(bool nacht) {
  if (cfg.night_theme && nacht && !nachtDesign) {
    tagDesign = active_theme_index; nachtDesign = true;
    if (active_theme_index != THEME_DARK) { LvLock lock; change_color_theme(THEME_DARK); aktualisiereUI(); }
  } else if (nachtDesign && (!nacht || !cfg.night_theme)) {
    nachtDesign = false;
    if (active_theme_index != tagDesign) { LvLock lock; change_color_theme(tagDesign); aktualisiereUI(); }
  }
}

void helligkeitPruefen() {
  struct tm ti;
  bool hasTime = getLocalTime(&ti, 0);
  uint32_t inaktiv;
  { LvLock lock; inaktiv = lv_disp_get_inactive_time(NULL); }

  if (cfg.night_modus != 0 && hasTime) {
    int nowMin = ti.tm_hour * 60 + ti.tm_min, von, bis;
    nachtGrenzen(&von, &bis);
    bool inNight = (von < bis) ? (nowMin >= von && nowMin < bis) : (nowMin >= von || nowMin < bis);  // über Mitternacht
    nachtDesignPruefen(inNight);
    uiNachtUhrPruefen(inNight);
    if (inNight && !dimmed)       { setBrightness(cfg.night_brightness); dimmed = true; }
    else if (!inNight && dimmed)  { setBrightness(cfg.brightness);       dimmed = false; }
    return;
  }
  nachtDesignPruefen(false);
  uiNachtUhrPruefen(false);
  if (dimmed) {
    // Berührung → wieder hell (im Nachtmodus hellt nur die Uhrzeit auf)
    if (inaktiv < 700) { setBrightness(cfg.brightness); dimmed = false; }
    return;
  }
  // dim_timeout: Minuten, -1 = 30 Sekunden, 0 = aus
  uint32_t grenze = (cfg.dim_timeout == -1) ? 30000UL : (uint32_t)max(cfg.dim_timeout, 0) * 60000UL;
  if (grenze > 0 && inaktiv >= grenze) {
    setBrightness(10);
    dimmed = true;
  }
}

// ── Menü-Callbacks (laufen im LVGL-Task) ─────────────────────────────────────
static void cbBrightness(lv_event_t* e) {
  cfg.brightness = lv_arc_get_value(lv_event_get_target(e));
  setBrightness(cfg.brightness);
}
static void cbBrightnessLoslassen(lv_event_t*) { speichereCfg(); }   // erst beim Loslassen speichern

static void cbPollenSwitch(lv_event_t* e) { cfg.pollen_warn = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); speichereCfg(); }
static void cbDwdSwitch(lv_event_t* e) {
  cfg.warn_region = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  speichereCfg();
  aktualisiereDwdWarnBtn();   // Button auf dem Hauptscreen sofort ein-/ausblenden
}

// Beim Öffnen des Menüs Schalter und Regler auf die gespeicherten Werte setzen
static void setzeSchalter(lv_obj_t* sw, bool an) {
  if (!sw) return;
  if (an) lv_obj_add_state(sw, LV_STATE_CHECKED); else lv_obj_clear_state(sw, LV_STATE_CHECKED);
}
static void cbMenuOeffnet(lv_event_t*) {
  setzeSchalter(objects.pollenswitch, cfg.pollen_warn);
  setzeSchalter(objects.dwdswitch,    cfg.warn_region);
  if (objects.labellightchangevalue) lv_arc_set_value(objects.labellightchangevalue, cfg.brightness);
}

void uiMenuInit() {
  LvLock lock;
  auto reg = [](lv_obj_t* o, lv_event_cb_t cb, lv_event_code_t ev) { if (o) lv_obj_add_event_cb(o, cb, ev, nullptr); };
  reg(objects.labellightchangevalue, cbBrightness,          LV_EVENT_VALUE_CHANGED);
  reg(objects.labellightchangevalue, cbBrightnessLoslassen, LV_EVENT_RELEASED);
  reg(objects.pollenswitch,          cbPollenSwitch,        LV_EVENT_VALUE_CHANGED);
  reg(objects.dwdswitch,             cbDwdSwitch,           LV_EVENT_VALUE_CHANGED);
  reg(objects.screenmenu,            cbMenuOeffnet,         LV_EVENT_SCREEN_LOAD_START);
  // Der Theme-Wechsel setzt alle Farben zurück → Farbkodierungen der Daten neu setzen
  reg(objects.labelswitchtheme, [](lv_event_t*) { aktualisiereUI(); }, LV_EVENT_CLICKED);
  cbMenuOeffnet(nullptr);
}
