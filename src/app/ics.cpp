// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "ics.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Tage seit 1970-01-01 aus Datum (proleptischer gregorianischer Kalender, Algorithmus von Howard Hinnant)
int icsTagAusDatum(int j, int m, int t) {
  j -= m <= 2;
  const int era = (j >= 0 ? j : j - 399) / 400;
  const unsigned yoe = (unsigned)(j - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int)doe - 719468;
}

void icsDatumAusTag(int z, int& j, int& m, int& t) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  j = (int)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  t = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  j += m <= 2;
}

int icsWochentag(int tag) { int w = (tag + 3) % 7; return w < 0 ? w + 7 : w; }   // 1970-01-01 war ein Donnerstag (Mo = 0)

static int tageImMonat(int j, int m) {
  static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((j % 4 == 0 && j % 100 != 0) || j % 400 == 0)) return 29;
  return d[m - 1];
}

IcsLeser::IcsLeser(int von, int bis, int (*utcVersatzMin)(int64_t)) : versatz_(utcVersatzMin), von_(von), bis_(bis) {}

void IcsLeser::fuettern(const char* daten, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char c = daten[i];
    if (c == '\r') continue;
    if (c == '\n') {
      // Fortsetzungszeile? Erst beim naechsten Zeichen entscheiden (beginnt mit Leerzeichen oder Tab)
      aktuell_ += '\x01';
      continue;
    }
    if (!aktuell_.empty() && aktuell_.back() == '\x01') {
      aktuell_.pop_back();
      if (c == ' ' || c == '\t') continue;           // Fortsetzung: Zeilenumbruch und fuehrendes Leerzeichen entfallen
      zeile(aktuell_); aktuell_.clear();
    }
    if (aktuell_.size() < 600) aktuell_ += c;        // sehr lange Zeilen (Beschreibungen) abschneiden
  }
}

void IcsLeser::ende() {
  if (!aktuell_.empty()) { if (aktuell_.back() == '\x01') aktuell_.pop_back(); zeile(aktuell_); aktuell_.clear(); }
}

static std::string entschluesseln(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) { char n = s[++i]; o += (n == 'n' || n == 'N') ? ' ' : n; }
    else o += s[i];
  }
  return o;
}

// "20261023", "20261023T120000", "20261023T120000Z"
bool IcsLeser::wertTag(const std::string& v, int& tag, bool& ganz) const {
  if (v.size() < 8) return false;
  for (int i = 0; i < 8; i++) if (v[i] < '0' || v[i] > '9') return false;
  int j = atoi(v.substr(0, 4).c_str()), m = atoi(v.substr(4, 2).c_str()), t = atoi(v.substr(6, 2).c_str());
  if (m < 1 || m > 12 || t < 1 || t > 31) return false;
  ganz = v.size() == 8;
  tag = icsTagAusDatum(j, m, t);
  if (!ganz && v.size() >= 15 && v.back() == 'Z') {      // UTC: in die Ortszeit umrechnen
    int h = atoi(v.substr(9, 2).c_str()), mi = atoi(v.substr(11, 2).c_str());
    int64_t sek = (int64_t)tag * 86400 + h * 3600 + mi * 60;
    int off = versatz_ ? versatz_(sek) : 0;
    int lokal = (int)((sek + (int64_t)off * 60) / 86400);
    if (sek + (int64_t)off * 60 < 0 && ((sek + (int64_t)off * 60) % 86400) != 0) lokal -= 1;
    tag = lokal;
  }
  return true;
}

