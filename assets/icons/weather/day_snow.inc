// Platzhalter (1x1, durchsichtig) fuer das fruehere PicoPixel-Bild day_snow; die Firmware setzt zur Laufzeit ein Symbol aus tools/wetter_icons.py.
#include "src/ui/screens.h"
static const uint8_t day_snow_map[] = { 0x00, 0x00, 0x00 };
const lv_img_dsc_t day_snow = {
  .header = { .cf = LV_IMG_CF_RGB565A8, .always_zero = 0, .reserved = 0, .w = 1, .h = 1 },
  .data_size = 3,
  .data = day_snow_map,
};
