// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// WetterCube Node als Quelle fuer Sensoren, ohne MQTT-Broker und ohne Home Assistant (WetterCube-eigener Standard):
// Der Node meldet sich im Heimnetz als mDNS-Dienst _wettercube._tcp mit TXT typ=node und liefert seine Messwerte unter GET /api/status
// (JSON: temp, feuchte, druck, taupunkt). Ein Sensor mit Quelle SQ_NODE traegt im Feld topic den Namen des Node (mDNS-Name ohne .local
// oder eine feste IP-Adresse) und im Feld json_key den Messwert. Abfrage alle 30 Sekunden je Node einmal, die Suche nur auf Anforderung.
// Reines ESP-IDF (esp_http_client, mdns) plus ArduinoJson. Gleiche Datei im Fusion (nur P4) und im Apex.
#include "../app/app_config.h"
#if WC_P4   // nur P4 (Entwicklungsstopp S3)
#include "sensors_intern.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "mdns.h"
#include "lwip/ip4_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <ArduinoJson.h>

void logDiag(const char* fmt, ...);   // app_state.cpp

static volatile bool s_suche = false;
static volatile int  s_status = 0;          // 0 nie gesucht, 1 sucht, 2 fertig
static SemaphoreHandle_t s_kmx = nullptr;
static NodeKandidat s_kand[NODE_KANDIDATEN];
static int s_nk = 0;

// Zwischenspeicher Name -> IP-Adresse (mDNS-Aufloesung kostet bis zu 2 s, die Adresse aendert sich selten)
struct NodeAdresse { char host[48]; uint32_t ip; };
static NodeAdresse s_adr[SENSOR_MAX];
static int s_nadr = 0;

void nodeSucheAnfordern() { s_suche = true; s_status = 1; }
int  nodeSucheStatus() { return s_status; }

int nodeKandidaten(NodeKandidat* out, int max) {
  if (!s_kmx) return 0;
  xSemaphoreTake(s_kmx, portMAX_DELAY);
  int n = s_nk < max ? s_nk : max;
  memcpy(out, s_kand, n * sizeof(NodeKandidat));
  xSemaphoreGive(s_kmx);
  return n;
}

static bool netzOk() {
  esp_netif_t* n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip;
  return n && esp_netif_get_ip_info(n, &ip) == ESP_OK && ip.ip.addr != 0;
}

static void merkeAdresse(const char* host, uint32_t ip) {
  for (int i = 0; i < s_nadr; i++) if (!strcmp(s_adr[i].host, host)) { s_adr[i].ip = ip; return; }
  if (s_nadr < SENSOR_MAX) { strlcpy(s_adr[s_nadr].host, host, sizeof(s_adr[0].host)); s_adr[s_nadr].ip = ip; s_nadr++; }
}
static uint32_t gemerkteAdresse(const char* host) {
  for (int i = 0; i < s_nadr; i++) if (!strcmp(s_adr[i].host, host)) return s_adr[i].ip;
  return 0;
}

// Name ohne ".local" (mDNS) oder feste IP-Adresse -> IPv4 (0 = nicht gefunden)
static uint32_t aufloesen(const char* host, bool neu) {
  ip4_addr_t a;
  if (ip4addr_aton(host, &a)) return a.addr;
  if (!neu) { uint32_t m = gemerkteAdresse(host); if (m) return m; }
  char name[48]; strlcpy(name, host, sizeof(name));
  char* p = strstr(name, ".local"); if (p) *p = 0;
  esp_ip4_addr_t ip = {};
  if (mdns_query_a(name, 2000, &ip) == ESP_OK && ip.addr) { merkeAdresse(host, ip.addr); return ip.addr; }
  return 0;
}

// GET http://<ip>/api/status, Antwort in buf; Rueckgabe Laenge, -1 bei Fehler
static int statusHolen(uint32_t ip, char* buf, int max) {
  char url[48];
  snprintf(url, sizeof(url), "http://%u.%u.%u.%u/api/status", (unsigned)(ip & 255), (unsigned)((ip >> 8) & 255), (unsigned)((ip >> 16) & 255), (unsigned)(ip >> 24));
  esp_http_client_config_t c = {};
  c.url = url; c.timeout_ms = 4000; c.buffer_size = 1024; c.buffer_size_tx = 512;
  esp_http_client_handle_t cl = esp_http_client_init(&c);
  if (!cl) return -1;
  int n = -1;
  if (esp_http_client_open(cl, 0) == ESP_OK) {
    esp_http_client_fetch_headers(cl);
    if (esp_http_client_get_status_code(cl) == 200) {
      n = 0;
      for (;;) { int r = esp_http_client_read(cl, buf + n, max - 1 - n); if (r <= 0) break; n += r; if (n >= max - 1) break; }
      buf[n] = 0;
    }
  }
  esp_http_client_close(cl);
  esp_http_client_cleanup(cl);
  return n;
}