void IcsLeser::zeile(const std::string& z) {
  if (z.empty()) return;
  if (z == "BEGIN:VEVENT") {
    imEreignis_ = true; abgesagt_ = false; ganz_ = false; hatStart_ = false; name_.clear(); rrule_.clear(); ausnahmen_.clear(); start_ = 0;
    return;
  }
  if (z == "END:VEVENT") { if (imEreignis_) ereignisFertig(); imEreignis_ = false; return; }
  if (!imEreignis_) return;
  size_t dp = z.find(':');
  if (dp == std::string::npos) return;
  std::string kopf = z.substr(0, dp), wert = z.substr(dp + 1);
  size_t sem = kopf.find(';');
  std::string schluessel = kopf.substr(0, sem);
  for (auto& c : schluessel) c = (char)toupper((unsigned char)c);
  if (schluessel == "SUMMARY") name_ = entschluesseln(wert);
  else if (schluessel == "STATUS") { if (wert.find("CANCELLED") != std::string::npos) abgesagt_ = true; }
  else if (schluessel == "RRULE") rrule_ = wert;
  else if (schluessel == "DTSTART") { bool g; hatStart_ = wertTag(wert, start_, g); ganz_ = g; }
  else if (schluessel == "EXDATE") {
    size_t a = 0;
    while (a <= wert.size()) {
      size_t b = wert.find(',', a); if (b == std::string::npos) b = wert.size();
      int t; bool g;
      if (wertTag(wert.substr(a, b - a), t, g)) ausnahmen_.push_back(t);
      a = b + 1;
    }
  }
}

static std::string rruleWert(const std::string& r, const char* schluessel) {
  std::string s = std::string(";") + r + ";";
  std::string suche = std::string(";") + schluessel + "=";
  size_t p = s.find(suche);
  if (p == std::string::npos) return "";
  p += suche.size();
  size_t q = s.find(';', p);
  return s.substr(p, q - p);
}

void IcsLeser::ereignisFertig() {
  ereignisse++;
  if (abgesagt_ || !hatStart_ || name_.empty()) return;
  if (start_ > letzterTag) letzterTag = start_;
  auto istAusnahme = [&](int t) { for (int a : ausnahmen_) if (a == t) return true; return false; };
  auto zufuegen = [&](int t) { if (t >= von_ && t <= bis_ && !istAusnahme(t)) ergebnis.push_back({name_, t}); };
  if (rrule_.empty()) { zufuegen(start_); return; }

  std::string freq = rruleWert(rrule_, "FREQ");
  int interval = atoi(rruleWert(rrule_, "INTERVAL").c_str()); if (interval < 1) interval = 1;
  int count = atoi(rruleWert(rrule_, "COUNT").c_str());
  int bisRegel = 1 << 30;
  std::string until = rruleWert(rrule_, "UNTIL");
  if (!until.empty()) { int t; bool g; if (wertTag(until, t, g)) bisRegel = t; }
  std::string byday = rruleWert(rrule_, "BYDAY");
  bool tage[7] = {false, false, false, false, false, false, false};
  static const char* WT[7] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
  for (int i = 0; i < 7; i++) if (byday.find(WT[i]) != std::string::npos) tage[i] = true;
  int gezaehlt = 0;
  int ende = bis_ < bisRegel ? bis_ : bisRegel;

  if (freq == "DAILY") {
    for (int t = start_, n = 0; t <= ende && n < 4000; t += interval, n++) {
      if (count && ++gezaehlt > count) break;
      zufuegen(t);
    }
  } else if (freq == "WEEKLY") {
    bool irgendein = false; for (int i = 0; i < 7; i++) irgendein |= tage[i];
    if (!irgendein) tage[icsWochentag(start_)] = true;
    int wochenanfang = start_ - icsWochentag(start_);    // Montag der Startwoche
    for (int w = wochenanfang, n = 0; w <= ende && n < 1500; w += 7 * interval, n++) {
      for (int d = 0; d < 7; d++) {
        if (!tage[d]) continue;
        int t = w + d;
        if (t < start_) continue;
        if (t > ende) break;
        if (count && ++gezaehlt > count) return;
        zufuegen(t);
      }
    }
  } else if (freq == "MONTHLY" || freq == "YEARLY") {
    int j, m, d; icsDatumAusTag(start_, j, m, d);
    int bymd = atoi(rruleWert(rrule_, "BYMONTHDAY").c_str()); if (bymd > 0) d = bymd;
    int schritt = freq == "MONTHLY" ? interval : 12 * interval;
    for (int n = 0; n < 1500; n++) {
      int mm = (m - 1) + n * schritt, jj = j + mm / 12; mm = mm % 12 + 1;
      if (d > tageImMonat(jj, mm)) continue;           // gibt es in diesem Monat nicht
      int t = icsTagAusDatum(jj, mm, d);
      if (t < start_) continue;
      if (t > ende) break;
      if (count && ++gezaehlt > count) break;
      zufuegen(t);
    }
  } else {
    zufuegen(start_);
  }
}
