// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Gemeinsamer Zustand: Konfiguration, Datenstrukturen, Hilfsfunktionen.
// Die Daten werden vom Netzwerk-Task (Core 0) befüllt; wer sie liest oder LVGL anfasst,
// hält dabei den LVGL-Lock (LvLock).
#include "../platform/Arduino.h"
#include "../platform/Preferences.h"
#include "../platform/WiFi.h"
#include "../platform/HTTPClient.h"
#include <esp_system.h>
#include <lvgl.h>
#include "app_config.h"
#include "../display/lvgl_port.h"

// ── LVGL-Lock als Scope-Guard ────────────────────────────────────────────────
struct LvLock {
  LvLock()  { lvgl_port_lock(-1); }
  ~LvLock() { lvgl_port_unlock(); }
};

// ── Konfiguration (aus Preferences geladen) ──────────────────────────────────
struct Config {
  String ssid;
  String pass;
  String location;
  float  lat = 0.0f;
  float  lon = 0.0f;
  int    dwd_region = 31;       // 31 = Brandenburg/Berlin; Liste siehe app_state.cpp
  bool   pollen_warn = true;
  int    pollen_schwelle = 2;   // 0=gering 1=mittel 2=hoch 3=stark
  int    pollen_auswahl = 0xFF; // „Meine Allergene“: Bit i = POLLEN_ANZEIGE[i] (Standard: alle)
  bool   hinweis_regen = true;  // Hinweis-Kachel auf dem Hauptscreen: Regen / Frost / UV
  bool   hinweis_frost = true;
  bool   hinweis_uv    = true;
  bool   hinweis_sensor = true; // Sensor-Grenzwert-Alarm in der Hinweis-Kachel
  bool   hinweis_druck = true;  // Wetterumschwung (Luftdruck faellt schnell)
  bool   hinweis_schatten = true; // heisser Tag mit starker Sonne: Rolllaeden / Sonnenschutz
  int    schatten_temp = 27;    // ab dieser Tageshoechsttemperatur (und UV 6 oder mehr)
  bool   hinweis_giessen = false; // Garten: Giessen lohnt sich (nur Fruehjahr bis Herbst)
  bool   hinweis_gefuehlt = true; // gefuehlte Temperatur liegt deutlich (5 Grad oder mehr) unter der gemessenen (Wind)
  bool   lueft_kuehlen = true;  // Luefter-Ampel: Kuehlluften im Sommer
  int    lueft_hitze = 24;      // ab dieser Raumtemperatur zaehlt ein Raum als warm
  int    kacheln_seite = 12;    // Kacheln je Seite auf der Sensor-Seite: 8 (gross) oder 12 (kompakt); mehr Kacheln: mehrere Seiten
  bool   anim_symbole = true;   // animierte Wettersymbole (Hauptsymbol und Nacht-Uhr), nur ESP32-P4
  bool   anim_stunden = false;  // auch die vier Stundensymbole animieren
  int    update_intervall_h = 6; // wie oft nach Updates gefragt wird: 1, 6 oder 24 Stunden
  int    update_modus = 2;      // Firmware-Updates: 0 = aus (keine Pruefung, keine Info), 1 = nur Web-Oberflaeche (kein Hinweis am Display), 2 = Display und Web-Oberflaeche
  bool   night_uhr = false;     // im Nachtmodus nur die grosse Uhr zeigen (Nacht-Uhr)
  bool   verlauf_regen = true;  // Regenbalken im Temperaturverlauf
  int    dim_timeout = 3;       // Minuten; 0 = aus
  int    brightness = 80;       // Prozent
  bool   warn_region = true;    // DWD-Warnungen aktiv
  int    night_modus = 0;       // 0 = aus, 1 = feste Uhrzeiten, 2 = Sonnenuntergang bis Sonnenaufgang
  int    night_sonne_ab  = 30;  // Modus 2: Nacht beginnt so viele Minuten nach Sonnenuntergang (auch negativ)
  int    night_sonne_bis = 0;   // Modus 2: Nacht endet so viele Minuten nach Sonnenaufgang (auch negativ)
  bool   night_theme = false;   // nachts dunkles Design (danach wieder das vorherige)
  // Benachrichtigungen aufs Handy (ntfy); Thema und Zugangsdaten stehen nie in Sicherungen oder im Log
  // Home Assistant per MQTT (Discovery und Zustand, homeassistant.cpp)
  bool   ha_aktiv = false;
  String ha_praefix = "wettercube-fusion";
  bool   ntfy_aktiv = false;
  String ntfy_server = "https://ntfy.sh";
  String ntfy_topic;
  String ntfy_token;            // nur noetig bei geschuetzten Themen
  String web_pw;                // Passwort der Web-Oberflaeche als "Salz:Hash" (leer = kein Passwort); steht nicht in der Sicherung
  int    ntfy_quellen = 1;      // Bit 0 = Sensor-Grenzwerte, Bit 1 = Lueften (spaeter weitere)
  bool   ntfy_entwarnung = true;
  // Lueften-Ampel und Feuchte-Warnung (lueften.cpp); beide getrennt ein-/ausschaltbar
  bool   lueft_ampel   = true;  // Streifen mit der Lueften-Ampel auf der Sensor-Seite
  bool   lueft_warnung = false; // Warnbildschirm (und ntfy), wenn ein Raum laenger zu feucht ist
  int    lueft_feuchte = 65;    // Feuchte-Grenze in Prozent (ab hier zu feucht, ab Grenze + 10 Schimmelgefahr)
  int    night_from = 22 * 60;  // Minuten seit Mitternacht
  int    night_to   =  6 * 60;
  int    night_brightness = 10;
  // Ruhezeit: in dieser Zeit erscheinen keine Warnungen auf dem Display (kein Blinken, kein Warnbildschirm, keine Hinweis-Kachel);
  // Handy-Meldungen (ntfy) laufen weiter
  int    ruhe_modus = 0;        // 0 = aus, 1 = feste Uhrzeiten, 2 = wie der Nachtmodus
  int    ruhe_von = 22 * 60;    // Minuten seit Mitternacht
  int    ruhe_bis =  7 * 60;
  // Ampel-Schwellwerte (°C) für das WetterAmpel-Addon
  int    ampel_gruen_min = 15, ampel_gruen_max = 19;
  int    ampel_gelb_min  = 20, ampel_gelb_max  = 24;
  int    ampel_rot_min   = 25, ampel_rot_max   = 99;
  int    ampel_modus     = 0;   // 0 = Temperatur, 1 = Pollen, 2 = Luftqualität (WetterAmpel-Addon)
  String bio_zone = "A";        // DWD-Biowetter-Zone A–K
  // Biowetter-Kategorien der Kurzübersicht (0=Befinden 1=Hypotonie 2=Bluthochdruck
  // 3=Rheuma entz. 4=Rheuma deg. 5=Asthma 6=Wärmebelastung)
  int ws_cat0 = 0, ws_cat1 = 1, ws_cat2 = 2, ws_cat3 = 5;
};
extern Config cfg;

