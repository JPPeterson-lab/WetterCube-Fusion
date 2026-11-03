// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Animierte Wettersymbole (Meteocons): vorgerenderte Bilderfolgen als PNG in der Firmware (assets/icons/anim/wetteranim.bin,
// erzeugt von tools/anim_pack.py). Ein LVGL-Timer entpackt je Bild per lodepng in einen von zwei PSRAM-Puffern und tauscht die Quelle
// des Bild-Widgets. Nur beim ESP32-P4 (FUSION_WETTERANIM); auf dem S3 bleiben die statischen Symbole.
#include <lvgl.h>

#ifdef FUSION_WETTERANIM
bool wetterAnimVerfuegbar();                                                                       // Animationsdaten in der Firmware vorhanden
// Widget obj zeigt die Animation "name" (z. B. "overcast_day_rain") in zielPx Kantenlaenge, links oben bei (xOben, yOben).
// Rueckgabe false: es gibt keine Animation dieses Namens (dann statisches Symbol nehmen). Gleicher Name und gleiche Lage: tut nichts.
bool wetterAnimSetzen(lv_obj_t* obj, const char* name, int zielPx, int xOben, int yOben);
void wetterAnimStoppen(lv_obj_t* obj);                                                              // Animation dieses Widgets beenden, Puffer freigeben
#else
static inline bool wetterAnimVerfuegbar() { return false; }
static inline bool wetterAnimSetzen(lv_obj_t*, const char*, int, int, int) { return false; }
static inline void wetterAnimStoppen(lv_obj_t*) {}
#endif
