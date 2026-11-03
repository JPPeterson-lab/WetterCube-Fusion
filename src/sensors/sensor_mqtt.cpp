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
#include <ctype.h>

static const char* TAG = "sens-mqtt";
void logDiag(const char* fmt, ...);
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
  for (int i = 0; i < s_nk; i++) if (strcmp(s_kand[i].topic, k.topic) == 0 && strcmp(s_kand[i].json_key, k.json_key) == 0) { s_kand[i] = k; xSemaphoreGive(s_kmx); return; }
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
  // Topics geloeschter oder geaenderter Sensoren abbestellen: frueher blieben sie bis zur naechsten Broker-Verbindung in der Liste,
  // nach vielen Aenderungen war sie voll und neue Sensoren bekamen still keine Werte mehr.
  int weg = 0;
  for (int k = 0; k < s_na; ) {
    bool gebraucht = false;
    for (int i = 0; i < n; i++) if (strcmp(s_abonniert[k], t[i]) == 0) { gebraucht = true; break; }
    if (gebraucht) { k++; continue; }
    esp_mqtt_client_unsubscribe(s_client, s_abonniert[k]);
    if (k != s_na - 1) strlcpy(s_abonniert[k], s_abonniert[s_na - 1], 96);
    s_na--; weg++;
  }
  int neu = 0;
  for (int i = 0; i < n; i++) {
    bool schon = false;
    for (int k = 0; k < s_na; k++) if (strcmp(s_abonniert[k], t[i]) == 0) { schon = true; break; }
    if (schon || s_na >= ABO_MAX) continue;
    const int mid = esp_mqtt_client_subscribe(s_client, t[i], 0);
    if (mid < 0) logDiag("[Sensoren] MQTT: Abo von %s fehlgeschlagen", t[i]);
    strlcpy(s_abonniert[s_na++], t[i], 96); neu++;
  }
  heap_caps_free(t);
  if (weg || neu) logDiag("[Sensoren] MQTT-Abos: %d neu, %d abbestellt, %d aktiv", neu, weg, s_na);
}

void sensorenMqttNeuAbonnieren() { abonniereListe(); }

// ── Discovery ────────────────────────────────────────────────────────────────
static void kopiereTopic(char* ziel, size_t n, const char* t, const char* basis) {
  if (!t) { ziel[0] = 0; return; }
  if (t[0] == '~' && basis) snprintf(ziel, n, "%s%s", basis, t + 1); else strlcpy(ziel, t, n);
}

// "{{ value_json.temperatur }}", "{{ value_json['temperatur'] }}" oder "{{ value_json.temperatur | round(1) }}" -> "temperatur".
// Nur ein Schluessel der obersten Ebene; alles andere (verschachtelt, Rechnungen) bleibt leer, dann wird die Nutzlast als Zahl gelesen.
static void schluesselAusVorlage(const char* t, char* ziel, size_t n) {
  ziel[0] = 0;
  const char* p = strstr(t, "value_json");
  if (!p) return;
  p += 10;
  char ende = 0;
  if (*p == '.') p++;
  else if (p[0] == '[' && (p[1] == '\'' || p[1] == '"')) { ende = p[1]; p += 2; }
  else return;
  size_t i = 0;
  while (*p && i + 1 < n) {
    if (ende ? *p == ende : !(isalnum((unsigned char)*p) || *p == '_' || *p == '-')) break;
    ziel[i++] = *p++;
  }
  ziel[i] = 0;
  if (ende ? *p != ende : (*p == '.' || *p == '[')) ziel[0] = 0;   // verschachtelt oder abgeschnitten: nicht unterstuetzt
}

static const char* verfuegbarkeit(JsonObjectConst o) {
  const char* av = o["avty_t"] | o["availability_topic"] | (const char*)nullptr;
  if (!av && o["availability"].is<JsonArrayConst>()) av = o["availability"][0]["topic"] | (const char*)nullptr;
  if (!av && o["avty"].is<JsonArrayConst>()) av = o["avty"][0]["t"] | (const char*)nullptr;
  return av;
}

