// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Schimmelwaechter (nur ESP32-P4, uebernommen aus dem Apex): rechnet je Raum aus Raumluft (Temperatur, Feuchte) und Aussentemperatur die Feuchte an
// der kuehlsten Stelle der Aussenwand (Oberflaechentemperatur nach Temperaturfaktor fRsi) und bewertet die Schimmelgefahr; zeigt Taupunkt,
// Hoechstfeuchte, die kritischen Stunden des Tages und einen 7-Tage-Verlauf (Speicherkarte). Nutzt die Raumsensoren wie die Lueften-Ampel.
// Abschaltbar (Seite /schimmel). Reine Orientierung, kein Gutachten.
// Der S3 bekommt keine neuen Funktionen (knapper interner RAM): dort sind alle Funktionen leer und die Seite gibt es nicht.
#include "sdkconfig.h"
#include <string>

#if CONFIG_IDF_TARGET_ESP32P4
#define SCHIMMEL_VERFUEGBAR 1
#else
#define SCHIMMEL_VERFUEGBAR 0
#endif

class WebServer;
void   schimmelInit();
void   schimmelTick();                                // Netzwerk-Task, etwa alle 30 s
bool   schimmelAn();
void   schimmelWebuiRegistrieren(WebServer& server);
std::string schimmelJson();
bool   schimmelEinstellungSetzen(const std::string& json, std::string* fehler);
struct SchimmelWert { double taupunkt, wandTemp, wandFeuchte, maxFeuchte; int stufe; };   // stufe 0 ok, 1 erhoeht, 2 Schimmelgefahr, 3 akut
SchimmelWert schimmelBewerten(double tRaum, double feuchte, double tAussen, double frsi);
void   schimmelDemoSetzen(bool an);                    // Beispielraeume fuer Bildschirmfotos

// Display-Seite (ui_schimmel.cpp); Aufrufer haelt den LVGL-Lock
void   uiSchimmelInit();
void   aktualisiereSchimmel();
