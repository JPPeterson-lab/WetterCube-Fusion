// Platzhalter (tools/deploy_ui.sh): die Firmware setzt das Icon zur Laufzeit aus icons.h
#include "src/ui/screens.h"
static const uint8_t humidity_stub_map[] = {0x00, 0x00, 0x00};
const lv_img_dsc_t humidity = {
  .header = { .cf = LV_IMG_CF_RGB565A8, .always_zero = 0, .reserved = 0, .w = 1, .h = 1 },
  .data_size = 3,
  .data = humidity_stub_map,
};