// Ein Sensor aus einer Discovery-Meldung. c: die Angaben des Sensors, wurzel: die ganze Meldung (bei der Geraete-Discovery
// stehen Zustands-Topic, Erreichbarkeit, "~" und Geraet dort gemeinsam fuer alle Bestandteile).
static void kandidatAus(JsonObjectConst c, JsonObjectConst wurzel) {
  const char* basis = c["~"] | wurzel["~"] | (const char*)nullptr;
  SensorKandidat k; memset(&k, 0, sizeof(k));
  const char* st = c["stat_t"] | c["state_topic"] | wurzel["stat_t"] | wurzel["state_topic"] | (const char*)nullptr;
  if (!st) return;
  kopiereTopic(k.topic, sizeof(k.topic), st, basis);
  const char* av = verfuegbarkeit(c);
  if (!av) av = verfuegbarkeit(wurzel);
  kopiereTopic(k.avail, sizeof(k.avail), av, basis);
  strlcpy(k.name, c["name"] | c["object_id"] | "Sensor", sizeof(k.name));
  strlcpy(k.einheit, c["unit_of_meas"] | c["unit_of_measurement"] | "", sizeof(k.einheit));
  schluesselAusVorlage(c["val_tpl"] | c["value_template"] | "", k.json_key, sizeof(k.json_key));
  JsonObjectConst dev = c["dev"].is<JsonObjectConst>() ? c["dev"].as<JsonObjectConst>() : c["device"].is<JsonObjectConst>() ? c["device"].as<JsonObjectConst>()
                      : wurzel["dev"].is<JsonObjectConst>() ? wurzel["dev"].as<JsonObjectConst>() : wurzel["device"].as<JsonObjectConst>();
  strlcpy(k.geraet, dev["name"] | dev["identifiers"][0] | "", sizeof(k.geraet));
  sensorenKandidatMelde(k);
}

static void discoveryAuswerten(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;
  JsonObjectConst o = doc.as<JsonObjectConst>();
  kandidatAus(o, o);
}

// Geraete-Discovery (seit Home Assistant 2024.12): eine Meldung je Geraet, die Sensoren stehen unter "cmps" (bzw. "components")
static void geraetAuswerten(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;
  JsonObjectConst w = doc.as<JsonObjectConst>();
  JsonObjectConst cmps = w["cmps"].is<JsonObjectConst>() ? w["cmps"].as<JsonObjectConst>() : w["components"].as<JsonObjectConst>();
  for (JsonPairConst kv : cmps) {
    JsonObjectConst c = kv.value().as<JsonObjectConst>();
    const char* p = c["p"] | c["platform"] | "";
    if (strcmp(p, "sensor") == 0) kandidatAus(c, w);
  }
}

static void discoveryMeldung(const char* topic, const char* json) {
  if (strncmp(topic, "homeassistant/sensor/", 21) == 0) discoveryAuswerten(json);
  else if (strncmp(topic, "homeassistant/device/", 21) == 0) geraetAuswerten(json);
  else if (strncmp(topic, "homeassistant/switch/", 21) == 0 || strncmp(topic, "homeassistant/light/", 20) == 0) schalterKandidatAuswerten(json);
}

// Grosse Discovery-Meldungen kommen in Stuecken (Puffer 2 KB); sie werden im PSRAM zusammengesetzt
#define DISCOVERY_MAX 12288
static char* s_stueck = nullptr;
static int s_stueckGesamt = 0, s_stueckDa = 0;
static char s_stueckTopic[128];
static void stueckeVerwerfen() { if (s_stueck) heap_caps_free(s_stueck); s_stueck = nullptr; s_stueckGesamt = s_stueckDa = 0; }

