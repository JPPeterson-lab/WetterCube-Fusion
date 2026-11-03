// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// WLAN auf esp_wifi/esp_netif (siehe WiFi.h). Station mit automatischem Wiederverbinden, Einrichtungs-Access-Point, Netzwerksuche.
#include "WiFi.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "../app/app_config.h"

extern volatile uint8_t wifiLetzterGrund;           // app_state.cpp: letzter Disconnect-Grund (für das /log)
extern void logDiag(const char* fmt, ...);

WiFiClass WiFi;

static bool s_init = false;
static esp_netif_t* s_sta = nullptr;
static esp_netif_t* s_ap = nullptr;
static volatile bool s_ip = false;                  // Station hat eine IP-Adresse
static bool s_begehrt = false;                      // begin() wurde aufgerufen, Verbindung ist gewollt
static bool s_autoReconnect = true;
static bool s_gestartet = false;
static fusion_wifi_mode_t s_mode = WIFI_OFF;
static wifi_ap_record_t s_scan[24];
static uint16_t s_scanN = 0;

static void ereignis(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT) {
    switch (id) {
      case WIFI_EVENT_STA_START: if (s_begehrt) esp_wifi_connect(); break;
      case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t* d = (wifi_event_sta_disconnected_t*)data;
        wifiLetzterGrund = d->reason;
        s_ip = false;
        if (s_begehrt && s_autoReconnect) esp_wifi_connect();
        break;
      }
      case WIFI_EVENT_AP_STACONNECTED:    logDiag("[Portal] Gerät verbunden (Station %d)", WiFi.softAPgetStationNum()); break;
      case WIFI_EVENT_AP_STADISCONNECTED: logDiag("[Portal] Gerät getrennt (Station %d)", WiFi.softAPgetStationNum()); break;
      default: break;
    }
  } else if (base == IP_EVENT) {
    if (id == IP_EVENT_STA_GOT_IP) s_ip = true;
    else if (id == IP_EVENT_STA_LOST_IP) s_ip = false;
    else if (id == IP_EVENT_AP_STAIPASSIGNED) {
      ip_event_ap_staipassigned_t* e = (ip_event_ap_staipassigned_t*)data;
      logDiag("[Portal] IP vergeben: %s", IPAddress(e->ip.addr).toString().c_str());
    }
  }
}

static bool initWlan() {
  if (s_init) return true;
  esp_netif_init();
  esp_err_t e = esp_event_loop_create_default();
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
  s_sta = esp_netif_create_default_wifi_sta();
  s_ap = esp_netif_create_default_wifi_ap();
  esp_netif_set_hostname(s_sta, MDNS_NAME);
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK) return false;
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ereignis, nullptr);
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ereignis, nullptr);
  s_init = true;
  return true;
}

bool WiFiClass::mode(fusion_wifi_mode_t m) {
  if (!initWlan()) return false;
  if (m == WIFI_OFF) { if (s_gestartet) esp_wifi_stop(); s_gestartet = false; s_mode = WIFI_OFF; s_ip = false; return true; }
  wifi_mode_t w = m == WIFI_STA ? WIFI_MODE_STA : m == WIFI_AP ? WIFI_MODE_AP : WIFI_MODE_APSTA;
  if (esp_wifi_set_mode(w) != ESP_OK) return false;
  s_mode = m;
  if (!s_gestartet) { if (esp_wifi_start() != ESP_OK) return false; s_gestartet = true; }
  return true;
}

bool WiFiClass::begin(const char* ssid, const char* pass) {
  if (s_mode == WIFI_OFF || s_mode == WIFI_AP) { if (!mode(s_mode == WIFI_AP ? WIFI_AP_STA : WIFI_STA)) return false; }
  wifi_config_t c = {};
  strlcpy((char*)c.sta.ssid, ssid ? ssid : "", sizeof(c.sta.ssid));
  strlcpy((char*)c.sta.password, pass ? pass : "", sizeof(c.sta.password));
  c.sta.threshold.authmode = (pass && pass[0]) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  c.sta.pmf_cfg.capable = true; c.sta.pmf_cfg.required = false;
  c.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  c.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  if (esp_wifi_set_config(WIFI_IF_STA, &c) != ESP_OK) return false;
  s_begehrt = true; s_ip = false;
  esp_wifi_disconnect();
  return esp_wifi_connect() == ESP_OK;
}

