// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Schalter-Kacheln: Geraete im Smart Home (Licht, Steckdose ...) vom Display aus per MQTT schalten. Eine Kachel zeigt den Zustand und
// schaltet beim Antippen um. Befehl und Zustand laufen ueber den MQTT-Broker (gleiche Verbindung wie die Sensoren); die Geraete
// koennen aus der Home-Assistant-Discovery (homeassistant/switch|light/+/+/config) uebernommen oder von Hand eingetragen werden.
#include <stdint.h>
#include <stddef.h>
#include <string>

#define SCHALTER_MAX         8
#define SCHALTER_KANDIDATEN  24

struct SchalterCfg {
  char    id[24];          // eindeutig
  char    name[24];        // Anzeigename
  char    raum[20];        // Untertitel (Raum)
  char    cmd_topic[96];   // hierhin wird der Befehl gesendet
  char    state_topic[96]; // von hier kommt der Zustand (leer: der Zustand wird nur gemerkt)
  char    json_key[16];    // Zustand aus JSON lesen (leer = Nutzlast ist der Zustand selbst)
  char    payload_an[16];  // gesendet fuer "an" (Standard ON)
  char    payload_aus[16]; // gesendet fuer "aus" (Standard OFF)
  char    state_an[16];    // als "an" erkannt (leer = wie payload_an)
  char    state_aus[16];   // als "aus" erkannt (leer = wie payload_aus)
  uint8_t sichtbar;        // 1 = Kachel auf dem Display
  uint8_t reihenfolge;
  uint8_t retain;          // Befehl mit Retain senden
  uint8_t rsv;
};
enum SchalterZustand : uint8_t { SZ_UNBEKANNT = 0, SZ_AUS = 1, SZ_AN = 2 };

struct SchalterKandidat {  // aus der MQTT-Discovery (homeassistant/switch|light/+/+/config)
  char name[32];
  char geraet[32];
  char cmd_topic[96];
  char state_topic[96];
  char payload_an[16], payload_aus[16], state_an[16], state_aus[16];
};

void schalterInit();                                       // Liste laden (vor dem Start des MQTT-Clients)
bool schalterSpeichern();
int  schalterAnzahl();
bool schalterHole(int i, SchalterCfg* cfg, uint8_t* zustand);   // Kopie, thread-sicher
int  schalterSichtbar(int* idx, int max);                  // sichtbare Schalter in Anzeigereihenfolge
bool schalterSetze(const SchalterCfg& c);                  // hinzufuegen oder (gleiche id) ersetzen
bool schalterLoesche(const char* id);
bool schalterSchalte(int i, bool an);                      // Befehl senden; false, wenn nicht verbunden
bool schalterUmschalten(int i);                            // das Gegenteil des Zustands (unbekannt: an)
uint32_t schalterAenderung();                              // Zaehler: steigt bei jeder Aenderung von Liste oder Zustand
int  schalterKandidaten(SchalterKandidat* out, int max);

std::string schalterZustandJson();                         // Liste, Zustaende und Kandidaten fuer die WebUI
std::string schalterSicherungJson();                       // Liste fuer die Einstellungssicherung
int  schalterAusJson(const char* json);                    // Liste uebernehmen (ersetzt sie); Rueckgabe: Anzahl oder -1
void schalterLogZeile(char* buf, size_t n);

class WebServer;
void schalterWebuiRegistrieren(WebServer& server);         // schalter_webui.cpp

// nur fuer sensor_mqtt.cpp
void schalterMqttWert(const char* topic, const char* nutzlast, int laenge);
int  schalterTopicsHole(char (*out)[96], int max);         // Zustands-Topics aller Schalter
void schalterKandidatAuswerten(const char* json);
void schalterWledMelden(const char* statusTopic);          // "wled/<id>/status" gesehen: WLED-Geraet als Kandidat
