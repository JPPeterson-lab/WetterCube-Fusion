// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// WetterCube Next verwalten: Geraeteliste (NVS), Einstellungen je Next, Seite /next mit einem Reiter je Next, Name wettercube-next.local.
#include "next_geraete.h"
#include "app_state.h"
#include "webui_theme.h"
#include "../sensors/sensors.h"
#include "../platform/WebServer.h"
#include <ArduinoJson.h>
#include <stddef.h>
#include <string.h>
#include "mdns.h"
#include "nvs.h"
#include "esp_timer.h"

namespace {
const int NEXT_MAX = 8;
const char* const NVS_NS = "nextg";

struct Geraet {
  // gespeichert (bis "gesehen")
  char id[13]; char name[24]; uint8_t verwaltet;
  uint8_t seiten, hell, nq, nverh, nhell, nwach; uint16_t wechsel, nvon, nbis; char r[104];
  uint8_t unten;   // erste Seite, unten: 0 = Raeume und Biowetter, 1 = nur Raeume, 2 = nur Biowetter (ab Next 0.3.0-beta2); Speicherstand ohne dieses Feld bleibt lesbar
  // fluechtig
  uint8_t gesehen; char ip[16]; char ver[24]; int64_t zuletzt_ms;
};
Geraet s_g[NEXT_MAX];
int s_n = 0;
const size_t GESPEICHERT = offsetof(Geraet, gesehen);
const size_t GESPEICHERT_ALT = offsetof(Geraet, unten);   // aeltere Speicherung (vor dem Feld unten)

bool hexId(const char* id) {
  if (!id || strlen(id) != 12) return false;
  for (int i = 0; i < 12; i++) if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
  return true;
}

void vorgaben(Geraet& g, const char* id, int nr) {
  memset(&g, 0, sizeof(g));
  strlcpy(g.id, id, sizeof(g.id));
  snprintf(g.name, sizeof(g.name), "Next %d", nr);
  g.seiten = 3; g.wechsel = 0; g.hell = 80; g.nq = 0; g.nvon = 22 * 60; g.nbis = 6 * 60 + 30; g.nverh = 0; g.nhell = 10; g.nwach = 15;
}

void speichern(int i) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  char k[4]; snprintf(k, sizeof(k), "g%d", i);
  nvs_set_blob(h, k, &s_g[i], GESPEICHERT);
  nvs_set_u8(h, "n", (uint8_t)s_n);
  nvs_commit(h); nvs_close(h);
}

void alleSpeichern() { for (int i = 0; i < s_n; i++) speichern(i); }

int finde(const char* id) { for (int i = 0; i < s_n; i++) if (strcmp(s_g[i].id, id) == 0) return i; return -1; }

bool einfachesZeichen(char c) { return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == ','; }

// Raeume: Sensor-IDs, durch Komma getrennt, bis vier; unbekannte Zeichen verwerfen
void raeumeSetzen(Geraet& g, const char* r) {
  char o[sizeof(g.r)]; size_t k = 0; int n = 1;
  for (const char* p = r ? r : ""; *p && k < sizeof(o) - 1; p++) {
    if (!einfachesZeichen(*p)) continue;
    if (*p == ',') { if (++n > 4 || k == 0 || o[k - 1] == ',') { if (n > 4) break; continue; } }
    o[k++] = *p;
  }
  while (k && o[k - 1] == ',') k--;
  o[k] = 0;
  strlcpy(g.r, o, sizeof(g.r));
}

int begrenzt(long v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : (int)v; }

String cfgJson(const Geraet& g) {
  char b[280];
  snprintf(b, sizeof(b), "{\"seiten\":%d,\"wechsel\":%d,\"hell\":%d,\"nq\":%d,\"nvon\":%d,\"nbis\":%d,\"nverh\":%d,\"nhell\":%d,\"nwach\":%d,\"unten\":%d,\"r\":\"%s\"}",
           g.seiten, g.wechsel, g.hell, g.nq, g.nvon, g.nbis, g.nverh, g.nhell, g.nwach, g.unten, g.r);
  return String(b);
}
}  // namespace