bool WiFiClass::disconnect(bool wifioff, bool) {
  s_begehrt = false; s_ip = false;
  if (s_gestartet) esp_wifi_disconnect();
  if (wifioff) mode(WIFI_OFF);
  return true;
}

bool WiFiClass::reconnect() { return s_gestartet && esp_wifi_connect() == ESP_OK; }
wl_status_t WiFiClass::status() { return s_ip ? WL_CONNECTED : (s_begehrt ? WL_DISCONNECTED : WL_IDLE_STATUS); }

String WiFiClass::SSID() {
  wifi_ap_record_t r;
  return (s_gestartet && esp_wifi_sta_get_ap_info(&r) == ESP_OK) ? String((const char*)r.ssid) : String();
}
int32_t WiFiClass::RSSI() {
  wifi_ap_record_t r;
  return (s_gestartet && esp_wifi_sta_get_ap_info(&r) == ESP_OK) ? r.rssi : 0;
}
IPAddress WiFiClass::localIP() {
  esp_netif_ip_info_t ip;
  return (s_sta && esp_netif_get_ip_info(s_sta, &ip) == ESP_OK) ? IPAddress(ip.ip.addr) : IPAddress();
}

bool WiFiClass::softAP(const char* ssid, const char* pass) {
  if (s_mode != WIFI_AP && s_mode != WIFI_AP_STA) { if (!mode(WIFI_AP_STA)) return false; }   // AP + Station (Station ruht), damit die Netzwerksuche geht
  wifi_config_t a = {};
  strlcpy((char*)a.ap.ssid, ssid, sizeof(a.ap.ssid));
  a.ap.ssid_len = strlen(ssid);
  a.ap.channel = 1;
  a.ap.authmode = WIFI_AUTH_OPEN;
  if (pass && strlen(pass) >= 8) {                  // WPA2 verlangt mindestens 8 Zeichen
    strlcpy((char*)a.ap.password, pass, sizeof(a.ap.password));
    a.ap.authmode = WIFI_AUTH_WPA2_PSK;
  }
  a.ap.max_connection = 4;
  return esp_wifi_set_config(WIFI_IF_AP, &a) == ESP_OK;
}
IPAddress WiFiClass::softAPIP() {
  esp_netif_ip_info_t ip;
  return (s_ap && esp_netif_get_ip_info(s_ap, &ip) == ESP_OK) ? IPAddress(ip.ip.addr) : IPAddress();
}
int WiFiClass::softAPgetStationNum() {
  wifi_sta_list_t l;
  return (s_gestartet && esp_wifi_ap_get_sta_list(&l) == ESP_OK) ? l.num : 0;
}

int16_t WiFiClass::scanNetworks() {
  if (s_mode == WIFI_OFF) mode(WIFI_STA);
  else if (s_mode == WIFI_AP) mode(WIFI_AP_STA);
  if (esp_wifi_scan_start(nullptr, true) != ESP_OK) return 0;
  s_scanN = sizeof(s_scan) / sizeof(s_scan[0]);
  if (esp_wifi_scan_get_ap_records(&s_scanN, s_scan) != ESP_OK) s_scanN = 0;
  return (int16_t)s_scanN;
}
String WiFiClass::SSID(int i) { return (i >= 0 && i < s_scanN) ? String((const char*)s_scan[i].ssid) : String(); }
int32_t WiFiClass::RSSI(int i) { return (i >= 0 && i < s_scanN) ? s_scan[i].rssi : 0; }
void WiFiClass::setAutoReconnect(bool an) { s_autoReconnect = an; }
bool WiFiClass::setSleep(bool an) { return esp_wifi_set_ps(an ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE) == ESP_OK; }
