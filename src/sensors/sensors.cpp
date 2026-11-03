// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Sensor-Registry: Liste, Korrektur, Speicherung (NVS), JSON fuer die WebUI und die Sicherung
#include "sensors_intern.h"
#include "schalter.h"
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <ArduinoJson.h>

static const char* TAG = "sensoren";
static const char* NVS_NS = "fusionsens";

static SensorCfg  s_cfg[SENSOR_MAX];
static SensorWert s_wert[SENSOR_MAX];
static int        s_n = 0;
static float      s_hoehe = 0.0f;
static MqttEinstellungen s_mqtt = {"", 1883, "", "", 0};
static SemaphoreHandle_t s_mx = nullptr;
bool g_internVorhanden = false;

#define LOCK()   xSemaphoreTakeRecursive(s_mx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGiveRecursive(s_mx)

static void kopiere(char* ziel, size_t n, const char* quelle) {
  if (!quelle) quelle = "";
  strlcpy(ziel, quelle, n);
}

// ── Korrektur ────────────────────────────────────────────────────────────────
static float sealevel(float stationshPa, float hoehe) {
  if (hoehe <= 0.5f) return stationshPa;
  return stationshPa / powf(1.0f - hoehe / 44330.0f, 5.255f);
}

// ── Grenzwert-Alarme ─────────────────────────────────────────────────────────
// Ein Alarm beginnt, wenn die Grenze mindestens ALARM_VERZOEGERUNG_MS lang verletzt ist (kurze Ausreisser zaehlen nicht), und
// endet erst, wenn der Wert um eine kleine Hysterese innerhalb der Grenze liegt (kein Flackern an der Grenze).
static const int64_t ALARM_VERZOEGERUNG_MS = 90 * 1000;
struct AlarmEreignis { SensorCfg c; SensorWert w; bool ein; };
static AlarmEreignis s_ereignisse[6];
static int s_ereignisN = 0;
static SensorAlarmHook s_alarmHook = nullptr;

static void alarmPruefenLocked(int i) {
  SensorWert& w = s_wert[i]; const SensorCfg& c = s_cfg[i];
  const int64_t jetzt = esp_timer_get_time() / 1000;
  if (!(c.grenze_flags & 3)) { w.alarm = 0; w.verstoss_ms = 0; return; }       // keine Grenze eingestellt: still zuruecksetzen
  const float hMin = fmaxf(0.3f, 0.02f * fabsf(c.grenze_min)), hMax = fmaxf(0.3f, 0.02f * fabsf(c.grenze_max));
  uint8_t soll = 0;
  if ((c.grenze_flags & 1) && w.wert < c.grenze_min) soll = 1;
  else if ((c.grenze_flags & 2) && w.wert > c.grenze_max) soll = 2;
  if (!soll && w.alarm == 1 && (c.grenze_flags & 1) && w.wert < c.grenze_min + hMin) soll = 1;   // Hysterese: Alarm bleibt
  if (!soll && w.alarm == 2 && (c.grenze_flags & 2) && w.wert > c.grenze_max - hMax) soll = 2;
  if (soll) {
    if (!w.verstoss_ms) w.verstoss_ms = jetzt;
    if (w.alarm != soll && jetzt - w.verstoss_ms >= ALARM_VERZOEGERUNG_MS) {
      w.alarm = soll;
      if (s_ereignisN < 6) s_ereignisse[s_ereignisN++] = {c, w, true};
    }
  } else {
    w.verstoss_ms = 0;
    if (w.alarm) { w.alarm = 0; if (s_ereignisN < 6) s_ereignisse[s_ereignisN++] = {c, w, false}; }
  }
}

// Gesammelte Ereignisse ausserhalb der Sperre melden (der Hook darf lange dauern, z. B. Netzwerk)
static void alarmeVerteilen() {
  for (;;) {
    AlarmEreignis e; bool da = false;
    LOCK();
    if (s_ereignisN > 0) { e = s_ereignisse[0]; for (int k = 1; k < s_ereignisN; k++) s_ereignisse[k - 1] = s_ereignisse[k]; s_ereignisN--; da = true; }
    UNLOCK();
    if (!da) return;
    if (s_alarmHook) s_alarmHook(e.c, e.w, e.ein);
  }
}

void sensorenSetzeAlarmHook(SensorAlarmHook h) { s_alarmHook = h; }

// Aus der Hauptschleife aufrufen: Der Hook (Log, Hinweis-Kachel, ntfy) braucht viel Stapel und darf deshalb nicht im MQTT-Task
// (6 KB Stapel) oder im Webserver-Task laufen, in denen die Messwerte eintreffen. Die Ereignisse warten hier in der Schlange.
void sensorenAlarmeVerarbeiten() { alarmeVerteilen(); }

