// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Allergie-Tagebuch: ein Eintrag je Tag (keine / leichte / starke Beschwerden) zusammen mit der Pollenbelastung des Tages.
// Die Eintraege liegen auf der Speicherkarte (tagebuch.csv); ohne Karte bleiben sie bis zum Neustart im Speicher.
#include <stdint.h>

struct TagebuchEintrag {
  int32_t datum;       // JJJJMMTT
  uint8_t stufe;       // 0 = keine, 1 = leichte, 2 = starke Beschwerden
  uint8_t pollen10;    // Pollenbelastung der gewaehlten Allergene (Skala 0-3) mal 10, 255 = unbekannt
  uint8_t aqi;         // europaeischer Luftqualitaetsindex, 255 = unbekannt
};

void tagebuchInit();                                   // einmal nach dem Start
bool tagebuchHeuteEintragen(int stufe);                // heutigen Eintrag setzen oder aendern (false: Uhrzeit unbekannt)
int  tagebuchHeute();                                  // Stufe von heute, -1 = noch kein Eintrag
int  tagebuchAbDatum(int32_t ab, TagebuchEintrag* out, int max);   // Eintraege ab einem Datum (JJJJMMTT), aufsteigend
bool tagebuchAufKarte();                               // wird gerade auf der Speicherkarte gespeichert?
void tagebuchDemo();
void tagebuchDemoEnde();                               // Beispieleintraege verwerfen, die echten Eintraege (Speicherkarte) wieder laden                                   // Beispieleintraege der letzten 30 Tage (nur Speicher, fuer Bildschirmfotos)
int32_t tagebuchDatumHeute(int tageZurueck = 0);       // JJJJMMTT, 0 = Uhrzeit unbekannt
class WebServer;
void tagebuchWebuiRegistrieren(WebServer& server);     // Seite /tagebuch und /api/tagebuch
