// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "schimmel.h"
#if SCHIMMEL_VERFUEGBAR
#include "app_state.h"
#include "lueften.h"
#include "modul_speicher.h"
#include "../storage/storage.h"
#include <stdio.h>
#include <unistd.h>
#include <ArduinoJson.h>
#include <mutex>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <math.h>
#include <time.h>
#include <string.h>

namespace {
std::mutex m;
bool g_an = false, g_demo = false; double g_frsi = 0.75;
std::map<std::string, double> g_frsiRaum;                          // je Raum aus einer Messung der Wandtemperatur berechnet (Vorrang vor dem Wandtyp)
double frsiFuer(const std::string& raum) { auto i = g_frsiRaum.find(raum); return i != g_frsiRaum.end() ? i->second : g_frsi; }
std::vector<std::string> g_reihe; bool g_reiheGesetzt = false;     // gezeigte Raeume in Anzeigereihenfolge; ungesetzt: alle in der Reihenfolge der Sensoren
struct Tag { int tag = -1; double minKritisch = 0, minErhoeht = 0; double hoechst = 0; };   // tag: Datum JJJJMMTT
std::map<std::string, Tag> g_tag, g_demoTag;                         // heute je Raum (g_demoTag: Beispielraeume)
std::map<std::string, std::vector<Tag>> g_verlauf;                   // fruehere Tage je Raum (aufsteigend, hoechstens 7)
std::map<std::string, std::vector<Tag>> g_demoVerlauf;               // Beispielverlauf (nur fuer Bildschirmfotos, nie gespeichert)
const int VERLAUF_TAGE = 7;
const char* const VERLAUF_DATEI = "/sdcard/schimmel_verlauf.csv";   // Raum;Datum;Hoechste Wandfeuchte;Minuten kritisch;Minuten erhoeht
const char* const VERLAUF_TMP = "/sdcard/schimmel_verlauf.tmp";
bool g_verlaufGeladen = false; time_t g_verlaufGeschrieben = 0;
LueftenStatus g_ls; time_t g_stand = 0;
const char* NS = "wcschimmel";
void speichern() {
  JsonDocument d; d["an"] = g_an; d["frsi"] = g_frsi;
  if (g_reiheGesetzt) { JsonArray a = d["reihe"].to<JsonArray>(); for (auto& n : g_reihe) a.add(n); }
  if (!g_frsiRaum.empty()) { JsonObject r = d["raumFrsi"].to<JsonObject>(); for (auto& e : g_frsiRaum) r[e.first] = (int)(e.second * 1000 + 0.5) / 1000.0; }
  std::string o; serializeJson(d, o); modulSchreiben(NS, "einst", o);
}
void laden() { JsonDocument d; if (deserializeJson(d, modulLesen(NS, "einst")) != DeserializationError::Ok) return; g_an = d["an"] | false; g_frsi = d["frsi"] | 0.75; if (g_frsi < 0.5 || g_frsi > 0.95) g_frsi = 0.75;
  if (d["raumFrsi"].is<JsonObject>()) for (JsonPairConst e : d["raumFrsi"].as<JsonObjectConst>()) { const double f = e.value() | 0.0; if (f >= 0.5 && f <= 0.99 && strlen(e.key().c_str()) < 20) g_frsiRaum[e.key().c_str()] = f; }
  if (d["reihe"].is<JsonArray>()) { g_reiheGesetzt = true; g_reihe.clear(); for (JsonVariantConst v : d["reihe"].as<JsonArrayConst>()) { std::string n = (const char*)(v | ""); if (!n.empty() && n.size() < 20) g_reihe.push_back(n); } } }
// Verlauf der Speicherkarte einmal einlesen (sobald die Karte bereit ist). Aufruf unter der Sperre.
void verlaufLaden() {
  if (g_verlaufGeladen || !speicherBereit()) return;
  g_verlaufGeladen = true;
  FILE* f = fopen(VERLAUF_DATEI, "r"); if (!f) return;
  char z[96];
  while (fgets(z, sizeof(z), f)) {
    char* t1 = strchr(z, ';'); if (!t1) continue; *t1 = 0;
    long d = 0; double h = 0, k = 0, e = 0;
    if (sscanf(t1 + 1, "%ld;%lf;%lf;%lf", &d, &h, &k, &e) != 4 || d < 20200101 || d > 21001231 || strlen(z) == 0 || strlen(z) >= 20) continue;
    Tag t; t.tag = (int)d; t.hoechst = h; t.minKritisch = k; t.minErhoeht = e;
    Tag& heute = g_tag[z];
    if (heute.tag < 0 || t.tag > heute.tag) { if (heute.tag >= 0) g_verlauf[z].push_back(heute); heute = t; }   // juengster Tag der Datei = laufender Tag
    else if (t.tag < heute.tag) g_verlauf[z].push_back(t);
  }
  fclose(f);
  for (auto& v : g_verlauf) { std::sort(v.second.begin(), v.second.end(), [](const Tag& a, const Tag& b) { return a.tag < b.tag; });
    if ((int)v.second.size() > VERLAUF_TAGE) v.second.erase(v.second.begin(), v.second.end() - VERLAUF_TAGE); }
}
// Verlauf als Text fuer die Speicherkarte (unter der Sperre erzeugen, ausserhalb schreiben)
std::string verlaufText() {
  std::string o; char z[96];
  for (auto& v : g_verlauf) for (auto& t : v.second) { snprintf(z, sizeof(z), "%s;%d;%.1f;%.0f;%.0f\n", v.first.c_str(), t.tag, t.hoechst, t.minKritisch, t.minErhoeht); o += z; }
  for (auto& v : g_tag) if (v.second.tag > 0) { snprintf(z, sizeof(z), "%s;%d;%.1f;%.0f;%.0f\n", v.first.c_str(), v.second.tag, v.second.hoechst, v.second.minKritisch, v.second.minErhoeht); o += z; }
  return o;
}
void verlaufSchreiben(const std::string& text) {
  if (!speicherBereit()) return;
  FILE* f = fopen(VERLAUF_TMP, "w"); if (!f) return;
  const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
  if (fclose(f) != 0 || !ok) { unlink(VERLAUF_TMP); return; }
  unlink(VERLAUF_DATEI); rename(VERLAUF_TMP, VERLAUF_DATEI);
}
// Beispielverlauf fuer Bildschirmfotos
int datumVor(time_t jetzt, int tage) { time_t t = jetzt - (time_t)tage * 86400; struct tm l; localtime_r(&t, &l); return (l.tm_year + 1900) * 10000 + (l.tm_mon + 1) * 100 + l.tm_mday; }
void demoVerlauf(const char* raum, const double* h, time_t jetzt) {
  std::vector<Tag>& v = g_demoVerlauf[raum]; v.clear();
  for (int k = 0; k < VERLAUF_TAGE - 1; k++) { Tag t; t.tag = datumVor(jetzt, VERLAUF_TAGE - 1 - k); t.hoechst = h[k]; t.minKritisch = h[k] >= 80 ? (h[k] - 78) * 40 : 0; t.minErhoeht = h[k] >= 70 ? (h[k] - 68) * 50 : 0; v.push_back(t); }
}
double saettigung(double t) { return 6.112 * exp(17.62 * t / (243.12 + t)); }          // Saettigungsdampfdruck in hPa (Magnus)
}  // namespace

