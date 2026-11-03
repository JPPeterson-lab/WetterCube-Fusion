// Platzhalter (1x1, durchsichtig) fuer das fruehere PicoPixel-Bild mist; die Firmware setzt zur Laufzeit ein Symbol aus tools/wetter_icons.py.
#include "src/ui/screens.h"
static const uint8_t mist_map[] = { 0x00, 0x00, 0x00 };
const lv_img_dsc_t mist = {
  .header = { .cf = LV_IMG_CF_RGB565A8, .always_zero = 0, .reserved = 0, .w = 1, .h = 1 },
  .data_size = 3,
  .data = mist_map,
};