int sensorenAlarme(int* idx, int max) {
  int n = 0;
  LOCK();
  const int64_t jetzt = esp_timer_get_time() / 1000;
  for (int i = 0; i < s_n && n < max; i++)
    if (s_wert[i].alarm && jetzt - s_wert[i].zuletzt_ms < 15 * 60 * 1000) idx[n++] = i;   // schweigt der Sensor laenger als 15 Minuten, gilt der Alarm nicht mehr
  UNLOCK();
  return n;
}

static void setzeWertLocked(int i, float roh, bool online) {
  SensorWert& w = s_wert[i];
  float v = roh + s_cfg[i].offset;
  if (s_cfg[i].einheit[0] == '%') v = fminf(fmaxf(v, 0.0f), 100.0f);   // Feuchte auf 0-100 % begrenzen (wie im ESPHome-Code)
  w.roh = roh; w.wert = v; w.gueltig = true; w.online = online; w.zuletzt_ms = esp_timer_get_time() / 1000;
  alarmPruefenLocked(i);
}

void sensorenInternMelde(uint8_t quelle, float roh) {
  if (!s_mx) return;
  LOCK();
  if (quelle == SQ_INT_DRUCK) roh = sealevel(roh, s_hoehe);
  for (int i = 0; i < s_n; i++) if (s_cfg[i].quelle == quelle) setzeWertLocked(i, roh, true);
  UNLOCK();
}

// Zahl aus der Nutzlast: reiner Text (ESPHome) oder JSON-Schluessel
void logDiag(const char* fmt, ...);   // app_state.cpp

static bool leseZahl(const SensorCfg& c, const char* nl, int len, float* out) {
  if (len <= 0) return false;
  if (c.json_key[0]) {
    if (len > 4096) return false;
    JsonDocument doc;
    if (deserializeJson(doc, nl, (size_t)len) != DeserializationError::Ok) return false;
    JsonVariant v = doc[(const char*)c.json_key];   // als Zeiger: ArduinoJson 7.3+ nimmt ein char-Feld sonst als Text fester Laenge (mit Nullen bis 15 Zeichen) und findet den Schluessel nie
    if (v.isNull()) return false;
    *out = v.as<float>(); return true;
  }
  char buf[160];
  if (len >= (int)sizeof(buf)) return false;
  memcpy(buf, nl, len); buf[len] = 0;
  char* ende = nullptr;
  float f = strtof(buf, &ende);
  if (ende == buf) return false;
  *out = f; return true;
}

void sensorenMqttWert(const char* topic, const char* nutzlast, int laenge) {
  if (!s_mx) return;
  LOCK();
  for (int i = 0; i < s_n; i++) {
    if (s_cfg[i].quelle != SQ_MQTT || strcmp(s_cfg[i].topic, topic) != 0) continue;
    float f;
    if (leseZahl(s_cfg[i], nutzlast, laenge, &f) && isfinite(f)) { setzeWertLocked(i, f, true); s_wert[i].meldung = 2; }
    else if (s_wert[i].meldung != 1) {   // einmal ins Log, wie die Meldung aussah (hilft bei falschem JSON-Schluessel oder Format)
      s_wert[i].meldung = 1;
      char probe[101]; int l = laenge < 100 ? laenge : 100; int k = 0;
      for (int j = 0; j < l; j++) probe[k++] = (nutzlast[j] >= 32 && nutzlast[j] < 127) ? nutzlast[j] : '?';
      probe[k] = 0;
      logDiag("[Sensoren] %s: Meldung auf %s, aber kein Wert%s%s%s (%d Byte): %s", s_cfg[i].name, topic, s_cfg[i].json_key[0] ? " fuer '" : "", s_cfg[i].json_key, s_cfg[i].json_key[0] ? "'" : "", laenge, probe);
    }
  }
  UNLOCK();
}

void sensorenMqttAvail(const char* topic, bool online) {
  if (!s_mx) return;
  LOCK();
  for (int i = 0; i < s_n; i++)
    if (s_cfg[i].quelle == SQ_MQTT && s_cfg[i].avail[0] && strcmp(s_cfg[i].avail, topic) == 0) s_wert[i].online = online;
  UNLOCK();
}

// ── Liste ────────────────────────────────────────────────────────────────────
int sensorenAnzahl() { return s_n; }

