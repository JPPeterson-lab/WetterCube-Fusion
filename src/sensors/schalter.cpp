// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Schalter-Kacheln: Liste (NVS), Zustand aus MQTT, Befehle senden, Kandidaten aus der Discovery, JSON fuer WebUI und Sicherung.
#include "schalter.h"
#include "sensors_intern.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <ArduinoJson.h>

static const char* TAG = "schalter";
static const char* NVS_NS = "fusionsw";
static_assert(sizeof(SchalterCfg) == 344, "SchalterCfg: Groesse geaendert, gespeicherte Liste im NVS wird nicht mehr erkannt");

// Die Listen liegen im PSRAM (der interne RAM ist knapp)
static SchalterCfg*       s_cfg = nullptr;
static uint8_t*           s_zust = nullptr;
static SchalterKandidat*  s_kand = nullptr;
static int  s_n = 0, s_nk = 0;
static volatile uint32_t s_aend = 1;
static SemaphoreHandle_t s_mx = nullptr;
extern void speicherSicherungAnfordern();

#define LOCK()   xSemaphoreTakeRecursive(s_mx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGiveRecursive(s_mx)

static void kopiere(char* ziel, size_t n, const char* q) { strlcpy(ziel, q ? q : "", n); }
static bool gleichOhneGross(const char* a, const char* b) { return strcasecmp(a, b) == 0; }

// ── Zustand aus der Nutzlast ──────────────────────────────────────────────────
static uint8_t zustandErkennen(const SchalterCfg& c, const char* nl, int len) {
  char buf[96];
  if (len <= 0 || len >= (int)sizeof(buf)) return SZ_UNBEKANNT;
  memcpy(buf, nl, len); buf[len] = 0;
  char wert[40] = "";
  if (c.json_key[0]) {
    JsonDocument doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) return SZ_UNBEKANNT;
    JsonVariant v = doc[c.json_key];
    if (v.isNull()) return SZ_UNBEKANNT;
    if (v.is<bool>()) strlcpy(wert, v.as<bool>() ? "true" : "false", sizeof(wert));
    else if (v.is<const char*>()) strlcpy(wert, v.as<const char*>(), sizeof(wert));
    else snprintf(wert, sizeof(wert), "%g", v.as<double>());
  } else {
    const char* p = buf; while (*p && isspace((unsigned char)*p)) p++;
    strlcpy(wert, p, sizeof(wert));
    for (int i = (int)strlen(wert) - 1; i >= 0 && isspace((unsigned char)wert[i]); i--) wert[i] = 0;
  }
  // Sonderfall ">0": jede Zahl groesser 0 heisst "an", 0 heisst "aus" (WLED meldet die Helligkeit auf <Topic>/g)
  if (!strcmp(c.state_an, ">0")) {
    char* ende = nullptr; double d = strtod(wert, &ende);
    if (ende != wert && *ende == 0) return d > 0 ? SZ_AN : SZ_AUS;
    return SZ_UNBEKANNT;
  }
  const char* an = c.state_an[0] ? c.state_an : c.payload_an;
  const char* aus = c.state_aus[0] ? c.state_aus : c.payload_aus;
  if (an[0] && gleichOhneGross(wert, an)) return SZ_AN;
  if (aus[0] && gleichOhneGross(wert, aus)) return SZ_AUS;
  if (!c.state_an[0] && (gleichOhneGross(wert, "true") || !strcmp(wert, "1"))) return SZ_AN;     // gaengige Schreibweisen
  if (!c.state_aus[0] && (gleichOhneGross(wert, "false") || !strcmp(wert, "0"))) return SZ_AUS;
  return SZ_UNBEKANNT;
}

void schalterMqttWert(const char* topic, const char* nutzlast, int laenge) {
  if (!s_cfg) return;
  LOCK();
  for (int i = 0; i < s_n; i++) {
    if (!s_cfg[i].state_topic[0] || strcmp(s_cfg[i].state_topic, topic) != 0) continue;
    uint8_t z = zustandErkennen(s_cfg[i], nutzlast, laenge);
    if (z != SZ_UNBEKANNT && z != s_zust[i]) { s_zust[i] = z; s_aend++; }
  }
  UNLOCK();
}

