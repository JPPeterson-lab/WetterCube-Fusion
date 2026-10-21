// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Netzwerk-Task (Core 0): WLAN, Zeit, Datenabruf, Aktualisierung der Oberfläche, WebUI.
// LVGL läuft in einem eigenen Task auf Core 1; hier wird nur unter LvLock auf die UI zugegriffen.
#include "app_state.h"
#include "historie.h"
#include "homeassistant.h"
#include "lueften.h"
#include "../sensors/sensors.h"
#include <time.h>
#include "navigation.h"
#include "../storage/storage.h"
#include "../ui/screens.h"

static const char* PORTAL_HINWEIS =
  "Bitte das Gerät einrichten.\n\n"
  "Verbinde Handy oder Rechner mit dem WLAN\n"
  PORTAL_SSID "\n"
  "Passwort: %PW%\n\n"
  "Es öffnet sich ein Konfigurationsportal.\n"
  "Falls nicht: im Browser http://192.168.4.1 aufrufen.";

// Einrichtungsmodus nach fehlgeschlagener Verbindung: alle 60 s erneut mit den gespeicherten Daten
// versuchen (z. B. war der Router beim Start noch nicht bereit) und bei Erfolg neu starten.
// Nicht stören, solange jemand per Handy am Portal hängt, und nicht, wenn nie ein WLAN eingerichtet wurde.
static unsigned long letzterPortalRetry = 0;
static void portalWlanRetry() {
  if (cfg.ssid.isEmpty()) return;
  if (WiFi.softAPgetStationNum() > 0) return;
  if (millis() - letzterPortalRetry < 60000UL) return;
  letzterPortalRetry = millis();

  WiFi.mode(WIFI_AP_STA);
  wifiLetzterGrund = 0;
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) { webuiBedienen(); delay(20); }
  if (WiFi.status() == WL_CONNECTED) {
    logDiag("[WLAN] Verbindung im Einrichtungsmodus hergestellt, Neustart");
    delay(500);
    ESP.restart();
  }
  logDiag("[WLAN] Wiederholung fehlgeschlagen: %s", wifiGrundName(wifiLetzterGrund));
  WiFi.disconnect(false, false);   // STA ruhigstellen, Portal-AP bleibt
}

static void portalBetrieb(const char* titel, const char* text) {
  portalPasswortNeu();                                   // neues Passwort fuer das Einrichtungs-WLAN, steht am Display
  String t = text; t.replace("%PW%", portalPasswort);
  uiHinweis(titel, t.c_str());
  startePortal();
  for (;;) { webuiBedienen(); portalWlanRetry(); delay(5); }   // bis zum Neustart nach dem Speichern
}

extern void zeitServerStarten();   // platform/Zeit.cpp
static bool zeitSynchronisieren() {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  zeitServerStarten();
  struct tm ti;
  for (int i = 0; i < 30; i++) { if (getLocalTime(&ti, 300)) return true; }
  return false;
}

// Alle Daten abrufen, Fortschritt auf dem Bootscreen zeigen
static void alleDatenLaden(bool mitBoot) {
  auto schritt = [&](const char* text, int pct) { if (mitBoot) uiBoot(text, pct); };
  if (cfg.lat == 0.0f && !cfg.location.isEmpty()) {
    schritt(("Ermittle Standort " + cfg.location + " ...").c_str(), 60);
    if (geocodeLocation(cfg.location)) speichereCfg();
  }
  schritt("Lade Wetterdaten ...", 68);   fetchWetter();
  schritt("Lade Pollenflug ...", 76);    fetchDwdPollen(); fetchOpenMeteoPollen();
  schritt("Lade Warnungen ...", 84);     fetchDwdWarnungen();
  schritt("Lade Biowetter ...", 90);     fetchBioWetter();
  schritt("Lade Warnkarte ...", 95);     fetchWarnkarte();
}