bool sensorenHole(int i, SensorCfg* cfg, SensorWert* wert) {
  if (!s_mx) return false;
  LOCK();
  bool ok = i >= 0 && i < s_n;
  if (ok) { if (cfg) *cfg = s_cfg[i]; if (wert) *wert = s_wert[i]; }
  UNLOCK();
  return ok;
}

int sensorenSuche(const char* id) {
  int r = -1;
  LOCK();
  for (int i = 0; i < s_n; i++) if (strcmp(s_cfg[i].id, id) == 0) { r = i; break; }
  UNLOCK();
  return r;
}

bool sensorenSetze(const SensorCfg& c) {
  if (!c.id[0]) return false;
  bool ok = true;
  LOCK();
  int i = -1;
  for (int k = 0; k < s_n; k++) if (strcmp(s_cfg[k].id, c.id) == 0) { i = k; break; }
  if (i < 0) {
    if (s_n >= SENSOR_MAX) ok = false;
    else { i = s_n++; memset(&s_wert[i], 0, sizeof(SensorWert)); }
  }
  if (ok) {
    float alterOffset = s_cfg[i].offset;
    bool neu = (i == s_n - 1) && s_wert[i].zuletzt_ms == 0;
    s_cfg[i] = c;
    if (s_wert[i].gueltig && !neu) { SensorWert& w = s_wert[i]; setzeWertLocked(i, w.roh, w.online); (void)alterOffset; }
  }
  UNLOCK();
  if (ok) sensorenMqttNeuAbonnieren();
  return ok;
}

bool sensorenLoesche(const char* id) {
  bool ok = false;
  LOCK();
  for (int i = 0; i < s_n; i++) if (strcmp(s_cfg[i].id, id) == 0) {
    for (int k = i; k < s_n - 1; k++) { s_cfg[k] = s_cfg[k + 1]; s_wert[k] = s_wert[k + 1]; }
    s_n--; ok = true; break;
  }
  UNLOCK();
  if (ok) sensorenMqttNeuAbonnieren();
  return ok;
}

bool sensorenAbgleich(const char* id, float referenz) {
  bool ok = false;
  LOCK();
  int i = sensorenSuche(id);
  if (i >= 0 && s_wert[i].gueltig) {
    s_cfg[i].offset = referenz - s_wert[i].roh;
    setzeWertLocked(i, s_wert[i].roh, s_wert[i].online);
    ok = true;
  }
  UNLOCK();
  return ok;
}

float sensorenHoehe() { return s_hoehe; }
void  sensorenSetzeHoehe(float m) { s_hoehe = fminf(fmaxf(m, -400.0f), 5000.0f); }
bool  sensorenInternVorhanden() { return g_internVorhanden; }

const char* sensorenIconName(const SensorCfg& c) {
  if (c.icon[0]) return c.icon;
  if (c.quelle == SQ_INT_TEMP) return "thermometer";
  if (c.quelle == SQ_INT_FEUCHTE) return "humidity";
  if (c.quelle == SQ_INT_DRUCK) return "barometer";
  if (strstr(c.einheit, "C") || strstr(c.einheit, "F")) return "thermometer";
  if (c.einheit[0] == '%') return "humidity";
  if (strstr(c.einheit, "hPa") || strstr(c.einheit, "Pa")) return "barometer";
  if (strstr(c.einheit, "km/h") || strstr(c.einheit, "m/s")) return "wind";
  if (strstr(c.einheit, "UV")) return "uv-index";
  if (strstr(c.einheit, "mm")) return "raindrop";
  return "thermometer";
}

void sensorenMqttHole(MqttEinstellungen* out) { LOCK(); *out = s_mqtt; UNLOCK(); }
bool sensorenHoleMqttIntern(MqttEinstellungen* out) { LOCK(); *out = s_mqtt; UNLOCK(); return true; }
void sensorenMqttSetze(const MqttEinstellungen& in, bool passUebernehmen) {
  LOCK();
  char alt[sizeof(s_mqtt.pass)]; strlcpy(alt, s_mqtt.pass, sizeof(alt));
  s_mqtt = in;
  if (!passUebernehmen) strlcpy(s_mqtt.pass, alt, sizeof(s_mqtt.pass));
  UNLOCK();
  sensorenMqttNeuVerbinden();
}

int sensorenTopicsHole(char (*out)[96], int max) {
  int n = 0;
  LOCK();
  for (int i = 0; i < s_n && n < max; i++) {
    if (s_cfg[i].quelle != SQ_MQTT) continue;
    if (s_cfg[i].topic[0]) strlcpy(out[n++], s_cfg[i].topic, 96);
    if (s_cfg[i].avail[0] && n < max) strlcpy(out[n++], s_cfg[i].avail, 96);
  }
  UNLOCK();
  return n;
}

