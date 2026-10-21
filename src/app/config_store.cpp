// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "app_state.h"
#include "../storage/storage.h"
#include <ArduinoJson.h>

static Preferences prefs;

void ladeCfg() {
  prefs.begin(PREFS_NAMESPACE, true);
  cfg.ssid             = prefs.getString("ssid",       "");
  cfg.pass             = prefs.getString("pass",       "");
  cfg.location         = prefs.getString("location",   "");
  cfg.lat              = prefs.getFloat ("lat",         0.0f);
  cfg.lon              = prefs.getFloat ("lon",         0.0f);
  cfg.dwd_region       = prefs.getInt   ("dwd_region",  31);
  cfg.pollen_warn      = prefs.getBool  ("pollen_warn", true);
  cfg.pollen_schwelle  = prefs.getInt   ("pol_schw",    2);
  cfg.pollen_auswahl   = prefs.getInt   ("pol_ausw",    0xFF);
  cfg.hinweis_regen    = prefs.getBool  ("hw_regen",    true);
  cfg.hinweis_frost    = prefs.getBool  ("hw_frost",    true);
  cfg.verlauf_regen    = prefs.getBool  ("vl_regen",    true);
  cfg.hinweis_uv       = prefs.getBool  ("hw_uv",       true);
  cfg.hinweis_sensor   = prefs.getBool  ("hw_sens",     true);
  cfg.hinweis_druck    = prefs.getBool  ("hw_druck",    true);
  cfg.hinweis_schatten = prefs.getBool  ("hw_schat",    true);
  cfg.hinweis_gefuehlt = prefs.getBool  ("hw_gef",      true);
  cfg.schatten_temp    = prefs.getInt   ("schat_t",     27);
  cfg.hinweis_giessen  = prefs.getBool  ("hw_giess",    false);
  cfg.lueft_kuehlen    = prefs.getBool  ("lu_kuehl",    true);
  cfg.lueft_hitze      = prefs.getInt   ("lu_hitze",    24);
  cfg.night_uhr        = prefs.getBool  ("night_uhr",   false);
  cfg.anim_symbole     = prefs.getBool  ("anim_sym",    true);
  cfg.update_modus     = constrain(prefs.getInt("upd_modus", 2), 0, 2);
  { int h = prefs.getInt("upd_std", 6); cfg.update_intervall_h = (h == 1 || h == 24) ? h : 6; }
  cfg.kacheln_seite    = prefs.getInt   ("kach_seite",  12);
  cfg.ha_aktiv         = prefs.getBool  ("ha_aktiv",    false);
  cfg.ha_praefix       = prefs.getString("ha_praefix",  "wettercube-fusion");
  cfg.anim_stunden     = prefs.getBool  ("anim_std",    false);
  cfg.dim_timeout      = prefs.getInt   ("dim_time",    3);
  cfg.brightness       = prefs.getInt   ("bright",      80);
  cfg.warn_region      = prefs.getBool  ("warn_reg",    true);
  cfg.night_modus      = prefs.getInt   ("night_modus", prefs.getBool("night_mode", false) ? 1 : 0);   // aelterer Schalter: an = feste Uhrzeiten
  cfg.night_sonne_ab   = prefs.getInt   ("night_s_ab",  30);
  cfg.night_sonne_bis  = prefs.getInt   ("night_s_bis", 0);
  cfg.night_theme      = prefs.getBool  ("night_theme", false);
  cfg.ntfy_aktiv       = prefs.getBool  ("ntfy_on",     false);
  cfg.ntfy_server      = prefs.getString("ntfy_srv",    "https://ntfy.sh");
  cfg.ntfy_topic       = prefs.getString("ntfy_top",    "");
  cfg.ntfy_token       = prefs.getString("ntfy_tok",    "");
  cfg.web_pw           = prefs.getString("web_pw",      "");
  cfg.ntfy_quellen     = prefs.getInt   ("ntfy_src",    1);
  cfg.ntfy_entwarnung  = prefs.getBool  ("ntfy_ent",    true);
  cfg.lueft_ampel      = prefs.getBool  ("lu_ampel",    true);
  cfg.lueft_warnung    = prefs.getBool  ("lu_warn",     false);
  cfg.lueft_feuchte    = prefs.getInt   ("lu_feucht",   65);
  cfg.night_from       = prefs.getInt   ("night_from",  22 * 60);
  cfg.night_to         = prefs.getInt   ("night_to",     6 * 60);
  cfg.night_brightness = prefs.getInt   ("night_bright", 10);
  cfg.ruhe_modus       = prefs.getInt   ("ruhe_modus",  0);
  cfg.ruhe_von         = prefs.getInt   ("ruhe_von",    22 * 60);
  cfg.ruhe_bis         = prefs.getInt   ("ruhe_bis",     7 * 60);
  cfg.ampel_gruen_min  = prefs.getInt   ("amp_gn_min",  15);
  cfg.ampel_gruen_max  = prefs.getInt   ("amp_gn_max",  19);
  cfg.ampel_gelb_min   = prefs.getInt   ("amp_ge_min",  20);
  cfg.ampel_gelb_max   = prefs.getInt   ("amp_ge_max",  24);
  cfg.ampel_rot_min    = prefs.getInt   ("amp_ro_min",  25);
  cfg.ampel_rot_max    = prefs.getInt   ("amp_ro_max",  99);
  cfg.ampel_modus      = prefs.getInt   ("amp_modus",   0);
  cfg.bio_zone         = prefs.getString("bio_zone",    "A");
  cfg.ws_cat0          = prefs.getInt   ("ws_cat0",     0);
  cfg.ws_cat1          = prefs.getInt   ("ws_cat1",     1);
  cfg.ws_cat2          = prefs.getInt   ("ws_cat2",     2);
  cfg.ws_cat3          = prefs.getInt   ("ws_cat3",     5);
  prefs.end();
}

