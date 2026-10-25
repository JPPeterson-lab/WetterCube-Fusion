// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "src/display/lvgl_port.h"

extern "C" { extern const lv_font_t font_montserrat_22; extern const lv_font_t font_montserrat_32; }
#define F22 (&font_montserrat_22)
#define F32 (&font_montserrat_32)

static QueueHandle_t s_q = nullptr;
static lv_obj_t* s_scr = nullptr;
static lv_obj_t* s_balken = nullptr;
static lv_obj_t* s_prozent = nullptr;

#ifdef NOTFALL_SNAPSHOT
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
// Bildschirm halb skaliert (512 x 300, RGB565) als Base64-Zeilen "S:..." ausgeben, Abschluss "SEND"
static void snapDump(lv_timer_t*) {
  lv_obj_t* scr = lv_scr_act();
  uint32_t sz = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
  void* buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return;
  lv_img_dsc_t dsc;
  if (lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &dsc, buf, sz) == LV_RES_OK) {
    const uint16_t* px = (const uint16_t*)buf;
    printf("SNAP 512 300\n");
    static uint16_t zeile[512]; static unsigned char rle[1600]; static unsigned char b64[2200];
    for (int y = 0; y < 600; y += 2) {
      for (int x = 0; x < 1024; x += 2) zeile[x / 2] = px[y * 1024 + x];
      size_t m = 0;   // Lauflängenkodierung: [Anzahl (max. 255), Pixel niedrig, Pixel hoch]
      for (int x = 0; x < 512;) {
        int n = 1; while (x + n < 512 && n < 255 && zeile[x + n] == zeile[x]) n++;
        rle[m++] = n; rle[m++] = zeile[x] & 0xFF; rle[m++] = zeile[x] >> 8; x += n;
      }
      size_t n = 0; mbedtls_base64_encode(b64, sizeof(b64), &n, rle, m);
      b64[n] = 0; printf("S:%s\n", b64); vTaskDelay(pdMS_TO_TICKS(2));
    }
    printf("SEND\n");
  }
  heap_caps_free(buf);
}
static void snapPlanen() { lv_timer_t* t = lv_timer_create(snapDump, 900, nullptr); lv_timer_set_repeat_count(t, 1); }
#else
static void snapPlanen() {}
#endif

struct Sperre { Sperre() { lvgl_port_lock(-1); } ~Sperre() { lvgl_port_unlock(); } };

static void cbTaste(lv_event_t* e) { int a = (int)(intptr_t)lv_event_get_user_data(e); xQueueSend(s_q, &a, 0); }

static void leeren() {
  snapPlanen();
  lv_obj_clean(s_scr);
  lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x12162b), 0);
  lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
  s_balken = s_prozent = nullptr;
}

static lv_obj_t* text(const lv_font_t* f, uint32_t farbe, int x, int y, int w, const char* t, lv_text_align_t ausr = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t* l = lv_label_create(s_scr);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(farbe), 0);
  lv_obj_set_style_text_align(l, ausr, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, w);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, t);
  return l;
}

// Große Taste mit Titel und Erklärung darunter; aktiv=false: grau und ohne Reaktion
static void taste(int x, int y, int w, int h, uint32_t farbe, const char* titel, const char* unter, int aktion, bool aktiv = true) {
  lv_obj_t* b = lv_btn_create(s_scr);
  lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(aktiv ? farbe : 0x3a3f55), 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* t = lv_label_create(b);
  lv_obj_set_style_text_font(t, F32, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(aktiv ? 0xffffff : 0x9aa0b8), 0);
  lv_label_set_text(t, titel);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 6, 2);
  if (unter && unter[0]) {
    lv_obj_t* u = lv_label_create(b);
    lv_obj_set_style_text_font(u, F22, 0);
    lv_obj_set_style_text_color(u, lv_color_hex(aktiv ? 0xe6ecff : 0x9aa0b8), 0);
    lv_label_set_long_mode(u, LV_LABEL_LONG_DOT);
    lv_obj_set_width(u, w - 24);
    lv_label_set_text(u, unter);
    lv_obj_align(u, LV_ALIGN_TOP_LEFT, 6, 42);
  }
  if (aktiv) lv_obj_add_event_cb(b, cbTaste, LV_EVENT_CLICKED, (void*)(intptr_t)aktion);
  else lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
}