int schalterTopicsHole(char (*out)[96], int max) {
  if (!s_cfg) return 0;
  int n = 0;
  LOCK();
  for (int i = 0; i < s_n && n < max; i++) {
    if (!s_cfg[i].state_topic[0]) continue;
    bool schon = false;
    for (int k = 0; k < n; k++) if (strcmp(out[k], s_cfg[i].state_topic) == 0) schon = true;
    if (!schon) strlcpy(out[n++], s_cfg[i].state_topic, 96);
  }
  UNLOCK();
  return n;
}

// ── Liste ──────────────────────────────────────────────────────────────────────
int schalterAnzahl() { return s_n; }
uint32_t schalterAenderung() { return s_aend; }

bool schalterHole(int i, SchalterCfg* cfg, uint8_t* zustand) {
  if (!s_cfg) return false;
  LOCK();
  bool ok = i >= 0 && i < s_n;
  if (ok) { if (cfg) *cfg = s_cfg[i]; if (zustand) *zustand = s_zust[i]; }
  UNLOCK();
  return ok;
}

int schalterSichtbar(int* idx, int max) {
  if (!s_cfg) return 0;
  int n = 0;
  LOCK();
  for (int i = 0; i < s_n && n < max; i++) if (s_cfg[i].sichtbar) idx[n++] = i;
  for (int a = 1; a < n; a++) { int t = idx[a], b = a - 1; while (b >= 0 && s_cfg[idx[b]].reihenfolge > s_cfg[t].reihenfolge) { idx[b + 1] = idx[b]; b--; } idx[b + 1] = t; }
  UNLOCK();
  return n;
}

static void standardsFuellen(SchalterCfg& c) {
  if (!c.payload_an[0]) strlcpy(c.payload_an, "ON", sizeof(c.payload_an));
  if (!c.payload_aus[0]) strlcpy(c.payload_aus, "OFF", sizeof(c.payload_aus));
  if (!c.name[0]) strlcpy(c.name, c.id, sizeof(c.name));
}

bool schalterSetze(const SchalterCfg& cIn) {
  if (!s_cfg || !cIn.id[0] || !cIn.cmd_topic[0]) return false;
  SchalterCfg c = cIn; standardsFuellen(c);
  bool ok = true;
  LOCK();
  int i = -1;
  for (int k = 0; k < s_n; k++) if (strcmp(s_cfg[k].id, c.id) == 0) { i = k; break; }
  if (i < 0) { if (s_n >= SCHALTER_MAX) ok = false; else { i = s_n++; s_zust[i] = SZ_UNBEKANNT; } }
  if (ok) { s_cfg[i] = c; s_aend++; }
  UNLOCK();
  if (ok) sensorenMqttNeuAbonnieren();
  return ok;
}

bool schalterLoesche(const char* id) {
  if (!s_cfg) return false;
  bool ok = false;
  LOCK();
  for (int i = 0; i < s_n; i++) if (strcmp(s_cfg[i].id, id) == 0) {
    for (int k = i; k < s_n - 1; k++) { s_cfg[k] = s_cfg[k + 1]; s_zust[k] = s_zust[k + 1]; }
    s_n--; ok = true; s_aend++; break;
  }
  UNLOCK();
  return ok;
}

// ── Schalten ───────────────────────────────────────────────────────────────────
bool schalterSchalte(int i, bool an) {
  SchalterCfg c; uint8_t z;
  if (!schalterHole(i, &c, &z)) return false;
  if (!sensorenMqttSende(c.cmd_topic, an ? c.payload_an : c.payload_aus, 0, c.retain != 0)) return false;
  LOCK();
  if (i < s_n && strcmp(s_cfg[i].id, c.id) == 0) { s_zust[i] = an ? SZ_AN : SZ_AUS; s_aend++; }    // sofort anzeigen, ein Zustands-Topic bestaetigt oder korrigiert es
  UNLOCK();
  ESP_LOGI(TAG, "%s -> %s", c.name, an ? "an" : "aus");
  return true;
}

bool schalterUmschalten(int i) {
  uint8_t z = SZ_UNBEKANNT;
  if (!schalterHole(i, nullptr, &z)) return false;
  return schalterSchalte(i, z != SZ_AN);
}

