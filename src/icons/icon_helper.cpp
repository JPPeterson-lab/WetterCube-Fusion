// Hilfsfunktion fuer Icons aus dem Code (die Tabelle selbst steht in icons.cpp und wird von tools/icons_code.py erzeugt)
#include "icons.h"

void iconSetzen(lv_obj_t* obj, const char* name, int px) {
  const lv_img_dsc_t* d = iconHolen(name);
  if (!obj || !d) return;
  lv_img_set_src(obj, d);
  lv_img_set_size_mode(obj, LV_IMG_SIZE_MODE_REAL);
  lv_img_set_antialias(obj, true);
  // Zoom aus Ziel- und Quellgroesse: ein fest gesetzter Zoom bei anderer Widget-Breite liesse LVGL das Bild kacheln oder abschneiden
  lv_img_set_zoom(obj, (uint16_t)(px * 256 / d->header.w));
  lv_obj_set_size(obj, px, px);
}
