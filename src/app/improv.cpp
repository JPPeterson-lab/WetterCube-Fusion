// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Improv Serial ueber UART0 (und, wo vorhanden, USB-Serial/JTAG). Das Lesen geschieht in einem eigenen Task, die Log-Ausgabe bleibt unveraendert.
// Pakete: "IMPROV", Version 1, Typ, Laenge, Daten, Pruefsumme (Summe aller Bytes davor), Zeilenumbruch. Geschrieben wird roh am Treiber vorbei an
// stdout (sonst machte die Konsole aus 0x0A ein 0x0D 0x0A und zerstoerte Pakete). Das Passwort wird nie ins Log geschrieben.
#include "improv.h"
#include "app_state.h"
#include <string.h>
#include <stdio.h>
#include "driver/uart.h"
#include "soc/soc_caps.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "hal/usb_serial_jtag_ll.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#define IMPROV_GERAET "WetterCube Fusion"
#define IMPROV_PORTAL portal_modus
#define IMPROV_BEDIENEN() webuiBedienen()

enum { T_ZUSTAND = 1, T_FEHLER = 2, T_RPC = 3, T_ERGEBNIS = 4 };
enum { C_WLAN = 1, C_ZUSTAND = 2, C_INFO = 3, C_NETZE = 4 };
enum { Z_GESTOPPT = 0, Z_BEREIT = 2, Z_VERBINDET = 3, Z_VERBUNDEN = 4 };
enum { F_KEINER = 0, F_PAKET = 1, F_BEFEHL = 2, F_VERBINDUNG = 3, F_UNBEKANNT = 255 };

static SemaphoreHandle_t s_schreib = nullptr;
static volatile bool s_anfrage = false;
static volatile int s_quelle = 0;                 // woher die letzte Anfrage kam: 0 = UART0, 1 = USB-Serial/JTAG (Antwort geht dorthin zurueck)
static char s_ssid[33], s_pass[64];

static void rohSchreiben(const uint8_t* b, size_t k) {
#if SOC_USB_SERIAL_JTAG_SUPPORTED
  if (s_quelle == 1) {
    size_t i = 0;
    TickType_t t0 = xTaskGetTickCount();
    while (i < k && xTaskGetTickCount() - t0 < pdMS_TO_TICKS(300)) {
      int n = usb_serial_jtag_ll_write_txfifo(b + i, k - i);
      i += n;
      usb_serial_jtag_ll_txfifo_flush();
      if (n == 0) vTaskDelay(1);
    }
    return;
  }
#endif
  uart_write_bytes(UART_NUM_0, (const char*)b, k);
}

static void senden(uint8_t typ, const uint8_t* d, size_t n) {
  uint8_t b[272];
  if (n > 255) return;
  memcpy(b, "IMPROV", 6);
  b[6] = 1; b[7] = typ; b[8] = (uint8_t)n;
  if (n) memcpy(b + 9, d, n);
  size_t k = 9 + n;
  uint8_t s = 0;
  for (size_t i = 0; i < k; i++) s += b[i];
  b[k++] = s; b[k++] = '\n';
  xSemaphoreTake(s_schreib, portMAX_DELAY);
  rohSchreiben(b, k);
  xSemaphoreGive(s_schreib);
}
static void zustand(uint8_t z) { senden(T_ZUSTAND, &z, 1); }
static void fehler(uint8_t f) { senden(T_FEHLER, &f, 1); }

// Zeichenkette mit vorangestellter Laenge anhaengen
static size_t text(uint8_t* ziel, size_t pos, const char* t) {
  size_t n = strlen(t); if (n > 100) n = 100;
  ziel[pos++] = (uint8_t)n; memcpy(ziel + pos, t, n);
  return pos + n;
}
// Ergebnis eines Befehls: Befehlsnummer, Laenge, dann die Zeichenketten
static void ergebnis(uint8_t befehl, const uint8_t* texte, size_t n) {
  uint8_t d[260]; d[0] = befehl; d[1] = (uint8_t)n; if (n) memcpy(d + 2, texte, n);
  senden(T_ERGEBNIS, d, n + 2);
}

static void adresseMelden() {
  uint8_t t[120]; char url[80];
  snprintf(url, sizeof(url), "http://%s/", WiFi.localIP().toString().c_str());
  size_t k = text(t, 0, url);
  ergebnis(C_WLAN, t, k);
}

static void zustandMelden() {
  if (WiFi.status() == WL_CONNECTED && !IMPROV_PORTAL) { zustand(Z_VERBUNDEN); adresseMelden(); }
  else if (IMPROV_PORTAL) zustand(Z_BEREIT);
  else zustand(Z_GESTOPPT);                      // Start laeuft noch (WLAN wird gerade aufgebaut)
}