void uiInit() {
  s_q = xQueueCreate(8, sizeof(int));
  Sperre l;
  s_scr = lv_obj_create(nullptr);
  lv_scr_load(s_scr);
}

void uiSende(int a) { xQueueSend(s_q, &a, 0); }

int uiWarte() { int a = 0; xQueueReceive(s_q, &a, portMAX_DELAY); return a; }

void uiSplash(const char* t) {
  Sperre l; leeren();
  text(F32, 0xffffff, 40, 240, 944, "Wiederherstellung", LV_TEXT_ALIGN_CENTER);
  text(F22, 0xc8cee8, 40, 300, 944, t, LV_TEXT_ALIGN_CENTER);
}

static const char* grundText(int g) {
  switch (g) {
    case 1:  return "Du hast die Wiederherstellung gestartet.";
    case 2:  return "Du hast beim Start den Bildschirm berührt.";
    case 3:  return "Das Gerät ist mehrfach hintereinander abgestürzt.";
    default: return "Die Firmware startet nicht oder es ist keine gültige Firmware vorhanden.";
  }
}

void uiHaupt(const Anzeige& a) {
  Sperre l; leeren();
  text(F32, 0xffffff, 32, 14, 700, "Wiederherstellung");
  text(F22, 0xffc857, 32, 58, 960, grundText(a.grund));
  text(F22, 0xc8cee8, 32, 90, 960, "Es wurde noch nichts verändert. Wähle, was passieren soll:");
  taste(32, 128, 960, 84, 0x1f6feb, "Firmware wiederherstellen", "Einstellungen, WLAN und Sensoren bleiben erhalten", A_FW);
  taste(32, 222, 960, 84, 0xb45309, "Komplett zurücksetzen", "Werkszustand mit neuer Firmware: löscht WLAN, Sensoren und Einstellungen", A_KOMPLETT);
  taste(32, 316, 960, 84, 0x7c3aed, "Nur Einstellungen löschen", "Die Firmware bleibt, WLAN und alle Einstellungen werden gelöscht", A_NUR_RESET);
  taste(32, 410, 470, 84, 0x374151, "Normal starten", "Ohne Änderungen starten", A_NORMAL);
  taste(522, 410, 470, 84, 0x0e7490, "Web-Passwort entfernen", "Alles andere bleibt", A_WEBPW);
  char z[200];
  if (!a.kartePresent) snprintf(z, sizeof(z), "Speicherkarte: nicht gefunden");
  else if (!a.karteDatei) snprintf(z, sizeof(z), "Speicherkarte: gefunden, aber ohne Firmware-Datei (notfall/firmware.bin)");
  else if (a.karte.ok) snprintf(z, sizeof(z), "Speicherkarte: Firmware Version %s, geprüft", a.karte.version);
  else snprintf(z, sizeof(z), "Speicherkarte: Firmware-Datei unbrauchbar (%s)", a.karte.fehler);
  text(F22, 0x9aa0b8, 32, 508, 960, z);
  snprintf(z, sizeof(z), "Sicherung der Einstellungen auf der Karte: %s", a.sicherung[0] ? a.sicherung : "keine");
  text(F22, 0x9aa0b8, 32, 538, 960, z);
}

static void quellenText(char* out, size_t n, const FwInfo& f, const char* extra) {
  if (f.ok) snprintf(out, n, "Version %s, geprüft%s", f.version, extra);
  else snprintf(out, n, "nicht nutzbar: %s", f.fehler[0] ? f.fehler : "keine Firmware");
}

