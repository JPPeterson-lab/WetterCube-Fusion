// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Muellabfuhr: Abfuhrkalender (ICS) aus einem Link des Entsorgers oder aus einer hochgeladenen Datei auf der Speicherkarte lesen, die naechsten
// Abholtage merken und als Hinweis ("Morgen: Gelbe Tonne, Biotonne") und in einer Kachel auf dem Hauptscreen zeigen. Viele Entsorger fassen alle
// Tonnen eines Tages in einem Eintrag zusammen ("Gelbe Tonne, Biotonne, Restmuelltonne"); der Eintrag wird deshalb an den Kommas geteilt.
// Der Link bleibt auf dem Geraet (Einstellungen), es gibt keinen Zwischenserver.
#include "app_state.h"
#include "ics.h"
#include "../storage/storage.h"
#include <algorithm>
#include <time.h>
#include "esp_heap_caps.h"

MuellDaten muell;

static int versatzMin(int64_t sek) {   // Versatz der Ortszeit gegen UTC in Minuten (Sommerzeit eingerechnet)
  time_t t = (time_t)sek; struct tm l; localtime_r(&t, &l);
  int64_t lokal = (int64_t)icsTagAusDatum(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday) * 86400 + l.tm_hour * 3600 + l.tm_min * 60 + l.tm_sec;
  return (int)((lokal - sek) / 60);
}

int muellHeute() {
  struct tm ti;
  if (!getLocalTime(&ti, 0)) return -1;
  return icsTagAusDatum(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
}

bool muellQuelleDa() { return !cfg.muell_url.isEmpty() || speicherKalenderVorhanden(); }

static void leserFuettern(const char* d, size_t n, void* ctx) { ((IcsLeser*)ctx)->fuettern(d, n); }

// Ergebnis des Lesers in MuellDaten umsetzen: je Abholtag eine Zeile, Namen an den Kommas geteilt
static bool uebernehmen(IcsLeser& leser) {
  std::vector<IcsEreignis>& e = leser.ergebnis;
  std::sort(e.begin(), e.end(), [](const IcsEreignis& a, const IcsEreignis& b) { return a.tag < b.tag; });
  MuellDaten neu;
  neu.endeTag = leser.letzterTag;
  for (const IcsEreignis& x : e) {
    if (neu.n == 0 || neu.t[neu.n - 1].tag != x.tag) {
      if (neu.n >= MuellDaten::N) break;
      neu.t[neu.n].tag = x.tag; neu.t[neu.n].namen[0] = 0; neu.n++;
    }
    MuellTermin& t = neu.t[neu.n - 1];
    size_t a = 0;
    while (a <= x.name.size()) {
      size_t b = x.name.find(',', a); if (b == std::string::npos) b = x.name.size();
      std::string teil = x.name.substr(a, b - a);
      size_t s = teil.find_first_not_of(" \t"), z = teil.find_last_not_of(" \t");
      a = b + 1;
      if (s == std::string::npos) continue;
      teil = teil.substr(s, z - s + 1);
      std::string vorhanden = std::string("\n") + t.namen + "\n";
      if (vorhanden.find("\n" + teil + "\n") != std::string::npos) continue;     // gleiche Tonne zweimal am Tag nur einmal
      size_t l = strlen(t.namen);
      if (l + teil.size() + 2 >= sizeof(t.namen)) continue;
      if (l) t.namen[l++] = '\n';
      memcpy(t.namen + l, teil.c_str(), teil.size()); t.namen[l + teil.size()] = 0;
    }
  }
  neu.geladen = leser.ereignisse > 0;
  neu.fehler = leser.ereignisse == 0;
  { LvLock lock; muell = neu; }
  logDiag("[Muell] %d Ereignisse im Kalender, %d Abholtage in den naechsten 60 Tagen, Kalender reicht bis Tag %d", leser.ereignisse, neu.n, leser.letzterTag);
  return neu.geladen;
}

bool fetchMuell() {
  spur("Muell: Abruf");
  if (!cfg.muell_aktiv) return false;
  const int heute = muellHeute();
  if (heute < 0) return false;
  IcsLeser leser(heute, heute + 60, versatzMin);
  String url = cfg.muell_url; url.trim();
  if (!url.isEmpty()) {
    if (WiFi.status() != WL_CONNECTED) return false;
    if (url.startsWith("webcal://")) url = "https://" + url.substring(9);
    if (!url.startsWith("https://")) { logDiag("[Muell] Der Link muss mit https:// oder webcal:// beginnen"); return false; }
    WiFiClientSecure sc;
    HTTPClient http;
    http.begin(sc, url);
    http.setTimeout(20000);
    http.addHeader("User-Agent", "WetterCubeFusion/" FIRMWARE_VERSION);
    int code = http.GETStream();
    if (code != 200) { logHttpFehler("Muell", code, sc); http.end(); muell.fehler = true; return false; }
    uint8_t* puffer = (uint8_t*)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!puffer) { http.end(); return false; }
    size_t gesamt = 0;
    for (;;) {
      int n = http.read(puffer, 2048);
      if (n <= 0) break;
      leser.fuettern((const char*)puffer, (size_t)n);
      gesamt += (size_t)n;
      if (gesamt > 4u * 1024 * 1024) break;   // Schutz gegen unsinnig grosse Antworten
    }
    heap_caps_free(puffer);
    http.end();
    logDiag("[Muell] Link: %u Bytes gelesen", (unsigned)gesamt);
  } else if (!speicherKalenderLesen(leserFuettern, &leser)) {
    logDiag("[Muell] weder Link noch Datei auf der Karte");
    return false;
  }
  leser.ende();
  return uebernehmen(leser);
}