// ── DWD-Regionen ─────────────────────────────────────────────────────────────
struct DwdRegion { int id; const char* name; };
extern const DwdRegion DWD_REGIONS[];
struct DwdBbox { int id; float minLon, minLat, maxLon, maxLat; };
const DwdBbox* getDwdBbox();   // WMS-Ausschnitt für cfg.dwd_region (Fallback: Deutschland)

// ── Datenstrukturen ──────────────────────────────────────────────────────────
struct WetterDaten {
  float  temp = 0, feels_like = 0, humidity = 0, wind_speed = 0;
  int    wind_dir = 0;
  float  pressure = 0;
  int    wmo_code = 0;
  float  uv_index = 0, uv_next3h = 0;   // uv_index = Tageshoechstwert
  float  uv_jetzt = 0;                  // UV-Index der aktuellen Stunde
  float  temp_min = 0, temp_max = 0;
  bool   is_day = true;
  bool   geladen = false;
  String sunrise, sunset;
  float  temp_forecast[4] = {};
  int    wmo_forecast[4] = {};
  bool   tag_forecast[4] = {true, true, true, true};   // Tag oder Nacht in der jeweiligen Stunde (Symbol)
  String time_forecast[4];
  float  wind_speed_forecast[4] = {};
  int    wind_dir_forecast[4] = {};
  // Regen-Nowcast (Open-Meteo minutely_15): Startzeit und Niederschlag je 15-Minuten-Slot
  static const int NC_MAX = 12;
  time_t nc_start[NC_MAX] = {};
  float  nc_mm[NC_MAX]    = {};
  int    nc_n             = 0;
  // Frost (nächste 12 Stunden) und UV (jetzt bis +3 Stunden) für die Hinweis-Kachel
  float  frost_min   = 99.0f;   // tiefste Temperatur der nächsten 12 Stunden (99 = keine Daten)
  bool   frost_jetzt = false;   // schon jetzt <= 0 °C
  String frost_ab;              // Uhrzeit der ersten Stunde mit <= 0 °C
  float  uv_hinweis  = 0.0f;
  // Temperaturverlauf: jetzt bis +24 Stunden (25 Punkte), stündlich
  float  vl_temp[25]  = {};
  float  vl_regen[25] = {};     // Niederschlag je Stunde in mm
  int    vl_n         = 0;      // Anzahl gültiger Punkte
  int    vl_h0        = 0;      // Stunde (0-23) des ersten Punkts
};