SchimmelWert schimmelBewerten(double t, double rh, double aus, double frsi) {
  SchimmelWert w; w.taupunkt = 243.12 * log(rh / 100.0 * saettigung(t) / 6.112) / (17.62 - log(rh / 100.0 * saettigung(t) / 6.112));
  w.wandTemp = aus + frsi * (t - aus); if (w.wandTemp > t) w.wandTemp = t;
  w.wandFeuchte = rh * saettigung(t) / saettigung(w.wandTemp); if (w.wandFeuchte > 100) w.wandFeuchte = 100;
  w.maxFeuchte = 80.0 * saettigung(w.wandTemp) / saettigung(t); if (w.maxFeuchte > 100) w.maxFeuchte = 100;
  w.stufe = w.wandFeuchte >= 90 ? 3 : w.wandFeuchte >= 80 ? 2 : w.wandFeuchte >= 70 ? 1 : 0;
  return w;
}

void schimmelInit() { std::lock_guard<std::mutex> l(m); laden(); logDiag("[Schimmel] Modul %s, fRsi %.2f", g_an ? "an" : "aus", g_frsi); }
bool schimmelAn() { return g_an; }
void schimmelDemoSetzen(bool an) { std::lock_guard<std::mutex> l(m); g_demo = an; }   // aendert den Schalter des Moduls nicht

