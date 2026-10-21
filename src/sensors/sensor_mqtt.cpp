// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// MQTT-Client fuer externe Sensoren (z. B. ESPHome): abonniert die Topics der Sensorliste und liest per Home-Assistant-
// Discovery (homeassistant/sensor/+/+/config) mit, welche Sensoren es im Netz gibt. Braucht einen Broker (z. B. Mosquitto),
// aber kein Home Assistant. Reines ESP-IDF (esp-mqtt, esp_netif), ArduinoJson nur zum Lesen der Discovery-Meldungen.
#include "sensors_intern.h"
#include "schalter.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <ArduinoJson.h>

static const char* TAG = "sens-mqtt";
static esp_mqtt_client_handle_t s_client = nullptr;
static volatile bool s_verbunden = false;
static volatile bool s_neustart = false;
static volatile int  s_fehler = 0;      // 0 keiner, 3 Anmeldung abgelehnt, 4 nicht erreichbar
static SemaphoreHandle_t s_kmx = nullptr;
static SensorKandidat* s_kand = nullptr;      // im PSRAM (der interne RAM ist knapp)
static int s_nk = 0;
static char (*s_abonniert)[96] = nullptr;      // im PSRAM
static int s_na = 0;
#define ABO_MAX (SENSOR_MAX * 2 + SCHALTER_MAX)
static char s_lwtTopic[96] = "", s_lwtMsg[16] = "";
static volatile uint32_t s_verbindungen = 0;

uint32_t sensorenMqttVerbindungen() { return s_verbindungen; }
void sensorenMqttLetzterWille(const char* topic, const char* nutzlast) {
  strlcpy(s_lwtTopic, topic ? topic : "", sizeof(s_lwtTopic)); strlcpy(s_lwtMsg, nutzlast ? nutzlast : "", sizeof(s_lwtMsg));
}
bool sensorenMqttSende(const char* topic, const char* nutzlast, int qos, bool retain) {
  if (!s_client || !s_verbunden || !topic || !topic[0]) return false;
  return esp_mqtt_client_enqueue(s_client, topic, nutzlast ? nutzlast : "", 0, qos, retain, true) >= 0;
}

bool sensorenMqttVerbunden() { return s_verbunden; }

int sensorenMqttStatus() {
  MqttEinstellungen m; sensorenHoleMqttIntern(&m);
  if (!m.aktiv || !m.host[0]) return 0;
  if (s_verbunden) return 2;
  return s_fehler ? s_fehler : 1;
}

int sensorenKandidaten(SensorKandidat* out, int max) {
  if (!s_kmx) return 0;
  if (!s_kand) return 0;
  xSemaphoreTake(s_kmx, portMAX_DELAY);
  int n = s_nk < max ? s_nk : max;
  memcpy(out, s_kand, n * sizeof(SensorKandidat));
  xSemaphoreGive(s_kmx);
  return n;
}

void sensorenKandidatMelde(const SensorKandidat& k) {
  if (!s_kand) return;
  xSemaphoreTake(s_kmx, portMAX_DELAY);
  for (int i = 0; i < s_nk; i++) if (strcmp(s_kand[i].topic, k.topic) == 0) { s_kand[i] = k; xSemaphoreGive(s_kmx); return; }
  if (s_nk < SENSOR_KANDIDATEN) s_kand[s_nk++] = k;
  xSemaphoreGive(s_kmx);
}

// ── Abonnements ──────────────────────────────────────────────────────────────
static void abonniereListe() {
  if (!s_client || !s_verbunden || !s_abonniert) return;
  char (*t)[96] = (char (*)[96])heap_caps_malloc(ABO_MAX * 96, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!t) return;
  int n = sensorenTopicsHole(t, SENSOR_MAX * 2);
  n += schalterTopicsHole(t + n, ABO_MAX - n);          // Zustands-Topics der Schalter-Kacheln
  for (int i = 0; i < n; i++) {
    bool schon = false;
    for (int k = 0; k < s_na; k++) if (strcmp(s_abonniert[k], t[i]) == 0) { schon = true; break; }
    if (schon || s_na >= ABO_MAX) continue;
    esp_mqtt_client_subscribe(s_client, t[i], 0);
    strlcpy(s_abonniert[s_na++], t[i], 96);
  }
  heap_caps_free(t);
}