struct PollenDaten {
  // Open-Meteo (Körner/m³), nächste Stunde
  float birke = -1, graeser = -1, erle = -1, beifuss = -1, ambrosia = -1;
  // Open-Meteo, 3 Stunden-Slots (h+1..h+3)
  float h_birke[3] = {-1,-1,-1}, h_graeser[3] = {-1,-1,-1}, h_erle[3] = {-1,-1,-1};
  float h_beifuss[3] = {-1,-1,-1}, h_ambrosia[3] = {-1,-1,-1};
  float h_roggen[3] = {-1,-1,-1}, h_esche[3] = {-1,-1,-1}, h_hasel[3] = {-1,-1,-1};
  int   h_slot[3] = {0,0,0};
  // Luftqualität
  int   aqi = -1, aqi_next = -1;
  float pm25 = -1, pm25_next = -1, pm10 = -1, pm10_next = -1;
  float no2 = -1, no2_next = -1, o3 = -1, o3_next = -1;
  float so2 = -1, co = -1, staub = -1;                       // weitere Werte (ug/m3)
  int   idx[5] = {-1,-1,-1,-1,-1};                           // Einzel-AQI jetzt: PM2.5, PM10, NO2, O3, SO2 (bestimmt den Leitschadstoff)
  int   aqi_h[24];                                           // AQI der Stunden h0 .. h0+23 (-1 = unbekannt)
  float pollen_h[24];                                        // staerkster der fuenf Open-Meteo-Pollen je Stunde (Koerner/m3, -1 = unbekannt)
  int   aqi_h0 = -1;                                         // Uhrzeit (0-23) des ersten Balkens, -1 = noch keine Daten
  PollenDaten() { for (int i = 0; i < 24; i++) { aqi_h[i] = -1; pollen_h[i] = -1; } }
  // DWD-Pollenflug: heute / morgen / übermorgen (Stufen 0–3)
  float dwd_birke = -1,    dwd_birke_tmr = -1,    dwd_birke_da = -1;
  float dwd_hasel = -1,    dwd_hasel_tmr = -1,    dwd_hasel_da = -1;
  float dwd_erle = -1,     dwd_erle_tmr = -1,     dwd_erle_da = -1;
  float dwd_esche = -1,    dwd_esche_tmr = -1,    dwd_esche_da = -1;
  float dwd_graeser = -1,  dwd_graeser_tmr = -1,  dwd_graeser_da = -1;
  float dwd_roggen = -1,   dwd_roggen_tmr = -1,   dwd_roggen_da = -1;
  float dwd_beifuss = -1,  dwd_beifuss_tmr = -1,  dwd_beifuss_da = -1;
  float dwd_ambrosia = -1, dwd_ambrosia_tmr = -1, dwd_ambrosia_da = -1;
};