// Kalender aus Text (hochgeladene Datei): auf der Karte ablegen und lesen. Rueckgabe: Fehlertext, leer = Erfolg
String muellDateiUebernehmen(const String& text) {
  if (text.indexOf("BEGIN:VCALENDAR") < 0 || text.indexOf("BEGIN:VEVENT") < 0) return "Das ist keine Kalenderdatei (ICS).";
  if (!speicherBereit()) return "Keine Speicherkarte: Die Datei kann nicht abgelegt werden.";
  if (!speicherKalenderSchreiben(text.c_str(), text.length())) return "Die Datei konnte nicht auf die Karte geschrieben werden.";
  return "";
}

// "Heute", "Morgen", sonst "Mo 26.10."
void muellTagText(int tag, char* buf, size_t n) {
  int heute = muellHeute();
  if (heute >= 0 && tag == heute) { strlcpy(buf, "Heute", n); return; }
  if (heute >= 0 && tag == heute + 1) { strlcpy(buf, "Morgen", n); return; }
  static const char* WT[7] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
  int j, m, d; icsDatumAusTag(tag, j, m, d);
  snprintf(buf, n, "%s %d.%d.", WT[icsWochentag(tag)], d, m);
}

// Hinweis-Kachel: Abholung heute (bis zur eingestellten Stunde) oder in den naechsten Tagen. Der erste Warntag (Abholtag minus Vorlauf) zaehlt
// erst ab der eingestellten Uhrzeit, die Tage dazwischen ganztaegig.
bool muellHinweisText(char* buf, size_t n) {
  if (!cfg.muell_aktiv || !muell.geladen) return false;
  const int heute = muellHeute();
  if (heute < 0) return false;
  struct tm ti; getLocalTime(&ti, 0);
  for (int i = 0; i < muell.n; i++) {
    const int diff = muell.t[i].tag - heute;                 // Tage bis zur Abholung
    if (diff < 0 || diff > cfg.muell_vorlauf) continue;
    if (diff == 0 && ti.tm_hour >= cfg.muell_bis) continue;   // am Abholtag nur bis zur eingestellten Stunde
    if (diff > 0 && diff == cfg.muell_vorlauf && ti.tm_hour < cfg.muell_ab) continue;   // erster Warntag erst ab der Uhrzeit
    String namen = muell.t[i].namen; namen.replace("\n", ", ");
    const char* wann = diff == 0 ? "Heute" : diff == 1 ? "Morgen" : "Übermorgen";
    snprintf(buf, n, "%s: %s", wann, namen.c_str());
    return true;
  }
  return false;
}

String muellListe() {   // fuer die Weboberflaeche
  if (!cfg.muell_aktiv) return "Müllabfuhr ist ausgeschaltet.";
  if (!muell.geladen) return muell.fehler ? "Der Kalender konnte nicht gelesen werden (Link oder Datei prüfen)." : "Noch nicht abgerufen.";
  String s;
  for (int i = 0; i < muell.n; i++) {
    char t[24]; muellTagText(muell.t[i].tag, t, sizeof(t));
    String namen = muell.t[i].namen; namen.replace("\n", ", ");
    s += String(t) + ": " + namen + "\n";
  }
  if (!s.length()) s = "Keine Abholtage in den nächsten 60 Tagen.\n";
  if (muell.endeTag >= 0) {
    int j, m, d; icsDatumAusTag(muell.endeTag, j, m, d);
    char e[64]; snprintf(e, sizeof(e), "Der Kalender reicht bis %d.%d.%d.", d, m, j);
    s += e;
    int heute = muellHeute();
    if (heute >= 0 && muell.endeTag - heute < 45) s += " Bald einen neuen Kalender laden!";
  }
  return s;
}