static void netTask(void*) {
  otaStartPruefen();
  logDiag("[Start] WetterCube Fusion v%s (SRAM frei %u, groesster Block %u)", FIRMWARE_VERSION,
          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  setBrightness(cfg.brightness);

  // ── WLAN ──
  if (cfg.ssid.isEmpty()) portalBetrieb("Einrichtung erforderlich", PORTAL_HINWEIS);

  uiBoot(("Verbinde mit " + cfg.ssid + " ...").c_str(), 10);
  WiFi.mode(WIFI_STA);
  // WLAN-Energiesparmodus bleibt im Normalbetrieb an (Standard): ohne ihn laeuft staendig WLAN-Empfang in den PSRAM, den auch das Display
  // fuer das Bild liest, und es entstehen Streifen am Bildschirmrand. Nur waehrend eines Updates oder Uploads ist er aus (siehe ota.cpp).
  wifiEventRegistrieren();
  // Zwei Versuche (je 15 s): ein neues WiFi.begin() hilft, wenn der Router beim ersten Mal noch nicht bereit war
  uint32_t t0 = millis();
  for (int versuch = 1; versuch <= 2 && WiFi.status() != WL_CONNECTED; versuch++) {
    wifiLetzterGrund = 0;
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    uint32_t tv = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - tv < 15000) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
      logDiag("[WLAN] Versuch %d fehlgeschlagen: %s", versuch, wifiGrundName(wifiLetzterGrund));
      if (versuch < 2) { WiFi.disconnect(); delay(300); }
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiStartInfo = "verbunden nach " + String((millis() - t0) / 1000.0, 1) + " s, Signal " + String(WiFi.RSSI()) + " dBm";
    logDiag("[WLAN] %s", wifiStartInfo.c_str());
  } else {
    wifiStartInfo = String("FEHLGESCHLAGEN: ") + wifiGrundName(wifiLetzterGrund);
    logDiag("[WLAN] Verbindung beim Start fehlgeschlagen, Einrichtungsmodus (Wiederholung alle 60 s)");
    portalBetrieb("WLAN-Verbindung fehlgeschlagen",
      "Das WLAN konnte nicht erreicht werden.\nSSID und Passwort prüfen.\n\n"
      "Verbinde Handy oder Rechner mit dem WLAN\n" PORTAL_SSID "\n"
      "Passwort: %PW%\n"
      "und rufe http://192.168.4.1 auf.\n\n"
      "Das Gerät versucht es alle 60 Sekunden selbst erneut.");
  }
  wifi_verbunden = true;
  uiBoot(("Verbunden: " + WiFi.localIP().toString()).c_str(), 30);

  uiBoot("Synchronisiere Zeit ...", 45);
  if (!zeitSynchronisieren()) logDiag("[Zeit] NTP-Synchronisation fehlgeschlagen");
  starteWebUI();
  uiBoot("Starte ...", 55);

  alleDatenLaden(true);
  if (!wetter.geladen) { delay(3000); fetchWetter(); }   // Open-Meteo antwortet direkt nach dem Start gelegentlich mit 503

  aktualisiereUI();
  aktualisiereDwdWarnBtn();
  uiBoot("Fertig", 100);
  delay(400);
  { LvLock lock; nav_goto(SCREEN_ID_SCREEN_1); }
  otaRollbackAnzeigen();    // war das letzte Update nicht gestartet: einmalige Meldung
  notfallHinweisPruefen();  // nach drei Abstürzen in Folge: Hinweis auf die Wiederherstellung
  logDiag("[Start] Bereit: SRAM frei %u (min %u), PSRAM frei %u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getFreePsram());

#ifdef FUSION_TEST_HANG
  logDiag("[TEST] Testfirmware haengt absichtlich (Rollback-Pruefung)");
  for (;;) delay(1000);
#endif
  // ── Dauerbetrieb ──
  unsigned long tUhr = millis(), tWetter = millis(), tPollen = millis(), tWarn = millis(),
                tKarte = millis(), tBio = millis(), tHell = millis(), tWlan = millis();
  if (!abrufHatteErfolg(AB_KARTE)) tKarte -= INTERVALL_KARTE - 120000UL;   // Karte beim Start nicht geladen: nach 2 Minuten erneut versuchen
  for (;;) {
    webuiBedienen();
    speicherTick();
    historieTick();
    otaGesundPruefen(millis());
    unsigned long jetzt = millis();

    if (jetzt - tHell >= 500) { tHell = jetzt; helligkeitPruefen(); uiWarnenPruefen(); sensorenAlarmeVerarbeiten(); }
    haTakt();                                  // Home Assistant: Discovery und Zustand (begrenzt sich selbst auf alle 400 ms)
    { static unsigned long tLueft = 0; if (jetzt - tLueft >= 10000) { tLueft = jetzt; lueftenBerechnen(); } }   // Lueften-Ampel und Feuchte-Warnung

    // Update-Prüfung: 2 Min. nach dem Start, danach alle 6 Stunden (nach einem Fehler nach 1 Stunde)
    if (jetzt - letzterUpdateCheck >= updateCheckIntervall) pruefeUpdate();

    // Installation per Touch (Popup "Jetzt installieren"): hier im Netzwerk-Task, nicht im LVGL-Callback
    if (otaStartAngefordert) {
      otaStartAngefordert = false;
      String fehler = installiereUpdate();
      if (fehler.length() == 0) updateErfolgNeustart();
      else                      updateFehlerAnzeigen(fehler);
    }

    if (jetzt - tUhr >= 60000) { tUhr = jetzt; aktualisiereUhr(); }

    // Ort, Region oder Zone in der WebUI geändert → alles neu
    if (datenNeuLaden) {
      datenNeuLaden = false;
      alleDatenLaden(false);
      aktualisiereUI();
      aktualisiereDwdWarnBtn();
      aktualisiereWarnliste();
      tWetter = tPollen = tWarn = tKarte = tBio = millis();
    }

    if (jetzt - tWetter >= INTERVALL_WETTER) {
      tWetter = jetzt;
      if (!fetchWetter()) tWetter -= INTERVALL_WETTER - 60000UL;   // Fehlschlag: nach 60 s erneut versuchen
      aktualisiereUI();
    }
    { static unsigned long tKlima = millis() - 30UL * 60000UL + 20000UL;   // erster Abruf ca. 20 s nach dem Start, danach alle 30 Minuten
      if (jetzt - tKlima >= 30UL * 60000UL) { tKlima = jetzt; if (!fetchKlima()) tKlima -= 30UL * 60000UL - 120000UL; aktualisiereHinweise(); } }
    if (jetzt - tPollen >= INTERVALL_POLLEN) {
      tPollen = jetzt;
      fetchDwdPollen();
      fetchOpenMeteoPollen();
      pollenWarnGezeigt = false;       // nach jedem Update darf eine neue Belastung erneut warnen
      pollenWarnBestaetigt = false;
      aktualisiereUI();
    }
    if (jetzt - tBio >= INTERVALL_BIO) {
      tBio = jetzt;
      fetchBioWetter();
      aktualisiereUI();
    }
    if (jetzt - tWarn >= INTERVALL_WARN) {
      tWarn = jetzt;
      fetchDwdWarnungen();
      aktualisiereDwdWarnBtn();
      { LvLock lock; if (lv_scr_act() == objects.screenwarnkarte2) aktualisiereWarnliste(); }
    }
    if (jetzt - tKarte >= INTERVALL_KARTE) {
      tKarte = jetzt;
      if (!fetchWarnkarte()) tKarte -= INTERVALL_KARTE - 120000UL;   // Fehlschlag: nach 2 Minuten erneut versuchen
    }

    // WLAN-Aussetzer: alle 30 s neu verbinden
    if (WiFi.status() != WL_CONNECTED && jetzt - tWlan >= 30000) {
      tWlan = jetzt;
      logDiag("[WLAN] getrennt, verbinde neu");
      WiFi.reconnect();
    }
    delay(5);
  }
}

void appStart() {
  xTaskCreatePinnedToCore(netTask, "net", 12 * 1024, NULL, 1, NULL, 0);
}
