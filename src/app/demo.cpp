// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Beispieldaten für Bildschirmfotos (nur über /demo): setzen Werte im RAM, damit Bildschirme mit Inhalt statt "keine" zu sehen sind.
// "/demo?an=1&v=1|2" setzt eine Variante, "/demo?an=0" lädt alle echten Daten neu. Nichts davon wird gespeichert.
#include "app_state.h"
#include "lueften.h"
#include <time.h>

void demoDatenSetzen(int variante) {
  LvLock lock;
  // Pollen: DWD-Stufen 0–3 (heute, morgen, übermorgen) und Open-Meteo-Körner/m³ (nächste Stunden)
  pollen.dwd_hasel = 0.5f;   pollen.dwd_hasel_tmr = 0.5f;   pollen.dwd_hasel_da = 0;
  pollen.dwd_erle = 1.0f;    pollen.dwd_erle_tmr = 1.5f;    pollen.dwd_erle_da = 1.0f;
  pollen.dwd_esche = 1.5f;   pollen.dwd_esche_tmr = 2.0f;   pollen.dwd_esche_da = 1.5f;
  pollen.dwd_birke = 2.0f;   pollen.dwd_birke_tmr = 2.5f;   pollen.dwd_birke_da = 2.0f;
  pollen.dwd_graeser = 2.5f; pollen.dwd_graeser_tmr = 3.0f; pollen.dwd_graeser_da = 2.5f;
  pollen.dwd_roggen = 1.0f;  pollen.dwd_roggen_tmr = 1.5f;  pollen.dwd_roggen_da = 1.0f;
  pollen.dwd_beifuss = 0.0f; pollen.dwd_beifuss_tmr = 0.5f; pollen.dwd_beifuss_da = 0.5f;
  pollen.dwd_ambrosia = 0.0f; pollen.dwd_ambrosia_tmr = 0.0f; pollen.dwd_ambrosia_da = 1.0f;
  pollen.birke = 60; pollen.graeser = 25; pollen.erle = 12; pollen.beifuss = 2; pollen.ambrosia = 0;
  for (int i = 0; i < 3; i++) {
    pollen.h_birke[i] = 40 + 20 * i; pollen.h_graeser[i] = 10 + 12 * i; pollen.h_erle[i] = 8 + 4 * i;
    pollen.h_beifuss[i] = 1 + i;     pollen.h_ambrosia[i] = 0;          pollen.h_roggen[i] = 6 + 5 * i;
    pollen.h_esche[i] = 15 + 6 * i;  pollen.h_hasel[i] = 3 + i;
  }
  // Luftqualität
  pollen.aqi = 42; pollen.aqi_next = 48; pollen.pm25 = 11; pollen.pm25_next = 13; pollen.pm10 = 19; pollen.pm10_next = 22;
  pollen.no2 = 24; pollen.no2_next = 27; pollen.o3 = 92; pollen.o3_next = 105;
  pollen.so2 = 6; pollen.co = 210; pollen.staub = 38;
  pollen.idx[0] = 28; pollen.idx[1] = 22; pollen.idx[2] = 25; pollen.idx[3] = 42; pollen.idx[4] = 4;
  { struct tm ti; time_t t = time(nullptr); localtime_r(&t, &ti); pollen.aqi_h0 = ti.tm_hour;
    static const int kurve[24] = {42, 45, 50, 57, 62, 58, 49, 41, 33, 27, 22, 18, 16, 15, 17, 21, 26, 30, 36, 41, 44, 47, 43, 38};
    for (int i = 0; i < 24; i++) { pollen.aqi_h[i] = kurve[(pollen.aqi_h0 + i) % 24]; pollen.pollen_h[i] = 12 + 40 * ((pollen.aqi_h0 + i) % 24 >= 7 && (pollen.aqi_h0 + i) % 24 <= 15); } }
  lueftenDemoSetzen();
  // Biowetter: verschiedene Werte (0 kein, 1 positiv, 2 gering, 3 hoch)
  static const int muster[BIO_PERIODEN][7] = {
    {0, 0, 0, 0, 0, 0, 0}, {1, 0, 2, 0, 2, 0, 0}, {1, 2, 3, 1, 2, 0, 2}, {0, 3, 2, 3, 1, 2, 0}, {2, 1, 0, 2, 3, 1, 1}};
  for (int p = 0; p < BIO_PERIODEN; p++) { bio.vorhanden[p] = true; for (int k = 0; k < 7; k++) bio.wert[p][k] = muster[p][k]; }
  bio.vorhanden[0] = false;   // für heute Vormittag liefert der DWD nichts: zeigt auch den Strich
  bio.geladen = true;
  if (variante >= 1) {   // Regenhinweis, DWD-Warnung und Update-Symbol auf dem Hauptscreen
    time_t jetzt = time(nullptr); time_t start = jetzt - (jetzt % 900);
    wetter.nc_n = WetterDaten::NC_MAX;
    for (int i = 0; i < wetter.nc_n; i++) { wetter.nc_start[i] = start + 900L * i; wetter.nc_mm[i] = (i >= 2 && i <= 6) ? 0.8f : 0.0f; }
    warnungen[0].aktiv = true; warnungen[0].typ = "Gewitter"; warnungen[0].level = 2;
    warnungen[0].titel = "Amtliche WARNUNG vor GEWITTER"; warnungen[0].region = "Landkreis Beispiel";
    warnungen[0].beschreibung = "Es treten Gewitter auf. Dabei sind Windböen bis 65 km/h, kleinkörniger Hagel und Starkregen möglich.";
    warnungen[1].aktiv = true; warnungen[1].typ = "Wind"; warnungen[1].level = 1;
    warnungen[1].titel = "Vorabinformation WINDBÖEN"; warnungen[1].region = "Landkreis Beispiel";
    warnungen[1].beschreibung = "Im Warnzeitraum treten einzelne Windböen bis 60 km/h aus Südwest auf.";
    anzahl_warnungen = 2;
    updateVerfuegbar = true; updateNeueVersion = "0.5.0";
  }
  if (variante == 2) {   // Frost und UV statt Regen
    wetter.nc_n = 0;
    wetter.frost_min = -2.0f; wetter.frost_jetzt = false; wetter.frost_ab = "05:00";
    wetter.uv_hinweis = 7.0f; wetter.uv_jetzt = 5.4f;
  }
  logDiag("[Demo] Beispieldaten gesetzt (Variante %d), /demo?an=0 stellt die echten Daten wieder her", variante);
}
