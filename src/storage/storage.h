#pragma once
// Speicherkarte (TF/microSD): Verlaufsdaten der Sensoren und dauerhaftes Log.
// Die Karte ist optional: fehlt sie oder ist sie defekt, laufen alle anderen Funktionen unverändert weiter.
// Zugangsdaten (WLAN, MQTT) werden nie auf die Karte geschrieben.
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "../platform/Arduino.h"

void speicherInit();                                       // einmal nach dem Start (nach Touch/IO-Erweiterung): Karte einhängen
void speicherTick();                                       // regelmäßig aus dem Netzwerk-Task: Karte suchen, Log und Verlauf schreiben
void speicherLogJetzt();                                   // Log sofort auf die Karte schreiben (vor einem gewollten Neustart)
bool speicherBereit();                                     // Karte eingehängt und beschreibbar
bool speicherInfo(uint64_t* gesamt, uint64_t* frei);       // Größe und freier Platz in Bytes
const char* speicherStatusText();                          // „nicht erkannt“, „bereit, 14.2 GB frei“ ...
int  speicherDateien(const char* ordner, char* out, size_t n);   // Liste „name;größe\n“ eines Unterordners (verlauf, log)
bool speicherVerlaufTag(int jahr, int monat, int tag, void (*zeile)(time_t zeit, const char* id, float wert, void* ctx), void* ctx);   // Verlaufsdatei eines Tages lesen (false: Datei fehlt)
bool speicherTextAnhaengen(const char* relPfad, const char* text);   // Text an eine Datei anhaengen (z. B. "klima/prognose.csv"), legt den Ordner an
bool speicherTextZeilen(const char* relPfad, void (*zeile)(const char* z, void* ctx), void* ctx);   // Datei zeilenweise lesen (false: keine Datei)
class WebServer;
void speicherWebuiRegistrieren(WebServer& server);          // Seite /karte (Dateiliste, Download)
void speicherSicherungAnfordern();                         // Einstellungen bald als notfall/einstellungen.json auf die Karte schreiben (ohne Passwörter)
String speicherSicherungLesen();                           // Inhalt dieser Sicherung ("" = keine)
String speicherNotfallStatus();                            // Zustand der Notfall-Kopie der Firmware auf der Karte (für das /log)
bool speicherKalenderSchreiben(const char* daten, size_t n);   // Abfuhrkalender (ICS) als kalender/abfuhr.ics ablegen
bool speicherKalenderLesen(void (*chunk)(const char*, size_t, void*), void* ctx);   // false: keine Datei oder keine Karte
bool speicherKalenderVorhanden();
void speicherKalenderLoeschen();
bool speicherDateiSenden(const char* relPfad, void (*chunk)(const char*, size_t, void*), void* ctx, size_t* groesse);   // Datei in Stücken liefern