void nextGeraeteInit() {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
  uint8_t n = 0; nvs_get_u8(h, "n", &n);
  for (int i = 0; i < n && i < NEXT_MAX; i++) {
    char k[4]; snprintf(k, sizeof(k), "g%d", i);
    Geraet g; memset(&g, 0, sizeof(g)); size_t len = GESPEICHERT;
    if (nvs_get_blob(h, k, &g, &len) != ESP_OK || (len != GESPEICHERT && len != GESPEICHERT_ALT) || !hexId(g.id)) continue;
    if (len == GESPEICHERT_ALT) g.unten = 0;
    g.name[sizeof(g.name) - 1] = 0; g.r[sizeof(g.r) - 1] = 0;
    g.seiten = begrenzt(g.seiten, 1, 3); g.hell = begrenzt(g.hell, 3, 100); g.nq = begrenzt(g.nq, 0, 2); g.nverh = begrenzt(g.nverh, 0, 2);
    g.nhell = begrenzt(g.nhell, 1, 100); g.nwach = begrenzt(g.nwach, 3, 120); g.wechsel = begrenzt(g.wechsel, 0, 600);
    g.nvon = begrenzt(g.nvon, 0, 1439); g.nbis = begrenzt(g.nbis, 0, 1439); g.unten = begrenzt(g.unten, 0, 2);
    s_g[s_n++] = g;
  }
  nvs_close(h);
}