// Luftdruck-Trend und Niederschlag der letzten Tage (data_klima.cpp), fuer Wetterumschwung und Giessen-Tipp
struct KlimaDaten {
  bool  geladen = false, druckOk = false, regenOk = false;
  float druck = 0, druckDelta3h = 0;                 // hPa jetzt, Aenderung in den letzten 3 Stunden
  float regen3Tage = 0, regenHeute = 0, regenMorgen = 0;   // mm
  float tmaxHeute = 0;
};

struct WarnDaten {
  bool   aktiv = false;
  String typ;
  int    level = 0;       // 1=Vorinformation 2=Warnung 3=Stark 4=Extrem
  String titel, beschreibung, region;
};

// Biowetter: 5 Zeitperioden x 7 Kategorien
// Perioden: 0=heute Vormittag 1=heute Nachmittag 2=morgen Vormittag 3=morgen Nachmittag 4=übermorgen Vormittag
// Werte: 0=kein Einfluss 1=positiv 2=gering 3=hoch/stark
#define BIO_PERIODEN 5
struct BioWetterDaten {
  int  wert[BIO_PERIODEN][7];
  bool vorhanden[BIO_PERIODEN] = {};   // false = der DWD liefert für diese Periode nichts ("-"); true mit Wert 0 = "kein Einfluss"
  bool geladen = false;
  BioWetterDaten() { memset(wert, 0, sizeof(wert)); }
};

extern WetterDaten    wetter;
extern PollenDaten    pollen;
extern KlimaDaten     klima;
extern BioWetterDaten bio;
extern WarnDaten      warnungen[5];
extern int            anzahl_warnungen;

// ── Zustand ──────────────────────────────────────────────────────────────────
extern bool wifi_verbunden;
extern bool portal_modus;
extern char portalPasswort[12];            // Passwort des Einrichtungs-WLANs (8 Ziffern, steht am Display)
void portalPasswortNeu();
extern bool pollenWarnAktiv, pollenWarnBestaetigt, pollenWarnGezeigt;
extern bool dwdWarnBestaetigt;
extern unsigned long letzterWetterFetchErfolg;   // für /api/ampel (Staleness)
extern unsigned long letzteAmpelAnfrage;
extern volatile bool datenNeuLaden;   // WebUI hat Ort/Region geändert → alles neu abrufen

// ── Diagnose-Log (Ringpuffer, per Browser unter /log abrufbar) ───────────────
#define LOG_RING_SIZE 40
#define LOG_LINE_LEN  128
void logDiag(const char* fmt, ...);
String logHolen();
int logNeuHolen(uint32_t* seqAb, char* out, int outLen);   // neue Zeilen für die Speicherkarte

// Absturz-Spur: die letzte Tätigkeit steht im RTC-Speicher und bleibt bei Panic, Watchdog und Brownout erhalten.
// Nach einem Absturz steht sie im /log („Zuletzt aktiv vor dem Neustart: …“)
void spur(const char* was);
void spurBeimStart();                 // früh im Start aufrufen (liest die Spur des letzten Laufs)
extern String spurVorAbsturz;

// Zeitpunkt des letzten erfolgreichen Abrufs je Datenquelle (Anzeige im /log; Erfolge selbst werden nicht einzeln protokolliert)
enum { AB_WETTER = 0, AB_DWD_POLLEN, AB_POLLEN_OM, AB_WARNUNGEN, AB_KARTE, AB_BIO, AB_ANZAHL };
void   abrufOk(int quelle);
bool   abrufHatteErfolg(int quelle);
String abrufErfolgText();

