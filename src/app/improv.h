// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// WLAN per USB-Kabel einrichten (Improv Serial, https://www.improv-wifi.com/serial/): WetterCube Control oder ein Web-Installer schickt WLAN-Name und
// Passwort ueber die serielle Schnittstelle (UART0, dieselbe wie das Log; beim Fusion zusaetzlich USB-Serial/JTAG). Das Geraet nimmt sie nur im
// Einrichtungsmodus an (Portal), probiert die Verbindung aus und speichert sie erst bei Erfolg.
void improvStart();        // Task, der die Pakete liest (vor dem WLAN-Start aufrufen)
void improvPortalTick();   // im Einrichtungsmodus aus der Hauptschleife aufrufen: fuehrt einen angeforderten Verbindungsversuch aus
