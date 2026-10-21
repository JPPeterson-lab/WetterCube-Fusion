// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Oberfläche der Notfall-App: große Tasten, einfache Sprache. Die Tasten schicken eine Aktion an die Warteschlange,
// ausgeführt wird im Hauptablauf (main.cpp), nie im LVGL-Callback.
#include "firmware.h"

enum Aktion { A_FW = 1, A_KOMPLETT, A_NUR_RESET, A_NORMAL, A_WEBPW, A_QUELLE_KARTE, A_QUELLE_S0, A_QUELLE_S1, A_JA, A_NEIN, A_ZURUECK };

struct Anzeige {
  int   grund = 0;                 // 0 keine gültige Firmware / startet nicht, 1 angefordert (Web-Oberfläche, Menü), 2 Touch beim Start, 3 mehrfach abgestürzt
  bool  kartePresent = false;
  bool  karteDatei = false;
  FwInfo karte;                    // notfall/firmware.bin auf der Karte
  FwInfo slot[2];                  // Firmware in app0 und app1
  int   slotZurueck = -1;          // Slot, der zuletzt lief (-1 unbekannt)
  char  sicherung[48] = "";        // Zeitpunkt der letzten Sicherung der Einstellungen auf der Karte (leer = keine)
};

void uiInit();
void uiSplash(const char* text);
void uiHaupt(const Anzeige& a);
void uiQuelle(const Anzeige& a, bool komplett);
void uiBestaetigen(const char* titel, const char* text, const char* jaText, bool gefaehrlich);
void uiFortschritt(const char* titel, const char* text);
void uiFortschrittWert(int prozent);
void uiErgebnis(bool ok, const char* titel, const char* text, const char* knopf);
void uiSende(int aktion);   // Taste von außen auslösen (Entwicklungs-Fernsteuerung über die serielle Schnittstelle)
int  uiWarte();   // blockiert, bis eine Taste gedrückt wurde (Wert aus Aktion)
