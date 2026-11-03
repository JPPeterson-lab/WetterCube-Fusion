// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Notfall-Anbindung der Hauptfirmware: Wiederherstellung starten (Web-Oberfläche, Touch beim Start, Absturz-Hinweis).
// Die Notfall-App liegt in der geschützten Partition "factory". Zum Starten wird der Wunsch in den NVS geschrieben und der Bereich
// "otadata" gelöscht: ohne gültigen Eintrag startet der Bootloader die Notfall-App. Sie stellt den Eintrag später wieder her.
#include "app_state.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs.h"
#include "esp_attr.h"
#include "navigation.h"
#include "../ui/screens.h"
#include "../ui/fonts/fonts.h"

static RTC_NOINIT_ATTR uint32_t s_absturzMagic;
static RTC_NOINIT_ATTR uint32_t s_absturzZaehler;     // Abstürze (Panic, Watchdog, Brownout) in Folge, seit dem letzten gesunden Start
static lv_obj_t* s_bootHinweis = nullptr;
static lv_obj_t* s_popup = nullptr;

static const esp_partition_t* notfallPartition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
}

// Enthält die Notfall-Partition eine gültige Notfall-App? (version leer lassen, wenn nicht gebraucht)
bool notfallBereit(String* version) {
  const esp_partition_t* f = notfallPartition();
  esp_app_desc_t d;
  if (!f || esp_ota_get_partition_description(f, &d) != ESP_OK) return false;
  if (strcmp(d.project_name, "wettercube_fusion_notfall") != 0) return false;
  uint8_t hash[32];
  if (esp_partition_get_sha256(f, hash) != ESP_OK) return false;     // Abbild vollständig und lesbar
  if (version) *version = d.version;
  return true;
}

// Wiederherstellung starten: grund 1 = angefordert, 2 = Touch beim Start, 3 = mehrfach abgestürzt
bool notfallStarten(int grund) {
  if (!notfallBereit(nullptr)) { logDiag("[Notfall] Notfall-App nicht installiert oder beschädigt, kein Wechsel"); return false; }
  const esp_partition_t* lauf = esp_ota_get_running_partition();
  int slot = (lauf && lauf->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) ? 1 : 0;
  nvs_handle_t h;
  if (nvs_open("wcfrec", NVS_READWRITE, &h) != ESP_OK) return false;
  nvs_set_u8(h, "grund", (uint8_t)grund); nvs_set_u8(h, "slot_zurueck", (uint8_t)slot); nvs_commit(h); nvs_close(h);
  const esp_partition_t* od = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
  if (!od || esp_partition_erase_range(od, 0, od->size) != ESP_OK) { logDiag("[Notfall] otadata lässt sich nicht löschen"); return false; }
  logDiag("[Notfall] Wechsel in die Wiederherstellung (Grund %d, zuletzt gelaufen: Slot %d)", grund, slot);
  delay(300);
  esp_restart();
  return true;
}

// ── Touch beim Start: auf dem Bootscreen steht kurz ein Hinweis, Berühren öffnet die Wiederherstellung ──
static void bootBerührt(lv_event_t*) {
  if (esp_timer_get_time() < 5000000LL) notfallStarten(2);
}
static void bootHinweisWeg(lv_timer_t* t) {
  if (s_bootHinweis) { lv_obj_del(s_bootHinweis); s_bootHinweis = nullptr; }
  lv_obj_remove_event_cb(objects.screenboot, bootBerührt);
  lv_timer_del(t);
}

// ── Absturz-Hinweis: nach drei Abstürzen in Folge fragt das Gerät, ob die Wiederherstellung geöffnet werden soll ──
static void popupSchliessen() { if (s_popup) { lv_obj_del(s_popup); s_popup = nullptr; } }
static void popupOeffnen(lv_event_t*) { notfallStarten(3); popupSchliessen(); }
static void popupWeiter(lv_event_t*) { s_absturzZaehler = 0; popupSchliessen(); }