void uiQuelle(const Anzeige& a, bool komplett) {
  Sperre l; leeren();
  text(F32, 0xffffff, 32, 14, 960, komplett ? "Woher soll die neue Firmware kommen?" : "Welche Firmware soll laufen?");
  text(F22, 0xffc857, 32, 58, 960, komplett ? "Danach werden WLAN, Sensoren und alle Einstellungen gelöscht." : "Deine Einstellungen bleiben erhalten.");
  char u[160], k[160];
  if (!a.kartePresent) snprintf(k, sizeof(k), "Keine Speicherkarte gefunden");
  else if (!a.karteDatei) snprintf(k, sizeof(k), "Datei notfall/firmware.bin nicht gefunden");
  else quellenText(k, sizeof(k), a.karte, "");
  taste(32, 100, 960, 84, 0x1f6feb, "Von der Speicherkarte", k, A_QUELLE_KARTE, a.kartePresent && a.karteDatei && a.karte.ok);
  for (int i = 0; i < 2; i++) {
    const char* extra = (a.slotZurueck == i) ? ", lief zuletzt" : (a.slotZurueck >= 0 ? ", vorherige Version" : "");
    quellenText(u, sizeof(u), a.slot[i], extra);
    char titel[48]; snprintf(titel, sizeof(titel), "Aus dem Gerät (Platz %c)", 'A' + i);
    taste(32, 194 + i * 94, 960, 84, 0x0f766e, titel, u, A_QUELLE_S0 + i, a.slot[i].ok);
  }
  taste(32, 400, 960, 70, 0x374151, "Zurück", "", A_ZURUECK);
}

void uiBestaetigen(const char* titel, const char* t, const char* ja, bool gefaehrlich) {
  Sperre l; leeren();
  text(F32, gefaehrlich ? 0xff7b72 : 0xffffff, 32, 40, 960, titel);
  text(F22, 0xe6ecff, 32, 110, 960, t);
  taste(32, 420, 470, 84, gefaehrlich ? 0xb91c1c : 0x1f6feb, ja, "", A_JA);
  taste(522, 420, 470, 84, 0x374151, "Abbrechen", "", A_NEIN);
}

void uiFortschritt(const char* titel, const char* t) {
  Sperre l; leeren();
  text(F32, 0xffc857, 32, 70, 960, titel, LV_TEXT_ALIGN_CENTER);
  text(F32, 0xff7b72, 32, 140, 960, "Gerät NICHT vom Strom trennen!", LV_TEXT_ALIGN_CENTER);
  text(F22, 0xe6ecff, 32, 210, 960, t, LV_TEXT_ALIGN_CENTER);
  s_balken = lv_bar_create(s_scr);
  lv_obj_set_size(s_balken, 760, 30); lv_obj_set_pos(s_balken, 132, 380);
  lv_bar_set_range(s_balken, 0, 100);
  lv_obj_set_style_bg_color(s_balken, lv_color_hex(0x333355), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_balken, lv_color_hex(0x4cd964), LV_PART_INDICATOR);
  s_prozent = text(F32, 0xffffff, 32, 430, 960, "0 %", LV_TEXT_ALIGN_CENTER);
}

void uiFortschrittWert(int p) {
  Sperre l;
  if (s_balken) lv_bar_set_value(s_balken, p, LV_ANIM_OFF);
  if (s_prozent) lv_label_set_text_fmt(s_prozent, "%d %%", p);
}

void uiErgebnis(bool ok, const char* titel, const char* t, const char* knopf) {
  Sperre l; leeren();
  text(F32, ok ? 0x4cd964 : 0xff7b72, 32, 60, 960, titel, LV_TEXT_ALIGN_CENTER);
  text(F22, 0xe6ecff, 52, 150, 920, t, LV_TEXT_ALIGN_CENTER);
  if (knopf && knopf[0]) taste(272, 420, 480, 84, 0x1f6feb, knopf, "", A_ZURUECK);
}
