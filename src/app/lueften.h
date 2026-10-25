// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Lueften-Ampel und Feuchte-Warnung: vergleicht Raumluft (Temperatur- und Feuchtesensor je Raum) mit der Aussenluft
// (Temperatur, Feuchte, AQI, Pollen) und sagt, ob Lueften jetzt sinnvoll ist. Rein rechnerisch, keine Oberflaeche.
#include <stdint.h>

enum LueftenZustand {
  LZ_KEINE_DATEN = 0,       // kein Raum mit Temperatur- und Feuchtesensor
  LZ_OK,                    // Raumluft in Ordnung
  LZ_LUEFTEN,               // Lueften lohnt sich: Raum feucht, draussen trockener und Luft gut
  LZ_WENIG,                 // Raum feucht, aber draussen nicht trockener: Lueften bringt wenig
  LZ_NICHT_NOETIG,          // Raumluft in Ordnung, draussen aber ungeeignet (Luftqualitaet, Pollen): Fenster besser zu
  LZ_NICHT_MOEGLICH,        // Lueften noetig, aber draussen ungeeignet
  LZ_KUEHLEN,               // Sommer: Raum warm, draussen deutlich kuehler -> Fenster auf
  LZ_HITZE_ZU               // Sommer: Raum warm, draussen nicht kuehler -> Fenster und Rolllaeden zu
};

struct LueftenRaum {
  char    raum[20];
  char    feuchteName[24];  // Name des Feuchtesensors (fuer die Warnung)
  float   temp, feuchte, taupunkt, absFeuchte;   // °C, %, °C, g/m3
  uint8_t stufe;            // 0 in Ordnung, 1 erhoeht, 2 hoch (ab Grenze), 3 Schimmelgefahr (Grenze + 10)
};

struct LueftenStatus {
  LueftenZustand zustand = LZ_KEINE_DATEN;
  uint32_t farbe = 0x888888;
  char     kopf[40]   = "";          // Ueberschrift des Streifens
  char     detail[120] = "";         // Gruende und Raeume
  int      nRaeume = 0;
  LueftenRaum raum[8];
  float    aussenTemp = 0, aussenFeuchte = 0, aussenAbs = 0;
  bool     aussenBekannt = false;
  // Warnung (Warnbildschirm / ntfy): ein Raum liegt lange genug ueber der Feuchte-Grenze
  bool     warnAktiv = false;
  bool     warnSchimmel = false;     // Stufe 3 (Grenze + 10 Prozentpunkte)
  char     warnTitel[24] = "";
  char     warnRaeume[110] = "";     // bis zu drei Zeilen "Kueche: 72 % (Taupunkt 17 °C)"
  char     warnHinweis[110] = "";    // was jetzt zu tun ist
};

extern LueftenStatus lueften;          // letzter berechneter Stand (Schreiben nur in lueftenBerechnen)
void lueftenBerechnen();               // alle ca. 10 Sekunden aufrufen
LueftenStatus lueftenKopie();          // thread-sichere Kopie fuer die Oberflaeche

void lueftenDemoSetzen();              // Beispielzustand fuer Bildschirmfotos
float taupunktC(float tempC, float rhProzent);          // Magnus-Formel
float absFeuchteGm3(float tempC, float rhProzent);      // absolute Feuchte in g/m3
