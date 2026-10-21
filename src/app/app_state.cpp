// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "app_state.h"
#include <stdarg.h>
#include "esp_attr.h"
#include "esp_system.h"
#include "../platform/WiFi.h"
#include <time.h>

Config cfg;

const DwdRegion DWD_REGIONS[] = {
  {10, "Schleswig-Holstein und Hamburg"},
  {20, "Mecklenburg-Vorpommern"},
  {30, "Niedersachsen und Bremen"},
  {31, "Brandenburg und Berlin"},
  {32, "Sachsen-Anhalt"},
  {33, "Thüringen"},
  {34, "Sachsen"},
  {40, "Nordrhein-Westfalen"},
  {50, "Hessen"},
  {60, "Rheinland-Pfalz und Saarland"},
  {71, "Bayern – Alpen"},
  {72, "Bayern – Alpenvorland"},
  {73, "Bayern – Ost"},
  {74, "Bayern – Nord"},
  {80, "Baden-Württemberg"},
  {0,  nullptr}
};

// WMS-Ausschnitt (EPSG:4326: minLon,minLat,maxLon,maxLat) pro DWD-Region
static const DwdBbox DWD_BBOX[] = {
  {10,  7.8f, 53.4f, 11.4f, 55.1f},  // Schleswig-Holstein + Hamburg
  {20, 10.6f, 53.1f, 14.4f, 54.7f},  // Mecklenburg-Vorpommern
  {30,  6.6f, 51.3f, 11.6f, 53.9f},  // Niedersachsen + Bremen
  {31, 11.3f, 51.4f, 14.8f, 53.6f},  // Brandenburg + Berlin
  {32, 10.6f, 51.0f, 13.2f, 53.1f},  // Sachsen-Anhalt
  {33,  9.9f, 50.2f, 12.7f, 51.6f},  // Thüringen
  {34, 11.9f, 50.2f, 15.1f, 51.7f},  // Sachsen
  {40,  5.9f, 50.3f,  9.5f, 52.5f},  // Nordrhein-Westfalen
  {50,  7.8f, 49.4f, 10.3f, 51.7f},  // Hessen
  {60,  6.1f, 48.9f,  8.5f, 50.9f},  // Rheinland-Pfalz + Saarland
  {71,  9.0f, 47.3f, 13.9f, 50.6f},  // Bayern (alle Teilregionen → gesamt)
  {72,  9.0f, 47.3f, 13.9f, 50.6f},
  {73,  9.0f, 47.3f, 13.9f, 50.6f},
  {74,  9.0f, 47.3f, 13.9f, 50.6f},
  {80,  7.5f, 47.5f, 10.5f, 49.8f},  // Baden-Württemberg
  {0, 0, 0, 0, 0}
};

const DwdBbox* getDwdBbox() {
  for (int i = 0; DWD_BBOX[i].id != 0; i++)
    if (DWD_BBOX[i].id == cfg.dwd_region) return &DWD_BBOX[i];
  static const DwdBbox germany = {0, 5.9f, 47.3f, 15.1f, 55.1f};
  return &germany;
}

WetterDaten    wetter;
PollenDaten    pollen;
KlimaDaten     klima;
BioWetterDaten bio;
WarnDaten      warnungen[5];
int            anzahl_warnungen = 0;

bool wifi_verbunden = false;
bool portal_modus   = false;
bool pollenWarnAktiv = false, pollenWarnBestaetigt = false, pollenWarnGezeigt = false;
bool dwdWarnBestaetigt = false;
unsigned long letzterWetterFetchErfolg = 0;
unsigned long letzteAmpelAnfrage = 0;
volatile bool datenNeuLaden = false;

// ── Start-Diagnose ───────────────────────────────────────────────────────────
String           resetGrundText = "";

// Absturz-Spur (RTC_NOINIT: bleibt bei Panic, Watchdog und Brownout erhalten, nicht bei Stromverlust)
static RTC_NOINIT_ATTR char     spurText[48];
static RTC_NOINIT_ATTR uint32_t spurMagic;
String spurVorAbsturz = "";
void spur(const char* was) {
  strlcpy(spurText, was, sizeof(spurText));
  spurMagic = 0x5350554Cu;
}
void spurBeimStart() {
  esp_reset_reason_t rr = esp_reset_reason();
  bool absturz = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT);
  if (absturz && spurMagic == 0x5350554Cu && spurText[0]) { spurText[sizeof(spurText) - 1] = 0; spurVorAbsturz = spurText; }
  spurMagic = 0; spurText[0] = 0;
}
String           wifiStartInfo  = "";
volatile uint8_t wifiLetzterGrund = 0;

