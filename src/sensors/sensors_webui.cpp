// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// WebUI fuer die Sensoren: Seite /sensoren (Verbinden, Sensoren auswaehlen, Anzeige; Schritt-fuer-Schritt fuer Einsteiger) und /api/sensoren.
// Nutzt vorerst den Arduino-WebServer; beim Wechsel auf esp_http_server aendert sich nur diese Datei.
#include "sensors.h"
#include "../app/webui_theme.h"
#include "../platform/WebServer.h"
#include <ArduinoJson.h>

static const char SENSOREN_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>WetterCube Fusion – Sensoren</title>
)HTML" WEBUI_THEME_HEAD WEBUI_THEME_CSS R"HTML(
<style>
body{font-family:Arial,sans-serif;background:var(--bg);color:var(--txt);margin:0;padding:14px;max-width:900px;margin:auto;line-height:1.45}
h1{font-size:1.4em} h2{font-size:1.1em;margin:0 0 6px} .card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;margin-bottom:14px}
.nr{display:inline-block;background:#1f6feb;color:#fff;border-radius:50%;width:1.6em;height:1.6em;text-align:center;line-height:1.6em;margin-right:8px;font-size:.9em}
label{display:block;font-size:.88em;color:var(--mut);margin:10px 0 3px} input,select{background:var(--bg);color:var(--txt);border:1px solid var(--bd);border-radius:6px;padding:8px;width:100%;box-sizing:border-box;font-size:1em}
input[type=checkbox]{width:auto;transform:scale(1.3)} .row{display:flex;gap:10px;flex-wrap:wrap}.row>div{flex:1;min-width:130px}
button{background:#238636;color:#fff;border:0;border-radius:6px;padding:10px 16px;cursor:pointer;font-size:.95em;margin-top:10px}
button.blau{background:#1f6feb} button.rot{background:#b62324;padding:6px 10px;margin:0} button.grau{background:var(--bd)}
table{width:100%;border-collapse:collapse;font-size:.92em} td,th{padding:6px 4px;border-bottom:1px solid var(--bd2);text-align:left;vertical-align:middle}
td input{padding:5px} .klein{font-size:.85em;color:var(--mut)} .ok{color:#3fb950}.aus{color:#f85149}.warn{color:#d29922}
details{margin-top:10px} summary{cursor:pointer;color:var(--acc)} .erw{display:none} body.erwei td.erw,body.erwei th.erw{display:table-cell} body.erwei div.erw{display:block}
.status{font-size:1.05em;margin-top:12px} .kand label{display:flex;gap:10px;align-items:center;margin:0;padding:9px 4px;border-bottom:1px solid var(--bd2);color:var(--txt);font-size:.95em}
</style></head><body>
)HTML" WEBUI_NAVBAR R"HTML(
<h1>Sensoren im Haus</h1>
<div class="card"><p style="margin-top:0">Hier zeigst du Messwerte aus deinem Haus auf dem WetterCube an, zum Beispiel Temperatur und Luftfeuchte in einzelnen R&auml;umen. Das geht mit Sensoren, die ihre Werte per <b>MQTT</b> senden (z.&nbsp;B. ESPHome). Home Assistant brauchst du daf&uuml;r nicht.</p>
<details><summary>Was ist MQTT und was brauche ich daf&uuml;r?</summary>
<p>MQTT ist wie ein <b>schwarzes Brett im Heimnetz</b>: Deine Sensoren h&auml;ngen dort ihre Messwerte auf, der WetterCube liest sie ab. Das schwarze Brett l&auml;uft meist schon in deinem Smart Home, bei Home Assistant als Zusatz <b>&bdquo;Mosquitto broker&ldquo;</b>. Du brauchst nur die <b>Adresse</b> (IP-Adresse) des Ger&auml;ts, auf dem es l&auml;uft.</p>
<p class="klein">Hast du keine Sensoren oder kein Smart Home, lass diese Seite einfach, der WetterCube funktioniert auch ohne. Der interne Sensor im Geh&auml;use (falls eingebaut) erscheint von selbst unten in der Liste.</p></details></div>

<div class="card"><h2><span class="nr">1</span>Verbinden</h2>
<label>Adresse deines Smart Homes (IP-Adresse des Ger&auml;ts mit dem MQTT-Dienst)</label>
<input id="mq_host" placeholder="z. B. 192.168.178.20" autocapitalize="off" autocorrect="off">
<p class="klein">Tipp: Die IP-Adresse findest du in deinem Router unter &bdquo;Heimnetz&ldquo; oder in Home Assistant unter Einstellungen &rarr; System &rarr; Netzwerk. Eine Zahlenadresse klappt zuverl&auml;ssiger als ein Name.</p>
<details><summary>Ist die Zentrale mit Benutzername und Passwort gesch&uuml;tzt?</summary>
<div class="row"><div><label>Benutzername</label><input id="mq_user" autocomplete="off"></div><div><label>Passwort</label><input id="mq_pass" type="password" autocomplete="new-password"></div><div><label>Port (meist nicht &auml;ndern)</label><input id="mq_port" type="number" value="1883"></div></div>
<p class="klein">Lege am besten einen eigenen Zugang nur zum Lesen f&uuml;r den WetterCube an. Das Passwort bleibt auf dem Ger&auml;t und steht nie in der Einstellungssicherung.</p></details>
<button onclick="verbinden()">Verbinden</button> <button class="grau" id="trennen" onclick="trennen()" style="display:none">Trennen</button>
<div class="status" id="status"></div></div>

<div class="card" id="schritt2"><h2><span class="nr">2</span>Sensoren ausw&auml;hlen</h2>
<p class="klein" style="margin-top:0">Sobald die Verbindung steht, erscheinen hier alle Sensoren, die sich im Netz melden. Haken setzen und &bdquo;Hinzuf&uuml;gen&ldquo; dr&uuml;cken.</p>
<div class="kand" id="kand"></div>
<p class="klein" id="kand_leer">Noch nichts gefunden. Das kann nach dem Verbinden bis zu einer Minute dauern. Meldet sich nichts, pr&uuml;fe, ob deine Sensoren per MQTT senden (bei ESPHome braucht die Konfiguration einen Abschnitt <code>mqtt:</code>).</p>
<label style="margin-top:8px"><input type="checkbox" id="zusammen" checked> Werte desselben Ger&auml;ts in einer Kachel zusammenfassen (kannst du sp&auml;ter in Schritt 3 &auml;ndern)</label>
<button class="blau" id="hinzu" onclick="hinzufuegen()" style="display:none">Ausgew&auml;hlte hinzuf&uuml;gen</button>
<label style="margin-top:12px"><input type="checkbox" id="technisch" onchange="zeichne()"> auch technische Werte zeigen (WLAN-Signal, Laufzeit &hellip;)</label></div>

<div class="card"><h2><span class="nr">3</span>Das zeigt der WetterCube an</h2>
<p class="klein" style="margin-top:0">Trage bei <b>Kachel</b> f&uuml;r mehrere Sensoren denselben Namen ein, dann erscheinen ihre Werte <b>zusammen in einer Kachel</b> (zum Beispiel Temperatur und Luftfeuchte eines Raums). Bleibt das Feld leer, bekommt der Sensor eine eigene Kachel.</p>
<div><button class="grau" style="margin:0 6px 8px 0" onclick="proRaum()">Pro Raum zusammenfassen</button><button class="grau" style="margin:0 0 8px 0" onclick="alleEinzeln()">Alle einzeln anzeigen</button></div>
<div style="overflow-x:auto"><table id="liste"><thead><tr><th>Anzeigen</th><th>Name</th><th>Raum</th><th>Kachel</th><th>Wert</th><th title="Warnen, wenn der Wert unter oder über diese Grenze geht (leer = aus)">Warnen unter / über</th><th class="erw">Einheit</th><th class="erw">Icon</th><th class="erw">Korrektur</th><th class="erw">Reihenf.</th><th></th></tr></thead><tbody></tbody></table><datalist id="kacheln"></datalist></div>
<p class="klein" id="liste_leer">Noch keine Sensoren ausgew&auml;hlt.</p>
<button onclick="speichern()">Speichern</button> <span id="msg" class="klein"></span>
<p class="klein"><b>Warnen unter / &uuml;ber:</b> Tr&auml;gst du hier Grenzen ein (zum Beispiel Luftfeuchte &uuml;ber 65), f&auml;rbt sich die Kachel auf dem Display rot, der Hauptscreen zeigt einen Hinweis, und bei eingerichtetem <a href="/#ampel">ntfy</a> kommt eine Nachricht aufs Handy. Der Alarm beginnt, wenn die Grenze mehr als eineinhalb Minuten verletzt ist, und endet erst etwas innerhalb der Grenze, damit er nicht flackert. Leer lassen = keine Warnung.</p>
<p class="klein">Nach dem Speichern erscheinen die Sensoren auf der Sensor-Seite des Displays. Ein gr&uuml;ner Punkt bedeutet: Der Sensor meldet sich, ein roter: Er schweigt seit mehr als 15 Minuten oder ist offline.</p>
<label style="margin-top:10px"><input type="checkbox" id="erweitert" onchange="document.body.classList.toggle('erwei',this.checked)"> Erweiterte Einstellungen zeigen (Icon, Korrektur, Reihenfolge, Abgleich, H&ouml;he)</label>
<div class="erw2" id="erw2" style="display:none">
<label>H&ouml;he &uuml;ber dem Meeresspiegel in Metern (f&uuml;r den Luftdruck des internen Sensors)</label><input id="hoehe" type="number" step="1" value="0" style="max-width:200px">
<p class="klein">Korrektur wird zum Messwert addiert. ESPHome-Sensoren sind meist schon korrigiert, dann 0 lassen. Beim Abgleich tr&auml;gst du den Wert eines Vergleichsger&auml;ts ein, die Korrektur wird berechnet.</p>
<details><summary>Sensor von Hand hinzuf&uuml;gen</summary>
<div class="row"><div><label>Name</label><input id="m_name"></div><div><label>Raum</label><input id="m_raum"></div><div><label>Einheit</label><input id="m_einh" placeholder="&deg;C"></div></div>
<label>MQTT-Topic des Messwerts (z.&nbsp;B. wohnzimmer/sensor/temperatur/state)</label><input id="m_topic"><button class="blau" onclick="handHinzu()">Hinzuf&uuml;gen</button></details></div></div>

<script>
var S={sensoren:[],kandidaten:[],mqtt:{}},sel={},LEER=document.getElementById('kand_leer').innerHTML;
var ST={0:['','Noch nicht verbunden.'],1:['warn','&#9679; Verbinde &hellip;'],2:['ok','&#9679; Verbunden'],3:['aus','&#9679; Anmeldung abgelehnt: Benutzername oder Passwort stimmen nicht.'],4:['aus','&#9679; Nicht erreichbar: Adresse und WLAN pr&uuml;fen.']};
function $(i){return document.getElementById(i);}
function esc(t){return String(t==null?'':t).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
function schoen(t){t=String(t||'').replace(/[-_]+/g,' ').trim();return t.replace(/\b\w/g,function(c){return c.toUpperCase();});}
function laden(){fetch('/api/sensoren').then(function(r){return r.json();}).then(function(d){S=d;
 if(document.activeElement.tagName!=='INPUT'||!$('mq_host').value){$('mq_host').value=d.mqtt.host||'';$('mq_port').value=d.mqtt.port||1883;$('mq_user').value=d.mqtt.user||'';}
 $('mq_pass').placeholder=d.mqtt.hat_passwort?'(gespeichert)':'';$('hoehe').value=d.hoehe;
 var s=ST[d.mqtt.status]||ST[0];$('status').className='status '+s[0];$('status').innerHTML=s[1];
 $('trennen').style.display=d.mqtt.aktiv?'inline-block':'none';
 zeichne();});}
function technisch(k){return /dbm|^s$|^$/i.test(k.einheit||'')||/uptime|signal|wifi|rssi|ssid|mac|ip adr|laufzeit/i.test(k.name||'');}
function zeichne(){var tb=document.querySelector('#liste tbody');tb.innerHTML='';
 S.sensoren.forEach(function(s,i){var tr=document.createElement('tr');
  var wert=s.gueltig?(s.wert.toFixed(s.dezimal)+' '+esc(s.einheit)):'<span class="klein">noch kein Wert</span>';
  var stat=s.gueltig?(s.online?'<span class="ok">&#9679;</span>':'<span class="aus">&#9679;</span>'):'';
  tr.innerHTML='<td><input type="checkbox" '+(s.sichtbar?'checked':'')+' onchange="S.sensoren['+i+'].sichtbar=this.checked?1:0"></td>'+
  '<td><input value="'+esc(s.name)+'" onchange="S.sensoren['+i+'].name=this.value"><div class="klein erw">'+(s.quelle==0?esc(s.topic):'intern')+'</div></td>'+
  '<td><input value="'+esc(s.raum)+'" size="8" onchange="S.sensoren['+i+'].raum=this.value"></td>'+
  '<td><input list="kacheln" value="'+esc(s.gruppe||'')+'" size="9" maxlength="19" placeholder="eigene" onchange="S.sensoren['+i+'].gruppe=this.value.trim().slice(0,19)"></td>'+
  '<td>'+stat+' '+wert+(s.alarm?' <b class="aus">&#9888; Alarm</b>':'')+'</td>'+
  '<td style="white-space:nowrap"><input type="number" step="any" placeholder="aus" style="width:62px" value="'+(s.grenze_min==null?'':s.grenze_min)+'" onchange="S.sensoren['+i+'].grenze_min=this.value===\'\'?null:parseFloat(this.value)"> / '+
  '<input type="number" step="any" placeholder="aus" style="width:62px" value="'+(s.grenze_max==null?'':s.grenze_max)+'" onchange="S.sensoren['+i+'].grenze_max=this.value===\'\'?null:parseFloat(this.value)"></td>'+
  '<td class="erw"><input value="'+esc(s.einheit)+'" size="4" onchange="S.sensoren['+i+'].einheit=this.value"></td>'+
  '<td class="erw"><select onchange="S.sensoren['+i+'].icon=this.value" style="width:110px"><option value="">auto ('+esc(s.icon_aktiv||'')+')</option>'+(S.icons||[]).map(function(n){return '<option value="'+n+'"'+(s.icon==n?' selected':'')+'>'+n+'</option>';}).join('')+'</select></td>'+
  '<td class="erw"><input type="number" step="0.1" value="'+s.offset+'" style="width:70px" onchange="S.sensoren['+i+'].offset=parseFloat(this.value)||0"> <button class="grau" style="margin:0;padding:4px 8px" onclick="abgleich('+i+')">Abgleich</button></td>'+
  '<td class="erw"><input type="number" value="'+s.reihenfolge+'" style="width:55px" onchange="S.sensoren['+i+'].reihenfolge=parseInt(this.value)||0"></td>'+
  '<td><button class="rot" title="Entfernen" onclick="weg('+i+')">&times;</button></td>';
  tb.appendChild(tr);});
 var gn={};S.sensoren.forEach(function(s){if(s.gruppe)gn[s.gruppe]=1;});$('kacheln').innerHTML=Object.keys(gn).map(function(n){return '<option value="'+esc(n)+'">';}).join('');
 $('liste_leer').style.display=S.sensoren.length?'none':'block';
 var kb=$('kand');kb.innerHTML='';var n=0;
 S.kandidaten.forEach(function(k,i){if(S.sensoren.some(function(s){return s.topic==k.topic;}))return;if(!$('technisch').checked&&technisch(k))return;n++;
  var l=document.createElement('label');l.innerHTML='<input type="checkbox" '+(sel[k.topic]?'checked':'')+' onchange="sel[S.kandidaten['+i+'].topic]=this.checked">'+
   '<span><b>'+esc(schoen(k.geraet)||'Sensor')+'</b>: '+esc(schoen(k.name))+(k.einheit?' <span class="klein">('+esc(k.einheit)+')</span>':'')+'</span>';kb.appendChild(l);});
 $('kand_leer').style.display=n?'none':'block';$('hinzu').style.display=n?'inline-block':'none';
 $('kand_leer').innerHTML=S.kandidaten.length&&!n?'Alle gefundenen Sensoren sind schon hinzugef&uuml;gt.':LEER;}
$('erweitert').addEventListener('change',function(){$('erw2').style.display=this.checked?'block':'none';});
function slug(t){return t.toLowerCase().replace(/[^a-z0-9]+/g,'_').replace(/^_|_$/g,'');}
function kurz(t){var h=5381;for(var i=0;i<t.length;i++)h=((h<<5)+h+t.charCodeAt(i))>>>0;return h.toString(36).slice(0,6);}
function neu(name,raum,einheit,topic,avail,gruppe){if(S.sensoren.length>=16){alert('Mehr als 16 Sensoren sind nicht möglich.');return false;}
 S.sensoren.push({id:(slug(raum+'_'+name).slice(0,15)||'sensor')+'_'+kurz(topic),name:name.slice(0,23),raum:raum.slice(0,19),gruppe:(gruppe||'').slice(0,19),grenze_min:null,grenze_max:null,einheit:einheit,topic:topic,avail:avail||'',json_key:'',icon:'',quelle:0,sichtbar:1,reihenfolge:S.sensoren.length,dezimal:1,offset:0,gueltig:false});return true;}
function raumName(g){var r=schoen(g).replace(/^(Klima|Raum)?sensor\s+/i,'');return r||schoen(g);}
function hinzufuegen(){var z={};S.kandidaten.forEach(function(k){if(sel[k.topic]&&!S.sensoren.some(function(s){return s.topic==k.topic;}))z[k.geraet]=(z[k.geraet]||0)+1;});
 var zus=$('zusammen').checked;S.kandidaten.forEach(function(k){if(sel[k.topic]&&!S.sensoren.some(function(s){return s.topic==k.topic;}))neu(schoen(k.name),raumName(k.geraet),k.einheit,k.topic,k.avail,zus&&z[k.geraet]>1?raumName(k.geraet):'');sel[k.topic]=false;});speichern();}
function proRaum(){var c={};S.sensoren.forEach(function(s){if(s.raum)c[s.raum]=(c[s.raum]||0)+1;});S.sensoren.forEach(function(s){s.gruppe=(s.raum&&c[s.raum]>1)?s.raum.slice(0,19):'';});zeichne();$('msg').textContent='Noch nicht gespeichert: bitte unten auf Speichern dr\u00fccken.';}
function alleEinzeln(){S.sensoren.forEach(function(s){s.gruppe='';});zeichne();$('msg').textContent='Noch nicht gespeichert: bitte unten auf Speichern dr\u00fccken.';}
function handHinzu(){if(!$('m_topic').value||!$('m_name').value){alert('Name und Topic ausfüllen.');return;}if(neu($('m_name').value,$('m_raum').value,$('m_einh').value,$('m_topic').value))speichern();}
function weg(i){if(confirm('Sensor entfernen?')){S.sensoren.splice(i,1);speichern();}}
function abgleich(i){var s=S.sensoren[i];if(!s.gueltig){alert('Noch kein Messwert vorhanden.');return;}
 var r=prompt('Referenzwert für "'+s.name+'" (aktuell '+s.wert.toFixed(s.dezimal)+' '+s.einheit+'):');if(r===null)return;r=parseFloat(String(r).replace(',','.'));if(isNaN(r))return;
 fetch('/api/sensoren/abgleich',{method:'POST',body:JSON.stringify({id:s.id,referenz:r})}).then(function(){laden();});}
function senden(body,fertig){fetch('/api/sensoren',{method:'POST',body:JSON.stringify(body)}).then(function(r){return r.json();}).then(fertig).catch(function(){$('msg').textContent='Fehler: Gerät nicht erreichbar';});}
function verbinden(){var h=$('mq_host').value.trim();if(!h){alert('Bitte zuerst die Adresse eintragen.');return;}
 var m={host:h,port:parseInt($('mq_port').value)||1883,user:$('mq_user').value,aktiv:1};var p=$('mq_pass').value;if(p)m.pass=p;
 $('status').className='status warn';$('status').innerHTML=ST[1][1];
 senden({mqtt:m},function(){$('mq_pass').value='';var n=0;var t=setInterval(function(){laden();if(++n>15)clearInterval(t);},2000);});}
function trennen(){senden({mqtt:{aktiv:0}},function(){setTimeout(laden,500);});}
function speichern(){var body={hoehe:parseFloat($('hoehe').value)||0,sensoren:S.sensoren.map(function(s){return {id:s.id,name:s.name,raum:s.raum,gruppe:s.gruppe||'',grenze_min:s.grenze_min==null?null:s.grenze_min,grenze_max:s.grenze_max==null?null:s.grenze_max,einheit:s.einheit,topic:s.topic,avail:s.avail,json_key:s.json_key,icon:s.icon||'',quelle:s.quelle,sichtbar:s.sichtbar,reihenfolge:s.reihenfolge,dezimal:s.dezimal,offset:s.offset};})};
 senden(body,function(d){$('msg').textContent=d.ok?'Gespeichert ('+d.n+' Sensoren).':'Fehler beim Speichern';setTimeout(laden,1000);});}
laden();setInterval(function(){if(!document.hidden&&document.activeElement.tagName!=='INPUT')laden();},5000);
</script></body></html>)HTML";

void sensorenWebuiRegistrieren(WebServer& server) {
  server.on("/sensoren", HTTP_GET, [&server]() { server.send_P(200, "text/html; charset=utf-8", SENSOREN_HTML); });
  server.on("/api/sensoren", HTTP_GET, [&server]() {
    std::string j = sensorenZustandJson();
    server.send(200, "application/json; charset=utf-8", j.c_str());
  });
  server.on("/api/sensoren", HTTP_POST, [&server]() {
    String body = server.arg("plain");
    int n = body.length() ? sensorenAusJson(body.c_str(), true) : -1;
    if (n < 0) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    server.send(200, "application/json", String("{\"ok\":true,\"n\":") + n + "}");
  });
  server.on("/api/sensoren/abgleich", HTTP_POST, [&server]() {
    JsonDocument doc;
    String body = server.arg("plain");
    bool ok = deserializeJson(doc, body) == DeserializationError::Ok && sensorenAbgleich(doc["id"] | "", doc["referenz"] | 0.0f);
    if (ok) sensorenSpeichern();
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
}
