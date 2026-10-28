// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Allergie-Tagebuch: Daten (Speicherkarte), Web-Seite /tagebuch zum Ansehen und Loeschen, Schnittstelle /api/tagebuch.
#include "tagebuch.h"
#include "app_state.h"
#include "webui_theme.h"
#include "../storage/storage.h"
#include "../platform/WebServer.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const int MAX_EINTRAEGE = 400;
static const char* const DATEI = "/sdcard/tagebuch.csv";
static const char* const DATEI_TMP = "/sdcard/tagebuch.tmp";

static TagebuchEintrag s_e[MAX_EINTRAEGE];
static int  s_n = 0;
static bool s_geladen = false;      // Datei der Karte eingelesen
static bool s_aufKarte = false;     // letzte Speicherung auf die Karte hat geklappt
static bool s_demo = false;         // Beispieldaten: nicht auf die Karte schreiben
static SemaphoreHandle_t s_mutex = nullptr;

struct Sperre {
  Sperre() { if (!s_mutex) s_mutex = xSemaphoreCreateMutex(); xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Sperre() { xSemaphoreGive(s_mutex); }
};

int32_t tagebuchDatumHeute(int tageZurueck) {
  time_t t = time(nullptr);
  if (t < 1700000000) return 0;                                   // Uhr noch nicht gestellt
  t -= (time_t)tageZurueck * 86400;
  struct tm ti; localtime_r(&t, &ti);
  return (ti.tm_year + 1900) * 10000 + (ti.tm_mon + 1) * 100 + ti.tm_mday;
}

static bool datumGueltig(long d) {
  const int j = d / 10000, m = (d / 100) % 100, t = d % 100;
  return j >= 2020 && j <= 2100 && m >= 1 && m <= 12 && t >= 1 && t <= 31;
}

// Eintrag einsortieren (nach Datum aufsteigend); gleiches Datum wird ersetzt. Aufruf unter der Sperre.
static void einfuegen(const TagebuchEintrag& e) {
  int i = 0;
  while (i < s_n && s_e[i].datum < e.datum) i++;
  if (i < s_n && s_e[i].datum == e.datum) { s_e[i] = e; return; }
  if (s_n >= MAX_EINTRAEGE) { memmove(s_e, s_e + 1, sizeof(TagebuchEintrag) * (s_n - 1)); s_n--; if (i > 0) i--; }
  memmove(s_e + i + 1, s_e + i, sizeof(TagebuchEintrag) * (s_n - i));
  s_e[i] = e; s_n++;
}

// Datei der Karte einlesen (einmal, sobald die Karte bereit ist); Eintraege im Speicher haben Vorrang
static void ladenFallsNoetig() {
  if (s_geladen || !speicherBereit()) return;
  s_geladen = true;
  FILE* f = fopen(DATEI, "r");
  if (!f) return;
  char z[64];
  while (fgets(z, sizeof(z), f)) {
    long d = 0; int st = 0, p = 255, a = 255;
    if (sscanf(z, "%ld,%d,%d,%d", &d, &st, &p, &a) < 2 || !datumGueltig(d) || st < 0 || st > 2) continue;
    bool da = false;
    for (int i = 0; i < s_n; i++) if (s_e[i].datum == d) { da = true; break; }
    if (da) continue;
    TagebuchEintrag e = { (int32_t)d, (uint8_t)st, (uint8_t)(p < 0 || p > 255 ? 255 : p), (uint8_t)(a < 0 || a > 255 ? 255 : a) };
    einfuegen(e);
  }
  fclose(f);
}

static void speichern() {
  s_aufKarte = false;
  if (s_demo || !speicherBereit()) return;
  FILE* f = fopen(DATEI_TMP, "w");
  if (!f) return;
  bool ok = true;
  for (int i = 0; i < s_n && ok; i++) ok = fprintf(f, "%ld,%d,%d,%d\n", (long)s_e[i].datum, s_e[i].stufe, s_e[i].pollen10, s_e[i].aqi) > 0;
  ok = (fclose(f) == 0) && ok;
  if (!ok) { unlink(DATEI_TMP); return; }
  unlink(DATEI);
  s_aufKarte = rename(DATEI_TMP, DATEI) == 0;
}

void tagebuchInit() {
  Sperre s;
  ladenFallsNoetig();
}

// Pollenbelastung der gewaehlten Allergene (Skala 0-3) mal 10; 255 = keine Daten
static uint8_t pollenJetzt() {
  float m = -1.0f;
  for (int i = 0; i < 8; i++) { if (!pollenGewaehlt(i)) continue; float v = pollenWertWarnung(i); if (v > m) m = v; }
  if (m < 0.0f) return 255;
  int v = (int)(m * 10.0f + 0.5f);
  return (uint8_t)(v > 30 ? 30 : v);
}

bool tagebuchHeuteEintragen(int stufe) {
  const int32_t heute = tagebuchDatumHeute();
  if (!heute || stufe < 0 || stufe > 2) return false;
  Sperre s;
  ladenFallsNoetig();
  TagebuchEintrag e = { heute, (uint8_t)stufe, pollenJetzt(), (uint8_t)(pollen.aqi >= 0 && pollen.aqi < 255 ? pollen.aqi : 255) };
  einfuegen(e);
  speichern();
  logDiag("[Tagebuch] %ld: Stufe %d, Pollen %d, AQI %d", (long)heute, stufe, e.pollen10, e.aqi);
  return true;
}

int tagebuchHeute() {
  const int32_t heute = tagebuchDatumHeute();
  Sperre s;
  ladenFallsNoetig();
  for (int i = s_n - 1; i >= 0; i--) { if (s_e[i].datum == heute) return s_e[i].stufe; if (s_e[i].datum < heute) break; }
  return -1;
}

int tagebuchAbDatum(int32_t ab, TagebuchEintrag* out, int max) {
  Sperre s;
  ladenFallsNoetig();
  int n = 0;
  for (int i = 0; i < s_n && n < max; i++) if (s_e[i].datum >= ab) out[n++] = s_e[i];
  return n;
}

bool tagebuchAufKarte() { return s_aufKarte; }

void tagebuchDemo() {
  Sperre s;
  s_demo = true; s_geladen = true; s_n = 0;
  static const uint8_t st[30]  = {0,0,0,1,0,0,1,1,0,0,0,0,1,2,2,1,0,0,0,1,1,2,1,0,0,0,1,1,0,0};
  static const uint8_t pol[30] = {3,4,6,12,8,7,14,16,9,5,4,6,15,24,26,18,8,6,5,13,17,25,15,9,6,5,12,16,8,6};
  for (int k = 0; k < 30; k++) {
    const int32_t d = tagebuchDatumHeute(29 - k);
    if (!d) break;
    TagebuchEintrag e = { d, st[k], pol[k], (uint8_t)(25 + (k * 7) % 30) };
    einfuegen(e);
  }
}

// ── Web: /tagebuch und /api/tagebuch ─────────────────────────────────────────
static const char TAGEBUCH_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>WetterCube Fusion – Allergie-Tagebuch</title>
)HTML" WEBUI_THEME_HEAD WEBUI_THEME_CSS R"HTML(
<style>
body{font-family:Arial,sans-serif;background:var(--bg);color:var(--txt);margin:0;padding:14px;max-width:900px;margin:auto;line-height:1.45}
h1{font-size:1.4em} .card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;margin-bottom:14px}
label{display:block;font-size:.88em;color:var(--mut);margin:10px 0 3px} input,select{background:var(--bg);color:var(--txt);border:1px solid var(--bd);border-radius:6px;padding:8px;box-sizing:border-box;font-size:1em}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end}
button{background:#238636;color:#fff;border:0;border-radius:6px;padding:9px 14px;cursor:pointer;font-size:.95em}
button.rot{background:#b62324} .klein{font-size:.85em;color:var(--mut)}
table{border-collapse:collapse;width:100%} td,th{border-bottom:1px solid var(--bd);padding:7px 6px;text-align:left;font-size:.95em} th{color:var(--mut);font-weight:normal;font-size:.85em}
.s0{color:#3fb950}.s1{color:#d29922}.s2{color:#f85149}
</style></head><body>
)HTML" WEBUI_NAVBAR R"HTML(
<h1>Allergie-Tagebuch</h1>
<div class="card"><p style="margin-top:0">Auf dem Display trägst du ein, wie es dir heute geht (Seite „Allergie-Tagebuch“). Hier siehst du alle Einträge zusammen mit der Pollenbelastung des Tages (Skala 0 bis 3 für deine gewählten Allergene) und der Luftqualität, kannst Einträge ergänzen oder löschen und die Liste herunterladen.</p>
<p class="klein" id="info" style="margin-bottom:0"></p></div>
<div class="card"><b>Eintrag ergänzen oder ändern</b>
<div class="row"><div><label>Datum</label><input type="date" id="f_datum"></div>
<div><label>Beschwerden</label><select id="f_stufe"><option value="0">keine</option><option value="1">leichte</option><option value="2">starke</option></select></div>
<div><button onclick="setzen()">Speichern</button></div></div>
<p class="klein">Für ein nachgetragenes Datum fehlen Pollen- und Luftwerte.</p></div>
<div class="card"><table><thead><tr><th>Datum</th><th>Beschwerden</th><th>Pollen (0–3)</th><th>Luft (AQI)</th><th></th></tr></thead><tbody id="tab"></tbody></table>
<p id="leer" class="klein" style="display:none">Noch keine Einträge.</p>
<div class="row" style="margin-top:12px"><a href="/api/tagebuch?csv=1" download="tagebuch.csv"><button type="button">Als CSV herunterladen</button></a><button class="rot" onclick="alle()">Alle Einträge löschen</button></div></div>
<script>
var N=['keine','leichte','starke'];
function dt(d){d=String(d);return d.substr(6,2)+'.'+d.substr(4,2)+'.'+d.substr(0,4)}
function laden(){fetch('/api/tagebuch').then(function(r){return r.json()}).then(function(j){
 document.getElementById('info').textContent=j.karte?'Die Einträge werden auf der Speicherkarte gespeichert.':'Keine Speicherkarte: Die Einträge gehen beim Neustart verloren.';
 var h='';for(var i=j.eintraege.length-1;i>=0;i--){var e=j.eintraege[i];
  h+='<tr><td>'+dt(e.d)+'</td><td class="s'+e.s+'">'+N[e.s]+'</td><td>'+(e.p<0?'–':String(e.p).replace('.',','))+'</td><td>'+(e.a<0?'–':e.a)+'</td><td><button class="rot" onclick="weg('+e.d+')">Löschen</button></td></tr>'}
 document.getElementById('tab').innerHTML=h;document.getElementById('leer').style.display=j.eintraege.length?'none':'block'})}
function senden(b){return fetch('/api/tagebuch',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}).then(function(r){return r.json()}).then(function(j){if(!j.ok)alert('Das hat nicht geklappt.');laden()})}
function setzen(){var d=document.getElementById('f_datum').value;if(!d){alert('Bitte ein Datum wählen.');return}senden({datum:d,stufe:+document.getElementById('f_stufe').value})}
function weg(d){if(confirm('Eintrag vom '+dt(d)+' löschen?'))senden({loeschen:d})}
function alle(){if(confirm('Wirklich ALLE Einträge löschen?'))senden({alle:1})}
document.getElementById('f_datum').value=new Date().toISOString().substr(0,10);laden();
</script></body></html>)HTML";

void tagebuchWebuiRegistrieren(WebServer& server) {
  server.on("/tagebuch", HTTP_GET, [&server]() { server.send_P(200, "text/html; charset=utf-8", TAGEBUCH_HTML); });
  server.on("/api/tagebuch", HTTP_GET, [&server]() {
    static TagebuchEintrag liste[MAX_EINTRAEGE];
    const int n = tagebuchAbDatum(0, liste, MAX_EINTRAEGE);
    String out;
    if (server.arg("csv") == "1") {
      out = "datum,beschwerden,pollen_0_bis_3,luft_aqi\n";
      static const char* const NAME[3] = {"keine", "leichte", "starke"};
      for (int i = 0; i < n; i++) {
        char z[80];
        const long d = liste[i].datum;
        snprintf(z, sizeof(z), "%04ld-%02ld-%02ld,%s,", d / 10000, (d / 100) % 100, d % 100, NAME[liste[i].stufe]);
        out += z;
        if (liste[i].pollen10 != 255) out += String(liste[i].pollen10 / 10.0f, 1);
        out += ",";
        if (liste[i].aqi != 255) out += String((int)liste[i].aqi);
        out += "\n";
      }
      server.sendHeader("Cache-Control", "no-store");
      server.send(200, "text/csv; charset=utf-8", out.c_str());
      return;
    }
    JsonDocument doc;
    JsonArray a = doc["eintraege"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject o = a.add<JsonObject>();
      o["d"] = liste[i].datum; o["s"] = liste[i].stufe;
      o["p"] = liste[i].pollen10 == 255 ? -1.0f : liste[i].pollen10 / 10.0f;
      o["a"] = liste[i].aqi == 255 ? -1 : (int)liste[i].aqi;
    }
    doc["karte"] = tagebuchAufKarte() || speicherBereit();
    serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json; charset=utf-8", out.c_str());
  });
  server.on("/api/tagebuch", HTTP_POST, [&server]() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    Sperre s;
    ladenFallsNoetig();
    if (doc["alle"] | false) { s_n = 0; speichern(); logDiag("[Tagebuch] alle Eintraege geloescht"); server.send(200, "application/json", "{\"ok\":true}"); return; }
    if (!doc["loeschen"].isNull()) {
      const long d = doc["loeschen"] | 0L;
      for (int i = 0; i < s_n; i++) if (s_e[i].datum == d) { memmove(s_e + i, s_e + i + 1, sizeof(TagebuchEintrag) * (s_n - i - 1)); s_n--; break; }
      speichern(); server.send(200, "application/json", "{\"ok\":true}"); return;
    }
    const char* ds = doc["datum"] | "";
    int j = 0, m = 0, t = 0;
    const int st = doc["stufe"] | -1;
    if (sscanf(ds, "%d-%d-%d", &j, &m, &t) != 3 || st < 0 || st > 2) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    const long d = (long)j * 10000 + m * 100 + t;
    if (!datumGueltig(d)) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    TagebuchEintrag e = { (int32_t)d, (uint8_t)st, 255, 255 };
    for (int i = 0; i < s_n; i++) if (s_e[i].datum == d) { e.pollen10 = s_e[i].pollen10; e.aqi = s_e[i].aqi; break; }   // vorhandene Pollen- und Luftwerte behalten
    einfuegen(e);
    speichern();
    server.send(200, "application/json", "{\"ok\":true}");
  });
}