// ── Start-Diagnose (Anzeige im /log) ─────────────────────────────────────────
extern String           resetGrundText;     // warum wurde neu gestartet (Strom, Software, Absturz, Brownout ...)
extern String           wifiStartInfo;      // WLAN beim Start: Dauer und Signal oder Fehlergrund
extern volatile uint8_t wifiLetzterGrund;   // letzter Disconnect-Grund des WLAN-Treibers
const char* resetGrundName(esp_reset_reason_t r);
const char* wifiGrundName(uint8_t g);
void wifiEventRegistrieren();            // früher nötig, die Ereignisse registriert jetzt die WLAN-Schicht selbst
// Protokolliert einen fehlgeschlagenen HTTPS-Abruf mit TLS-Fehlertext und Speicherstand
inline void logHttpFehler(const char* tag, int code, WiFiClientSecure& sc) {
  char e[100] = "";
  int n = sc.lastError(e, sizeof(e));
  logDiag("[%s] HTTP %d, TLS %d (%s), SRAM frei %u, groesster Block %u", tag, code, n, e,
          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

// ── Konfiguration ────────────────────────────────────────────────────────────
void ladeCfg();
void speichereCfg();
bool geocodeLocation(const String& city);

// ── Daten abrufen (blockierend, nur aus dem Netzwerk-Task aufrufen) ──────────
bool fetchWetter();                           // true = erfolgreich
bool fetchKlima();                            // Luftdruck-Trend, Regen der letzten Tage
void fetchDwdPollen();
void fetchOpenMeteoPollen();
void fetchBioWetter();
void fetchDwdWarnungen();
void uiSensorenInit();                       // Sensor-Seite (ui_sensoren.cpp)
bool fetchWarnkarte();                        // true = erfolgreich (bei Fehler nach 60 s erneut versuchen)
void warnkarteFreigeben();                    // Kartenbild ausblenden und Speicher freigeben (vor dem Dekodieren der neuen Karte)

// ── Pollen-Gesamtwert und WetterAmpel ────────────────────────────────────────
extern const char* const POLLEN_ANZEIGE[8];   // Hasel, Erle, Esche, Birke, Gräser, Roggen, Beifuß, Ambrosia
bool  pollenGewaehlt(int i);                  // Allergen i in „Meine Allergene“ gewählt?
String schlimmstesPollen();                   // Name des stärksten gewählten Allergens
float pollenWertWarnung(int i);               // Wert eines Allergens (DWD-Skala 0–3): Open-Meteo, sonst DWD-Tageswert
void  ampelBerechnen(const char*& active, const char*& modus, String& detail);   // Farbe je nach Modus + Klartext

// ── Darstellung der Daten (Farben, Texte, Icons) ─────────────────────────────
float openMeteoToDwd(float grains);
const char* pollenText(float v);
lv_color_t pollenColor(float v);
lv_color_t tempColor(float t);
lv_color_t aqiColor(int aqi);
lv_color_t pm25Color(float v);
lv_color_t pm10Color(float v);
lv_color_t no2Color(float v);
lv_color_t o3Color(float v);
lv_color_t so2Color(float v);
lv_color_t coColor(float v);
lv_color_t staubColor(float v);
lv_color_t uvColor(float uv);
const char* aqiStatusText(int aqi);
lv_color_t bioWertColor(int v);
const char* bioWertText(int v);
const lv_img_dsc_t* wmoZuImage(int wmo, bool day);
const char* wmoZuAnimName(int wmo, bool day);
void mondPhase(time_t t, float* beleuchtet, int* phase8);   // Beleuchtung in Prozent, Phase 0 (Neumond) bis 7
const char* mondName(int phase8);   // Name des Symbols im Animationspaket (z. B. "overcast_day_rain")
const char* wetterText(int wmo);    // WMO-Wettercode als deutscher Text
void wetterIconZeichnen(lv_obj_t* obj, const lv_img_dsc_t* d, int zielPx, int xOben, int yOben);   // Symbol skaliert und lagerichtig zeigen
const char* windRichtung(int deg);
String bereinigeText(const String& s);   // UTF-8 -> nur Zeichen, die die Schriften enthalten

// ── Bedienoberfläche ─────────────────────────────────────────────────────────
void aktualisiereUI();                    // alle Labels/Bilder aus den Daten befüllen (nimmt den LVGL-Lock selbst)
void aktualisiereUhr();                   // nur Uhrzeit, Datum und Hinweis-Kachel (jede Minute)
void aktualisiereHinweise();              // Hinweis-Kachel (Sensor-Alarm / Regen / Frost / UV) links oben auf dem Hauptscreen
void uiNachtInit();                       // Nacht-Uhr (ui_nacht.cpp)
void uiNachtUhrPruefen(bool nacht);       // in den Nachtmodus wechseln / zurueck, alle 500 ms
void uiNachtUhrErzwingen();               // Inhalt jetzt setzen (Screenshot), Aufrufer haelt den LVGL-Lock
void uiSystemInit();                      // Systembereich im Menue (ui_system.cpp)
void uiSystemErzwingen();                 // jetzt aktualisieren (Screenshot)
void uiLueftenInit();                     // Warnbildschirm Lueften / Schimmelgefahr (ui_lueften.cpp)
void uiLueftenPruefen();                  // Warnbildschirm zeigen oder schliessen, alle 500 ms
void aktualisiereLueftenWarnScreen();     // Texte des Warnbildschirms setzen (Aufrufer haelt den LVGL-Lock)
void absturzInfoMelden();                 // Aufzeichnung des letzten Absturzes ins Log (crashinfo.cpp)
void sensorAlarmInit();                   // Grenzwert-Alarme der Sensoren verdrahten (sensor_alarm.cpp)
bool hinweisSensorText(char* buf, size_t n);   // Text des ersten Sensor-Alarms fuer die Hinweis-Kachel
struct SensorCfg;
void sensorBezeichnung(const SensorCfg& c, char* out, size_t n);
void uiWarnInit();                        // Warnkarte, Liste, Warn-Button, Blink-Timer anlegen
void aktualisiereWarnliste();
void aktualisiereWarnkarte();             // neue Karte (warnkarteDsc) anzeigen
void aktualisiereDwdWarnBtn();
void uiWarnenPruefen();                   // Regen-/Pollenwarn-Screens bei neuer Warnung zeigen
void uiMenuInit();
void demoDatenSetzen(int variante);        // Beispieldaten für Bildschirmfotos (/demo), siehe demo.cpp
void uiSensorenErzwingen();               // Sensor-Kacheln jetzt aufbauen (Screenshot), Aufrufer hält den LVGL-Lock
void uiVerlaufInit();                     // Temperaturverlauf: Diagramme einrichten
void uiLuftInit();                        // Luftqualitaet: Verlauf, weitere Werte und Tipp (ui_luft.cpp)
void uiLuftErzwingen();                   // jetzt neu aufbauen (Screenshot), Aufrufer haelt den LVGL-Lock
void aktualisiereVerlauf();               // Temperaturverlauf mit den aktuellen Wetterdaten füllen
class WebServer;
void screenshotRegistrieren(WebServer& server);   // screenshot.cpp: /screenshot.bmp (Diagnose)
void uiBoot(const char* text, int pct);   // Bootscreen-Status (nimmt den LVGL-Lock selbst)
void uiHinweis(const char* titel, const char* text);   // Einrichtungs-Hinweis (Vollbild)

// Warnkarte (vom Netzwerk-Task dekodiert, von der UI angezeigt)
extern lv_img_dsc_t warnkarteDsc;
extern uint8_t*     warnkarteBuf;
extern bool         warnkarteNeu;
extern int          warnkarteW, warnkarteH;

// ── Firmware-Update (OTA) ────────────────────────────────────────────────────
extern bool          updateVerfuegbar;       // neuere Version veröffentlicht → Symbol auf dem Hauptscreen
extern String        updateNeueVersion;
extern volatile bool otaStartAngefordert;    // per Touch angefordert; gestartet wird im Netzwerk-Task
extern unsigned long letzterUpdateCheck;
extern unsigned long updateCheckIntervall;
extern int           updateLetztesErgebnis;   // 0 = noch nicht geprueft, 1 = ok, 2 = fehlgeschlagen
unsigned long updateIntervallMs();          // Pruefabstand aus cfg.update_intervall_h
bool   versionNeuer(const String& a, const String& b);
bool   holeNeuesteVersion(String& latest);   // true = Abfrage erfolgreich
void   pruefeUpdate();                       // setzt updateVerfuegbar, passt das Symbol an
String installiereUpdate();                  // Fehlertext, leer = Erfolg (Fortschritt auf dem Display)
void   updateErfolgNeustart();
void   updateFehlerAnzeigen(const String& fehler);
String webuiSicherungJson();                 // Einstellungen als JSON (ohne Passwörter), für den Download und die Sicherung auf der Karte
bool   otaAktuellGueltig();                  // laufende Firmware ist als gültig bestätigt (oder per USB geflasht)
// Notfall-Anbindung (notfall.cpp)
bool   notfallBereit(String* version);       // Notfall-App in der Partition "factory" vorhanden und vollständig?
bool   notfallStarten(int grund);            // 1 angefordert, 2 Touch beim Start, 3 Abstürze; startet neu in die Wiederherstellung
void   notfallZaehlen();                     // früh im Start: Abstürze in Folge zählen
void   notfallGesund();                      // 60 s ohne Absturz: Zähler zurücksetzen
void   notfallInit();                        // Hinweis auf dem Bootscreen (Touch öffnet die Wiederherstellung)
void   notfallHinweisPruefen();              // nach drei Abstürzen in Folge den Hinweis zeigen
// Rollback-Schutz und Update aus Datei (ota.cpp)
void   otaStartPruefen();                    // früh im Start: Zustand der laufenden Firmware lesen
void   otaGesundPruefen(unsigned long jetzt);// regelmäßig: 60 s nach dem Start die Firmware als gültig melden
bool   otaWartetAufGueltig();
void   otaRollbackAnzeigen();                // einmalige Meldung nach einem zurückgenommenen Update
String otaSlotInfo();                        // für das /log
String otaDateiStart(size_t gesamt);         // Fehlertext, leer = ok
String otaDateiSchreiben(uint8_t* buf, size_t len);
String otaDateiEnde();
void   otaDateiAbbrechen();
void   erstelleUpdateBadge();                // Symbol + Popup auf dem Hauptscreen (einmal beim Start)
void   aktualisiereUpdateBadge();
void   updateModusGeaendert();               // nach dem Umstellen von cfg.update_modus: Hinweis am Display anpassen, ggf. bald pruefen

// ── Helligkeit ───────────────────────────────────────────────────────────────
void setBrightness(int prozent);          // 0–100, nimmt den LVGL-Lock selbst
void helligkeitPruefen();                 // Nachtmodus + Dimm-Timeout, regelmäßig aufrufen
bool nachtJetzt();                      // true: der Nachtmodus der Fusion gilt gerade
bool warnRuheAktiv();                    // true: gerade Ruhezeit, auf dem Display keine Warnungen zeigen
bool nachtGrenzen(int* vonMin, int* bisMin);   // Beginn und Ende der Nacht (Minuten seit Mitternacht); true = aus den Sonnenzeiten, false = feste Uhrzeiten

// ── Netzwerk / WebUI ─────────────────────────────────────────────────────────
void starteWebUI();
void startePortal();
void webuiBedienen();                     // WebServer + DNS bedienen (Netzwerk-Task)
void appStart();                          // startet den Netzwerk-Task
