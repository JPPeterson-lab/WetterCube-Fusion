// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Weboberfläche Schimmelwächter: Seite /schimmel und /api/schimmel*
#include "schimmel.h"
#if SCHIMMEL_VERFUEGBAR
#include "webui_theme.h"
#include "../platform/WebServer.h"
#include <ArduinoJson.h>

static const char SCHIMMEL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>WetterCube Fusion – Schimmelwächter</title>
)HTML" WEBUI_THEME_HEAD WEBUI_THEME_CSS R"HTML(
<style>
body{font-family:Arial,sans-serif;background:var(--bg);color:var(--txt);margin:0;padding:14px;max-width:960px;margin:auto;line-height:1.45}
h1{font-size:1.4em} h2{font-size:1.1em;margin:0 0 8px} .card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;margin-bottom:14px}
.klein{font-size:.85em;color:var(--mut)} select{background:var(--bg);color:var(--txt);border:1px solid var(--bd);border-radius:6px;padding:6px;font-size:.95em}
input[type=checkbox]{transform:scale(1.25);margin-right:6px} label.cb{display:inline-flex;align-items:center;margin:4px 12px 4px 0}
.zeile{display:flex;align-items:center;gap:8px;margin:8px 0;flex-wrap:wrap}
button{background:var(--btn);color:#fff;border:0;padding:8px 14px;cursor:pointer;font-size:.92em;margin:6px 6px 0 0}button.grau{background:var(--bd);color:var(--txt);padding:4px 12px;margin:0}
.r{border:1px solid var(--bd);border-radius:10px;padding:12px;margin:10px 0;background:var(--bg)}.r h3{margin:0 0 6px;font-size:1.05em;display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.pille{border-radius:999px;padding:2px 12px;color:#fff;font-size:.8em;font-weight:700}.p0{background:var(--gruen)}.p1{background:var(--amber)}.p2{background:var(--rot)}.p3{background:#8b1d1d}
.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}.g div{background:var(--card);border:1px solid var(--bd2);border-radius:8px;padding:8px}.g b{display:block;font-size:1.2em}
.leiste{position:relative;height:14px;border-radius:7px;background:linear-gradient(90deg,var(--gruen) 0 70%,var(--amber) 70% 80%,var(--rot) 80% 90%,#8b1d1d 90% 100%);margin:8px 0 2px}.leiste i{position:absolute;top:-4px;width:4px;height:22px;background:var(--txt);border-radius:2px}
</style></head><body>
)HTML" WEBUI_NAVBAR R"HTML(
<h1>Schimmelwächter</h1><p class="klein" style="margin-top:-6px">Nur beim WetterCube Fusion mit ESP32-P4.</p>
<div class="card"><div class="zeile"><label class="cb"><input type="checkbox" id="an" onchange="speichern()"><b>Schimmelwächter einschalten</b></label></div>
<p class="klein" style="margin:0">Schimmel wächst an der kältesten Stelle der Außenwand, nicht in der Raummitte. Der Fusion schätzt aus Raumluft und Außentemperatur, wie feucht es dort wird: ab 80 % Wandfeuchte über mehrere Tage droht Schimmel. Nutzt dieselben Raumsensoren (MQTT) wie die Lüften-Ampel.</p>
<div class="zeile"><span>Wandtyp</span><select id="frsi" onchange="speichern()"><option value="0.9">sehr gut gedämmt, Neubau (0,90)</option><option value="0.85">gut gedämmt (0,85)</option><option value="0.75">normal (0,75)</option><option value="0.65">Altbau, kalte Außenwand (0,65)</option><option value="0.55">Außenecke, Möbel davor (0,55)</option></select></div></div>
<div class="card" id="anzeigeKarte" style="display:none"><h2>Anzeige und Reihenfolge</h2>
<p class="klein" style="margin:0 0 8px">Welche Räume am Display gezeigt werden und in welcher Reihenfolge. Mehr als vier Räume verteilen sich auf mehrere Seiten, am Display mit den Tasten &lt; und &gt;. Neue Räume erscheinen hier abgehakt oder aus, je nach Auswahl.</p>
<div id="anzeige"></div>
<p><button onclick="anzSpeichern()">Speichern</button><button class="grau" onclick="anzAlle()">Alle Räume zeigen</button> <span id="amsg" class="klein"></span></p></div>
<div id="raeume"></div>
<p class="klein"><b>Genauer mit einer Messung:</b> Miss mit einem Infrarot-Thermometer die kälteste Stelle der Außenwand im Raum (Ecke, Fensterlaibung, hinter einem Schrank) und trag den Wert beim Raum unter „Wand gemessen“ ein. Der Fusion rechnet daraus mit der Innen- und Außentemperatur dieses Moments den Faktor für diesen Raum aus. Am besten an einem kalten Tag messen, innen und außen sollten mindestens 8 °C auseinanderliegen.</p>
<p class="klein">Orientierung nach einfacher Rechnung (Temperaturfaktor, Magnus-Formel), kein Gutachten. Hohe Stufen bitte ernst nehmen: stoßlüften, Möbel von der Außenwand abrücken, Raum gleichmäßig heizen.</p>
<script>
var D={};function $(i){return document.getElementById(i)}function esc(t){return String(t==null?'':t).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function api(p,b,cb){var x=new XMLHttpRequest();x.open(b?'POST':'GET',p);x.setRequestHeader('Content-Type','application/json');x.onload=function(){var j={};try{j=JSON.parse(x.responseText)}catch(e){}cb&&cb(j)};x.send(b?JSON.stringify(b):null)}
function laden(){api('/api/schimmel',null,function(d){D=d;if(document.activeElement!==$('an'))$('an').checked=d.an;if(document.activeElement!==$('frsi')){var fv=(+d.frsi);$('frsi').value=fv===0.9?'0.9':String(fv.toFixed(2))}var ae=document.activeElement;if(!(ae&&ae.id&&ae.id.indexOf('mw_')===0))zeichne();anzBauen()})}
function speichern(){api('/api/schimmel/einstellung',{an:$('an').checked,frsi:+$('frsi').value},function(){setTimeout(laden,1200)})}
var A=[];window.aBearb=0;
function anzBauen(){$('anzeigeKarte').style.display=D.an?'block':'none';if(!D.an||window.aBearb)return;var al=D.alle||[],re=D.reihe||[];A=[];
 if(D.reiheGesetzt){re.forEach(function(n){A.push({n:n,an:1})});al.forEach(function(n){if(re.indexOf(n)<0)A.push({n:n,an:0})})}else al.forEach(function(n){A.push({n:n,an:1})});anzZeichne()}
function anzZeichne(){var h='';A.forEach(function(r,i){h+='<div class="zeile"><label class="cb"><input type="checkbox" '+(r.an?'checked ':'')+'onchange="A['+i+'].an=this.checked?1:0;window.aBearb=1"><b>'+esc(r.n)+'</b></label><button class="grau" onclick="schieb('+i+',-1)">&uarr;</button><button class="grau" onclick="schieb('+i+',1)">&darr;</button></div>'});$('anzeige').innerHTML=h||'<span class="klein">Noch keine Räume bekannt (Sensoren mit Raum, Temperatur und Feuchte nötig).</span>'}
function schieb(i,r){var j=i+r;if(j<0||j>=A.length)return;var t=A[i];A[i]=A[j];A[j]=t;window.aBearb=1;anzZeichne()}
function anzSpeichern(){var re=A.filter(function(r){return r.an}).map(function(r){return r.n});api('/api/schimmel/einstellung',{reihe:re},function(j){$('amsg').textContent=j.ok?'Gespeichert.':(j.fehler||'Fehler');if(j.ok)window.aBearb=0;setTimeout(laden,600)})}
function anzAlle(){api('/api/schimmel/einstellung',{reiheAlle:true},function(j){$('amsg').textContent=j.ok?'Alle Räume werden gezeigt.':(j.fehler||'Fehler');window.aBearb=0;setTimeout(laden,600)})}
function mess(r){var v=($('mw_'+r).value||'').replace(',','.');if(!v)return;api('/api/schimmel/einstellung',{messung:{raum:r,wand:+v}},function(j){alert(j.ok?'Gespeichert. Der Faktor für '+r+' wird jetzt aus deiner Messung berechnet.':(j.fehler||'Fehler'));laden()})}
function messWeg(r){if(confirm('Messung für '+r+' löschen? Dann gilt wieder der Wandtyp oben.'))api('/api/schimmel/einstellung',{messungLoeschen:r},function(){laden()})}
var ST=['in Ordnung','erhöht','Schimmelgefahr','akut'];
function zeichne(){var h='';if(!D.an){$('raeume').innerHTML='';return}
 if(!D.aussenBekannt)h='<div class="card klein">Noch keine Außentemperatur oder keine Räume mit Temperatur- und Feuchtesensor (Seite „Sensoren“, Raum eintragen).</div>';
 else if(!(D.raeume||[]).length)h='<div class="card klein">Kein Raum mit Temperatur- und Feuchtesensor gefunden.</div>';
 (D.raeume||[]).forEach(function(r){h+='<div class="r"><h3>'+esc(r.raum)+' <span class="pille p'+r.stufe+'">'+ST[r.stufe]+'</span></h3><div class="g"><div>Raum<b>'+r.temp.toFixed(1)+' °C · '+r.feuchte.toFixed(0)+' %</b></div><div>Taupunkt<b>'+r.taupunkt.toFixed(1)+' °C</b></div><div>kalte Wand (geschätzt)<b>'+r.wandTemp.toFixed(1)+' °C</b></div><div>Feuchte an der Wand<b>'+r.wandFeuchte.toFixed(0)+' %</b></div></div>'+
  '<div class="leiste"><i style="left:calc('+Math.min(100,r.wandFeuchte)+'% - 2px)"></i></div><div class="klein" style="display:flex;justify-content:space-between"><span>0 %</span><span>70</span><span>80</span><span>90</span><span>100 %</span></div>'+
  '<div class="zeile klein" style="margin-top:8px">Wand: '+(r.frsiGemessen?'<b>gemessen</b>, Faktor '+r.frsi.toFixed(2).replace('.',',')+' <button class="grau" onclick="messWeg(\''+esc(r.raum)+'\')">Messung löschen</button>':'Wandtyp oben (Faktor '+r.frsi.toFixed(2).replace('.',',')+')')+
  ' &nbsp; <input type="text" size="5" id="mw_'+esc(r.raum)+'" placeholder="°C"> <button class="grau" onclick="mess(\''+esc(r.raum)+'\')">Wand gemessen</button></div>'+
  '<div class="klein" style="margin-top:6px">Raumfeuchte unter <b>'+r.maxFeuchte.toFixed(0)+' %</b> halten, damit die Wand unter 80 % bleibt. Heute: '+r.stundenErhoeht.toFixed(1)+' h erhöht, '+r.stundenKritisch.toFixed(1)+' h kritisch (seit Programmstart bzw. 0 Uhr), höchste Wandfeuchte '+r.hoechstWand.toFixed(0)+' %.</div></div>'});
 h+=D.aussenBekannt?'<div class="klein">Außen: '+D.aussen.toFixed(1)+' °C, '+D.aussenFeuchte.toFixed(0)+' % Feuchte ('+(D.aussenQuelle?'gemessen: '+esc(D.aussenQuelle):'Wetterdaten')+').</div>':'';$('raeume').innerHTML=h}
wcThemeLabel();laden();setInterval(laden,30000);
</script>
</body></html>
)HTML";

void schimmelWebuiRegistrieren(WebServer& server) {
  server.on("/schimmel", HTTP_GET, [&server]() { server.send_P(200, "text/html; charset=utf-8", SCHIMMEL_HTML); });
  server.on("/api/schimmel", HTTP_GET, [&server]() { server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json; charset=utf-8", schimmelJson().c_str()); });
  server.on("/api/schimmel/einstellung", HTTP_POST, [&server]() {
    std::string fe; bool ok = schimmelEinstellungSetzen(server.arg("plain").c_str(), &fe);
    JsonDocument d; d["ok"] = ok; if (!ok) d["fehler"] = fe; std::string o; serializeJson(d, o); server.send(200, "application/json; charset=utf-8", o.c_str());
  });
}
#else
void schimmelWebuiRegistrieren(WebServer&) {}
#endif
