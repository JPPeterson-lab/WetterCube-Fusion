// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Zentrale Konstanten des Projekts

#include "sdkconfig.h"
// Flackert das Display beim Schreiben der Firmware in den Flash? Nur der Arduino-IDE-Build tut das (Display-ISR nicht flash-sicher);
// der ESP-IDF-Build (platforms/idf) setzt CONFIG_LCD_RGB_ISR_IRAM_SAFE und zeigt das Bild ruhig weiter.
#if defined(CONFIG_LCD_RGB_ISR_IRAM_SAFE) && CONFIG_LCD_RGB_ISR_IRAM_SAFE
#define UPDATE_DISPLAY_FLACKERT 0
#else
#define UPDATE_DISPLAY_FLACKERT 1
#endif

// Muss mit docs/version.json übereinstimmen (sobald es Releases gibt)
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.9.9-alpha16"
#endif
#define OTA_VERSION_URL  "https://raw.githubusercontent.com/JPPeterson-lab/WetterCube-Fusion/main/docs/version.json"
#define OTA_BIN_URL      "https://jppeterson-lab.github.io/WetterCube-Fusion/firmware/firmware.bin"

#define MDNS_NAME        "wettercube-fusion"   // http://wettercube-fusion.local
#define PORTAL_SSID      "WetterCube-Fusion-Setup"
#define PREFS_NAMESPACE  "wcfusion"

// Aktualisierungsintervalle (ms)
#define INTERVALL_WETTER   600000UL    // 10 Min
#define INTERVALL_POLLEN  3600000UL    // 60 Min
#define INTERVALL_BIO    10800000UL    // 3 Std (DWD aktualisiert 2x täglich)
#define INTERVALL_WARN     900000UL    // 15 Min
#define INTERVALL_KARTE   1800000UL    // 30 Min

// Automatische Update-Prüfung (Hinweis-Symbol auf dem Hauptscreen)
#define UPDATE_CHECK_ERST      120000UL    // 2 Min. nach dem Start
#define UPDATE_CHECK_INTERVALL 21600000UL  // danach alle 6 Stunden
#define UPDATE_CHECK_RETRY     3600000UL   // nach einem Fehler nach 1 Stunde erneut

// Warnkarte: maximale Bildgröße in Pixeln (Bereich über der Button-Leiste)
#define KARTE_MAX_W  1000
#define KARTE_MAX_H   530
