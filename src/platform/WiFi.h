// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// WLAN-Zugriff auf Basis von esp_wifi und esp_netif (eigener Code): Station, Einrichtungs-Access-Point, Suche, Signalstärke.
#include "Arduino.h"

typedef enum { WL_NO_SHIELD = 255, WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 } wl_status_t;
typedef enum { WIFI_MODE_OFF_ = 0, WIFI_MODE_STA_ = 1, WIFI_MODE_AP_ = 2, WIFI_MODE_APSTA_ = 3 } fusion_wifi_mode_t;
#define WIFI_OFF   WIFI_MODE_OFF_
#define WIFI_STA   WIFI_MODE_STA_
#define WIFI_AP    WIFI_MODE_AP_
#define WIFI_AP_STA WIFI_MODE_APSTA_

class IPAddress {
 public:
  IPAddress() {}
  IPAddress(uint32_t a) : addr_(a) {}
  String toString() const { char b[20]; snprintf(b, sizeof(b), "%u.%u.%u.%u", (unsigned)(addr_ & 255), (unsigned)((addr_ >> 8) & 255), (unsigned)((addr_ >> 16) & 255), (unsigned)(addr_ >> 24)); return String(b); }
  uint32_t raw() const { return addr_; }
 private:
  uint32_t addr_ = 0;   // wie in lwIP: Byte 0 = erstes Oktett
};

class WiFiClass {
 public:
  bool mode(fusion_wifi_mode_t m);
  bool begin(const char* ssid, const char* pass);
  bool disconnect(bool wifioff = false, bool eraseap = false);
  bool reconnect();
  wl_status_t status();
  String SSID();
  int32_t RSSI();
  IPAddress localIP();
  bool softAP(const char* ssid, const char* pass = nullptr);   // mit Passwort (mindestens 8 Zeichen): WPA2, sonst offen
  IPAddress softAPIP();
  int softAPgetStationNum();
  int16_t scanNetworks();
  String SSID(int i);
  int32_t RSSI(int i);
  void setAutoReconnect(bool an);
  bool setSleep(bool an);
};
extern WiFiClass WiFi;