void speichereCfg() {
  speicherSicherungAnfordern();   // Sicherung auf der Speicherkarte nachziehen (falls eine steckt)
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString("ssid",        cfg.ssid);
  prefs.putString("pass",        cfg.pass);
  prefs.putString("location",    cfg.location);
  prefs.putFloat ("lat",         cfg.lat);
  prefs.putFloat ("lon",         cfg.lon);
  prefs.putInt   ("dwd_region",  cfg.dwd_region);
  prefs.putBool  ("pollen_warn", cfg.pollen_warn);
  prefs.putInt   ("pol_schw",    cfg.pollen_schwelle);
  prefs.putInt   ("pol_ausw",    cfg.pollen_auswahl);
  prefs.putBool  ("hw_regen",    cfg.hinweis_regen);
  prefs.putBool  ("hw_frost",    cfg.hinweis_frost);
  prefs.putBool  ("vl_regen",    cfg.verlauf_regen);
  prefs.putBool  ("hw_uv",       cfg.hinweis_uv);
  prefs.putBool  ("hw_sens",     cfg.hinweis_sensor);
  prefs.putBool  ("hw_druck",    cfg.hinweis_druck);
  prefs.putBool  ("hw_schat",    cfg.hinweis_schatten);
  prefs.putBool  ("hw_gef",      cfg.hinweis_gefuehlt);
  prefs.putInt   ("schat_t",     cfg.schatten_temp);
  prefs.putBool  ("hw_giess",    cfg.hinweis_giessen);
  prefs.putBool  ("lu_kuehl",    cfg.lueft_kuehlen);
  prefs.putInt   ("lu_hitze",    cfg.lueft_hitze);
  prefs.putBool  ("night_uhr",   cfg.night_uhr);
  prefs.putBool  ("anim_sym",    cfg.anim_symbole);
  prefs.putInt   ("upd_modus",   cfg.update_modus);
  prefs.putInt   ("upd_std",     cfg.update_intervall_h);
  prefs.putInt   ("kach_seite",  cfg.kacheln_seite);
  prefs.putBool  ("ha_aktiv",    cfg.ha_aktiv);
  prefs.putString("ha_praefix",  cfg.ha_praefix);
  prefs.putBool  ("anim_std",    cfg.anim_stunden);
  prefs.putInt   ("dim_time",    cfg.dim_timeout);
  prefs.putInt   ("bright",      cfg.brightness);
  prefs.putBool  ("warn_reg",    cfg.warn_region);
  prefs.putInt   ("night_modus", cfg.night_modus);
  prefs.putInt   ("night_s_ab",  cfg.night_sonne_ab);
  prefs.putInt   ("night_s_bis", cfg.night_sonne_bis);
  prefs.putBool  ("night_theme", cfg.night_theme);
  prefs.putBool  ("ntfy_on",     cfg.ntfy_aktiv);
  prefs.putString("ntfy_srv",    cfg.ntfy_server);
  prefs.putString("ntfy_top",    cfg.ntfy_topic);
  prefs.putString("ntfy_tok",    cfg.ntfy_token);
  prefs.putString("web_pw",      cfg.web_pw);
  prefs.putInt   ("ntfy_src",    cfg.ntfy_quellen);
  prefs.putBool  ("ntfy_ent",    cfg.ntfy_entwarnung);
  prefs.putBool  ("lu_ampel",    cfg.lueft_ampel);
  prefs.putBool  ("lu_warn",     cfg.lueft_warnung);
  prefs.putInt   ("lu_feucht",   cfg.lueft_feuchte);
  prefs.putInt   ("night_from",  cfg.night_from);
  prefs.putInt   ("night_to",    cfg.night_to);
  prefs.putInt   ("night_bright",cfg.night_brightness);
  prefs.putInt   ("ruhe_modus",  cfg.ruhe_modus);
  prefs.putInt   ("ruhe_von",    cfg.ruhe_von);
  prefs.putInt   ("ruhe_bis",    cfg.ruhe_bis);
  prefs.putInt   ("amp_gn_min",  cfg.ampel_gruen_min);
  prefs.putInt   ("amp_gn_max",  cfg.ampel_gruen_max);
  prefs.putInt   ("amp_ge_min",  cfg.ampel_gelb_min);
  prefs.putInt   ("amp_ge_max",  cfg.ampel_gelb_max);
  prefs.putInt   ("amp_ro_min",  cfg.ampel_rot_min);
  prefs.putInt   ("amp_ro_max",  cfg.ampel_rot_max);
  prefs.putInt   ("amp_modus",   cfg.ampel_modus);
  prefs.putString("bio_zone",    cfg.bio_zone);
  prefs.putInt   ("ws_cat0",     cfg.ws_cat0);
  prefs.putInt   ("ws_cat1",     cfg.ws_cat1);
  prefs.putInt   ("ws_cat2",     cfg.ws_cat2);
  prefs.putInt   ("ws_cat3",     cfg.ws_cat3);
  prefs.end();
}

// Stadtname URL-tauglich machen (Umlaute und Leerzeichen)
static String urlKodieren(const String& s) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

bool geocodeLocation(const String& city) {
  WiFiClientSecure sc;
  HTTPClient http;
  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlKodieren(city) +
               "&count=1&language=de&format=json";
  logDiag("[Geo] Suche '%s'", city.c_str());
  http.begin(sc, url);
  bool ok = false;
  int code = http.GET();
  if (code != 200) {
    logHttpFehler("Geo", code, sc);
    logDiag("[Geo] Fehlgeschlagen fuer '%s': HTTP %d", city.c_str(), code);
  } else {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString()) != DeserializationError::Ok) {
      logDiag("[Geo] Fehlgeschlagen fuer '%s': JSON-Fehler", city.c_str());
    } else if (doc["results"].size() == 0) {
      logDiag("[Geo] Fehlgeschlagen fuer '%s': kein Treffer", city.c_str());
    } else {
      cfg.lat = doc["results"][0]["latitude"].as<float>();
      cfg.lon = doc["results"][0]["longitude"].as<float>();
      logDiag("[Geo] Gefunden: %.4f, %.4f", cfg.lat, cfg.lon);
      ok = true;
    }
  }
  http.end();
  return ok;
}
