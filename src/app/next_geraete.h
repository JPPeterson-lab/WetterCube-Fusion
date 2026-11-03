// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// WetterCube Next verwalten: Der Fusion merkt sich jedes Next, das bei ihm Daten abholt (Kennung = MAC-Adresse), und zeigt auf
// http://wettercube-next.local (Seite /next) einen Reiter je Next. Ist ein Next "vom Fusion verwaltet", liefert /api/next seine
// Einstellungen (Seiten, Raeume, Helligkeit, Nachtmodus) mit; das Next uebernimmt und speichert sie. Die Liste wird nur im HTTP-Server-Task benutzt (nur der Namensdienst laeuft im Netzwerk-Task).
#include <string>

class WebServer;

void nextGeraeteInit();                                  // gespeicherte Geraete laden (einmal nach dem Start)
void nextGeraeteWebuiRegistrieren(WebServer& server);    // Seite /next und /api/nextgeraete
void nextAliasPruefen();                                 // Name wettercube-next.local auf die Adresse des Fusion zeigen lassen (alle 30 s aufrufen)

// Aus /api/next: ein Next meldet sich (Kennung, Adresse, Version, aktuelle Einstellungen als Zahlenliste "c" und Raeume "r")
void nextGeraetMeldet(const char* id, const char* ip, const char* ver, const char* c, const char* r, const char* u);
const char* nextGeraeteStatusText();                     // kurzer Text fuer die Einstellungsseite ("2 Next-Displays bekannt, 1 online")
// Einstellungen eines verwalteten Next als JSON-Teil ",\"cfg\":{...}" (leer, wenn nicht verwaltet oder unbekannt)
std::string nextCfgJson(const char* id);