static void befehl(const uint8_t* d, size_t laenge) {   // d = Befehlsnummer, Datenlaenge, Daten
  if (laenge < 2 || (size_t)d[1] + 2 > laenge) { fehler(F_PAKET); return; }
  const uint8_t cmd = d[0], dl = d[1];
  const uint8_t* nutz = d + 2;
  fehler(F_KEINER);
  if (cmd == C_ZUSTAND) { zustandMelden(); return; }
  if (cmd == C_INFO) {
    uint8_t t[200]; size_t k = 0;
    k = text(t, k, IMPROV_GERAET); k = text(t, k, FIRMWARE_VERSION); k = text(t, k, CONFIG_IDF_TARGET); k = text(t, k, MDNS_NAME);
    ergebnis(C_INFO, t, k);
    return;
  }
  if (cmd == C_NETZE) {
    if (IMPROV_PORTAL) {
      const int n = WiFi.scanNetworks();
      for (int i = 0; i < n && i < 20; i++) {
        String s = WiFi.SSID(i);
        if (s.isEmpty()) continue;
        uint8_t t[140]; char r[12]; snprintf(r, sizeof(r), "%d", (int)WiFi.RSSI(i));
        size_t k = text(t, 0, s.c_str()); k = text(t, k, r); k = text(t, k, WiFi.verschluesselt(i) ? "YES" : "NO");
        ergebnis(C_NETZE, t, k);
      }
    }
    ergebnis(C_NETZE, nullptr, 0);                       // leeres Ergebnis: Liste zu Ende
    return;
  }
  if (cmd == C_WLAN) {
    if (!IMPROV_PORTAL) { fehler(F_UNBEKANNT); logDiag("[Improv] WLAN-Daten abgelehnt: Gerät ist nicht im Einrichtungsmodus"); return; }
    if (dl < 2) { fehler(F_PAKET); return; }
    const size_t sl = nutz[0];
    if (sl == 0 || sl > 32 || 1 + sl + 1 > dl) { fehler(F_PAKET); return; }
    const size_t pl = nutz[1 + sl];
    if (pl > 63 || 1 + sl + 1 + pl > dl) { fehler(F_PAKET); return; }
    if (s_anfrage) { fehler(F_UNBEKANNT); return; }      // der letzte Versuch laeuft noch
    memcpy(s_ssid, nutz + 1, sl); s_ssid[sl] = 0;
    memcpy(s_pass, nutz + 2 + sl, pl); s_pass[pl] = 0;
    s_anfrage = true;
    zustand(Z_VERBINDET);
    logDiag("[Improv] WLAN-Daten per Kabel erhalten (Netz '%s')", s_ssid);
    return;
  }
  fehler(F_BEFEHL);
}

// Paketleser je Quelle: sammelt Bytes, ignoriert alles dazwischen (Log-Zeilen), prueft Kopf und Pruefsumme
struct Leser { uint8_t puf[272]; size_t n = 0; };
static Leser s_leser[2];
static void fuettern(int quelle, uint8_t b) {
  static const char KOPF[] = "IMPROV";
  Leser& l = s_leser[quelle];
  if (l.n < 6) {
    if (b == (uint8_t)KOPF[l.n]) { l.puf[l.n++] = b; return; }
    l.n = 0;
    if (b == (uint8_t)KOPF[0]) l.puf[l.n++] = b;
    return;
  }
  l.puf[l.n++] = b;
  if (l.n < 9) return;
  const size_t gesamt = 9 + (size_t)l.puf[8] + 1;
  if (gesamt > sizeof(l.puf)) { l.n = 0; return; }
  if (l.n < gesamt) return;
  uint8_t s = 0;
  for (size_t i = 0; i + 1 < gesamt; i++) s += l.puf[i];
  const bool ok = l.puf[6] == 1 && s == l.puf[gesamt - 1];
  s_quelle = quelle;
  if (!ok) { fehler(F_PAKET); }
  else if (l.puf[7] == T_RPC) befehl(l.puf + 9, l.puf[8]);
  l.n = 0;
}

static void improvTask(void*) {
  uint8_t puf[64];
  for (;;) {
    const int n = uart_read_bytes(UART_NUM_0, puf, sizeof(puf), pdMS_TO_TICKS(20));
    for (int i = 0; i < n; i++) fuettern(0, puf[i]);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    while (usb_serial_jtag_ll_rxfifo_data_available()) {
      const int m = usb_serial_jtag_ll_read_rxfifo(puf, sizeof(puf));
      for (int i = 0; i < m; i++) fuettern(1, puf[i]);
    }
#endif
  }
}

void improvStart() {
  if (s_schreib) return;
  s_schreib = xSemaphoreCreateMutex();
  // Treiber fuer das Lesen von UART0 (die Konsole schreibt weiter ohne Treiber; ein bereits installierter Treiber ist in Ordnung)
  if (!uart_is_driver_installed(UART_NUM_0)) uart_driver_install(UART_NUM_0, 512, 512, 0, nullptr, 0);
  xTaskCreatePinnedToCore(improvTask, "improv", 4096, nullptr, 2, nullptr, 0);
}

void improvPortalTick() {
  if (!s_anfrage) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(s_ssid, s_pass);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { IMPROV_BEDIENEN(); delay(20); }
  if (WiFi.status() == WL_CONNECTED) {
    cfg.ssid = s_ssid; cfg.pass = s_pass;               // alle anderen Einstellungen bleiben, wie sie sind
    speichereCfg();
    logDiag("[Improv] Verbunden mit '%s', gespeichert, Neustart", s_ssid);
    zustand(Z_VERBUNDEN);
    adresseMelden();
    memset(s_pass, 0, sizeof(s_pass)); s_anfrage = false;
    delay(1500);
    ESP.restart();
  } else {
    logDiag("[Improv] Verbindung mit '%s' fehlgeschlagen", s_ssid);
    WiFi.disconnect();
    memset(s_pass, 0, sizeof(s_pass)); s_anfrage = false;
    fehler(F_VERBINDUNG);
    zustand(Z_BEREIT);
  }
}