void sensorenMqttNeuAbonnieren() { abonniereListe(); }

// ── Discovery ────────────────────────────────────────────────────────────────
static void kopiereTopic(char* ziel, size_t n, const char* t, const char* basis) {
  if (!t) { ziel[0] = 0; return; }
  if (t[0] == '~' && basis) snprintf(ziel, n, "%s%s", basis, t + 1); else strlcpy(ziel, t, n);
}

static void discoveryAuswerten(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;
  const char* basis = doc["~"] | (const char*)nullptr;
  SensorKandidat k; memset(&k, 0, sizeof(k));
  const char* st = doc["stat_t"] | doc["state_topic"] | (const char*)nullptr;
  if (!st) return;
  kopiereTopic(k.topic, sizeof(k.topic), st, basis);
  const char* av = doc["avty_t"] | doc["availability_topic"] | (const char*)nullptr;
  if (!av && doc["availability"].is<JsonArray>()) av = doc["availability"][0]["topic"] | (const char*)nullptr;
  if (!av && doc["avty"].is<JsonArray>()) av = doc["avty"][0]["t"] | (const char*)nullptr;
  kopiereTopic(k.avail, sizeof(k.avail), av, basis);
  strlcpy(k.name, doc["name"] | doc["object_id"] | "Sensor", sizeof(k.name));
  strlcpy(k.einheit, doc["unit_of_meas"] | doc["unit_of_measurement"] | "", sizeof(k.einheit));
  JsonObject dev = doc["dev"].is<JsonObject>() ? doc["dev"].as<JsonObject>() : doc["device"].as<JsonObject>();
  strlcpy(k.geraet, dev["name"] | dev["identifiers"][0] | "", sizeof(k.geraet));
  sensorenKandidatMelde(k);
}

// ── Ereignisse ───────────────────────────────────────────────────────────────
static void ereignis(void*, esp_event_base_t, int32_t id, void* daten) {
  esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)daten;
  switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
      s_verbunden = true; s_fehler = 0; s_na = 0; s_verbindungen++;
      ESP_LOGI(TAG, "mit dem Broker verbunden");
      esp_mqtt_client_subscribe(s_client, "homeassistant/sensor/+/+/config", 0);   // Discovery (retained)
      esp_mqtt_client_subscribe(s_client, "homeassistant/switch/+/+/config", 0);   // Schalter und Lichter fuer die Schalter-Kacheln
      esp_mqtt_client_subscribe(s_client, "homeassistant/light/+/+/config", 0);
      esp_mqtt_client_subscribe(s_client, "wled/+/status", 0);                     // WLED-Lampen (Standard-Themenpraefix) fuer die Schalter-Kacheln
      abonniereListe();
      break;
    case MQTT_EVENT_DISCONNECTED:
      s_verbunden = false; ESP_LOGW(TAG, "Verbindung zum Broker getrennt");
      break;
    case MQTT_EVENT_ERROR:
      if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        s_fehler = 3; ESP_LOGW(TAG, "Broker lehnt die Anmeldung ab (Benutzer/Passwort?)");
      } else if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        s_fehler = 4; ESP_LOGW(TAG, "Broker nicht erreichbar (Adresse/Port/WLAN?)");
      }
      break;
    case MQTT_EVENT_DATA: {
      if (e->current_data_offset != 0 || e->data_len != e->total_data_len || e->topic_len <= 0 || e->topic_len >= 128) break;   // nur ganze Meldungen
      char topic[128]; memcpy(topic, e->topic, e->topic_len); topic[e->topic_len] = 0;
      if (strncmp(topic, "homeassistant/sensor/", 21) == 0) {
        if (e->data_len > 0 && e->data_len < 1500) {
          char* buf = (char*)heap_caps_malloc(e->data_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
          if (buf) { memcpy(buf, e->data, e->data_len); buf[e->data_len] = 0; discoveryAuswerten(buf); heap_caps_free(buf); }
        }
        break;
      }
      if (strncmp(topic, "homeassistant/switch/", 21) == 0 || strncmp(topic, "homeassistant/light/", 20) == 0) {
        if (e->data_len > 0 && e->data_len < 1500) {
          char* buf = (char*)heap_caps_malloc(e->data_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
          if (buf) { memcpy(buf, e->data, e->data_len); buf[e->data_len] = 0; schalterKandidatAuswerten(buf); heap_caps_free(buf); }
        }
        break;
      }
      if (strncmp(topic, "wled/", 5) == 0) schalterWledMelden(topic);               // meldet sich ein WLED-Geraet, wird es Kandidat (nur "<id>/status")
      schalterMqttWert(topic, e->data, e->data_len);        // Zustand einer Schalter-Kachel (ON/OFF/JSON), passt kein Topic, passiert nichts
      if ((e->data_len == 6 && memcmp(e->data, "online", 6) == 0) || (e->data_len == 7 && memcmp(e->data, "offline", 7) == 0)) {
        sensorenMqttAvail(topic, e->data_len == 6);
        break;
      }
      sensorenMqttWert(topic, e->data, e->data_len);
      break;
    }
    default: break;
  }
}

