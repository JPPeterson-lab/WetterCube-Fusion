// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Sensoren: interner Umweltsensor (AHT20 + BMP280 am I2C-Bus) und externe Sensoren per MQTT (z. B. ESPHome).
// Nur ESP-IDF-Funktionen (nvs, i2c_master, esp-mqtt) plus ArduinoJson; keine Arduino-Klassen im Kern.
#include <stdint.h>
#include <stddef.h>
#include <string>

#define SENSOR_MAX        16     // konfigurierbare Sensoren
#define SENSOR_KANDIDATEN 32     // per MQTT-Discovery gefundene Sensoren (nur im RAM)

enum SensorQuelle : uint8_t { SQ_MQTT = 0, SQ_INT_TEMP = 1, SQ_INT_FEUCHTE = 2, SQ_INT_DRUCK = 3 };

struct SensorCfg {
  char    id[24];        // eindeutig, z. B. "wohnzimmer_temp" oder "int_temp"
  char    name[24];      // Anzeigename
  char    raum[20];      // Raum (zum Gruppieren)
  char    einheit[8];    // "°C", "%", "hPa" ...
  char    topic[96];     // MQTT-Topic des Zustands (leer bei internen Sensoren)
  char    avail[64];     // MQTT-Topic der Erreichbarkeit ("online"/"offline"), optional
  char    json_key[16];  // Wert aus JSON lesen (leer = Nutzlast ist die Zahl selbst, wie bei ESPHome)
  char    icon[16];      // Icon-Name (siehe icons.h, z. B. "thermometer"); leer = automatisch nach Einheit
  uint8_t quelle;        // SensorQuelle
  uint8_t sichtbar;      // 1 = auf dem Display zeigen
  uint8_t reihenfolge;   // Sortierung in der Anzeige
  uint8_t dezimal;       // Nachkommastellen
  float   offset;        // zusaetzliche Korrektur (ESPHome-Sensoren sind meist schon korrigiert: dann 0)
  char    gruppe[20];    // Kachel: Sensoren mit gleichem Namen erscheinen zusammen in einer Kachel, leer = eigene Kachel
  float   grenze_min;    // Warnen, wenn der Wert unter dieser Grenze liegt (nur bei grenze_flags & 1)
  float   grenze_max;    // Warnen, wenn der Wert ueber dieser Grenze liegt (nur bei grenze_flags & 2)
  uint8_t grenze_flags;  // Bit 0: untere Grenze aktiv, Bit 1: obere Grenze aktiv
};

struct SensorWert {
  float   wert;          // angezeigter Wert (Rohwert + offset)
  float   roh;           // Wert vor der Korrektur (fuer den Abgleich)
  bool    gueltig;       // schon ein Wert empfangen
  bool    online;        // laut Erreichbarkeits-Topic bzw. zuletzt aktualisiert
  int64_t zuletzt_ms;    // Zeitpunkt (esp_timer) des letzten Werts
  uint8_t alarm;         // 0 = in Ordnung, 1 = unter der unteren Grenze, 2 = ueber der oberen Grenze
  int64_t verstoss_ms;   // seit wann die Grenze verletzt ist (0 = nicht), fuer die Verzoegerung
};

struct MqttEinstellungen {
  char     host[64];
  uint16_t port;
  char     user[32];
  char     pass[64];     // wird nie in Sicherungen oder Logs ausgegeben
  uint8_t  aktiv;
};

struct SensorKandidat {  // aus der MQTT-Discovery (homeassistant/sensor/+/+/config)
  char name[32];
  char geraet[32];
  char topic[96];
  char avail[64];
  char einheit[8];
};

void sensorenInit();                                   // Einstellungen laden, interner Sensor-Task, MQTT starten
bool sensorenSpeichern();                              // Liste, Hoehe und MQTT-Einstellungen in den NVS schreiben

int  sensorenAnzahl();
bool sensorenHole(int i, SensorCfg* cfg, SensorWert* wert);   // Kopie, thread-sicher
int  sensorenSuche(const char* id);                    // Index oder -1
bool sensorenSetze(const SensorCfg& cfg);              // hinzufuegen oder (gleiche id) ersetzen
bool sensorenLoesche(const char* id);
bool sensorenAbgleich(const char* id, float referenz); // offset so setzen, dass der Anzeigewert gleich der Referenz ist

void sensorenMqttHole(MqttEinstellungen* out);
void sensorenMqttSetze(const MqttEinstellungen& in, bool passUebernehmen);
bool sensorenMqttVerbunden();
int  sensorenMqttStatus();                             // 0 nicht eingerichtet/aus, 1 verbindet, 2 verbunden, 3 Anmeldung abgelehnt, 4 nicht erreichbar
int  sensorenKandidaten(SensorKandidat* out, int max);
bool sensorenMqttSende(const char* topic, const char* nutzlast, int qos, bool retain);   // Meldung senden (nicht blockierend); false, wenn nicht verbunden
void sensorenMqttLetzterWille(const char* topic, const char* nutzlast);                 // "Letzter Wille" des Clients (retain); wirkt ab der naechsten Verbindung, "" = keiner
void sensorenMqttNeuVerbinden();                                                         // Client neu starten (z. B. nach Aenderung des "Letzten Willens")
uint32_t sensorenMqttVerbindungen();                                                      // zaehlt jede (Wieder-)Verbindung zum Broker

const char* sensorenIconName(const SensorCfg& c);      // gewaehlter oder automatisch gewaehlter Icon-Name
float sensorenHoehe();                                 // Standorthoehe in m (fuer den Druck auf Meereshoehe)
void  sensorenSetzeHoehe(float m);
bool  sensorenInternVorhanden();

std::string sensorenZustandJson();                     // Konfiguration + aktuelle Werte (ohne Passwort) fuer die WebUI
std::string sensorenSicherungJson();                   // Teil der Einstellungssicherung (ohne Passwort)
int  sensorenAusJson(const char* json, bool mitMqtt);  // Liste aus JSON uebernehmen; Rueckgabe: Anzahl
void sensorenLogZeile(char* buf, size_t n);            // Kurzstatus fuer das /log

// Fuer die Oberflaeche: sichtbare Sensoren in Anzeigereihenfolge
struct SensorAnzeige { char raum[20]; char name[24]; char text[16]; char einheit[8]; bool online; bool gueltig; };
int  sensorenAnzeige(SensorAnzeige* out, int max);
int  sensorenSichtbar(int* idx, int max);

// Grenzwert-Alarme: der Hook wird ausserhalb der Sensor-Sperre aufgerufen, wenn ein Alarm beginnt (ein = true) oder endet
typedef void (*SensorAlarmHook)(const SensorCfg& cfg, const SensorWert& wert, bool ein);
void sensorenSetzeAlarmHook(SensorAlarmHook h);
void sensorenAlarmeVerarbeiten();         // Hauptschleife: gesammelte Alarm-Ereignisse an den Hook melden
int  sensorenAlarme(int* idx, int max);   // Registry-Indizes der Sensoren mit aktivem Alarm              // Registry-Indizes der sichtbaren Sensoren in Anzeigereihenfolge (fuer die Kacheln)

class WebServer;
void sensorenWebuiRegistrieren(WebServer& server);   // sensors_webui.cpp
