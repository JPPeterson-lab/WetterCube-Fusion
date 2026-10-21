// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// WetterCube Fusion – Notfall-App (Wiederherstellung)
// Startet nur, wenn keine gültige Firmware vorhanden ist oder die Wiederherstellung angefordert wurde (Web-Oberfläche, Touch beim
// Start, Absturz-Hinweis). Verändert wird erst nach einer Auswahl und Bestätigung am Gerät. Die Notfall-Partition selbst wird nie
// beschrieben: bricht der Strom ab, lässt sich die Wiederherstellung einfach neu starten.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "src/display/lvgl_port.h"
#include "sd.h"
#include "firmware.h"
#include "ui.h"

static const char* TAG = "notfall";
static Anzeige A;

// ── Wunsch der Hauptfirmware (NVS "wcfrec"): Grund und Slot, der zuletzt lief ──
#ifdef NOTFALL_SNAPSHOT
// Entwicklungs-Fernsteuerung: Zeichen auf der seriellen Schnittstelle lösen Tasten aus (1 Firmware, 2 Komplett, 3 Nur Reset, 4 Normal,
// k Karte, a/b Slot A/B, j Ja, n Nein, z Zurück). Gibt es nur im Test-Build.
static void fernsteuerung(void*) {
  for (;;) {
    int c = getchar();
    int a = 0;
    switch (c) { case '1': a = A_FW; break; case '2': a = A_KOMPLETT; break; case '3': a = A_NUR_RESET; break; case '4': a = A_NORMAL; break; case '5': a = A_WEBPW; break;
      case 'k': a = A_QUELLE_KARTE; break; case 'a': a = A_QUELLE_S0; break; case 'b': a = A_QUELLE_S1; break;
      case 'j': a = A_JA; break; case 'n': a = A_NEIN; break; case 'z': a = A_ZURUECK; break; }
    if (a) { printf("FERN %c\n", c); uiSende(a); }
    else vTaskDelay(pdMS_TO_TICKS(40));
  }
}
#endif

static void flagsLesen() {
  A.grund = 0; A.slotZurueck = -1;
  nvs_handle_t h;
  if (nvs_open("wcfrec", NVS_READONLY, &h) != ESP_OK) return;
  uint8_t v = 0;
  if (nvs_get_u8(h, "grund", &v) == ESP_OK) A.grund = v;
  if (nvs_get_u8(h, "slot_zurueck", &v) == ESP_OK && v < 2) A.slotZurueck = v;
  nvs_close(h);
}

