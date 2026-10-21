// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Home Assistant per MQTT (Discovery + Zustand), siehe homeassistant.h. Die Meldungen entstehen mit snprintf in einem PSRAM-Puffer
// (der interne RAM ist knapp); gesendet wird ueber den MQTT-Client der Sensoren (sensorenMqttSende).
#include "homeassistant.h"
#include "app_state.h"
#include "app_config.h"
#include "lueften.h"
#include "../sensors/sensors.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"

namespace {
struct Entitaet { const char* key; const char* name; const char* art; const char* klasse; const char* einheit; const char* icon; };   // art: "sensor" oder "binary_sensor"
const Entitaet E[] = {
  {"temp",          "Außentemperatur",          "sensor", "temperature",            "°C",    ""},
  {"gefuehlt",      "Gefühlte Temperatur",      "sensor", "temperature",            "°C",    ""},
  {"temp_min",      "Tiefsttemperatur heute",   "sensor", "temperature",            "°C",    ""},
  {"temp_max",      "Höchsttemperatur heute",   "sensor", "temperature",            "°C",    ""},
  {"feuchte",       "Außenluftfeuchte",         "sensor", "humidity",               "%",     ""},
  {"druck",         "Luftdruck",                "sensor", "atmospheric_pressure",   "hPa",   ""},
  {"wind",          "Windgeschwindigkeit",      "sensor", "wind_speed",             "km/h",  ""},
  {"wind_richtung", "Windrichtung",             "sensor", "",                       "°",     "mdi:compass-outline"},
  {"uv",            "UV-Index jetzt",           "sensor", "",                       "",      "mdi:sun-wireless-outline"},
  {"uv_max",        "UV-Index Tagesmaximum",    "sensor", "",                       "",      "mdi:sun-wireless"},
  {"wetter",        "Wetter",                   "sensor", "",                       "",      "mdi:weather-partly-cloudy"},
  {"mond",          "Mondphase",                "sensor", "",                       "",      "mdi:moon-waning-crescent"},
  {"mond_hell",     "Mond beleuchtet",          "sensor", "",                       "%",     "mdi:moon-full"},
  {"aqi",           "Luftqualität (AQI)",       "sensor", "aqi",                    "",      ""},
  {"pm25",          "Feinstaub PM2.5",          "sensor", "pm25",                   "µg/m³", ""},
  {"pm10",          "Feinstaub PM10",           "sensor", "pm10",                   "µg/m³", ""},
  {"o3",            "Ozon",                     "sensor", "ozone",                  "µg/m³", ""},
  {"no2",           "Stickstoffdioxid",         "sensor", "nitrogen_dioxide",       "µg/m³", ""},
  {"pollen",        "Pollen stärkste Stufe",    "sensor", "",                       "",      "mdi:flower-pollen"},
  {"pollen_name",   "Pollen Leitallergen",      "sensor", "",                       "",      "mdi:flower-pollen-outline"},
  {"lueften",       "Lüften",                   "sensor", "",                       "",      "mdi:window-open-variant"},
  {"alarme",        "Sensor-Alarme",            "sensor", "",                       "",      "mdi:alert-circle-outline"},
  {"dwd",           "DWD-Warnungen",            "sensor", "",                       "",      "mdi:weather-lightning-rainy"},
  {"int_temp",      "Gehäuse Temperatur",       "sensor", "temperature",            "°C",    ""},
  {"int_feuchte",   "Gehäuse Luftfeuchte",      "sensor", "humidity",               "%",     ""},
  {"int_druck",     "Gehäuse Luftdruck",        "sensor", "atmospheric_pressure",   "hPa",   ""},
  {"lueften_jetzt", "Lüften empfohlen",         "binary_sensor", "",                "",      "mdi:window-open"},
  {"feuchte_warn",  "Feuchte-Warnung",          "binary_sensor", "problem",         "",      ""},
  {"sensor_alarm",  "Sensor-Alarm",             "binary_sensor", "problem",         "",      ""},
};
const int NE = (int)(sizeof(E) / sizeof(E[0]));

char* s_buf = nullptr;                 // Sendepuffer (PSRAM)
const size_t BUF = 2600;
char s_node[16] = "";                  // eindeutige Kennung aus der MAC-Adresse
char s_base[40] = "";                  // Themenpraefix
bool s_aktivMerker = false;            // war schon einmal aktiv gemeldet (zum Entfernen beim Abschalten)
uint32_t s_verbZaehler = 0;
int  s_phase = 0;                      // 0 nichts, 1 Konfigurationen werden gesendet, 2 fertig
int  s_naechste = 0;
bool s_neuAnfordern = false, s_entfernen = false;
unsigned long s_tZustand = 0, s_tTakt = 0, s_tStatus = 0;
uint32_t s_zustandHash = 0;
unsigned long s_gemeldetVor = 0;       // millis() der letzten Zustandsmeldung
int s_gemeldet = 0;                    // Zahl der gesendeten Entitaeten
char s_status[96] = "aus";

void praefixSetzen() {
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(s_node, sizeof(s_node), "wcf_%02x%02x%02x", mac[3], mac[4], mac[5]);
  const char* p = cfg.ha_praefix.c_str();
  size_t n = 0;
  for (; *p && n < sizeof(s_base) - 1; p++) {                       // nur Kleinbuchstaben, Ziffern, - und _
    char c = *p; if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') s_base[n++] = c;
  }
  s_base[n] = 0;
  if (!s_base[0]) strlcpy(s_base, "wettercube-fusion", sizeof(s_base));
}

// Text fuer JSON: Anfuehrungszeichen und Rueckstriche escapen
void jsonText(char* ziel, size_t n, const char* t) {
  size_t k = 0;
  for (; *t && k + 2 < n; t++) { if (*t == '"' || *t == '\\') ziel[k++] = '\\'; ziel[k++] = (*t == '\n' || *t == '\r') ? ' ' : *t; }
  ziel[k] = 0;
}

bool sensorWert(const char* id, float* v) {
  int i = sensorenSuche(id); SensorCfg c; SensorWert w;
  if (i < 0 || !sensorenHole(i, &c, &w) || !w.gueltig) return false;
  *v = w.wert; return true;
}

// Zustand als JSON in den Puffer; fehlende Werte werden weggelassen
size_t zustandBauen(char* b, size_t n) {
  size_t k = 0;
  auto f = [&](const char* key, float v, int dez) { if (k + 40 < n) k += snprintf(b + k, n - k, "\"%s\":%.*f,", key, dez, v); };
  auto i = [&](const char* key, int v) { if (k + 40 < n) k += snprintf(b + k, n - k, "\"%s\":%d,", key, v); };
  auto t = [&](const char* key, const char* v) { char e[120]; jsonText(e, sizeof(e), v); if (k + 160 < n) k += snprintf(b + k, n - k, "\"%s\":\"%s\",", key, e); };
  auto bo = [&](const char* key, bool v) { if (k + 40 < n) k += snprintf(b + k, n - k, "\"%s\":%s,", key, v ? "true" : "false"); };
  b[k++] = '{';
  if (wetter.geladen) {
    f("temp", wetter.temp, 1); f("gefuehlt", wetter.feels_like, 1); f("temp_min", wetter.temp_min, 1); f("temp_max", wetter.temp_max, 1);
    f("feuchte", wetter.humidity, 0); f("druck", wetter.pressure, 0); f("wind", wetter.wind_speed, 0); i("wind_richtung", wetter.wind_dir);
    f("uv", wetter.uv_jetzt, 1); f("uv_max", wetter.uv_index, 1);
    t("wetter", wetterText(wetter.wmo_code)); i("wmo", wetter.wmo_code); bo("tag", wetter.is_day);
  }
  struct tm ti;
  if (getLocalTime(&ti, 0)) { float hell; int ph; mondPhase(time(nullptr), &hell, &ph); t("mond", mondName(ph)); f("mond_hell", hell, 0); }
  if (pollen.aqi >= 0) i("aqi", pollen.aqi);
  if (pollen.pm25 >= 0) f("pm25", pollen.pm25, 1);
  if (pollen.pm10 >= 0) f("pm10", pollen.pm10, 1);
  if (pollen.o3 >= 0) f("o3", pollen.o3, 0);
  if (pollen.no2 >= 0) f("no2", pollen.no2, 0);
  { float best = -1; int bi = -1;
    for (int p = 0; p < 8; p++) { if (!pollenGewaehlt(p)) continue; float w = pollenWertWarnung(p); if (w > best) { best = w; bi = p; } }
    if (best >= 0) { f("pollen", best, 1); t("pollen_name", best > 0 && bi >= 0 ? POLLEN_ANZEIGE[bi] : "keine"); } }
  { LueftenStatus ls = lueftenKopie();
    t("lueften", ls.kopf[0] ? ls.kopf : "keine Daten");
    bo("lueften_jetzt", ls.zustand == LZ_LUEFTEN || ls.zustand == LZ_KUEHLEN);
    bo("feuchte_warn", ls.warnAktiv); }
  { int al[SENSOR_MAX]; int na = sensorenAlarme(al, SENSOR_MAX); i("alarme", na); bo("sensor_alarm", na > 0); }
  i("dwd", anzahl_warnungen);
  { float v; if (sensorWert("int_temp", &v)) f("int_temp", v, 1); if (sensorWert("int_feuchte", &v)) f("int_feuchte", v, 0); if (sensorWert("int_druck", &v)) f("int_druck", v, 0); }
  k += snprintf(b + k, n - k, "\"version\":\"%s\"}", FIRMWARE_VERSION);
  return k;
}

bool ipText(char* ziel, size_t n) {
  esp_netif_t* nf = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip;
  if (!nf || esp_netif_get_ip_info(nf, &ip) != ESP_OK || ip.ip.addr == 0) return false;
  snprintf(ziel, n, IPSTR, IP2STR(&ip.ip));
  return true;
}

// Discovery-Konfiguration einer Entitaet (leer = Entitaet in Home Assistant loeschen)
void konfigSenden(const Entitaet& e, bool loeschen) {
  char topic[96];
  snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s_%s/config", e.art, s_node, s_node, e.key);
  if (loeschen) { sensorenMqttSende(topic, "", 0, true); return; }
  char ip[20] = ""; ipText(ip, sizeof(ip));
  size_t k = 0;
  const bool bin = strcmp(e.art, "binary_sensor") == 0;
  k += snprintf(s_buf + k, BUF - k, "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"stat_t\":\"%s/zustand\",\"val_tpl\":\"{{ value_json.%s | default(none) }}\","
                "\"avty_t\":\"%s/status\"", e.name, s_node, e.key, s_base, e.key, s_base);
  if (bin) k += snprintf(s_buf + k, BUF - k, ",\"pl_on\":\"True\",\"pl_off\":\"False\"");
  if (e.klasse[0]) k += snprintf(s_buf + k, BUF - k, ",\"dev_cla\":\"%s\"", e.klasse);
  if (!bin && e.einheit[0]) k += snprintf(s_buf + k, BUF - k, ",\"unit_of_meas\":\"%s\"", e.einheit);
  if (!bin && e.einheit[0] && strcmp(e.key, "mond_hell") != 0) k += snprintf(s_buf + k, BUF - k, ",\"stat_cla\":\"measurement\"");
  if (e.icon[0]) k += snprintf(s_buf + k, BUF - k, ",\"ic\":\"%s\"", e.icon);
  k += snprintf(s_buf + k, BUF - k, ",\"dev\":{\"ids\":[\"%s\"],\"name\":\"WetterCube Fusion\",\"mf\":\"JPPeterson-lab\",\"mdl\":\"WetterCube Fusion\",\"sw\":\"%s\"%s%s%s}}",
                s_node, FIRMWARE_VERSION, ip[0] ? ",\"cu\":\"http://" : "", ip, ip[0] ? "/\"" : "");
  sensorenMqttSende(topic, s_buf, 0, true);
}

uint32_t hash(const char* s) { uint32_t h = 2166136261u; for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; } return h; }
}  // namespace

