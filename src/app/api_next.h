// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Schnittstelle fuer WetterCube Next (kleine Zusatzdisplays, ESP32 CYD 2,8"): /api/next liefert die Daten fuer Seiten wie
// "Temperatur und Biowetter" oder "Allergene und Vorhersage" als JSON. Die Zusatzdisplays fragen die Fusion per HTTP ab (kein Broker,
// kein Home Assistant noetig) und finden sie ueber den mDNS-Dienst _wettercube._tcp.
#include <string>

class WebServer;
void apiNextRegistrieren(WebServer& server);       // GET /api/next[?b=wetter,vorhersage,pollen,bio,luft,sensoren,lueften][&e=<ETag>]
std::string nextJson(const char* bloecke);         // bloecke: Komma-Liste, leer = alle
