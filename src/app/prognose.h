// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Prognose-Check (nur P4, nach dem Vorbild des WetterCube Apex): Der Fusion legt einmal am Tag die erste Vorhersage (Hoechst- und Tiefstwert
// der naechsten drei Tage) auf der Speicherkarte ab (klima/prognose.csv) und vergleicht sie spaeter mit den tatsaechlichen Tageswerten aus dem
// Verlauf (verlauf/JJJJ-MM-TT.csv): mit dem eigenen Aussensensor, sobald er 3 Tage Werte hat (abschaltbar, cfg.prognose_sensor), sonst mit den
// Wetterdaten (aussen_temp). Angezeigt im Zusatzfeld. Ohne Speicherkarte gibt es keine Auswertung.
#include <stdint.h>

struct PrognoseErgebnis {
  bool  fertig = false;              // Auswertung wurde berechnet
  bool  sdFehlt = false;             // keine Speicherkarte
  int   n[3] = {};                   // ausgewertete Tage je Vorlauf (1, 2, 3 Tage vorher), die letzten 30 Tage
  float abwMax[3] = {}, abwMin[3] = {};   // mittlere Abweichung (Betrag) von Hoechst- und Tiefstwert in Grad
  int   trefferMax[3] = {};          // Prozent der Tage, an denen der Hoechstwert hoechstens 2 Grad daneben lag
  bool  gestern = false;             // Vorhersage von gestern fuer gestern vorhanden
  float gFcMax = 0, gFcMin = 0, gIstMax = 0, gIstMin = 0;
  char  quelle[24] = "";             // verglichen mit diesem Aussensensor, leer = mit den Wetterdaten (Open-Meteo)
  int   sensorTage = 0;              // Tage mit Werten des Aussensensors, solange es weniger als 3 sind (dann noch Wetterdaten)
};

void prognoseTick();                          // Netzwerk-Task: Vorhersage ablegen, Auswertung auffrischen (auf dem S3 leer)
bool prognoseHole(PrognoseErgebnis* out);     // false: noch keine Auswertung
