// Erzeugt von tools/icons_code.py. Nicht von Hand aendern.
#include "icons.h"
#include <string.h>

#include "assets/icons/code/ic_thermometer.inc"
#include "assets/icons/code/ic_humidity.inc"
#include "assets/icons/code/ic_barometer.inc"
#include "assets/icons/code/ic_wind.inc"
#include "assets/icons/code/ic_compass.inc"
#include "assets/icons/code/ic_uv_index.inc"
#include "assets/icons/code/ic_raindrop.inc"
#include "assets/icons/code/ic_sunrise.inc"
#include "assets/icons/code/ic_sunset.inc"
#include "assets/icons/code/ic_pollen.inc"
#include "assets/icons/code/ic_pollen_flower.inc"
#include "assets/icons/code/ic_pollen_tree.inc"
#include "assets/icons/code/ic_pollen_grass.inc"
#include "assets/icons/code/ic_pollen_weed.inc"
#include "assets/icons/code/ic_windpfeil.inc"
#include "assets/icons/code/ic_bio_befinden.inc"
#include "assets/icons/code/ic_bio_bp_niedrig.inc"
#include "assets/icons/code/ic_bio_bp_hoch.inc"
#include "assets/icons/code/ic_bio_rheuma_entz.inc"
#include "assets/icons/code/ic_bio_rheuma_deg.inc"
#include "assets/icons/code/ic_bio_asthma.inc"
#include "assets/icons/code/ic_bio_waerme.inc"
#include "assets/icons/code/ic_humidity_36.inc"
#include "assets/icons/code/ic_barometer_36.inc"
#include "assets/icons/code/ic_windpfeil_40.inc"
#include "assets/icons/code/ic_moon_new.inc"
#include "assets/icons/code/ic_moon_waxing_crescent.inc"
#include "assets/icons/code/ic_moon_first_quarter.inc"
#include "assets/icons/code/ic_moon_waxing_gibbous.inc"
#include "assets/icons/code/ic_moon_full.inc"
#include "assets/icons/code/ic_moon_waning_gibbous.inc"
#include "assets/icons/code/ic_moon_last_quarter.inc"
#include "assets/icons/code/ic_moon_waning_crescent.inc"

struct IconEintrag { const char* name; const lv_img_dsc_t* dsc; };
static const IconEintrag ICONS[] = {
  {"thermometer", &ic_thermometer},
  {"humidity", &ic_humidity},
  {"barometer", &ic_barometer},
  {"wind", &ic_wind},
  {"compass", &ic_compass},
  {"uv-index", &ic_uv_index},
  {"raindrop", &ic_raindrop},
  {"sunrise", &ic_sunrise},
  {"sunset", &ic_sunset},
  {"pollen", &ic_pollen},
  {"pollen-flower", &ic_pollen_flower},
  {"pollen-tree", &ic_pollen_tree},
  {"pollen-grass", &ic_pollen_grass},
  {"pollen-weed", &ic_pollen_weed},
  {"windpfeil", &ic_windpfeil},
  {"bio-befinden", &ic_bio_befinden},
  {"bio-bp-niedrig", &ic_bio_bp_niedrig},
  {"bio-bp-hoch", &ic_bio_bp_hoch},
  {"bio-rheuma-entz", &ic_bio_rheuma_entz},
  {"bio-rheuma-deg", &ic_bio_rheuma_deg},
  {"bio-asthma", &ic_bio_asthma},
  {"bio-waerme", &ic_bio_waerme},
  {"humidity-36", &ic_humidity_36},
  {"barometer-36", &ic_barometer_36},
  {"windpfeil-40", &ic_windpfeil_40},
  {"moon-new", &ic_moon_new},
  {"moon-waxing-crescent", &ic_moon_waxing_crescent},
  {"moon-first-quarter", &ic_moon_first_quarter},
  {"moon-waxing-gibbous", &ic_moon_waxing_gibbous},
  {"moon-full", &ic_moon_full},
  {"moon-waning-gibbous", &ic_moon_waning_gibbous},
  {"moon-last-quarter", &ic_moon_last_quarter},
  {"moon-waning-crescent", &ic_moon_waning_crescent},
};

int iconAnzahl() { return (int)(sizeof(ICONS) / sizeof(ICONS[0])); }
const char* iconName(int i) { return (i >= 0 && i < iconAnzahl()) ? ICONS[i].name : ""; }
const lv_img_dsc_t* iconHolen(const char* name) {
  if (!name) return nullptr;
  for (const IconEintrag& e : ICONS) if (strcmp(e.name, name) == 0) return e.dsc;
  return nullptr;
}