void nextGeraetMeldet(const char* id, const char* ip, const char* ver, const char* c, const char* r, const char* u) {
  if (!hexId(id)) return;
  int i = finde(id);
  bool neu = false;
  if (i < 0) {
    if (s_n >= NEXT_MAX) { static bool gemeldet = false; if (!gemeldet) { gemeldet = true; logDiag("[Next] Mehr als %d Geraete: weitere werden nicht verwaltet", NEXT_MAX); } return; }
    i = s_n++; vorgaben(s_g[i], id, s_n); neu = true;
    logDiag("[Next] Neues Geraet %s", id);
  }
  Geraet& g = s_g[i];
  g.gesehen = 1; g.zuletzt_ms = esp_timer_get_time() / 1000;
  { char o[sizeof(g.ip)]; size_t k = 0; for (const char* p = ip ? ip : ""; *p && k < sizeof(o) - 1; p++) if (isdigit((unsigned char)*p) || *p == '.') o[k++] = *p; o[k] = 0; strlcpy(g.ip, o, sizeof(g.ip)); }
  { char o[sizeof(g.ver)]; size_t k = 0; for (const char* p = ver ? ver : ""; *p && k < sizeof(o) - 1; p++) if (einfachesZeichen(*p)) o[k++] = *p; o[k] = 0; strlcpy(g.ver, o, sizeof(g.ver)); }
  bool geaendert = neu;
  if (!g.verwaltet && c && c[0]) {                                  // nicht verwaltet: die gemeldeten Einstellungen sind der Stand des Next
    int v[9]; int n = sscanf(c, "%d,%d,%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]);
    if (n == 9) {
      Geraet z = g;
      z.seiten = begrenzt(v[0], 1, 3); z.wechsel = begrenzt(v[1], 0, 600); z.hell = begrenzt(v[2], 3, 100); z.nq = begrenzt(v[3], 0, 2);
      z.nvon = begrenzt(v[4], 0, 1439); z.nbis = begrenzt(v[5], 0, 1439); z.nverh = begrenzt(v[6], 0, 2); z.nhell = begrenzt(v[7], 1, 100); z.nwach = begrenzt(v[8], 3, 120);
      raeumeSetzen(z, r);
      if (u && u[0]) z.unten = begrenzt(atoi(u), 0, 2);
      if (memcmp(&z, &g, GESPEICHERT) != 0) { memcpy(&g, &z, GESPEICHERT); geaendert = true; }
    }
  }
  if (geaendert) speichern(i);
}

std::string nextCfgJson(const char* id) {
  if (!hexId(id)) return "";
  const int i = finde(id);
  if (i < 0 || !s_g[i].verwaltet) return "";
  return std::string(",\"cfg\":") + cfgJson(s_g[i]).c_str();
}

const char* nextGeraeteStatusText() {
  static char t[64];
  if (s_n == 0) { snprintf(t, sizeof(t), "Noch kein Next gemeldet."); return t; }
  const int64_t jetzt = esp_timer_get_time() / 1000; int online = 0;
  for (int i = 0; i < s_n; i++) if (s_g[i].gesehen && jetzt - s_g[i].zuletzt_ms < 120000) online++;
  snprintf(t, sizeof(t), "%d Next-Display%s bekannt, %d online.", s_n, s_n == 1 ? "" : "s", online);
  return t;
}

// ── Name wettercube-next.local ───────────────────────────────────────────────
void nextAliasPruefen() {
  if (WiFi.status() != WL_CONNECTED) return;
  static uint32_t zuletzt = 0; static bool da = false;
  const uint32_t ip = WiFi.localIP().raw();
  if (ip == 0 || (da && ip == zuletzt)) return;
  mdns_ip_addr_t a = {}; a.addr.type = ESP_IPADDR_TYPE_V4; a.addr.u_addr.ip4.addr = ip; a.next = nullptr;
  esp_err_t e = da ? mdns_delegate_hostname_set_address("wettercube-next", &a) : mdns_delegate_hostname_add("wettercube-next", &a);
  if (e == ESP_OK) { da = true; zuletzt = ip; logDiag("[Next] wettercube-next.local zeigt auf %s", WiFi.localIP().toString().c_str()); }
  else if (!da) { static uint8_t versuche = 0; if (versuche++ < 3) logDiag("[Next] Name wettercube-next.local nicht angelegt (Fehler %d)", (int)e); }
}

// ── Web: /next und /api/nextgeraete ──────────────────────────────────────────
static const char NEXT_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>WetterCube Fusion – Next-Displays</title>
)HTML" WEBUI_THEME_HEAD WEBUI_THEME_CSS R"HTML(
<style>
body{font-family:Arial,sans-serif;background:var(--bg);color:var(--txt);margin:0;padding:14px;max-width:900px;margin:auto;line-height:1.45}
h1{font-size:1.4em} h2{font-size:1.05em;margin:16px 0 6px} .card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;margin-bottom:14px}
label{display:block;font-size:.88em;color:var(--mut);margin:10px 0 3px} input,select{background:var(--bg);color:var(--txt);border:1px solid var(--bd);border-radius:6px;padding:8px;width:100%;box-sizing:border-box;font-size:1em}
input[type=checkbox]{width:auto;transform:scale(1.3);margin-right:8px} .z{display:flex;align-items:center;margin:6px 0;color:var(--txt);font-size:.95em}
.row{display:flex;gap:10px;flex-wrap:wrap}.row>div{flex:1;min-width:130px}
button{background:#238636;color:#fff;border:0;border-radius:6px;padding:10px 16px;cursor:pointer;font-size:.95em;margin-top:12px}
button.rot{background:#b62324} .klein{font-size:.85em;color:var(--mut)} .ok{color:#3fb950}.aus{color:#f85149}
.tabs{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:12px}.tab{background:var(--tab);color:var(--txt);border:1px solid var(--bd);border-radius:8px;padding:8px 14px;cursor:pointer;margin:0;font-size:.95em}
.tab.aktiv{background:#1f6feb;border-color:#1f6feb;color:#fff} .pkt{display:inline-block;width:.6em;height:.6em;border-radius:50%;margin-right:6px;background:#8b949e}.pkt.on{background:#3fb950}
.hinweis{background:var(--in);border:1px solid var(--bd);border-radius:8px;padding:10px 12px;margin:10px 0;font-size:.92em}
</style></head><body>
)HTML" WEBUI_NAVBAR R"HTML(
<h1>Next-Displays</h1>
<div class="card"><p style="margin-top:0">Jedes WetterCube Next, das bei diesem Fusion Daten abholt, bekommt einen Reiter. Hier stellst du alle Next-Displays an einer Stelle ein, unter <b>wettercube-next.local</b>.
Ein neues Next erscheint von selbst, sobald es den Fusion gefunden hat.</p>
<p class="klein" style="margin-bottom:0">Die Einstellungen gelten erst, wenn im Reiter <b>„Vom Fusion verwalten“</b> eingeschaltet ist. Das Next holt sie dann mit der nächsten Datenabfrage ab und speichert sie.
Anzeigeart, Drehen, Farben, WLAN und Update bleiben auf der Seite des jeweiligen Next (Link im Reiter).</p></div>
<div class="tabs" id="tabs"></div>
<div id="panel"></div>
<script>
var D={geraete:[],sensoren:[]},cur=0,NQ=['Nie (kein Nachtmodus)','Wie der Fusion (nach dessen Einstellungen)','Eigene Zeiten'],UN=['Räume und Biowetter nebeneinander','Nur Räume','Nur Biowetter'],NV=['Display aus (Antippen schaltet kurz an)','Dunkles Design, gedimmt','Nacht-Uhr (Uhrzeit, Datum, Wetter, gedimmt)'];
function esc(t){return String(t).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function hm(m){m=+m;var h=Math.floor(m/60)%24,n=m%60;return(h<10?'0':'')+h+':'+(n<10?'0':'')+n}
function minuten(s,v){var p=String(s).split(':');return p.length==2&&+p[0]>=0&&+p[0]<24&&+p[1]>=0&&+p[1]<60?(+p[0])*60+(+p[1]):v}
function laden(){fetch('/api/nextgeraete').then(function(r){return r.json()}).then(function(d){D=d;if(cur>=D.geraete.length)cur=0;zeige()}).catch(function(){document.getElementById('panel').innerHTML='<div class="card">Die Liste konnte nicht geladen werden.</div>'})}
function zeige(){
 var t='';for(var i=0;i<D.geraete.length;i++){var g=D.geraete[i];t+='<button class="tab'+(i==cur?' aktiv':'')+'" onclick="cur='+i+';zeige()"><span class="pkt'+(g.zuletzt>=0&&g.zuletzt<120?' on':'')+'"></span>'+esc(g.name)+'</button>'}
 document.getElementById('tabs').innerHTML=t;
 var p=document.getElementById('panel');
 if(!D.geraete.length){p.innerHTML='<div class="card"><p>Noch kein Next gemeldet.</p><p class="klein">Ein WetterCube Next ab Version 0.2.0-alpha1 meldet sich selbst, sobald es diesen Fusion im Netz gefunden hat. Bei älteren Versionen zuerst das Next aktualisieren.</p></div>';return}
 var g=D.geraete[cur],c=g.cfg,verw=!!g.verwaltet;
 var st=g.zuletzt<0?'noch nicht gesehen':g.zuletzt<120?'<span class="ok">online</span>, zuletzt vor '+g.zuletzt+' s':'<span class="aus">nicht erreichbar</span>, zuletzt vor '+Math.round(g.zuletzt/60)+' min';
 var h='<div class="card"><div class="row"><div><label>Name dieses Next</label><input id="f_name" maxlength="22" value="'+esc(g.name)+'"></div>'+
  '<div style="flex:2;min-width:220px"><label>Status</label><div style="padding:8px 0">'+st+'<br><span class="klein">Version '+esc(g.ver||'?')+(g.ip?' · <a href="http://'+esc(g.ip)+'/" target="_blank">Geräte-Einstellungen ('+esc(g.ip)+')</a>':'')+'</span></div></div></div>'+
  '<div class="z" style="margin-top:12px"><label style="display:flex;align-items:center;margin:0;color:var(--txt);font-size:1em"><input type="checkbox" id="f_verw"'+(verw?' checked':'')+' onchange="zeige2()">Vom Fusion verwalten</label></div>'+
  '<div class="hinweis" id="f_hin">'+(verw?'Das Next übernimmt die Einstellungen unten mit seiner nächsten Datenabfrage (wenige Sekunden).':'Aus: Das Next nutzt seine eigenen Einstellungen. Unten steht, was es gerade meldet. Zum Ändern „Vom Fusion verwalten“ einschalten und speichern.')+'</div></div>';
 h+='<div class="card" id="f_cfg"><h2 style="margin-top:0">Anzeige</h2>'+
  '<div class="z"><input type="checkbox" id="f_s0"'+(c.seiten&1?' checked':'')+'>Temperatur und Biowetter</div><div class="z"><input type="checkbox" id="f_s1"'+(c.seiten&2?' checked':'')+'>Allergene und Vorhersage</div>'+
  '<div class="row"><div><label>Weiterblättern (Sekunden, 0 = nur Antippen)</label><input type="number" id="f_wechsel" min="0" max="600" value="'+c.wechsel+'"></div><div><label>Helligkeit am Tag (%)</label><input type="number" id="f_hell" min="3" max="100" value="'+c.hell+'"></div></div>'+
  '<label>Erste Seite, unten</label><select id="f_unten">'+UN.map(function(t,q){return '<option value="'+q+'"'+(c.unten==q?' selected':'')+'>'+t+'</option>'}).join('')+'</select>'+
  '<h2>Räume</h2><div class="klein">Welche vier Werte zeigt die Kachel „Räume“? Ohne Auswahl die ersten vier.</div><div class="row">';
 var ids=(c.r||'').split(',');
 for(var k=0;k<4;k++){h+='<div><label>Wert '+(k+1)+'</label><select id="f_r'+k+'"><option value="">–</option>';for(var s=0;s<D.sensoren.length;s++){var x=D.sensoren[s];h+='<option value="'+esc(x.id)+'"'+(ids[k]==x.id?' selected':'')+'>'+esc(x.label)+'</option>'}h+='</select></div>'}
 h+='</div><h2>Nachtmodus</h2><label>Wann ist Nacht?</label><select id="f_nq">';for(var q=0;q<3;q++)h+='<option value="'+q+'"'+(c.nq==q?' selected':'')+'>'+NQ[q]+'</option>';
 h+='</select><div class="row"><div><label>Eigene Zeiten: von</label><input type="time" id="f_nvon" value="'+hm(c.nvon)+'"></div><div><label>bis</label><input type="time" id="f_nbis" value="'+hm(c.nbis)+'"></div></div>'+
  '<label>Was passiert nachts?</label><select id="f_nverh">';for(var v=0;v<3;v++)h+='<option value="'+v+'"'+(c.nverh==v?' selected':'')+'>'+NV[v]+'</option>';
 h+='</select><div class="row"><div><label>Helligkeit nachts (%)</label><input type="number" id="f_nhell" min="1" max="100" value="'+c.nhell+'"></div><div><label>Antippen schaltet nachts an für (Sekunden)</label><input type="number" id="f_nwach" min="3" max="120" value="'+c.nwach+'"></div></div>'+
  '<button onclick="speichern()">Speichern</button> <button class="rot" onclick="loeschen()">Dieses Next aus der Liste entfernen</button> <span class="klein" id="f_msg"></span></div>';
 p.innerHTML=h;
}
function zeige2(){var v=document.getElementById('f_verw').checked;document.getElementById('f_hin').textContent=v?'Das Next übernimmt die Einstellungen unten mit seiner nächsten Datenabfrage (wenige Sekunden).':'Aus: Das Next nutzt seine eigenen Einstellungen.'}
function w(i){return document.getElementById(i).value}
function speichern(){
 var r=[];for(var k=0;k<4;k++){var x=w('f_r'+k);if(x)r.push(x)}
 var s=(document.getElementById('f_s0').checked?1:0)|(document.getElementById('f_s1').checked?2:0);if(!s)s=1;
 var g=D.geraete[cur];
 var b={id:g.id,name:w('f_name'),verwaltet:document.getElementById('f_verw').checked?1:0,cfg:{seiten:s,wechsel:+w('f_wechsel'),hell:+w('f_hell'),nq:+w('f_nq'),nvon:minuten(w('f_nvon'),1320),nbis:minuten(w('f_nbis'),390),nverh:+w('f_nverh'),nhell:+w('f_nhell'),nwach:+w('f_nwach'),unten:+w('f_unten'),r:r.join(',')}};
 fetch('/api/nextgeraete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}).then(function(r){return r.json()}).then(function(j){document.getElementById('f_msg').textContent=j.ok?'Gespeichert':'Fehler';if(j.ok)laden()}).catch(function(){document.getElementById('f_msg').textContent='Fehler'})}
function loeschen(){var g=D.geraete[cur];if(!confirm('„'+g.name+'“ aus der Liste entfernen? Meldet sich das Next wieder, erscheint es neu.'))return;
 fetch('/api/nextgeraete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:g.id,loeschen:1})}).then(function(){cur=0;laden()})}
laden();setInterval(function(){if(document.activeElement&&/INPUT|SELECT/.test(document.activeElement.tagName))return;laden()},20000);
</script></body></html>)HTML";

static bool sicherText(const char* s) { for (; *s; s++) if ((unsigned char)*s < 32) return false; return true; }

void nextGeraeteWebuiRegistrieren(WebServer& server) {
  server.on("/next", HTTP_GET, [&server]() { server.send_P(200, "text/html; charset=utf-8", NEXT_HTML); });
  server.on("/api/nextgeraete", HTTP_GET, [&server]() {
    JsonDocument doc;
    JsonArray sa = doc["sensoren"].to<JsonArray>();
    for (int i = 0; i < sensorenAnzahl(); i++) {
      SensorCfg c; SensorWert w;
      if (!sensorenHole(i, &c, &w) || !c.sichtbar) continue;
      JsonObject o = sa.add<JsonObject>();
      o["id"] = c.id;
      String l = String(c.raum) + (c.raum[0] && c.name[0] ? " – " : "") + c.name;
      o["label"] = l;
    }
    JsonArray ga = doc["geraete"].to<JsonArray>();
    const int64_t jetzt = esp_timer_get_time() / 1000;
    for (int i = 0; i < s_n; i++) {
      const Geraet& g = s_g[i];
      JsonObject o = ga.add<JsonObject>();
      o["id"] = g.id; o["name"] = g.name; o["verwaltet"] = g.verwaltet ? 1 : 0; o["ip"] = g.ip; o["ver"] = g.ver;
      o["zuletzt"] = g.gesehen ? (int)((jetzt - g.zuletzt_ms) / 1000) : -1;
      JsonObject c = o["cfg"].to<JsonObject>();
      c["seiten"] = g.seiten; c["wechsel"] = g.wechsel; c["hell"] = g.hell; c["nq"] = g.nq; c["nvon"] = g.nvon; c["nbis"] = g.nbis;
      c["nverh"] = g.nverh; c["nhell"] = g.nhell; c["nwach"] = g.nwach; c["unten"] = g.unten; c["r"] = g.r;
    }
    String out; serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json; charset=utf-8", out.c_str());
  });
  server.on("/api/nextgeraete", HTTP_POST, [&server]() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    const char* id = doc["id"] | "";
    const int i = finde(id);
    if (i < 0) { server.send(404, "application/json", "{\"ok\":false}"); return; }
    if (doc["loeschen"] | false) {
      for (int k = i; k < s_n - 1; k++) s_g[k] = s_g[k + 1];
      s_n--;
      nvs_handle_t h; if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) { char k[4]; snprintf(k, sizeof(k), "g%d", s_n); nvs_erase_key(h, k); nvs_commit(h); nvs_close(h); }
      alleSpeichern();
      logDiag("[Next] Geraet %s entfernt", id);
      server.send(200, "application/json", "{\"ok\":true}");
      return;
    }
    Geraet& g = s_g[i];
    const char* name = doc["name"] | g.name;
    if (name[0] && strlen(name) < sizeof(g.name) && sicherText(name)) strlcpy(g.name, name, sizeof(g.name));
    g.verwaltet = (doc["verwaltet"] | 0) ? 1 : 0;
    JsonObject c = doc["cfg"];
    if (!c.isNull()) {
      g.seiten = begrenzt(c["seiten"] | (int)g.seiten, 1, 3); g.wechsel = begrenzt(c["wechsel"] | (int)g.wechsel, 0, 600); g.hell = begrenzt(c["hell"] | (int)g.hell, 3, 100);
      g.nq = begrenzt(c["nq"] | (int)g.nq, 0, 2); g.nvon = begrenzt(c["nvon"] | (int)g.nvon, 0, 1439); g.nbis = begrenzt(c["nbis"] | (int)g.nbis, 0, 1439);
      g.nverh = begrenzt(c["nverh"] | (int)g.nverh, 0, 2); g.nhell = begrenzt(c["nhell"] | (int)g.nhell, 1, 100); g.nwach = begrenzt(c["nwach"] | (int)g.nwach, 3, 120);
      g.unten = begrenzt(c["unten"] | (int)g.unten, 0, 2);
      raeumeSetzen(g, c["r"] | "");
    }
    speichern(i);
    logDiag("[Next] %s gespeichert (%s)", g.name, g.verwaltet ? "vom Fusion verwaltet" : "eigene Einstellungen");
    server.send(200, "application/json", "{\"ok\":true}");
  });
}
