// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Benachrichtigungen aufs Handy ueber ntfy (https://ntfy.sh oder eigener Server). Allgemein gehalten, damit spaetere Quellen
// (Lueften, DWD-Warnungen, Pollen ...) denselben Weg nutzen: ntfySenden() stellt die Nachricht in eine kleine Warteschlange,
// ein kurzlebiger Task schickt sie ab (TLS braucht Stapel, der nur waehrend des Sendens belegt wird).
#include <stdint.h>

enum NtfyQuelle { NTFY_Q_SENSOR = 1, NTFY_Q_LUEFTEN = 2 };   // Bits in cfg.ntfy_quellen (weitere folgen)

bool ntfyBereit();                                                    // eingeschaltet, Server und Thema eingetragen
bool ntfyQuelleAktiv(NtfyQuelle q);                                   // bereit und fuer diese Quelle eingeschaltet
void ntfySenden(const char* titel, const char* text, int prio, const char* tags);   // prio 1..5 (3 = normal), tags z. B. "warning"
void ntfyTest();                                                      // Testnachricht senden
const char* ntfyStatusText();                                         // letztes Ergebnis fuer die Web-Oberflaeche
