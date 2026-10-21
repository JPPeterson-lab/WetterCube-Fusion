// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// WebUI fuer die Schalter-Kacheln: Seite /schalter (Geraete aus dem Netz uebernehmen oder von Hand eintragen, Anzeige, Test) und /api/schalter.
#include "schalter.h"
#include "sensors.h"
#include "../app/webui_theme.h"
#include "../platform/WebServer.h"
#include <ArduinoJson.h>

static const char SCHALTER_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>WetterCube Fusion – Schalter</title>
)HTML" WEBUI_THEME_HEAD WEBUI_THEME_CSS R"HTML(
<style>
body{font-family:Arial,sans-serif;background:var(--bg);color:var(--txt);margin:0;padding:14px;max-width:900px;margin:auto;line-height:1.45}
h1{font-size:1.4em} h2{font-size:1.1em;margin:0 0 6px} .card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;margin-bottom:14px}
.nr{display:inline-block;background:#1f6feb;color:#fff;border-radius:50%;width:1.6em;height:1.6em;text-align:center;line-height:1.6em;margin-right:8px;font-size:.9em}
label{display:block;font-size:.88em;color:var(--mut);margin:10px 0 3px} input,select{background:var(--bg);color:var(--txt);border:1px solid var(--bd);border-radius:6px;padding:8px;width:100%;box-sizing:border-box;font-size:.95em}
input[type=checkbox]{width:auto;transform:scale(1.3)} .row{display:flex;gap:10px;flex-wrap:wrap}.row>div{flex:1;min-width:130px}
button{background:#238636;color:#fff;border:0;border-radius:6px;padding:10px 16px;cursor:pointer;font-size:.95em;margin-top:10px}
button.blau{background:#1f6feb} button.rot{background:#b62324;padding:6px 10px;margin:0} button.grau{background:var(--bd);color:var(--txt)}
table{width:100%;border-collapse:collapse;font-size:.92em} td,th{padding:6px 4px;border-bottom:1px solid var(--bd2);text-align:left;vertical-align:middle}
td input{padding:5px} .klein{font-size:.85em;color:var(--mut)} .ok{color:#3fb950}.aus{color:#f85149}.warn{color:#d29922}
details{margin-top:10px} summary{cursor:pointer;color:var(--acc)} .erw{display:none} body.erwei td.erw,body.erwei th.erw{display:table-cell}
.kand label{display:flex;gap:10px;align-items:center;margin:0;padding:9px 4px;border-bottom:1px solid var(--bd2);color:var(--txt);font-size:.95em}
</style></head><body>
)HTML" WEBUI_NAVBAR R"HTML(
<h1>Schalter auf dem Display</h1>
<div class="card"><p style="margin-top:0">Hier legst du Kacheln an, mit denen du Ger&auml;te in deinem Smart Home vom WetterCube aus schaltest, zum Beispiel Licht oder eine Steckdose. Die Kacheln erscheinen auf der <b>Sensor-Seite</b> des Displays hinter den Sensoren. <b>Antippen schaltet um</b>.</p>
<p class="klein">Das funktioniert &uuml;ber dieselbe MQTT-Verbindung wie die Sensoren (siehe <a href="/sensoren">Sensoren</a>, Schritt 1). Der WetterCube sendet den Befehl an ein MQTT-Thema, der Zustand kommt von einem zweiten Thema zur&uuml;ck. Home Assistant meldet seine Schalter und Lichter dem Broker automatisch, dann gen&uuml;gt unten ein Haken.</p>
<p class="klein" id="mqtt"></p></div>

<div class="card"><h2><span class="nr">1</span>Ger&auml;te aus dem Netz &uuml;bernehmen</h2>
<div class="kand" id="kand"></div>
<p class="klein" id="kand_leer">Noch nichts gefunden. Sobald die MQTT-Verbindung steht und Home Assistant Schalter oder Lichter meldet (MQTT-Discovery) oder eine WLED-Lampe am Broker h&auml;ngt, erscheinen sie hier. Das kann bis zu einer Minute dauern.</p>
<button class="blau" id="hinzu" onclick="hinzufuegen()" style="display:none">Ausgew&auml;hlte hinzuf&uuml;gen</button>
<details><summary>Meine Lampe taucht hier nicht auf (zum Beispiel WLED)</summary>
<p class="klein">Home Assistant selbst steht nicht im MQTT-Netz: Ger&auml;te, die Home Assistant &uuml;ber eine eigene Integration einbindet (zum Beispiel <b>WLED</b> &uuml;ber seine WLED-Integration), erscheinen hier nicht. Der WetterCube braucht den Weg &uuml;ber den <b>MQTT-Broker</b>.</p>
<p class="klein"><b>WLED:</b> Schalte in WLED unter <i>Config &rarr; Sync Interfaces &rarr; MQTT</i> die Verbindung zum selben Broker ein (Adresse, Port, Benutzer, Passwort wie in Schritt 1 auf der Seite Sensoren). Dann meldet sich die Lampe hier von selbst (bei dem Standard-Ger&auml;tethema <code>wled/&hellip;</code>), oder du tr&auml;gst ihr <b>Ger&auml;tethema</b> hier ein:</p>
<div class="row"><div><label>Name</label><input id="w_name" placeholder="z. B. Stehlampe"></div><div><label>WLED-Ger&auml;tethema (z.&nbsp;B. wled/ab12cd)</label><input id="w_topic" placeholder="wled/ab12cd" autocapitalize="off" autocorrect="off"></div></div>
<button class="blau" onclick="wledHinzu()">WLED hinzuf&uuml;gen</button></details></div>

<div class="card"><h2><span class="nr">2</span>Das zeigt der WetterCube an</h2>
<div style="overflow-x:auto"><table id="liste"><thead><tr><th>Anzeigen</th><th>Name</th><th>Raum</th><th>Zustand</th><th>Test</th><th class="erw">Befehl-Thema</th><th class="erw">Zustand-Thema</th><th class="erw">An / Aus senden</th><th class="erw">An / Aus erkennen</th><th class="erw">JSON-Schl&uuml;ssel</th><th class="erw">Reihenfolge</th><th></th></tr></thead><tbody></tbody></table></div>
<p class="klein" id="liste_leer">Noch keine Schalter eingerichtet.</p>
<button onclick="speichern()">Speichern</button> <span id="msg" class="klein"></span>
<label style="margin-top:10px"><input type="checkbox" id="erweitert" onchange="document.body.classList.toggle('erwei',this.checked)"> Erweiterte Einstellungen zeigen (Themen, Texte, Reihenfolge)</label>
<details><summary>Schalter von Hand hinzuf&uuml;gen</summary>
<div class="row"><div><label>Name</label><input id="m_name" placeholder="z. B. Stehlampe"></div><div><label>Raum</label><input id="m_raum" placeholder="z. B. Wohnzimmer"></div></div>
<label>Befehl-Thema (dorthin sendet der WetterCube, z.&nbsp;B. wohnzimmer/stehlampe/set)</label><input id="m_cmd" autocapitalize="off" autocorrect="off">
<label>Zustand-Thema (von dort kommt der Zustand zur&uuml;ck, optional, z.&nbsp;B. wohnzimmer/stehlampe/state)</label><input id="m_state" autocapitalize="off" autocorrect="off">
<div class="row"><div><label>Text f&uuml;r &bdquo;an&ldquo;</label><input id="m_an" placeholder="ON"></div><div><label>Text f&uuml;r &bdquo;aus&ldquo;</label><input id="m_aus" placeholder="OFF"></div></div>
<button class="blau" onclick="handHinzu()">Hinzuf&uuml;gen</button></details>
<p class="klein">Ohne Zustand-Thema merkt sich der WetterCube nur, was er zuletzt gesendet hat. Bis zum ersten Zustand zeigt die Kachel &bdquo;?&ldquo;. Pro Kachel ist ein Ger&auml;t m&ouml;glich, bis zu 8 Kacheln.</p></div>

<script>
var S={schalter:[],kandidaten:[],mqtt_status:0},sel={},LEER=document.getElementById('kand_leer').innerHTML;
var ZS=['<span class="klein">?</span>','<b class="aus">aus</b>','<b class="ok">an</b>'];
function $(i){return document.getElementById(i);}
function esc(t){return String(t==null?'':t).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
function schoen(t){t=String(t||'').replace(/[-_]+/g,' ').trim();return t.replace(/\b\w/g,function(c){return c.toUpperCase();});}
function slug(t){return t.toLowerCase().replace(/[^a-z0-9]+/g,'_').replace(/^_|_$/g,'');}
function kurz(t){var h=5381;for(var i=0;i<t.length;i++)h=((h<<5)+h+t.charCodeAt(i))>>>0;return h.toString(36).slice(0,6);}
function laden(){fetch('/api/schalter').then(function(r){return r.json();}).then(function(d){S=d;
 $('mqtt').innerHTML=d.mqtt_status==2?'<span class="ok">&#9679; MQTT verbunden</span>':'<span class="aus">&#9679; MQTT nicht verbunden:</span> zuerst unter <a href="/sensoren">Sensoren</a> verbinden.';zeichne();});}
function zeichne(){var tb=document.querySelector('#liste tbody');tb.innerHTML='';
 S.schalter.forEach(function(s,i){var tr=document.createElement('tr');
  tr.innerHTML='<td><input type="checkbox" '+(s.sichtbar?'checked':'')+' onchange="S.schalter['+i+'].sichtbar=this.checked?1:0"></td>'+
  '<td><input value="'+esc(s.name)+'" maxlength="23" onchange="S.schalter['+i+'].name=this.value"></td>'+
  '<td><input value="'+esc(s.raum)+'" size="8" maxlength="19" onchange="S.schalter['+i+'].raum=this.value"></td>'+
  '<td>'+ZS[s.zustand||0]+'</td>'+
  '<td style="white-space:nowrap"><button class="grau" style="margin:0 3px 0 0;padding:4px 8px" onclick="test(\''+esc(s.id)+'\',true)">an</button><button class="grau" style="margin:0;padding:4px 8px" onclick="test(\''+esc(s.id)+'\',false)">aus</button></td>'+
  '<td class="erw"><input value="'+esc(s.cmd_topic)+'" onchange="S.schalter['+i+'].cmd_topic=this.value.trim()"></td>'+
  '<td class="erw"><input value="'+esc(s.state_topic)+'" onchange="S.schalter['+i+'].state_topic=this.value.trim()"></td>'+
  '<td class="erw" style="white-space:nowrap"><input value="'+esc(s.payload_an)+'" size="5" maxlength="15" onchange="S.schalter['+i+'].payload_an=this.value"> <input value="'+esc(s.payload_aus)+'" size="5" maxlength="15" onchange="S.schalter['+i+'].payload_aus=this.value"></td>'+
  '<td class="erw" style="white-space:nowrap"><input value="'+esc(s.state_an)+'" size="5" maxlength="15" placeholder="wie senden" onchange="S.schalter['+i+'].state_an=this.value"> <input value="'+esc(s.state_aus)+'" size="5" maxlength="15" placeholder="wie senden" onchange="S.schalter['+i+'].state_aus=this.value"></td>'+
  '<td class="erw"><input value="'+esc(s.json_key)+'" size="8" maxlength="15" onchange="S.schalter['+i+'].json_key=this.value"></td>'+
  '<td class="erw"><input type="number" value="'+s.reihenfolge+'" style="width:55px" onchange="S.schalter['+i+'].reihenfolge=parseInt(this.value)||0"></td>'+
  '<td><button class="rot" title="Entfernen" onclick="weg('+i+')">&times;</button></td>';
  tb.appendChild(tr);});
 $('liste_leer').style.display=S.schalter.length?'none':'block';
 var kb=$('kand');kb.innerHTML='';var n=0;
 S.kandidaten.forEach(function(k,i){if(S.schalter.some(function(s){return s.cmd_topic==k.cmd_topic;}))return;n++;
  var l=document.createElement('label');l.innerHTML='<input type="checkbox" '+(sel[k.cmd_topic]?'checked':'')+' onchange="sel[S.kandidaten['+i+'].cmd_topic]=this.checked"><span><b>'+esc(schoen(k.name))+'</b>'+(k.geraet?' <span class="klein">('+esc(schoen(k.geraet))+')</span>':'')+'</span>';kb.appendChild(l);});
 $('kand_leer').style.display=n?'none':'block';$('hinzu').style.display=n?'inline-block':'none';
 $('kand_leer').innerHTML=S.kandidaten.length&&!n?'Alle gefundenen Ger&auml;te sind schon hinzugef&uuml;gt.':LEER;}
function neu(name,raum,cmd,state,an,aus,stan,staus){if(S.schalter.length>=8){alert('Mehr als 8 Schalter sind nicht möglich.');return false;}
 S.schalter.push({id:(slug(raum+'_'+name).slice(0,15)||'schalter')+'_'+kurz(cmd),name:name.slice(0,23),raum:raum.slice(0,19),cmd_topic:cmd,state_topic:state||'',json_key:'',payload_an:an||'ON',payload_aus:aus||'OFF',state_an:stan||'',state_aus:staus||'',sichtbar:1,reihenfolge:S.schalter.length,retain:0,zustand:0});return true;}
function hinzufuegen(){S.kandidaten.forEach(function(k){if(sel[k.cmd_topic]&&!S.schalter.some(function(s){return s.cmd_topic==k.cmd_topic;}))neu(schoen(k.name),'',k.cmd_topic,k.state_topic,k.payload_an,k.payload_aus,k.state_an,k.state_aus);});sel={};speichern();}
function wledHinzu(){var t=$('w_topic').value.trim().replace(/\/+$/,''),n=$('w_name').value.trim();if(!t||!n){alert('Name und WLED-Gerätethema ausfüllen.');return;}
 if(neu(n,'',t,t+'/g','ON','OFF','>0','0'))speichern();}
function handHinzu(){if(!$('m_cmd').value.trim()||!$('m_name').value.trim()){alert('Name und Befehl-Thema ausfüllen.');return;}
 if(neu($('m_name').value.trim(),$('m_raum').value.trim(),$('m_cmd').value.trim(),$('m_state').value.trim(),$('m_an').value.trim(),$('m_aus').value.trim()))speichern();}
function weg(i){if(confirm('Schalter entfernen?')){S.schalter.splice(i,1);speichern();}}
function test(id,an){fetch('/api/schalter/schalten',{method:'POST',body:JSON.stringify({id:id,an:an})}).then(function(r){return r.json();}).then(function(d){$('msg').textContent=d.ok?'Befehl gesendet.':'Nicht gesendet: MQTT ist nicht verbunden.';setTimeout(laden,800);});}
function speichern(){var body={schalter:S.schalter.map(function(s){return {id:s.id,name:s.name,raum:s.raum,cmd_topic:s.cmd_topic,state_topic:s.state_topic,json_key:s.json_key||'',payload_an:s.payload_an,payload_aus:s.payload_aus,state_an:s.state_an||'',state_aus:s.state_aus||'',sichtbar:s.sichtbar,reihenfolge:s.reihenfolge,retain:s.retain||0};})};
 fetch('/api/schalter',{method:'POST',body:JSON.stringify(body)}).then(function(r){return r.json();}).then(function(d){$('msg').textContent=d.ok?'Gespeichert ('+d.n+' Schalter).':'Fehler beim Speichern';setTimeout(laden,800);}).catch(function(){$('msg').textContent='Fehler: Gerät nicht erreichbar';});}
laden();setInterval(function(){if(!document.hidden&&document.activeElement.tagName!=='INPUT')laden();},5000);
</script></body></html>)HTML";

void schalterWebuiRegistrieren(WebServer& server) {
  server.on("/schalter", HTTP_GET, [&server]() { server.send_P(200, "text/html; charset=utf-8", SCHALTER_HTML); });
  server.on("/api/schalter", HTTP_GET, [&server]() {
    std::string j = schalterZustandJson();
    server.send(200, "application/json; charset=utf-8", j.c_str());
  });
  server.on("/api/schalter", HTTP_POST, [&server]() {
    String body = server.arg("plain");
    int n = body.length() ? schalterAusJson(body.c_str()) : -1;
    if (n < 0) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    server.send(200, "application/json", String("{\"ok\":true,\"n\":") + n + "}");
  });
  server.on("/api/schalter/schalten", HTTP_POST, [&server]() {
    JsonDocument doc;
    String body = server.arg("plain");
    bool ok = false;
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
      const char* id = doc["id"] | "";
      for (int i = 0; i < schalterAnzahl(); i++) { SchalterCfg c; if (schalterHole(i, &c, nullptr) && strcmp(c.id, id) == 0) { ok = schalterSchalte(i, doc["an"] | false); break; } }
    }
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
}
