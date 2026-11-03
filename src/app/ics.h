// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Kleiner iCalendar-Leser (ICS) fuer den Abfuhrkalender: liest streamend, entfaltet Zeilen und liefert die Termine eines Tagesfensters.
// Unterstuetzt VEVENT mit DTSTART (ganztaegig, lokal, UTC), SUMMARY, RRULE (DAILY, WEEKLY mit BYDAY, MONTHLY, YEARLY; INTERVAL, COUNT, UNTIL),
// EXDATE und STATUS:CANCELLED. Ohne Arduino- und IDF-Abhaengigkeiten, damit er am Rechner getestet werden kann.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct IcsEreignis {
  std::string name;
  int tag;            // Tage seit 1970-01-01 (Ortszeit)
};

int  icsTagAusDatum(int j, int m, int t);
void icsDatumAusTag(int tag, int& j, int& m, int& t);
int  icsWochentag(int tag);    // 0 = Montag ... 6 = Sonntag

class IcsLeser {
 public:
  // Fenster [von, bis] in Tagen seit 1970-01-01; utcVersatzMin liefert fuer eine UTC-Zeit (Sekunden seit 1970) den Versatz der Ortszeit in Minuten
  IcsLeser(int von, int bis, int (*utcVersatzMin)(int64_t) = nullptr);
  void fuettern(const char* daten, size_t n);
  void ende();
  std::vector<IcsEreignis> ergebnis;   // unsortiert, mit Wiederholungen
  int ereignisse = 0;                  // gelesene VEVENT
  int letzterTag = -1;                 // spaetester Beginn eines Ereignisses (auch ausserhalb des Fensters), fuer den Hinweis "Kalender endet"
 private:
  void zeile(const std::string& z);
  void ereignisFertig();
  bool wertTag(const std::string& v, int& tag, bool& ganz) const;
  int (*versatz_)(int64_t);
  int von_, bis_;
  std::string zeile_, aktuell_;
  bool imEreignis_ = false, abgesagt_ = false, ganz_ = false, hatStart_ = false;
  std::string name_, rrule_;
  std::vector<int> ausnahmen_;
  int start_ = 0;
};
