#pragma once
// Icons aus dem Code (nicht aus PicoPixel): keine Begrenzung durch die Bilder-Anzahl dort.
// Quellen und Umwandlung: tools/icons_code.py; Namen siehe dort (z. B. "thermometer", "pollen-tree", "windpfeil").
#include <lvgl.h>
const lv_img_dsc_t* iconHolen(const char* name);   // nullptr, wenn es den Namen nicht gibt
void iconSetzen(lv_obj_t* img, const char* name, int px);   // Icon in px Kantenlaenge auf ein lv_img-Widget setzen (Zoom aus Zielgroesse und Quellbreite)
int         iconAnzahl();
const char* iconName(int i);