std::string statusJson() {
  char* b = (char*)heap_caps_malloc(BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!b) return "{}";
  size_t n = zustandBauen(b, BUF);
  std::string s(b, n);
  heap_caps_free(b);
  return s;
}

const char* haStatusText() { return s_status; }

void haInit() {
  s_buf = (char*)heap_caps_malloc(BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  praefixSetzen();
  if (cfg.ha_aktiv) { char t[64]; snprintf(t, sizeof(t), "%s/status", s_base); sensorenMqttLetzterWille(t, "offline"); s_aktivMerker = true; }
  strlcpy(s_status, cfg.ha_aktiv ? "wartet auf die MQTT-Verbindung" : "aus", sizeof(s_status));
}

void haKonfigGeaendert() {
  const bool war = s_aktivMerker;
  if (war && !cfg.ha_aktiv) s_entfernen = true;                      // abgeschaltet: Entitaeten in Home Assistant loeschen
  char alt[40]; strlcpy(alt, s_base, sizeof(alt));
  praefixSetzen();
  if (cfg.ha_aktiv) { char t[64]; snprintf(t, sizeof(t), "%s/status", s_base); sensorenMqttLetzterWille(t, "offline"); }
  else sensorenMqttLetzterWille("", "");
  if (cfg.ha_aktiv && strcmp(alt, s_base) != 0) s_neuAnfordern = true;
  if (cfg.ha_aktiv) s_aktivMerker = true;
  s_phase = 0; s_verbZaehler = 0;
  if (!s_entfernen) sensorenMqttNeuVerbinden();                      // der neue "Letzte Wille" gilt erst ab der naechsten Verbindung
}

void haNeuMelden() { s_neuAnfordern = true; s_phase = 0; }

void haTakt() {
  if (!s_buf) return;
  const unsigned long jetzt = millis();
  if (jetzt - s_tTakt < 400) return;
  s_tTakt = jetzt;
  const bool verbunden = sensorenMqttVerbunden();
  if (s_entfernen) {                                                  // Entitaeten loeschen (einmalig, wenn verbunden)
    if (!verbunden) { strlcpy(s_status, "wird entfernt, sobald MQTT verbunden ist", sizeof(s_status)); return; }
    for (int i = 0; i < NE; i++) konfigSenden(E[i], true);
    char t[64]; snprintf(t, sizeof(t), "%s/status", s_base); sensorenMqttSende(t, "offline", 0, true);
    snprintf(t, sizeof(t), "%s/zustand", s_base); sensorenMqttSende(t, "", 0, true);
    s_entfernen = false; s_aktivMerker = false; s_phase = 0;
    sensorenMqttLetzterWille("", ""); sensorenMqttNeuVerbinden();
    strlcpy(s_status, "aus", sizeof(s_status));
    return;
  }
  if (!cfg.ha_aktiv) { strlcpy(s_status, "aus", sizeof(s_status)); return; }
  MqttEinstellungen m; sensorenMqttHole(&m);
  if (!m.aktiv || !m.host[0]) { strlcpy(s_status, "MQTT-Zugang fehlt (Seite Sensoren)", sizeof(s_status)); return; }
  if (!verbunden) { s_phase = 0; strlcpy(s_status, "wartet auf die MQTT-Verbindung", sizeof(s_status)); return; }

  const uint32_t v = sensorenMqttVerbindungen();
  if (v != s_verbZaehler || s_neuAnfordern) {                         // neue Verbindung oder Anforderung: Anmeldung von vorn
    s_verbZaehler = v; s_neuAnfordern = false; s_phase = 1; s_naechste = 0; s_gemeldet = 0;
    char t[64]; snprintf(t, sizeof(t), "%s/status", s_base); sensorenMqttSende(t, "online", 1, true);
  }
  if (s_phase == 1) {                                                 // eine Konfiguration je Durchlauf, damit der Ausgangspuffer nicht ueberlaeuft
    for (int k = 0; k < 1 && s_naechste < NE; k++, s_naechste++) {
      const Entitaet& e = E[s_naechste];
      if (strncmp(e.key, "int_", 4) == 0 && !sensorenInternVorhanden()) { konfigSenden(e, true); continue; }   // ohne internen Sensor keine Entitaet
      konfigSenden(e, false); s_gemeldet++;
    }
    if (s_naechste >= NE) { s_phase = 2; s_tZustand = 0; }
    snprintf(s_status, sizeof(s_status), "meldet Entitäten an (%d von %d)", s_naechste, NE);
    return;
  }
  if (s_phase == 2 && (s_tZustand == 0 || jetzt - s_tZustand >= 5000)) {
    size_t n = zustandBauen(s_buf, BUF);
    (void)n;
    uint32_t h = hash(s_buf);
    if (h != s_zustandHash || s_tZustand == 0 || jetzt - s_gemeldetVor >= 60000) {     // bei Aenderung (hoechstens alle 5 s), sonst jede Minute
      char t[64]; snprintf(t, sizeof(t), "%s/zustand", s_base);
      if (sensorenMqttSende(t, s_buf, 0, true)) { s_zustandHash = h; s_gemeldetVor = jetzt ? jetzt : 1; }
    }
    s_tZustand = jetzt ? jetzt : 1;
    unsigned long vor = (jetzt - s_gemeldetVor) / 1000;
    snprintf(s_status, sizeof(s_status), "verbunden, %d Entitäten, Zustand vor %lu s gesendet", s_gemeldet, vor);
  }
}