static void flagsLoeschen() {
  nvs_handle_t h;
  if (nvs_open("wcfrec", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_all(h); nvs_commit(h); nvs_close(h);
}

static void analysieren() {
  uiSplash("Prüfe Speicherkarte und Firmware ...");
  A.kartePresent = sdBereit();
  A.karteDatei = false; A.karte = FwInfo();
  A.sicherung[0] = 0;
  if (A.kartePresent) {
    struct stat st;
    if (stat("/sdcard/notfall/firmware.bin", &st) == 0) { A.karteDatei = true; A.karte = pruefeDatei("/sdcard/notfall/firmware.bin"); }
    if (stat("/sdcard/notfall/einstellungen.json", &st) == 0) {
      struct tm t; localtime_r(&st.st_mtime, &t);
      snprintf(A.sicherung, sizeof(A.sicherung), "%02d.%02d.%04d %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min);
    }
  }
  for (int i = 0; i < 2; i++) A.slot[i] = pruefeSlot(slotHolen(i));
  ESP_LOGI(TAG, "Karte: %d Datei: %d ok: %d | Slot A: %d (%s) Slot B: %d (%s)", A.kartePresent, A.karteDatei, A.karte.ok,
           A.slot[0].ok, A.slot[0].version, A.slot[1].ok, A.slot[1].version);
}

static void neustart(const char* titel, const char* text) {
  flagsLoeschen();
  uiErgebnis(true, titel, text, "");
  vTaskDelay(pdMS_TO_TICKS(3500));
  esp_restart();
}

// Slot, in den eine neue Firmware geschrieben wird: nie der Slot, der zuletzt lief (er bleibt als Rückfall erhalten)
static int zielSlot() {
  if (A.slotZurueck == 0) return 1;
  if (A.slotZurueck == 1) return 0;
  if (!A.slot[0].ok) return 0;
  if (!A.slot[1].ok) return 1;
  return 0;
}

static void fortschrittCb(int p) { uiFortschrittWert(p); }

// true = Wiederherstellung ausgeführt (Neustart folgt), false = zurück zum Menü
static bool installieren(int quelle, bool komplett) {
  char fehler[100] = "";
  if (quelle == A_QUELLE_KARTE) {
    const esp_partition_t* ziel = slotHolen(zielSlot());
    uiFortschritt("Firmware wird installiert", "Die Firmware von der Speicherkarte wird geprüft und in den Speicher geschrieben. Das dauert etwa eine Minute.");
    if (!installiereDatei("/sdcard/notfall/firmware.bin", ziel, fortschrittCb, fehler, sizeof(fehler))) {
      uiErgebnis(false, "Das hat nicht geklappt", fehler, "Zurück");
      uiWarte(); return false;
    }
  } else {
    int i = quelle == A_QUELLE_S0 ? 0 : 1;
    if (!setzeStartSlot(slotHolen(i), fehler, sizeof(fehler))) { uiErgebnis(false, "Das hat nicht geklappt", fehler, "Zurück"); uiWarte(); return false; }
  }
  if (komplett) {
    flagsLoeschen();
    if (!werksreset(fehler, sizeof(fehler))) { uiErgebnis(false, "Zurücksetzen fehlgeschlagen", fehler, "Zurück"); uiWarte(); return false; }
    neustart("Fertig", "Die neue Firmware ist installiert, alle Einstellungen wurden gelöscht.\nDas Gerät startet jetzt neu und fragt nach dem WLAN.");
  } else {
    neustart("Fertig", "Die Firmware ist wiederhergestellt, deine Einstellungen sind erhalten.\nDas Gerät startet jetzt neu.");
  }
  return true;
}

static bool waehleQuelle(bool komplett, int* quelle) {
  uiQuelle(A, komplett);
  for (;;) {
    int a = uiWarte();
    if (a == A_ZURUECK) return false;
    if (a == A_QUELLE_KARTE || a == A_QUELLE_S0 || a == A_QUELLE_S1) { *quelle = a; return true; }
  }
}

static bool bestaetigen(const char* titel, const char* text, const char* ja, bool gefaehrlich) {
  uiBestaetigen(titel, text, ja, gefaehrlich);
  for (;;) { int a = uiWarte(); if (a == A_JA) return true; if (a == A_NEIN) return false; }
}

static void normalStarten() {
  int i = (A.slotZurueck >= 0 && A.slot[A.slotZurueck].ok) ? A.slotZurueck : (A.slot[0].ok ? 0 : (A.slot[1].ok ? 1 : -1));
  char fehler[100] = "";
  if (i < 0) {
    uiErgebnis(false, "Keine Firmware vorhanden", "Im Gerät liegt keine gültige Firmware. Bitte installiere die Firmware von der Speicherkarte (notfall/firmware.bin).", "Zurück");
    uiWarte(); return;
  }
  if (!setzeStartSlot(slotHolen(i), fehler, sizeof(fehler))) { uiErgebnis(false, "Das hat nicht geklappt", fehler, "Zurück"); uiWarte(); return; }
  neustart("Normaler Start", "Die vorhandene Firmware wird gestartet.");
}

extern "C" void app_main(void) {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
  esp_err_t e = nvs_flash_init();      // nie löschen: hier liegen die Einstellungen der Hauptfirmware
  if (e != ESP_OK) ESP_LOGW(TAG, "NVS: %s", esp_err_to_name(e));
  flagsLesen();

  esp_lcd_touch_handle_t tp = boardTouchInit();               // initialisiert auch I2C (und die IO-Erweiterung des S3-Boards)
  sdEinhaengen();                                            // wie in der Hauptfirmware vor dem Display: danach liefert die Karte keine Antwort mehr
  esp_lcd_panel_handle_t panel = boardPanelInit();
  void* fb = nullptr;
  boardFramebuffer(&fb);
  if (fb) memset(fb, 0, BOARD_LCD_H_RES * BOARD_LCD_V_RES * BOARD_LCD_BYTES_PRO_PIXEL);
  boardBacklightAn();
  ESP_ERROR_CHECK(lvgl_port_init(panel, tp));
  uiInit();
#ifdef NOTFALL_SNAPSHOT
  xTaskCreate(fernsteuerung, "fern", 3072, nullptr, 2, nullptr);
#endif
  analysieren();

#ifdef NOTFALL_DEMO
  {   // Entwicklungs-Vorschau: alle Bildschirme nacheinander mit Beispieldaten (für tools/notfall_snap.py)
    Anzeige D = A;
    D.grund = 3; D.kartePresent = true; D.karteDatei = true;
    D.karte.ok = true; strcpy(D.karte.version, "0.9.9-alpha7"); D.karte.groesse = 3400000;
    D.slot[0].ok = true; strcpy(D.slot[0].version, "0.9.9-alpha7"); D.slot[1].ok = false; strcpy(D.slot[1].fehler, "Prüfsumme stimmt nicht (Datei beschädigt)");
    D.slotZurueck = 0; strcpy(D.sicherung, "17.10.2026 08:12");
    auto warte = [] { vTaskDelay(pdMS_TO_TICKS(7000)); };
    vTaskDelay(pdMS_TO_TICKS(8000));              // der erste Bildschirm (Vorschau 1) kommt vom normalen Ablauf
    uiHaupt(D); warte();
    uiQuelle(D, false); warte();
    D.karte.ok = false; strcpy(D.karte.fehler, "Prüfsumme stimmt nicht (Datei beschädigt)"); D.slot[1].ok = true; strcpy(D.slot[1].version, "0.9.9-alpha6"); D.slotZurueck = 1;
    uiQuelle(D, true); warte();
    uiBestaetigen("Firmware wiederherstellen?", "Die gewählte Firmware wird installiert. Deine Einstellungen, WLAN und Sensoren bleiben erhalten.\nDas dauert etwa eine Minute. Das Gerät dabei nicht vom Strom trennen.", "Ja, installieren", false); warte();
    uiBestaetigen("Alles zurücksetzen?", "Es wird neue Firmware installiert und danach ALLES gelöscht: WLAN-Zugang, Sensoren und alle Einstellungen. Das Gerät fragt danach wie neu nach dem WLAN.\n\nSicherung der Einstellungen auf der Karte (ohne Passwörter): 17.10.2026 08:12", "Ja, alles zurücksetzen", true); warte();
    uiFortschritt("Firmware wird installiert", "Die Firmware von der Speicherkarte wird geprüft und in den Speicher geschrieben. Das dauert etwa eine Minute."); uiFortschrittWert(42); warte();
    uiErgebnis(true, "Fertig", "Die Firmware ist wiederhergestellt, deine Einstellungen sind erhalten.\nDas Gerät startet jetzt neu.", ""); warte();
    uiErgebnis(false, "Das hat nicht geklappt", "Prüfsumme stimmt nicht (Datei beschädigt)", "Zurück"); warte();
  }
#endif
  for (;;) {
    uiHaupt(A);
    int a = uiWarte();
    int quelle = 0;
    if (a == A_FW) {
      if (!waehleQuelle(false, &quelle)) continue;
      if (!bestaetigen("Firmware wiederherstellen?", "Die gewählte Firmware wird installiert. Deine Einstellungen, WLAN und Sensoren bleiben erhalten.\nDas dauert etwa eine Minute. Das Gerät dabei nicht vom Strom trennen.", "Ja, installieren", false)) continue;
      installieren(quelle, false);
    } else if (a == A_KOMPLETT) {
      if (!waehleQuelle(true, &quelle)) continue;
      char t[400];
      snprintf(t, sizeof(t), "Es wird neue Firmware installiert und danach ALLES gelöscht: WLAN-Zugang, Sensoren und alle Einstellungen. Das Gerät fragt danach wie neu nach dem WLAN.\n\nSicherung der Einstellungen auf der Karte (ohne Passwörter): %s", A.sicherung[0] ? A.sicherung : "keine vorhanden");
      if (!bestaetigen("Alles zurücksetzen?", t, "Ja, alles zurücksetzen", true)) continue;
      installieren(quelle, true);
    } else if (a == A_NUR_RESET) {
      if (!bestaetigen("Einstellungen löschen?", "WLAN-Zugang, Sensoren und alle Einstellungen werden gelöscht. Die Firmware bleibt. Das Gerät fragt danach wie neu nach dem WLAN.", "Ja, löschen", true)) continue;
      char fehler[100] = "";
      flagsLoeschen();
      if (!werksreset(fehler, sizeof(fehler))) { uiErgebnis(false, "Das hat nicht geklappt", fehler, "Zurück"); uiWarte(); continue; }
      int i = (A.slotZurueck >= 0 && A.slot[A.slotZurueck].ok) ? A.slotZurueck : (A.slot[0].ok ? 0 : (A.slot[1].ok ? 1 : -1));
      if (i < 0) { uiErgebnis(true, "Einstellungen gelöscht", "Es ist aber keine gültige Firmware im Gerät. Bitte installiere die Firmware von der Speicherkarte.", ""); vTaskDelay(portMAX_DELAY); }
      setzeStartSlot(slotHolen(i), fehler, sizeof(fehler));
      neustart("Einstellungen gelöscht", "Das Gerät startet jetzt neu und fragt nach dem WLAN.");
    } else if (a == A_WEBPW) {
      if (!bestaetigen("Web-Passwort entfernen?", "Das Passwort der Web-Oberfläche wird gelöscht. Danach kann jeder im Heimnetz die Einstellungen ändern, bis du ein neues Passwort setzt. WLAN, Sensoren, Einstellungen und Firmware bleiben unverändert.", "Ja, entfernen", false)) continue;
      char fehler[100] = "";
      if (!webPasswortEntfernen(fehler, sizeof(fehler))) { uiErgebnis(false, "Das hat nicht geklappt", fehler, "Zurück"); uiWarte(); continue; }
      uiErgebnis(true, "Web-Passwort entfernt", "Du kannst in der Web-Oberfläche ein neues Passwort setzen.", "Weiter");
      uiWarte();
    } else if (a == A_NORMAL) {
      normalStarten();
    }
  }
}
