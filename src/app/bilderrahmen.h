// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Bilderrahmen: Fotos (JPEG) aus dem Ordner bilder/ der Speicherkarte. Dieses Modul listet, laedt und dekodiert sie (ohne LVGL) und bietet die
// Web-Schnittstelle zum Hochladen und Loeschen. Die Anzeige steht in ui_rahmen.cpp.
#include <stdint.h>
#include <stddef.h>

class WebServer;

struct RahmenBild {
  uint16_t* px = nullptr;   // RGB565, Zeile fuer Zeile, im PSRAM (Aufrufer gibt es mit heap_caps_free frei)
  int w = 0, h = 0;         // Groesse in Pixeln (passt in maxW x maxH, Seitenverhaeltnis bleibt)
  char name[40] = "";
};

int  rahmenAnzahl(bool neuLesen = false);                                  // Anzahl der JPEG-Dateien in bilder/ (0 ohne Karte)
bool rahmenName(int index, char* out, size_t n);                            // Dateiname des Bildes Nr. index
bool rahmenDekodieren(int index, int maxW, int maxH, RahmenBild* out);      // laedt und dekodiert (mehrere Sekunden: nicht im UI-Task aufrufen)
void rahmenWebuiRegistrieren(WebServer& server);                            // /api/bilder, /api/bild (Hochladen), /api/bild_loeschen
