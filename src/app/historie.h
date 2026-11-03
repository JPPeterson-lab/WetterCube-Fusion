// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Verlaufsdaten für die Diagramme (Antippen einer Kachel): Sensoren und Außenwerte von Open-Meteo.
//  - 24 Stunden und 7 Tage liegen im Speicher (alle 10 Minuten ein Wert, beim Start aus der Speicherkarte und von Open-Meteo aufgefüllt)
//  - 30 Tage: Außenwerte direkt von Open-Meteo, Sensoren aus den Tagesdateien der Speicherkarte (stundenweise gemittelt)
#include <time.h>

enum HistBereich { HB_24H = 0, HB_7D = 1, HB_30D = 2 };
enum Hist30Status { H30_LEER = 0, H30_LAEDT = 1, H30_FERTIG = 2, H30_FEHLT = 3 };

#define HIST_MAX_PUNKTE 720

void historieTick();                       // Netzwerk-Task: Werte sammeln, Daten nachladen (kurz, darf oft aufgerufen werden)

// Außenwerte (Open-Meteo): "aussen_temp", "aussen_druck", "aussen_feuchte"; Sensoren: ihre id
// Füllt out mit bis zu max Werten (NAN = Lücke), t0 = Zeit des ersten Werts, schrittSek = Abstand. Rückgabe: Anzahl, 0 = keine Daten.
int  historieHole(const char* id, int bereich, float* out, int max, time_t* t0, int* schrittSek);

void historie30Anfordern(const char* id);  // 30 Tage für diese id im Hintergrund laden
int  historie30Status();                   // Hist30Status der letzten Anforderung