// ── Ereignisse ───────────────────────────────────────────────────────────────
static void ereignis(void*, esp_event_base_t, int32_t id, void* daten) {
  esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)daten;
  switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
      s_verbunden = true; s_fehler = 0; s_na = 0; s_verbindungen++;
      ESP_LOGI(TAG, "mit dem Broker verbunden");
      esp_mqtt_client_subscribe(s_client, "homeassistant/sensor/+/+/config", 0);   // Discovery (retained), mit Knoten-Kennung (ESPHome, WetterCube Node)
      esp_mqtt_client_subscribe(s_client, "homeassistant/sensor/+/config", 0);     // Discovery ohne Knoten-Kennung (z. B. Tasmota, eigene Skripte)
      esp_mqtt_client_subscribe(s_client, "homeassistant/device/+/config", 0);     // Geraete-Discovery (eine Meldung je Geraet)
      esp_mqtt_client_subscribe(s_client, "homeassistant/switch/+/+/config", 0);   // Schalter und Lichter fuer die Schalter-Kacheln
      esp_mqtt_client_subscribe(s_client, "homeassistant/light/+/+/config", 0);
      esp_mqtt_client_subscribe(s_client, "wled/+/status", 0);                     // WLED-Lampen (Standard-Topic-Praefix) fuer die Schalter-Kacheln
      abonniereListe();
      break;
    case MQTT_EVENT_SUBSCRIBED:
      if (e->data_len >= 1 && (uint8_t)e->data[0] == 0x80) logDiag("[Sensoren] MQTT: Broker lehnt ein Abo ab (Rechte des Benutzers, ACL?), Nachricht %d", e->msg_id);
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
      if (e->current_data_offset > 0) {                     // Fortsetzung einer grossen Meldung
        if (!s_stueck || e->current_data_offset != s_stueckDa || s_stueckDa + e->data_len > s_stueckGesamt) { stueckeVerwerfen(); break; }
        memcpy(s_stueck + s_stueckDa, e->data, e->data_len); s_stueckDa += e->data_len;
        if (s_stueckDa == s_stueckGesamt) { s_stueck[s_stueckGesamt] = 0; discoveryMeldung(s_stueckTopic, s_stueck); stueckeVerwerfen(); }
        break;
      }
      if (e->topic_len <= 0 || e->topic_len >= 128) break;
      char topic[128]; memcpy(topic, e->topic, e->topic_len); topic[e->topic_len] = 0;
      if (strncmp(topic, "homeassistant/", 14) == 0) {
        if (e->data_len <= 0 || e->total_data_len > DISCOVERY_MAX) break;   // leer = Geraet abgemeldet
        if (e->data_len == e->total_data_len) {
          char* buf = (char*)heap_caps_malloc(e->data_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
          if (buf) { memcpy(buf, e->data, e->data_len); buf[e->data_len] = 0; discoveryMeldung(topic, buf); heap_caps_free(buf); }
        } else {
          stueckeVerwerfen();
          s_stueck = (char*)heap_caps_malloc(e->total_data_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
          if (s_stueck) { s_stueckGesamt = e->total_data_len; memcpy(s_stueck, e->data, e->data_len); s_stueckDa = e->data_len; strlcpy(s_stueckTopic, topic, sizeof(s_stueckTopic)); }
        }
        break;
      }
      if (e->data_len != e->total_data_len) break;           // Messwerte nur als ganze Meldung
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

// Waehrend eines Firmware-Updates gibt der MQTT-Client seinen internen Speicher frei (TLS und Netzpuffer brauchen ihn)
static volatile bool s_pause = false;
void sensorenMqttPause(bool pause) {
  s_pause = pause;
  if (!pause) return;
  s_neustart = true;                                                // der Waechter beendet den Client
  for (int i = 0; i < 40 && s_client; i++) vTaskDelay(pdMS_TO_TICKS(100));
}

static void waechter(void*) {
  for (;;) {
    if (s_neustart) { s_neustart = false; clientStoppen(); }
    if (!s_client && !s_pause && netzOk()) clientStarten();
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void sensorenMqttStart() {
  s_kmx = xSemaphoreCreateMutex();
  s_kand = (SensorKandidat*)heap_caps_calloc(SENSOR_KANDIDATEN, sizeof(SensorKandidat), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_abonniert = (char (*)[96])heap_caps_calloc(ABO_MAX, 96, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  xTaskCreate(waechter, "sens-mqtt", 4096, nullptr, 3, nullptr);
}
