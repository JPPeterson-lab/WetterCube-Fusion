// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Board-Schicht: alles, was vom jeweiligen Waveshare-Board abhaengt. Das uebrige Programm kennt nur diese Funktionen.
//   src/board/s3/  ESP32-S3-Touch-LCD-7B   (RGB-Display, IO-Erweiterung CH422G, SD ueber SPI)       Backup, nur noch Fehlerbehebungen
//   src/board/p4/  ESP32-P4-WIFI6-Touch-LCD-7B (MIPI-DSI-Display EK79007, Helligkeit per PWM, SD ueber SDMMC, WLAN ueber ESP32-C6)
// Das Ziel waehlt CMake (IDF_TARGET): es wird nur das passende Verzeichnis uebersetzt.
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "driver/i2c_master.h"
#include "sdmmc_cmd.h"
#include "../hardware/touch.h"

#define BOARD_LCD_H_RES 1024
#define BOARD_LCD_V_RES 600
#define BOARD_LCD_BYTES_PRO_PIXEL 2        // RGB565

const char* boardName();                                  // z. B. "ESP32-P4-WIFI6-Touch-LCD-7B"
esp_lcd_touch_handle_t boardTouchInit();                  // initialisiert auch den I2C-Bus (und, wo vorhanden, die IO-Erweiterung)
esp_lcd_panel_handle_t boardPanelInit();                  // Display starten (Rueckgabe: Panel-Handle fuer esp_lcd_panel_draw_bitmap)
void  boardFramebuffer(void** fb);                        // erster Framebuffer (RGB565), nullptr wenn nicht zugaenglich
void  boardBacklightAn();                                 // Hintergrundbeleuchtung einschalten (Helligkeit wie zuletzt gesetzt)
void  boardHelligkeit(int prozent);                       // Helligkeit 3 bis 100 Prozent
uint32_t boardBilderGezaehlt();                           // fertig ausgegebene Bilder seit dem Start (Diagnose), 0 wenn nicht verfuegbar
i2c_master_bus_handle_t boardI2cBus();                    // Bus fuer weitere Geraete (z. B. Umweltsensor), nullptr vor boardTouchInit()
// Speicherkarte einhaengen (FAT unter "mount"). true bei Erfolg; diag bekommt bei Misserfolg eine Zeile fuer das Log (kann leer bleiben)
bool  boardSdEinhaengen(const char* mount, sdmmc_card_t** card, char* diag, size_t diagGroesse);
