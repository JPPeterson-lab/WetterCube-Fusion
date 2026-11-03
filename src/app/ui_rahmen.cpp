// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Bilderrahmen-Anzeige: Nach einer einstellbaren Zeit ohne Beruehrung zeigt das Display Fotos von der Speicherkarte (Ordner bilder/), eines nach dem
// anderen, statt auf dem Hauptscreen stehen zu bleiben. Antippen zeigt den Hauptscreen. Optional auch nachts (gedimmt) statt der Nacht-Uhr.
// Das Dekodieren dauert einige Sekunden und laeuft in einem eigenen Task; der Bildschirm (schwarz, ein Bild) entsteht im Code, nicht in PicoPixel.
#include "app_state.h"
#ifdef FUSION_BILDERRAHMEN
#include "navigation.h"
#include "bilderrahmen.h"
#include "../ui/screens.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static lv_obj_t* s_scr = nullptr;
static lv_obj_t* s_img = nullptr;
static lv_img_dsc_t s_dsc;
static uint16_t* s_px = nullptr;                    // das gerade gezeigte Bild
static volatile bool s_laedt = false;               // Task laeuft
static volatile bool s_neuDa = false;               // Task ist fertig: s_neu gueltig (nur bei Erfolg px != nullptr)
static RahmenBild s_neu;
static int s_index = -1;                            // zuletzt gezeigtes Bild
static int s_fehler = 0;                            // Bilder in Folge, die nicht ladbar waren
static unsigned long s_wechsel = 0;                 // Zeitpunkt des letzten Wechsels
static unsigned long s_sperreBis = 0;               // nach lauter Fehlern eine Weile nicht mehr versuchen

static void zurueck(lv_event_t*) { nav_goto(SCREEN_ID_SCREEN_1); }

static void bauen() {
  if (s_scr) return;
  s_scr = lv_obj_create(nullptr);
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
  s_img = lv_img_create(s_scr);
  lv_obj_add_event_cb(s_scr, zurueck, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);
}

static void freigeben() {
  if (s_img) lv_img_set_src(s_img, nullptr);
  if (s_px) { heap_caps_free(s_px); s_px = nullptr; }
}

static void ladeTask(void* arg) {
  const int idx = (int)(intptr_t)arg;
  RahmenBild b;
  if (!rahmenDekodieren(idx, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL), &b)) { b.px = nullptr; b.w = b.h = 0; }
  s_neu = b;
  s_neu.name[sizeof(s_neu.name) - 1] = 0;
  s_neuDa = true;
  s_laedt = false;
  vTaskDelete(nullptr);
}

static void ladenStarten(int index) {
  if (s_laedt) return;
  s_laedt = true; s_neuDa = false;
  s_index = index;
  if (xTaskCreatePinnedToCore(ladeTask, "rahmen", 12 * 1024, (void*)(intptr_t)index, 1, nullptr, 0) != pdPASS) s_laedt = false;
}

// Das fertig dekodierte Bild zeigen (UI-Lock gehalten); false = nichts gezeigt (Bild nicht ladbar)
static bool zeigen() {
  s_neuDa = false;
  if (!s_neu.px) { if (++s_fehler >= 5) s_sperreBis = millis() + 10UL * 60000UL; return false; }
  s_fehler = 0;
  bauen();
  uint16_t* alt = s_px;
  s_px = s_neu.px; s_neu.px = nullptr;
  s_dsc.header.always_zero = 0;
  s_dsc.header.w = (uint32_t)s_neu.w; s_dsc.header.h = (uint32_t)s_neu.h;
  s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  s_dsc.data_size = (uint32_t)s_neu.w * s_neu.h * 2;
  s_dsc.data = (const uint8_t*)s_px;
  lv_img_set_src(s_img, &s_dsc);
  lv_obj_align(s_img, LV_ALIGN_CENTER, 0, 0);
  lv_obj_invalidate(s_scr);
  if (alt) heap_caps_free(alt);
  s_wechsel = millis();
  return true;
}

static bool wartescreen(lv_obj_t* a) {
  return a == objects.screenboot || a == objects.screenwarnungpollen || a == objects.screenwarnunglueften;
}

// Aus helligkeitPruefen() alle 500 ms; nacht = Nachtmodus gerade aktiv
void uiRahmenPruefen(bool nacht) {
  if (updateLaeuft) return;
  const int n = cfg.rahmen_an ? rahmenAnzahl() : 0;          // Dateiliste (zwischengespeichert), vor dem UI-Lock
  LvLock lock;
  lv_obj_t* aktiv = lv_scr_act();
  const bool aufRahmen = s_scr && aktiv == s_scr;
  const bool erlaubt = n > 0 && (!nacht || cfg.rahmen_nacht) && millis() >= s_sperreBis;
  if (aufRahmen) {
    if (!erlaubt) { nav_goto(SCREEN_ID_SCREEN_1); return; }
    if (s_neuDa) { zeigen(); return; }
    if (!s_laedt && millis() - s_wechsel >= (unsigned long)cfg.rahmen_sek * 1000UL) ladenStarten((s_index + 1) % n);
    return;
  }
  if (s_px && !s_laedt) freigeben();                          // nicht mehr gezeigt: Speicher zurueck
  if (!erlaubt || wartescreen(aktiv)) return;
  if (s_neuDa) { if (zeigen()) lv_scr_load(s_scr); else if (n > 1) ladenStarten((s_index + 1) % n); return; }
  if (s_laedt) return;
  if (lv_disp_get_inactive_time(NULL) >= (uint32_t)cfg.rahmen_min * 60000UL) ladenStarten((s_index + 1) % n);
}

// Soll der Bilderrahmen nachts die Nacht-Uhr ersetzen? (ui_nacht.cpp)
bool rahmenStattNachtUhr() { return cfg.rahmen_an && cfg.rahmen_nacht && rahmenAnzahl() > 0; }
#else
void uiRahmenPruefen(bool) {}
bool rahmenStattNachtUhr() { return false; }
#endif
