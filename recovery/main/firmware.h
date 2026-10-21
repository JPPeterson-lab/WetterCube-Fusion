// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Prüfen und Installieren von Firmware-Abbildern (aus einer Datei auf der Karte oder aus einem OTA-Slot)
#include <stddef.h>
#include <stdint.h>
#include "esp_partition.h"

struct FwInfo {
  bool   ok = false;            // vollständig geprüft (Kopf, Chip, Projekt, Länge, SHA-256)
  char   version[32] = "";
  size_t groesse = 0;           // Länge des Abbilds in Bytes
  char   fehler[96] = "";       // Klartext, warum nicht ok
};

FwInfo pruefeDatei(const char* pfad);
FwInfo pruefeSlot(const esp_partition_t* p);
const esp_partition_t* slotHolen(int index);                                   // 0 = app0 (ota_0), 1 = app1 (ota_1)
int  slotIndex(const esp_partition_t* p);
// Datei in einen Slot schreiben und diesen als Startpartition setzen; fortschritt(0..100); true = Erfolg, sonst fehler gefüllt
bool installiereDatei(const char* pfad, const esp_partition_t* ziel, void (*fortschritt)(int), char* fehler, size_t fehlerLen);
bool setzeStartSlot(const esp_partition_t* p, char* fehler, size_t fehlerLen);
bool webPasswortEntfernen(char* fehler, size_t fehlerLen);                      // nur das Passwort der Web-Oberfläche löschen, alles andere bleibt
bool werksreset(char* fehler, size_t fehlerLen);                               // Einstellungsspeicher (NVS) löschen