const char* resetGrundName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "Strom angeschlossen";
    case ESP_RST_SW:        return "Software-Neustart (z. B. nach Update)";
    case ESP_RST_PANIC:     return "ABSTURZ (Panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "ABSTURZ (Watchdog)";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (Spannungseinbruch, Netzteil/Kabel pruefen)";
    case ESP_RST_EXT:       return "Reset-Pin";
    case ESP_RST_DEEPSLEEP: return "Deep-Sleep";
    default:                return "unbekannt";
  }
}

const char* wifiGrundName(uint8_t g) {
  switch (g) {
    case 0:   return "kein Fehlercode (Router antwortet nicht)";
    case 2:   return "Authentifizierung abgelaufen";
    case 15:  return "4-Wege-Handshake Timeout (Passwort?)";
    case 200: return "Beacon-Timeout";
    case 201: return "Netzwerk nicht gefunden (SSID/Reichweite/2,4 GHz?)";
    case 202: return "Authentifizierung fehlgeschlagen (Passwort?)";
    case 203: return "Assoziierung fehlgeschlagen";
    case 205: return "Verbindung fehlgeschlagen";
    default:  return "anderer Grund";
  }
}

void wifiEventRegistrieren() {}   // die WLAN-Schicht (platform/wlan.cpp) schreibt den Disconnect-Grund selbst nach wifiLetzterGrund

// ── Letzter erfolgreicher Abruf je Datenquelle ───────────────────────────────
static const char* const AB_NAME[AB_ANZAHL] = {"Wetter", "DWD-Pollen", "Pollen/Luft", "DWD-Warnungen", "Karte", "Biowetter"};
static unsigned long abrufErfolg[AB_ANZAHL] = {0};
void abrufOk(int quelle) { abrufErfolg[quelle] = millis() ? millis() : 1; }
bool abrufHatteErfolg(int quelle) { return abrufErfolg[quelle] != 0; }
String abrufErfolgText() {
  String out;
  for (int i = 0; i < AB_ANZAHL; i++) {
    out += String(i ? ", " : "") + AB_NAME[i] + " ";
    out += abrufErfolg[i] ? "vor " + String((millis() - abrufErfolg[i]) / 60000UL) + " Min." : String("noch nie");
  }
  return out;
}

// ── Diagnose-Log ─────────────────────────────────────────────────────────────
static char logRing[LOG_RING_SIZE][LOG_LINE_LEN];
static uint32_t logSeqRing[LOG_RING_SIZE];   // laufende Nummer je Zeile (für das Schreiben auf die Speicherkarte)
static uint32_t logSeq = 0;
static int  logRingPos = 0, logRingCount = 0;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

void logDiag(const char* fmt, ...) {
  char msg[LOG_LINE_LEN];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  char line[LOG_LINE_LEN];
  struct tm ti;
  if (getLocalTime(&ti, 0)) snprintf(line, sizeof(line), "%02d:%02d:%02d %s", ti.tm_hour, ti.tm_min, ti.tm_sec, msg);
  else                      snprintf(line, sizeof(line), "[%lus] %s", millis() / 1000, msg);

  portENTER_CRITICAL(&logMux);
  memcpy(logRing[logRingPos], line, LOG_LINE_LEN);
  logSeqRing[logRingPos] = ++logSeq;
  logRingPos = (logRingPos + 1) % LOG_RING_SIZE;
  if (logRingCount < LOG_RING_SIZE) logRingCount++;
  portEXIT_CRITICAL(&logMux);

  puts(line);
}

// Zeilen mit laufender Nummer > *seqAb (älteste zuerst) mit Zeilenumbruch in out kopieren; *seqAb wird nachgeführt
int logNeuHolen(uint32_t* seqAb, char* out, int outLen) {
  int len = 0;
  portENTER_CRITICAL(&logMux);
  int count = logRingCount, pos = logRingPos;
  portEXIT_CRITICAL(&logMux);
  char zeile[LOG_LINE_LEN];
  for (int i = count - 1; i >= 0; i--) {
    int idx = (pos - 1 - i + LOG_RING_SIZE * 2) % LOG_RING_SIZE;
    uint32_t seq;
    portENTER_CRITICAL(&logMux);
    seq = logSeqRing[idx];
    memcpy(zeile, logRing[idx], LOG_LINE_LEN);
    portEXIT_CRITICAL(&logMux);
    if (seq <= *seqAb) continue;
    zeile[LOG_LINE_LEN - 1] = 0;
    int l = (int)strlen(zeile);
    if (len + l + 1 >= outLen) break;
    memcpy(out + len, zeile, l); len += l; out[len++] = '\n';
    *seqAb = seq;
  }
  return len;
}

String logHolen() {
  String out;
  portENTER_CRITICAL(&logMux);
  int count = logRingCount, pos = logRingPos;
  portEXIT_CRITICAL(&logMux);
  for (int i = 0; i < count; i++) {
    int idx = (pos - 1 - i + LOG_RING_SIZE * 2) % LOG_RING_SIZE;
    out += String(logRing[idx]) + "\n";
  }
  return out;
}