// ── Speichern / Laden (NVS) ──────────────────────────────────────────────────
extern void speicherSicherungAnfordern();   // storage.cpp: Sicherung der Einstellungen auf der Speicherkarte nachziehen

bool sensorenSpeichern() {
  speicherSicherungAnfordern();
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
  LOCK();
  bool ok = true;
  // Jeder Sensor als eigener Eintrag ("s0", "s1" ...). Frueher stand die ganze Liste in einem Block ("liste", bei 14 Sensoren 4,3 KB):
  // beim Umschreiben brauchte der NVS Platz fuer alte und neue Fassung gleichzeitig, bei vollem NVS schlug das fehl, die
  // Anzahl war aber schon geschrieben, und nach dem Neustart passte nichts mehr zusammen (Liste leer). Jetzt braucht ein
  // Umschreiben hoechstens Platz fuer einen Sensor, und die Anzahl wird erst geschrieben, wenn alle Sensoren gespeichert sind.
  {
    size_t alt = 0;
    if (nvs_get_blob(h, "liste", nullptr, &alt) == ESP_OK) { nvs_erase_key(h, "liste"); nvs_commit(h); }   // alten Block zuerst freigeben (die Liste liegt im RAM)
    bool listeOk = true;
    for (int i = 0; i < s_n; i++) {
      char k[6]; snprintf(k, sizeof(k), "s%d", i);
      if (nvs_set_blob(h, k, &s_cfg[i], sizeof(SensorCfg)) != ESP_OK) { listeOk = false; logDiag("[Sensoren] Speichern von Sensor %d fehlgeschlagen (NVS voll?)", i + 1); break; }
    }
    if (listeOk) {
      ok &= nvs_set_u8(h, "fmt", 2) == ESP_OK;
      ok &= nvs_set_u8(h, "n", (uint8_t)s_n) == ESP_OK;
      for (int i = s_n; i < SENSOR_MAX; i++) { char k[6]; snprintf(k, sizeof(k), "s%d", i); nvs_erase_key(h, k); }
    } else ok = false;
  }
  ok &= nvs_set_i32(h, "hoehe", (int32_t)lroundf(s_hoehe * 10.0f)) == ESP_OK;
  ok &= nvs_set_str(h, "mq_host", s_mqtt.host) == ESP_OK;
  ok &= nvs_set_u16(h, "mq_port", s_mqtt.port) == ESP_OK;
  ok &= nvs_set_str(h, "mq_user", s_mqtt.user) == ESP_OK;
  ok &= nvs_set_str(h, "mq_pass", s_mqtt.pass) == ESP_OK;
  ok &= nvs_set_u8(h, "mq_aktiv", s_mqtt.aktiv) == ESP_OK;
  UNLOCK();
  ok &= nvs_commit(h) == ESP_OK;
  nvs_close(h);
  if (!ok) ESP_LOGW(TAG, "Speichern fehlgeschlagen");
  return ok;
}

// Die Migration unten haengt am Aufbau der Struktur: neue Felder nur am Ende anfuegen, sonst wird die gespeicherte Liste nicht mehr erkannt
static_assert(offsetof(SensorCfg, offset) + sizeof(float) == 276, "SensorCfg: Aufbau bis v0.6.x geaendert");
static_assert(offsetof(SensorCfg, gruppe) + sizeof(((SensorCfg*)0)->gruppe) == 296, "SensorCfg: Aufbau ab v0.7.0 geaendert");
static_assert(sizeof(SensorCfg) == 308, "SensorCfg: Groesse geaendert, ALTE_GROESSEN in lade() pruefen");
static_assert(offsetof(SensorCfg, ort) == 305, "SensorCfg: ort muss im Fuellbyte nach grenze_flags liegen");

static bool enthaeltKlein(const char* s, const char* w) {   // ohne Gross-/Kleinschreibung (nur ASCII, Umlaute wie geschrieben)
  for (const char* p = s; *p; p++) { const char* a = p; const char* b = w; while (*a && *b && ((*a | 0x20) == (*b | 0x20) || *a == *b)) { a++; b++; } if (!*b) return true; }
  return false;
}
bool sensorIstAussen(const SensorCfg& c) {
  if (c.ort == 1) return false;
  if (c.ort == 2) return true;
  static const char* W[] = {"aussen", "außen", "Außen", "draussen", "draußen", "balkon", "garten", "terrasse", "outdoor", "outside", "wetterstation"};
  for (const char* w : W) if (enthaeltKlein(c.raum, w) || enthaeltKlein(c.name, w)) return true;
  return false;
}