// ── Kandidaten aus der Discovery ───────────────────────────────────────────────
static void topicKopieren(char* ziel, size_t n, const char* t, const char* basis) {
  if (!t) { ziel[0] = 0; return; }
  if (t[0] == '~' && basis) snprintf(ziel, n, "%s%s", basis, t + 1); else strlcpy(ziel, t, n);
}

void schalterKandidatAuswerten(const char* json) {
  if (!s_kand) return;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;
  const char* basis = doc["~"] | (const char*)nullptr;
  SchalterKandidat k; memset(&k, 0, sizeof(k));
  const char* cmd = doc["cmd_t"] | doc["command_topic"] | (const char*)nullptr;
  if (!cmd) return;
  topicKopieren(k.cmd_topic, sizeof(k.cmd_topic), cmd, basis);
  const char* st = doc["stat_t"] | doc["state_topic"] | (const char*)nullptr;
  topicKopieren(k.state_topic, sizeof(k.state_topic), st, basis);
  JsonObject dev = doc["dev"].is<JsonObject>() ? doc["dev"].as<JsonObject>() : doc["device"].as<JsonObject>();
  strlcpy(k.name, doc["name"] | doc["object_id"] | dev["name"] | "Schalter", sizeof(k.name));
  strlcpy(k.geraet, dev["name"] | dev["identifiers"][0] | "", sizeof(k.geraet));
  strlcpy(k.payload_an, doc["pl_on"] | doc["payload_on"] | "ON", sizeof(k.payload_an));
  strlcpy(k.payload_aus, doc["pl_off"] | doc["payload_off"] | "OFF", sizeof(k.payload_aus));
  strlcpy(k.state_an, doc["stat_on"] | doc["state_on"] | "", sizeof(k.state_an));
  strlcpy(k.state_aus, doc["stat_off"] | doc["state_off"] | "", sizeof(k.state_aus));
  LOCK();
  bool da = false;
  for (int i = 0; i < s_nk; i++) if (strcmp(s_kand[i].cmd_topic, k.cmd_topic) == 0) { s_kand[i] = k; da = true; break; }
  if (!da && s_nk < SCHALTER_KANDIDATEN) s_kand[s_nk++] = k;
  UNLOCK();
}

// WLED meldet sich mit "<Geraetethema>/status" (online/offline, retained), nicht ueber die Home-Assistant-Discovery: daraus einen Kandidaten machen
void schalterWledMelden(const char* topic) {
  if (!s_kand || strncmp(topic, "wled/", 5) != 0) return;
  const char* id = topic + 5;
  const char* ende = strstr(id, "/status");
  if (!ende || ende == id || ende[7] != 0 || ende - id > 40) return;
  SchalterKandidat k; memset(&k, 0, sizeof(k));
  char kennung[48]; snprintf(kennung, sizeof(kennung), "%.*s", (int)(ende - id), id);
  snprintf(k.name, sizeof(k.name), "WLED %s", kennung);
  strlcpy(k.geraet, "WLED", sizeof(k.geraet));
  snprintf(k.cmd_topic, sizeof(k.cmd_topic), "wled/%s", kennung);          // "ON", "OFF" und "T" (umschalten) auf dem Geraetethema
  snprintf(k.state_topic, sizeof(k.state_topic), "wled/%s/g", kennung);    // Helligkeit: 0 = aus
  strlcpy(k.payload_an, "ON", sizeof(k.payload_an)); strlcpy(k.payload_aus, "OFF", sizeof(k.payload_aus));
  strlcpy(k.state_an, ">0", sizeof(k.state_an)); strlcpy(k.state_aus, "0", sizeof(k.state_aus));
  LOCK();
  bool da = false;
  for (int i = 0; i < s_nk; i++) if (strcmp(s_kand[i].cmd_topic, k.cmd_topic) == 0) { da = true; break; }
  if (!da && s_nk < SCHALTER_KANDIDATEN) s_kand[s_nk++] = k;
  UNLOCK();
}

int schalterKandidaten(SchalterKandidat* out, int max) {
  if (!s_kand) return 0;
  LOCK();
  int n = s_nk < max ? s_nk : max;
  memcpy(out, s_kand, n * sizeof(SchalterKandidat));
  UNLOCK();
  return n;
}

