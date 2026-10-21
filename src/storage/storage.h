#pragma once
// Speicherkarte (TF/microSD): Verlaufsdaten der Sensoren und dauerhaftes Log.
// Die Karte ist optional: fehlt sie oder ist sie defekt, laufen alle anderen Funktionen unverändert weiter.
// Zugangsdaten (WLAN, MQTT) werden nie auf die Karte geschrieben.
#include <stdint.h>
#include <stddef.h>
#include "../platform/Arduino.h"

void speicherInit();                                       // einmal nach dem Start (nach Touch/IO-Erweiterung): Karte einhängen
void speicherTick();                                       // regelmäßig aus dem Netzwerk-Task: Karte suchen, Log und Verlauf schreiben
bool speicherBereit();                                     // Karte eingehängt und beschreibbar
bool speicherInfo(uint64_t* gesamt, uint64_t* frei);       // Größe und freier Platz in Bytes
const char* speicherStatusText();                          // „nicht erkannt“, „bereit, 14.2 GB frei“ ...
int  speicherDateien(const char* ordner, char* out, size_t n);   // Liste „name;größe\n“ eines Unterordners (verlauf, log)
class WebServer;
void speicherWebuiRegistrieren(WebServer& server);          // Seite /karte (Dateiliste, Download)
void speicherSicherungAnfordern();                         // Einstellungen bald als notfall/einstellungen.json auf die Karte schreiben (ohne Passwörter)
String speicherSicherungLesen();                           // Inhalt dieser Sicherung ("" = keine)
String speicherNotfallStatus();                            // Zustand der Notfall-Kopie der Firmware auf der Karte (für das /log)
bool speicherDateiSenden(const char* relPfad, void (*chunk)(const char*, size_t, void*), void* ctx, size_t* groesse);   // Datei in Stücken liefern