static const char* const WERTE[4] = {"temp", "feuchte", "druck", "taupunkt"};

// Werte eines Node lesen; false = nicht erreichbar oder kein Node
static bool nodeLesen(uint32_t ip, float w[4], bool g[4], char* name, size_t nName) {
  static char buf[2048];
  if (statusHolen(ip, buf, sizeof(buf)) <= 0) return false;
  JsonDocument filter;
  filter["geraet"] = true; filter["name"] = true;
  for (const char* k : WERTE) filter[k] = true;
  JsonDocument doc;
  if (deserializeJson(doc, buf, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;
  if (strcmp(doc["geraet"] | "", "node") != 0) return false;
  if (name) strlcpy(name, doc["name"] | "", nName);
  for (int i = 0; i < 4; i++) { g[i] = doc[WERTE[i]].is<float>(); w[i] = g[i] ? doc[WERTE[i]].as<float>() : NAN; if (g[i] && !isfinite(w[i])) g[i] = false; }
  return true;
}

// Suche: alle _wettercube._tcp mit typ=node, je Node die aktuellen Werte
static void suchen() {
  mdns_result_t* res = nullptr;
  if (mdns_query_ptr("_wettercube", "_tcp", 3000, 20, &res) != ESP_OK) { s_status = 2; return; }
  NodeKandidat neu[NODE_KANDIDATEN]; int k = 0;
  for (mdns_result_t* r = res; r && k < NODE_KANDIDATEN; r = r->next) {
    bool istNode = false;
    for (size_t t = 0; t < r->txt_count; t++) if (r->txt[t].key && !strcmp(r->txt[t].key, "typ") && r->txt[t].value && !strcmp(r->txt[t].value, "node")) istNode = true;
    if (!istNode || !r->hostname) continue;
    uint32_t ip = 0;
    for (mdns_ip_addr_t* a = r->addr; a; a = a->next) if (a->addr.type == ESP_IPADDR_TYPE_V4) { ip = a->addr.u_addr.ip4.addr; break; }
    if (!ip) continue;
    bool doppelt = false;
    for (int i = 0; i < k; i++) if (!strcmp(neu[i].host, r->hostname)) doppelt = true;
    if (doppelt) continue;
    NodeKandidat& c = neu[k];
    memset(&c, 0, sizeof(c));
    strlcpy(c.host, r->hostname, sizeof(c.host));
    snprintf(c.ip, sizeof(c.ip), "%u.%u.%u.%u", (unsigned)(ip & 255), (unsigned)((ip >> 8) & 255), (unsigned)((ip >> 16) & 255), (unsigned)(ip >> 24));
    merkeAdresse(c.host, ip);
    if (!nodeLesen(ip, c.wert, c.gueltig, c.name, sizeof(c.name))) continue;
    if (!c.name[0]) strlcpy(c.name, c.host, sizeof(c.name));
    k++;
  }
  mdns_query_results_free(res);
  xSemaphoreTake(s_kmx, portMAX_DELAY);
  memcpy(s_kand, neu, k * sizeof(NodeKandidat)); s_nk = k;
  xSemaphoreGive(s_kmx);
  s_status = 2;
  logDiag("[Sensoren] Node-Suche: %d WetterCube Node gefunden", k);
}

// Eingerichtete Nodes abfragen (jeder Node einmal, egal wie viele Werte von ihm in der Liste stehen)
static void abfragen() {
  static char hosts[SENSOR_MAX][48];
  const int n = sensorenNodeHosts(hosts, SENSOR_MAX);
  for (int i = 0; i < n; i++) {
    float w[4]; bool g[4];
    uint32_t ip = aufloesen(hosts[i], false);
    bool ok = ip && nodeLesen(ip, w, g, nullptr, 0);
    if (!ok && ip) { ip = aufloesen(hosts[i], true); ok = ip && nodeLesen(ip, w, g, nullptr, 0); }   // neue Adresse vom Router?
    if (!ok) { sensorenNodeOffline(hosts[i]); continue; }
    for (int k = 0; k < 4; k++) if (g[k]) sensorenNodeWert(hosts[i], WERTE[k], w[k]);
  }
}

static void nodeTask(void*) {
  uint32_t letzte = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!netzOk()) continue;
    if (s_suche) { s_suche = false; suchen(); }
    if (letzte == 0 || (xTaskGetTickCount() - letzte) >= pdMS_TO_TICKS(30000)) { letzte = xTaskGetTickCount(); abfragen(); }
  }
}

void sensorenNodeStart() {
  if (!s_kmx) s_kmx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(nodeTask, "sens-node", 6144, nullptr, 2, nullptr, 0);
}
#endif  // WC_P4