void notfallHinweisPruefen() {
  if (s_absturzZaehler < 3 || otaWartetAufGueltig() || !notfallBereit(nullptr)) return;
  LvLock lock;
  if (s_popup) return;
  s_popup = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_popup, 1024, 600);
  lv_obj_set_style_bg_color(s_popup, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_popup, LV_OPA_70, 0);
  lv_obj_set_style_border_width(s_popup, 0, 0);
  lv_obj_set_style_radius(s_popup, 0, 0);
  lv_obj_clear_flag(s_popup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* k = lv_obj_create(s_popup);
  lv_obj_set_size(k, 840, 380); lv_obj_center(k);
  lv_obj_set_style_bg_color(k, lv_color_hex(0x1a1a2e), 0);
  lv_obj_set_style_border_color(k, lv_color_hex(0xf5a623), 0);
  lv_obj_set_style_border_width(k, 3, 0);
  lv_obj_set_style_radius(k, 16, 0);
  lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* t = lv_label_create(k);
  lv_obj_set_style_text_font(t, &font_montserrat_32, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(0xf5a623), 0);
  lv_label_set_text(t, "Das Gerät stürzt immer wieder ab");
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_t* x = lv_label_create(k);
  lv_obj_set_style_text_font(x, &font_montserrat_22, 0);
  lv_obj_set_style_text_color(x, lv_color_hex(0xffffff), 0);
  lv_label_set_long_mode(x, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(x, 780);
  lv_obj_set_style_text_align(x, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(x, "Es ist mehrfach hintereinander neu gestartet. Du kannst die Wiederherstellung öffnen und dort die Firmware erneuern oder die Einstellungen zurücksetzen. Dabei wird nichts ohne deine Bestätigung verändert.");
  lv_obj_align(x, LV_ALIGN_TOP_MID, 0, 70);
  const char* texte[2] = {"Wiederherstellung öffnen", "Weiter"};
  lv_event_cb_t cbs[2] = {popupOeffnen, popupWeiter};
  uint32_t farben[2] = {0x1f6feb, 0x374151};
  for (int i = 0; i < 2; i++) {
    lv_obj_t* b = lv_btn_create(k);
    lv_obj_set_size(b, 380, 72);
    lv_obj_align(b, i == 0 ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT, i == 0 ? 10 : -10, -10);
    lv_obj_set_style_bg_color(b, lv_color_hex(farben[i]), 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &font_montserrat_22, 0);
    lv_label_set_text(l, texte[i]);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cbs[i], LV_EVENT_CLICKED, nullptr);
  }
  logDiag("[Notfall] %u Abstürze in Folge: Hinweis auf die Wiederherstellung wird gezeigt", (unsigned)s_absturzZaehler);
}

// früh im Start (vor allem anderen): Abstürze zählen
void notfallZaehlen() {
  esp_reset_reason_t rr = esp_reset_reason();
  bool absturz = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT);
  if (!absturz) s_absturzZaehler = 0;
  else if (s_absturzMagic == 0x4E4F5446u) s_absturzZaehler++;
  else s_absturzZaehler = 1;
  s_absturzMagic = 0x4E4F5446u;
}

// 60 s ohne Absturz: der Zähler beginnt wieder bei null
void notfallGesund() { s_absturzZaehler = 0; }

// Einmal nach dem Aufbau der Oberfläche: Hinweis auf dem Bootscreen anlegen
void notfallInit() {
  if (!objects.screenboot || !notfallBereit(nullptr)) return;
  s_bootHinweis = lv_label_create(objects.screenboot);
  lv_obj_set_style_text_font(s_bootHinweis, &font_montserrat_22, 0);
  lv_obj_set_style_text_color(s_bootHinweis, lv_color_hex(0x8b949e), 0);
  lv_label_set_text(s_bootHinweis, "Zum Wiederherstellen jetzt den Bildschirm berühren");
  lv_obj_align(s_bootHinweis, LV_ALIGN_BOTTOM_MID, 0, -24);
  lv_obj_add_flag(objects.screenboot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(objects.screenboot, bootBerührt, LV_EVENT_PRESSED, nullptr);
  lv_timer_create(bootHinweisWeg, 5000, nullptr);
}