void schimmelTick() {
  bool an, demo; { std::lock_guard<std::mutex> l(m); an = g_an || g_demo; demo = g_demo; } if (!an) return;
  LueftenStatus ls = lueftenKopie();
  if (demo) { ls = LueftenStatus(); ls.aussenBekannt = true; ls.aussenTemp = 3; ls.aussenFeuchte = 85; ls.nRaeume = 3;
    strcpy(ls.raum[0].raum, "Küche"); ls.raum[0].temp = 22; ls.raum[0].feuchte = 68; strcpy(ls.raum[1].raum, "Bad"); ls.raum[1].temp = 24; ls.raum[1].feuchte = 74; strcpy(ls.raum[2].raum, "Schlafzimmer"); ls.raum[2].temp = 18; ls.raum[2].feuchte = 55; }
  time_t jetzt = time(nullptr); if (jetzt < 1700000000) return;      // Uhr noch nicht gestellt
  struct tm lt; localtime_r(&jetzt, &lt); const int tag = (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
  std::string schreiben;
  {
  std::lock_guard<std::mutex> l(m);
  if (!demo) verlaufLaden();
  const double dt = g_stand ? std::min<double>(5.0, (jetzt - g_stand) / 60.0) : 0; g_ls = ls; g_stand = jetzt;
  if (!ls.aussenBekannt) return;
  bool neuerTag = false;
  if (demo) { static const double H1[6] = {72, 76, 83, 79, 74, 77}, H2[6] = {81, 86, 84, 88, 83, 85}, H3[6] = {58, 61, 60, 63, 59, 62};
    demoVerlauf("Küche", H1, jetzt); demoVerlauf("Bad", H2, jetzt); demoVerlauf("Schlafzimmer", H3, jetzt); }
  for (int i = 0; i < ls.nRaeume; i++) {
    Tag& t = (demo ? g_demoTag : g_tag)[ls.raum[i].raum];
    if (t.tag != tag) {                                                // neuer Tag: gestern in den Verlauf
      if (t.tag > 0 && !demo) { std::vector<Tag>& v = g_verlauf[ls.raum[i].raum]; v.push_back(t); if ((int)v.size() > VERLAUF_TAGE) v.erase(v.begin()); neuerTag = true; }
      t = Tag(); t.tag = tag;
    }
    SchimmelWert w = schimmelBewerten(ls.raum[i].temp, ls.raum[i].feuchte, ls.aussenTemp, frsiFuer(ls.raum[i].raum));
    if (w.stufe >= 2) t.minKritisch += dt;
    if (w.stufe >= 1) t.minErhoeht += dt;
    if (w.wandFeuchte > t.hoechst) t.hoechst = w.wandFeuchte;
  }
  if (!demo && g_verlaufGeladen && (neuerTag || jetzt - g_verlaufGeschrieben >= 3600)) { g_verlaufGeschrieben = jetzt; schreiben = verlaufText(); }   // stuendlich sichern
  }
  if (!schreiben.empty()) verlaufSchreiben(schreiben);
}

std::string schimmelJson() {
  std::lock_guard<std::mutex> l(m); JsonDocument d; d["an"] = g_an || g_demo; d["frsi"] = g_frsi; d["stand"] = (long long)g_stand;
  d["aussenBekannt"] = g_ls.aussenBekannt; d["aussen"] = g_ls.aussenTemp; d["aussenFeuchte"] = g_ls.aussenFeuchte; d["aussenQuelle"] = g_ls.aussenQuelle;
  // Reihenfolge: gewaehlte Raeume (auch wenn sie gerade keine Daten haben), sonst alle in Sensor-Reihenfolge
  std::vector<std::string> bekannt; for (int i = 0; i < g_ls.nRaeume; i++) bekannt.push_back(g_ls.raum[i].raum);
  std::vector<std::string> zeigen = g_reiheGesetzt ? g_reihe : bekannt;
  d["reiheGesetzt"] = g_reiheGesetzt;
  JsonArray al = d["alle"].to<JsonArray>();                                     // Auswahlliste der Weboberflaeche: erst die gewaehlten, dann die uebrigen
  JsonArray re = d["reihe"].to<JsonArray>();
  for (auto& n : zeigen) { al.add(n); re.add(n); }
  if (g_reiheGesetzt) for (auto& n : bekannt) if (std::find(zeigen.begin(), zeigen.end(), n) == zeigen.end()) al.add(n);
  JsonArray a = d["raeume"].to<JsonArray>();
  if (g_ls.aussenBekannt) for (auto& n : zeigen) for (int i = 0; i < g_ls.nRaeume; i++) {
    if (n != g_ls.raum[i].raum) continue;
    const LueftenRaum& r = g_ls.raum[i]; const double fr = frsiFuer(r.raum); SchimmelWert w = schimmelBewerten(r.temp, r.feuchte, g_ls.aussenTemp, fr); auto& tq = g_demo ? g_demoTag : g_tag; Tag t = tq.count(r.raum) ? tq[r.raum] : Tag();
    JsonObject o = a.add<JsonObject>(); o["raum"] = r.raum; o["frsi"] = fr; o["frsiGemessen"] = g_frsiRaum.count(r.raum) > 0; o["temp"] = r.temp; o["feuchte"] = r.feuchte; o["taupunkt"] = w.taupunkt; o["wandTemp"] = w.wandTemp; o["wandFeuchte"] = w.wandFeuchte;
    o["maxFeuchte"] = w.maxFeuchte; o["stufe"] = w.stufe; o["stundenKritisch"] = t.minKritisch / 60.0; o["stundenErhoeht"] = t.minErhoeht / 60.0; o["hoechstWand"] = t.hoechst;
    // Verlauf: die letzten 7 Tage einschliesslich heute (d Datum, h hoechste Wandfeuchte, k Stunden kritisch); k7 Summe der kritischen Stunden
    JsonArray va = o["verlauf"].to<JsonArray>(); double k7 = 0; int tageKritisch = 0;
    const auto& quelle = g_demo ? g_demoVerlauf : g_verlauf; auto vi = quelle.find(r.raum);
    if (vi != quelle.end()) {
      const std::vector<Tag>& v = vi->second; const int ab = std::max(0, (int)v.size() - (VERLAUF_TAGE - 1));
      for (int k = ab; k < (int)v.size(); k++) { const Tag& vt = v[k]; if (t.tag > 0 && vt.tag >= t.tag) continue;
        JsonObject e = va.add<JsonObject>(); e["d"] = vt.tag; e["h"] = vt.hoechst; e["k"] = vt.minKritisch / 60.0; k7 += vt.minKritisch / 60.0; if (vt.hoechst >= 80) tageKritisch++; }
    }
    { JsonObject e = va.add<JsonObject>(); e["d"] = t.tag; e["h"] = t.hoechst; e["k"] = t.minKritisch / 60.0; k7 += t.minKritisch / 60.0; if (t.hoechst >= 80) tageKritisch++; }
    o["kritisch7"] = k7; o["tageKritisch7"] = tageKritisch;
    break;
  }
  std::string o; serializeJson(d, o); return o;
}
bool schimmelEinstellungSetzen(const std::string& json, std::string* fehler) {
  JsonDocument d; if (deserializeJson(d, json) != DeserializationError::Ok) { *fehler = "Ungültige Anfrage"; return false; }
  std::lock_guard<std::mutex> l(m);
  if (!d["an"].isNull()) { g_an = d["an"] | false; }
  if (!d["frsi"].isNull()) { double f = d["frsi"] | 0.75; if (f < 0.5 || f > 0.95) { *fehler = "Temperaturfaktor 0,5 bis 0,95"; return false; } g_frsi = f; }
  // Gemessene Wandtemperatur eines Raums: fRsi = (Wand - aussen) / (innen - aussen) mit den Werten dieses Moments
  if (d["messung"].is<JsonObject>()) {
    const std::string raum = (const char*)(d["messung"]["raum"] | ""); const double wand = d["messung"]["wand"] | -100.0;
    if (raum.empty() || raum.size() >= 20) { *fehler = "Ungültiger Raum"; return false; }
    if (wand < -10 || wand > 40) { *fehler = "Wandtemperatur zwischen -10 und 40 °C eingeben"; return false; }
    if (!g_ls.aussenBekannt) { *fehler = "Die Außentemperatur ist noch nicht bekannt"; return false; }
    const LueftenRaum* r = nullptr; for (int i = 0; i < g_ls.nRaeume; i++) if (raum == g_ls.raum[i].raum) r = &g_ls.raum[i];
    if (!r) { *fehler = "Für diesen Raum liegen gerade keine Werte vor"; return false; }
    const double diff = r->temp - g_ls.aussenTemp;
    if (diff < 8) { char b[160]; snprintf(b, sizeof(b), "Innen und außen liegen nur %.1f °C auseinander. Bitte an einem kälteren Tag messen (mindestens 8 °C Unterschied), sonst wird die Rechnung ungenau.", diff); *fehler = b; return false; }
    if (wand > r->temp + 0.5) { *fehler = "Die Wand kann nicht wärmer als die Raumluft sein. Bitte an der Außenwand messen (kälteste Stelle, z. B. Ecke)."; return false; }
    double f = (wand - g_ls.aussenTemp) / diff; if (f < 0.5) f = 0.5; if (f > 0.99) f = 0.99;
    g_frsiRaum[raum] = f; logDiag("[Schimmel] %s: Wand %.1f °C gemessen (innen %.1f, aussen %.1f) -> fRsi %.2f", raum.c_str(), wand, r->temp, g_ls.aussenTemp, f);
    speichern(); return true;
  }
  if (!d["messungLoeschen"].isNull()) { g_frsiRaum.erase((const char*)(d["messungLoeschen"] | "")); speichern(); return true; }
  if (d["reiheAlle"] | false) { g_reiheGesetzt = false; g_reihe.clear(); }
  else if (d["reihe"].is<JsonArray>()) {
    std::vector<std::string> neu;
    for (JsonVariantConst v : d["reihe"].as<JsonArrayConst>()) {
      std::string n = (const char*)(v | ""); if (n.empty() || n.size() >= 20) { *fehler = "Ungültiger Raumname"; return false; }
      if (std::find(neu.begin(), neu.end(), n) == neu.end()) neu.push_back(n);
    }
    if (neu.size() > 12) { *fehler = "Höchstens 12 Räume"; return false; }
    g_reihe = neu; g_reiheGesetzt = true;
  }
  speichern(); return true;
}

#else  // S3: keine neuen Funktionen
void schimmelInit() {}
void schimmelTick() {}
bool schimmelAn() { return false; }
std::string schimmelJson() { return "{\"an\":false}"; }
bool schimmelEinstellungSetzen(const std::string&, std::string* fehler) { if (fehler) *fehler = "Nur beim ESP32-P4"; return false; }
SchimmelWert schimmelBewerten(double, double, double, double) { return SchimmelWert{0, 0, 0, 0, 0}; }
void schimmelDemoSetzen(bool) {}
#endif