// ── Speichern / Laden ──────────────────────────────────────────────────────────
bool schalterSpeichern() {
  if (!s_cfg) return false;
  speicherSicherungAnfordern();
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
  LOCK();
  bool ok = nvs_set_u8(h, "n", (uint8_t)s_n) == ESP_OK;
  if (s_n > 0) ok &= nvs_set_blob(h, "liste", s_cfg, s_n * sizeof(SchalterCfg)) == ESP_OK; else nvs_erase_key(h, "liste");
  UNLOCK();
  ok &= nvs_commit(h) == ESP_OK;
  nvs_close(h);
  if (!ok) ESP_LOGW(TAG, "Speichern fehlgeschlagen");
  return ok;
}

void schalterInit() {
  if (s_mx) return;
  s_mx = xSemaphoreCreateRecursiveMutex();
  s_cfg = (SchalterCfg*)heap_caps_calloc(SCHALTER_MAX, sizeof(SchalterCfg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_zust = (uint8_t*)heap_caps_calloc(SCHALTER_MAX, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_kand = (SchalterKandidat*)heap_caps_calloc(SCHALTER_KANDIDATEN, sizeof(SchalterKandidat), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_cfg || !s_zust || !s_kand) { ESP_LOGW(TAG, "kein Speicher fuer die Schalter"); s_cfg = nullptr; return; }
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
  uint8_t n = 0; nvs_get_u8(h, "n", &n);
  if (n > SCHALTER_MAX) n = SCHALTER_MAX;
  size_t len = 0;
  if (n > 0 && nvs_get_blob(h, "liste", nullptr, &len) == ESP_OK && len == n * sizeof(SchalterCfg) && nvs_get_blob(h, "liste", s_cfg, &len) == ESP_OK) s_n = n;
  nvs_close(h);
  int ok = 0;                                       // unbrauchbare Eintraege verwerfen
  for (int i = 0; i < s_n; i++) {
    SchalterCfg& c = s_cfg[i];
    c.id[sizeof(c.id) - 1] = 0; c.cmd_topic[sizeof(c.cmd_topic) - 1] = 0; c.state_topic[sizeof(c.state_topic) - 1] = 0;
    bool gut = c.id[0] && c.cmd_topic[0];
    for (int k = 0; k < ok && gut; k++) if (strcmp(s_cfg[k].id, c.id) == 0) gut = false;
    if (gut) s_cfg[ok++] = c;
  }
  s_n = ok;
  ESP_LOGI(TAG, "%d Schalter geladen", s_n);
}

// ── JSON ───────────────────────────────────────────────────────────────────────
static void cfgInJson(JsonObject o, const SchalterCfg& c) {
  o["id"] = std::string(c.id); o["name"] = std::string(c.name); o["raum"] = std::string(c.raum);
  o["cmd_topic"] = std::string(c.cmd_topic); o["state_topic"] = std::string(c.state_topic); o["json_key"] = std::string(c.json_key);
  o["payload_an"] = std::string(c.payload_an); o["payload_aus"] = std::string(c.payload_aus);
  o["state_an"] = std::string(c.state_an); o["state_aus"] = std::string(c.state_aus);
  o["sichtbar"] = c.sichtbar; o["reihenfolge"] = c.reihenfolge; o["retain"] = c.retain;
}

std::string schalterZustandJson() {
  JsonDocument doc;
  JsonArray arr = doc["schalter"].to<JsonArray>();
  for (int i = 0; i < schalterAnzahl(); i++) {
    SchalterCfg c; uint8_t z;
    if (!schalterHole(i, &c, &z)) continue;
    JsonObject o = arr.add<JsonObject>(); cfgInJson(o, c); o["zustand"] = z;
  }
  JsonArray ka = doc["kandidaten"].to<JsonArray>();
  SchalterKandidat* k = (SchalterKandidat*)heap_caps_malloc(SCHALTER_KANDIDATEN * sizeof(SchalterKandidat), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  int nk = k ? schalterKandidaten(k, SCHALTER_KANDIDATEN) : 0;
  for (int i = 0; i < nk; i++) {
    JsonObject o = ka.add<JsonObject>();
    o["name"] = std::string(k[i].name); o["geraet"] = std::string(k[i].geraet); o["cmd_topic"] = std::string(k[i].cmd_topic);
    o["state_topic"] = std::string(k[i].state_topic); o["payload_an"] = std::string(k[i].payload_an); o["payload_aus"] = std::string(k[i].payload_aus);
    o["state_an"] = std::string(k[i].state_an); o["state_aus"] = std::string(k[i].state_aus);
  }
  if (k) heap_caps_free(k);
  doc["mqtt_status"] = sensorenMqttStatus();
  std::string out; serializeJson(doc, out); return out;
}

std::string schalterSicherungJson() {
  JsonDocument doc;
  JsonArray arr = doc["schalter"].to<JsonArray>();
  for (int i = 0; i < schalterAnzahl(); i++) { SchalterCfg c; if (schalterHole(i, &c, nullptr)) cfgInJson(arr.add<JsonObject>(), c); }
  std::string out; serializeJson(doc, out); return out;
}

int schalterAusJson(const char* json) {
  if (!s_cfg) return -1;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok || !doc["schalter"].is<JsonArray>()) return -1;
  SchalterCfg* neu = (SchalterCfg*)heap_caps_calloc(SCHALTER_MAX, sizeof(SchalterCfg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!neu) return -1;
  int n = 0;
  for (JsonObject o : doc["schalter"].as<JsonArray>()) {
    if (n >= SCHALTER_MAX) break;
    SchalterCfg c; memset(&c, 0, sizeof(c));
    kopiere(c.id, sizeof(c.id), o["id"] | ""); kopiere(c.cmd_topic, sizeof(c.cmd_topic), o["cmd_topic"] | "");
    if (!c.id[0] || !c.cmd_topic[0]) continue;
    kopiere(c.name, sizeof(c.name), o["name"] | c.id); kopiere(c.raum, sizeof(c.raum), o["raum"] | "");
    kopiere(c.state_topic, sizeof(c.state_topic), o["state_topic"] | ""); kopiere(c.json_key, sizeof(c.json_key), o["json_key"] | "");
    kopiere(c.payload_an, sizeof(c.payload_an), o["payload_an"] | ""); kopiere(c.payload_aus, sizeof(c.payload_aus), o["payload_aus"] | "");
    kopiere(c.state_an, sizeof(c.state_an), o["state_an"] | ""); kopiere(c.state_aus, sizeof(c.state_aus), o["state_aus"] | "");
    c.sichtbar = o["sichtbar"] | 1; c.reihenfolge = o["reihenfolge"] | n; c.retain = o["retain"] | 0;
    standardsFuellen(c);
    neu[n++] = c;
  }
  LOCK();
  uint8_t alteZ[SCHALTER_MAX]; SchalterCfg* alt = (SchalterCfg*)heap_caps_malloc(sizeof(SchalterCfg) * SCHALTER_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  int an = s_n;
  if (alt) memcpy(alt, s_cfg, sizeof(SchalterCfg) * SCHALTER_MAX);
  memcpy(alteZ, s_zust, SCHALTER_MAX);
  memset(s_zust, 0, SCHALTER_MAX);
  memcpy(s_cfg, neu, sizeof(SchalterCfg) * SCHALTER_MAX);
  s_n = n;
  if (alt) for (int i = 0; i < n; i++) for (int k = 0; k < an; k++) if (strcmp(alt[k].id, s_cfg[i].id) == 0) s_zust[i] = alteZ[k];   // Zustaende ueber die id behalten
  s_aend++;
  UNLOCK();
  if (alt) heap_caps_free(alt);
  heap_caps_free(neu);
  sensorenMqttNeuAbonnieren();
  schalterSpeichern();
  return n;
}

void schalterLogZeile(char* buf, size_t n) {
  int an = 0, aus = 0, unb = 0;
  LOCK();
  for (int i = 0; i < s_n; i++) { if (s_zust[i] == SZ_AN) an++; else if (s_zust[i] == SZ_AUS) aus++; else unb++; }
  int gesamt = s_n;
  UNLOCK();
  snprintf(buf, n, "Schalter: %d eingerichtet (%d an, %d aus, %d ohne Zustand)", gesamt, an, aus, unb);
}
