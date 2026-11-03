// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Home Assistant per MQTT: Die Fusion meldet sich ueber die MQTT-Discovery als Geraet "WetterCube Fusion" an (Aussenwetter, Mond, UV,
// Luftqualitaet, Pollen, Lueften, Alarme, interner Sensor) und sendet ihren Zustand als ein JSON. Dieselbe JSON liefert /api/status
// (REST-Sensor in Home Assistant ohne MQTT). Gleiche Verbindung wie die Sensoren, kein zusaetzliches Konto.
#include <string>

void haInit();                       // nach ladeCfg(): "Letzter Wille" setzen, vor sensorenInit()
void haTakt();                       // Hauptschleife (mehrmals pro Sekunde): Discovery nach (Wieder-)Verbindung, Zustand bei Aenderung und alle 60 s
void haKonfigGeaendert();            // nach Aenderung von Schalter oder Praefix: Letzten Willen anpassen, neu verbinden
void haNeuMelden();                  // alle Entitaeten erneut senden (WebUI-Knopf)
const char* haStatusText();          // Kurztext fuer WebUI und Log
std::string statusJson();            // alle Werte als JSON (fuer /api/status und den MQTT-Zustand)