// ── Start / Neustart ─────────────────────────────────────────────────────────
static void clientStoppen() {
  if (s_client) { esp_mqtt_client_stop(s_client); esp_mqtt_client_destroy(s_client); s_client = nullptr; }
  s_verbunden = false; s_fehler = 0; s_na = 0;
}

static bool netzOk() {
  esp_netif_t* n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip;
  return n && esp_netif_get_ip_info(n, &ip) == ESP_OK && ip.ip.addr != 0;
}

static void clientStarten() {
  MqttEinstellungen m; sensorenHoleMqttIntern(&m);
  if (!m.aktiv || !m.host[0]) return;
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char cid[40]; snprintf(cid, sizeof(cid), "wettercube-fusion-%02x%02x%02x", mac[3], mac[4], mac[5]);
  esp_mqtt_client_config_t c = {};
  c.broker.address.hostname = m.host;
  c.broker.address.port = m.port ? m.port : 1883;
  c.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  if (m.user[0]) { c.credentials.username = m.user; c.credentials.authentication.password = m.pass; }
  c.credentials.client_id = cid;
  c.buffer.size = 2048;                  // Discovery-Meldungen von ESPHome sind bis ca. 1 KB gross
  c.buffer.out_size = 512;               // gesendet wird nur das Abonnieren
  c.task.stack_size = 6144;   // 4096 reichte nicht mehr, als die Alarm-Folgen noch im MQTT-Task liefen (Stapelueberlauf in 0.9.3/0.9.4)
  c.session.keepalive = 60;
  if (s_lwtTopic[0]) {                   // Letzter Wille (Home Assistant: Verfuegbarkeit des Geraets)
    c.session.last_will.topic = s_lwtTopic; c.session.last_will.msg = s_lwtMsg; c.session.last_will.msg_len = (int)strlen(s_lwtMsg);
    c.session.last_will.qos = 1; c.session.last_will.retain = true;
  }
  c.network.reconnect_timeout_ms = 10000;
  s_client = esp_mqtt_client_init(&c);
  if (!s_client) return;
  esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, ereignis, nullptr);
  esp_mqtt_client_start(s_client);
  ESP_LOGI(TAG, "MQTT-Client gestartet (%s:%u)", m.host, (unsigned)(m.port ? m.port : 1883));
}

void sensorenMqttNeuVerbinden() { s_neustart = true; }

static void waechter(void*) {
  for (;;) {
    if (s_neustart) { s_neustart = false; clientStoppen(); }
    if (!s_client && netzOk()) clientStarten();
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void sensorenMqttStart() {
  s_kmx = xSemaphoreCreateMutex();
  s_kand = (SensorKandidat*)heap_caps_calloc(SENSOR_KANDIDATEN, sizeof(SensorKandidat), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_abonniert = (char (*)[96])heap_caps_calloc(ABO_MAX, 96, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  xTaskCreate(waechter, "sens-mqtt", 4096, nullptr, 3, nullptr);
}