// Fruehere Groessen von SensorCfg im NVS (neue Felder kommen immer am Ende dazu): 276 Byte bis v0.6.x, 296 Byte ab v0.7.0 (mit "gruppe")
static const size_t ALTE_GROESSEN[] = {296, 276};

static void lade() {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
  uint8_t n = 0;
  nvs_get_u8(h, "n", &n);
  if (n > SENSOR_MAX) n = SENSOR_MAX;
  size_t len = 0;
  uint8_t fmt = 0; nvs_get_u8(h, "fmt", &fmt);
  if (fmt == 2) {   // ein Eintrag je Sensor
    for (int i = 0; i < n; i++) {
      char k[6]; snprintf(k, sizeof(k), "s%d", i);
      SensorCfg c; memset(&c, 0, sizeof(c)); len = sizeof(c);
      if (nvs_get_blob(h, k, &c, &len) != ESP_OK) { logDiag("[Sensoren] Sensor %d fehlt im NVS", i + 1); continue; }   // kleinere (aeltere) Eintraege: neue Felder bleiben 0
      s_cfg[s_n++] = c;
    }
  } else if (nvs_get_blob(h, "liste", nullptr, &len) == ESP_OK && len > 0) {   // alter Block
    // Passt die Anzahl nicht zum Block (fehlgeschlagenes Speichern frueherer Versionen), zaehlt der Block: lieber die zuletzt
    // vollstaendig gespeicherte Liste als gar keine
    size_t sz = 0;
    if (len == n * sizeof(SensorCfg)) sz = sizeof(SensorCfg);
    else for (size_t a : ALTE_GROESSEN) if (len == n * a) { sz = a; break; }
    if (!sz && len % sizeof(SensorCfg) == 0) sz = sizeof(SensorCfg);
    if (!sz) for (size_t a : ALTE_GROESSEN) if (len % a == 0) { sz = a; break; }
    int m = sz ? (int)(len / sz) : 0; if (m > SENSOR_MAX) m = SENSOR_MAX;
    uint8_t* alt = m ? (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    if (alt && nvs_get_blob(h, "liste", alt, &len) == ESP_OK) {
      for (int i = 0; i < m; i++) { memset(&s_cfg[i], 0, sizeof(SensorCfg)); memcpy(&s_cfg[i], alt + i * sz, sz); }
      s_n = m;
      if (m != n) logDiag("[Sensoren] Gespeicherte Anzahl %d passt nicht zur Liste, %d Sensoren aus dem letzten vollstaendigen Stand geladen", n, m);
    }
    if (alt) heap_caps_free(alt);
  }
  int32_t ho = 0; if (nvs_get_i32(h, "hoehe", &ho) == ESP_OK) s_hoehe = ho / 10.0f;
  size_t l = sizeof(s_mqtt.host); nvs_get_str(h, "mq_host", s_mqtt.host, &l);
  nvs_get_u16(h, "mq_port", &s_mqtt.port);
  l = sizeof(s_mqtt.user); nvs_get_str(h, "mq_user", s_mqtt.user, &l);
  l = sizeof(s_mqtt.pass); nvs_get_str(h, "mq_pass", s_mqtt.pass, &l);
  nvs_get_u8(h, "mq_aktiv", &s_mqtt.aktiv);
  nvs_close(h);
  for (int i = 0; i < s_n; i++) { s_cfg[i].id[sizeof(s_cfg[i].id) - 1] = 0; s_cfg[i].topic[sizeof(s_cfg[i].topic) - 1] = 0; s_cfg[i].gruppe[sizeof(s_cfg[i].gruppe) - 1] = 0; }
  // Unbrauchbare Eintraege (z. B. aus einer fruehen fehlerhaften Speicherung) verwerfen: ID nicht druckbar oder doppelt
  int ok = 0;
  for (int i = 0; i < s_n; i++) {
    bool gut = s_cfg[i].id[0] != 0;
    for (const char* p = s_cfg[i].id; *p && gut; p++) if ((unsigned char)*p < 0x21 || (unsigned char)*p > 0x7E) gut = false;
    for (int k = 0; k < ok && gut; k++) if (strcmp(s_cfg[k].id, s_cfg[i].id) == 0) gut = false;
    if (gut) s_cfg[ok++] = s_cfg[i];
  }
  s_n = ok;
}

// Interne Sensoren anlegen, sobald sie erkannt wurden (Standard: sichtbar, Offset 0)
void sensorenInternAnlegen() {
  struct { const char* id; const char* name; const char* einheit; uint8_t q; uint8_t dez; } d[3] = {
    {"int_temp", "Temperatur", "°C", SQ_INT_TEMP, 1}, {"int_feuchte", "Luftfeuchte", "%", SQ_INT_FEUCHTE, 0}, {"int_druck", "Luftdruck", "hPa", SQ_INT_DRUCK, 0}};
  bool geaendert = false;
  for (auto& e : d) {
    if (sensorenSuche(e.id) >= 0) continue;
    SensorCfg c; memset(&c, 0, sizeof(c));
    kopiere(c.id, sizeof(c.id), e.id); kopiere(c.name, sizeof(c.name), e.name); kopiere(c.raum, sizeof(c.raum), "Gehaeuse");
    kopiere(c.einheit, sizeof(c.einheit), e.einheit);
    c.quelle = e.q; c.sichtbar = 1; c.dezimal = e.dez; c.reihenfolge = (uint8_t)s_n;
    if (sensorenSetze(c)) geaendert = true;
  }
  if (geaendert) sensorenSpeichern();
}

void sensorenInit() {
  if (!s_mx) s_mx = xSemaphoreCreateRecursiveMutex();
  lade();
  ESP_LOGI(TAG, "%d Sensoren geladen, MQTT %s", s_n, s_mqtt.aktiv ? "aktiv" : "aus");
  schalterInit();           // Schalter-Kacheln: Liste laden, bevor der MQTT-Client ihre Topics abonniert
  sensorenInternStart();
  sensorenMqttStart();
}

// ── JSON ─────────────────────────────────────────────────────────────────────
static void cfgInJson(JsonObject o, const SensorCfg& c) {
  // Texte als std::string uebergeben: ArduinoJson speichert char-Felder sonst nur als Zeiger, und die Struktur c lebt nur kurz
  o["id"] = std::string(c.id); o["name"] = std::string(c.name); o["raum"] = std::string(c.raum); o["einheit"] = std::string(c.einheit);
  o["topic"] = std::string(c.topic); o["avail"] = std::string(c.avail); o["json_key"] = std::string(c.json_key); o["icon"] = std::string(c.icon); o["gruppe"] = std::string(c.gruppe); o["ort"] = c.ort; o["aussen"] = sensorIstAussen(c);
  if (c.grenze_flags & 1) o["grenze_min"] = c.grenze_min; else o["grenze_min"] = nullptr;
  if (c.grenze_flags & 2) o["grenze_max"] = c.grenze_max; else o["grenze_max"] = nullptr;
  o["icon_aktiv"] = std::string(sensorenIconName(c));
  o["quelle"] = c.quelle; o["sichtbar"] = c.sichtbar; o["reihenfolge"] = c.reihenfolge; o["dezimal"] = c.dezimal;
  o["offset"] = roundf(c.offset * 100.0f) / 100.0f;
}

std::string sensorenZustandJson() {
  JsonDocument doc;
  MqttEinstellungen m; sensorenMqttHole(&m);
  JsonObject mq = doc["mqtt"].to<JsonObject>();
  mq["host"] = m.host; mq["port"] = m.port; mq["user"] = m.user; mq["aktiv"] = m.aktiv;
  mq["hat_passwort"] = m.pass[0] != 0; mq["verbunden"] = sensorenMqttVerbunden(); mq["status"] = sensorenMqttStatus();
  doc["hoehe"] = s_hoehe; doc["intern_vorhanden"] = g_internVorhanden;
  { extern int iconAnzahl(); extern const char* iconName(int); JsonArray ic = doc["icons"].to<JsonArray>(); for (int i = 0; i < iconAnzahl(); i++) if (strcmp(iconName(i), "windpfeil") != 0) ic.add(iconName(i)); }
  JsonArray arr = doc["sensoren"].to<JsonArray>();
  int64_t jetzt = esp_timer_get_time() / 1000;
  for (int i = 0; i < sensorenAnzahl(); i++) {
    SensorCfg c; SensorWert w;
    if (!sensorenHole(i, &c, &w)) continue;
    JsonObject o = arr.add<JsonObject>();
    cfgInJson(o, c);
    o["gueltig"] = w.gueltig; o["online"] = w.online; o["alarm"] = w.alarm;
    if (w.gueltig) { o["wert"] = w.wert; o["roh"] = w.roh; o["alter_s"] = (long)((jetzt - w.zuletzt_ms) / 1000); }
  }
  JsonArray ka = doc["kandidaten"].to<JsonArray>();
  SensorKandidat* k = (SensorKandidat*)heap_caps_malloc(SENSOR_KANDIDATEN * sizeof(SensorKandidat), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  int nk = k ? sensorenKandidaten(k, SENSOR_KANDIDATEN) : 0;
  for (int i = 0; i < nk; i++) {
    JsonObject o = ka.add<JsonObject>();
    o["name"] = k[i].name; o["geraet"] = k[i].geraet; o["topic"] = k[i].topic; o["avail"] = k[i].avail; o["einheit"] = k[i].einheit; if (k[i].json_key[0]) o["json_key"] = k[i].json_key;
  }
  if (k) heap_caps_free(k);
  std::string out; serializeJson(doc, out); return out;
}

std::string sensorenSicherungJson() {
  JsonDocument doc;
  MqttEinstellungen m; sensorenMqttHole(&m);
  JsonObject mq = doc["mqtt"].to<JsonObject>();   // ohne Passwort
  mq["host"] = m.host; mq["port"] = m.port; mq["user"] = m.user; mq["aktiv"] = m.aktiv;
  doc["hoehe"] = s_hoehe;
  JsonArray arr = doc["sensoren"].to<JsonArray>();
  for (int i = 0; i < sensorenAnzahl(); i++) { SensorCfg c; if (sensorenHole(i, &c, nullptr)) cfgInJson(arr.add<JsonObject>(), c); }
  std::string out; serializeJson(doc, out); return out;
}

int sensorenAusJson(const char* json, bool mitMqtt) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return -1;
  if (doc["hoehe"].is<float>()) sensorenSetzeHoehe(doc["hoehe"].as<float>());
  if (mitMqtt && doc["mqtt"].is<JsonObject>()) {
    JsonObject mq = doc["mqtt"];
    MqttEinstellungen m; sensorenMqttHole(&m);
    kopiere(m.host, sizeof(m.host), mq["host"] | m.host);
    m.port = mq["port"] | m.port;
    kopiere(m.user, sizeof(m.user), mq["user"] | m.user);
    m.aktiv = mq["aktiv"] | m.aktiv;
    const char* pw = mq["pass"] | "";
    bool neuesPw = pw[0] != 0;
    if (neuesPw) kopiere(m.pass, sizeof(m.pass), pw);
    sensorenMqttSetze(m, neuesPw);
  }
  int n = 0;
  if (doc["sensoren"].is<JsonArray>()) {
    // Liste ersetzen, bestehende Werte bleiben ueber die id erhalten
    // Die alte Liste liegt auf dem Heap: auf dem Stapel des Webserver-Tasks (6 KB) waeren das ueber 5 KB und er laeuft ueber
    SensorCfg* alt = (SensorCfg*)heap_caps_malloc(sizeof(SensorCfg) * SENSOR_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    SensorWert* altW = (SensorWert*)heap_caps_malloc(sizeof(SensorWert) * SENSOR_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!alt || !altW) { if (alt) heap_caps_free(alt); if (altW) heap_caps_free(altW); return -1; }
    LOCK();
    int an = s_n;
    memcpy(alt, s_cfg, sizeof(SensorCfg) * SENSOR_MAX); memcpy(altW, s_wert, sizeof(SensorWert) * SENSOR_MAX);
    s_n = 0; memset(s_wert, 0, sizeof(s_wert));
    for (JsonObject o : doc["sensoren"].as<JsonArray>()) {
      if (s_n >= SENSOR_MAX) break;
      SensorCfg c; memset(&c, 0, sizeof(c));
      kopiere(c.id, sizeof(c.id), o["id"] | ""); if (!c.id[0]) continue;
      kopiere(c.name, sizeof(c.name), o["name"] | c.id); kopiere(c.raum, sizeof(c.raum), o["raum"] | "");
      kopiere(c.einheit, sizeof(c.einheit), o["einheit"] | ""); kopiere(c.topic, sizeof(c.topic), o["topic"] | "");
      kopiere(c.avail, sizeof(c.avail), o["avail"] | ""); kopiere(c.json_key, sizeof(c.json_key), o["json_key"] | "");
      kopiere(c.icon, sizeof(c.icon), o["icon"] | ""); kopiere(c.gruppe, sizeof(c.gruppe), o["gruppe"] | ""); { int ot = o["ort"] | 0; c.ort = (ot >= 0 && ot <= 2) ? (uint8_t)ot : 0; }
      if (!o["grenze_min"].isNull()) { c.grenze_min = o["grenze_min"].as<float>(); c.grenze_flags |= 1; }
      if (!o["grenze_max"].isNull()) { c.grenze_max = o["grenze_max"].as<float>(); c.grenze_flags |= 2; }
      c.quelle = o["quelle"] | 0; if (c.quelle > SQ_INT_DRUCK) c.quelle = SQ_MQTT;
      c.sichtbar = o["sichtbar"] | 1; c.reihenfolge = o["reihenfolge"] | s_n; c.dezimal = o["dezimal"] | 1;
      c.offset = o["offset"] | 0.0f;
      s_cfg[s_n] = c;
      for (int k = 0; k < an; k++) if (strcmp(alt[k].id, c.id) == 0) { s_wert[s_n] = altW[k]; if (s_wert[s_n].gueltig) setzeWertLocked(s_n, altW[k].roh, altW[k].online); }
      s_n++; n++;
    }
    UNLOCK();
    heap_caps_free(alt); heap_caps_free(altW);
      sensorenMqttNeuAbonnieren();
  }
  sensorenSpeichern();
  return n;
}

void sensorenLogZeile(char* buf, size_t n) {
  int al[SENSOR_MAX]; int na = sensorenAlarme(al, SENSOR_MAX);
  int grenzen = 0; LOCK(); for (int i = 0; i < s_n; i++) if (s_cfg[i].grenze_flags & 3) grenzen++; UNLOCK();
  snprintf(buf, n, "Sensoren: %d eingerichtet, MQTT %s%s, interner Sensor %s, Grenzwerte bei %d Sensoren, %d Alarm(e)", s_n,
           s_mqtt.aktiv ? (sensorenMqttVerbunden() ? "verbunden" : "nicht verbunden") : "aus",
           s_mqtt.aktiv && s_mqtt.host[0] ? "" : "", g_internVorhanden ? "erkannt" : "nicht erkannt", grenzen, na);  // MQTT-Sensoren ohne Wert: keine Meldung auf dem Topic (Abo, Broker-Rechte, Topic falsch) oder Meldung ohne lesbaren Wert
  int ohne = 0, still = 0; char erstes[64] = "";
  LOCK();
  for (int i = 0; i < s_n; i++) {
    if (s_cfg[i].quelle != SQ_MQTT || s_wert[i].gueltig) continue;
    ohne++;
    if (s_wert[i].meldung == 0) { still++; if (!erstes[0]) strlcpy(erstes, s_cfg[i].topic, sizeof(erstes)); }
  }
  UNLOCK();
  if (ohne) { size_t l = strlen(buf); snprintf(buf + l, n > l ? n - l : 0, "; MQTT ohne Wert: %d, davon %d ohne jede Meldung%s%s", ohne, still, still ? " (z. B. " : "", still ? erstes : ""); if (still) { l = strlen(buf); snprintf(buf + l, n > l ? n - l : 0, ")"); } }
}

// ── Anzeige ──────────────────────────────────────────────────────────────────
int sensorenSichtbar(int* idx, int max) {
  int n = 0;
  LOCK();
  for (int i = 0; i < s_n && n < max; i++) if (s_cfg[i].sichtbar) idx[n++] = i;
  for (int a = 1; a < n; a++) { int t = idx[a], b = a - 1; while (b >= 0 && s_cfg[idx[b]].reihenfolge > s_cfg[t].reihenfolge) { idx[b + 1] = idx[b]; b--; } idx[b + 1] = t; }
  UNLOCK();
  return n;
}

int sensorenAnzeige(SensorAnzeige* out, int max) {
  int idx[SENSOR_MAX], n = 0;
  LOCK();
  for (int i = 0; i < s_n; i++) if (s_cfg[i].sichtbar) idx[n++] = i;
  for (int a = 1; a < n; a++) { int t = idx[a], b = a - 1; while (b >= 0 && s_cfg[idx[b]].reihenfolge > s_cfg[t].reihenfolge) { idx[b + 1] = idx[b]; b--; } idx[b + 1] = t; }
  int m = n < max ? n : max;
  int64_t jetzt = esp_timer_get_time() / 1000;
  for (int k = 0; k < m; k++) {
    const SensorCfg& c = s_cfg[idx[k]]; const SensorWert& w = s_wert[idx[k]];
    kopiere(out[k].raum, sizeof(out[k].raum), c.raum); kopiere(out[k].name, sizeof(out[k].name), c.name);
    kopiere(out[k].einheit, sizeof(out[k].einheit), c.einheit);
    out[k].gueltig = w.gueltig;
    bool frisch = w.gueltig && (jetzt - w.zuletzt_ms) < 15 * 60 * 1000;   // 15 Minuten ohne Wert: veraltet
    out[k].online = w.online && frisch;
    if (w.gueltig) snprintf(out[k].text, sizeof(out[k].text), "%.*f", c.dezimal, w.wert); else strlcpy(out[k].text, "--", sizeof(out[k].text));
  }
  UNLOCK();
  return m;
}
