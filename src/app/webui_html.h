// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
#include "webui_theme.h"
// HTML-Strings in .h ausgelagert, da der Arduino-Präprozessor
// Raw-String-Literals in .ino falsch parst (beendet String bei " inmitten von R"...").

const char PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="de"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WetterCube Fusion – Einrichtung</title>
<style>
  body{font-family:Arial,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:20px}
  h1{color:#58a6ff;font-size:1.4em}
  .card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px;max-width:420px;margin:auto}
  label{display:block;margin-top:12px;color:#8b949e;font-size:.9em}
  input,select{width:100%;padding:8px;border-radius:4px;border:1px solid #30363d;
    background:#21262d;color:#e6edf3;box-sizing:border-box;margin-top:4px}
  button{margin-top:20px;width:100%;padding:10px;background:#238636;color:#fff;
    border:none;border-radius:4px;font-size:1em;cursor:pointer}
  button:hover{background:#2ea043}
  .hint{font-size:.8em;color:#8b949e;margin-top:4px}
</style></head><body>
<div class="card">
  <h1>WetterCube Fusion Einrichtung</h1>
  <form action="/speichern" method="post">
    <label>WLAN-Netzwerk</label>
    <select name="ssid">%NETZWERKE%</select>
    <label>WLAN-Passwort</label>
    <input type="password" name="pass" placeholder="Passwort eingeben">
    <label>Standort (Stadt)</label>
    <input type="text" name="location" placeholder="z.B. Berlin" value="%LOCATION%">
    <p class="hint">Keine Umlaute: Munchen statt München, Koln statt Koeln</p>
    <label>DWD-Pollenregion</label>
    <select name="dwd_region">%DWD_REGIONEN%</select>
    <button type="submit">Speichern &amp; Neustart</button>
  </form>
</div></body></html>
)rawhtml";

const char PORTAL_OK_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="de"><head>
<meta charset="UTF-8"><title>Gespeichert</title>
<meta http-equiv="refresh" content="5;url=/">
<style>
  body{font-family:Arial;background:#0d1117;color:#e6edf3;text-align:center;padding-top:60px}
  .ok{color:#2ea043;font-size:2em}
</style></head><body>
<div class="ok">Gespeichert</div>
<p>Geraet wird neu gestartet...</p>
</body></html>
)rawhtml";

const char WEBUI_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="de"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WetterCube Fusion</title>
)rawhtml" WEBUI_THEME_HEAD WEBUI_THEME_CSS R"rawhtml(
<style>
  *{box-sizing:border-box}
  body{font-family:Arial,sans-serif;background:var(--bg);color:var(--txt);margin:0;padding:16px;max-width:860px;margin:auto}
  h1{color:var(--acc);margin:0 0 4px}
  .top{display:flex;justify-content:space-between;align-items:flex-start;gap:10px;margin-bottom:14px}
  .ver{color:var(--mut);font-size:.85em}
  .tabs{display:flex;gap:6px;overflow-x:auto;margin:0 0 16px;padding-bottom:4px;-webkit-overflow-scrolling:touch}
  .tabs a{flex:0 0 auto;padding:9px 16px;border-radius:8px;background:var(--tab);color:var(--txt);text-decoration:none;font-size:.95em;border:1px solid var(--bd);white-space:nowrap}
  .tabs a.aktiv{background:#1f6feb;border-color:#1f6feb;color:#fff}
  .tab{display:none}
  .card{background:var(--card);border:1px solid var(--bd);border-radius:8px;padding:16px;margin-bottom:16px}
  h2{color:var(--txt);font-size:1em;margin:0 0 12px}
  label{display:block;color:var(--mut);font-size:.85em;margin-top:10px}
  input[type=text],input[type=number],select{width:100%;padding:7px;border-radius:4px;
    border:1px solid var(--bd);background:var(--in);color:var(--txt);margin-top:3px}
  input[type=range]{width:100%;margin-top:6px}
  .row{display:flex;align-items:center;justify-content:space-between;margin-top:10px}
  .toggle{position:relative;width:44px;height:24px;flex-shrink:0}
  .toggle input{opacity:0;width:0;height:0}
  .slider{position:absolute;inset:0;background:var(--slider);border-radius:12px;cursor:pointer;transition:.3s}
  .slider::before{content:'';position:absolute;width:18px;height:18px;left:3px;bottom:3px;
    background:#fff;border-radius:50%;transition:.3s}
  input:checked+.slider{background:#238636}
  input:checked+.slider::before{transform:translateX(20px)}
  .btn{display:inline-block;padding:9px 16px;border:none;border-radius:4px;cursor:pointer;font-size:.9em;margin-top:6px;text-decoration:none}
  .btn-gruen{background:#238636;color:#fff}.btn-gruen:hover{background:#2ea043}
  .btn-blau{background:#1f6feb;color:#fff}.btn-blau:hover{background:#388bfd}
  .btn-rot{background:#b91c1c;color:#fff}.btn-rot:hover{background:#dc2626}
  .status{font-size:.85em;color:var(--mut);margin-top:6px}
  .hint{font-size:.78em;color:var(--mut);margin-top:3px}
  .row{display:flex;gap:10px;margin-top:8px}
  .col{flex:1}
  .savebar{position:sticky;bottom:0;background:var(--bg);border-top:1px solid var(--bd);padding:10px 0;margin-top:4px;display:flex;align-items:center;gap:12px;z-index:5}
  .savebar .btn{margin:0}
  #dirty{font-size:.85em;color:#d29922;display:none}
</style></head><body>
<div class="top">
  <div><h1>WetterCube Fusion</h1>
  <div class="ver">Firmware: %VERSION% | IP: %IP% | wettercube-fusion.local</div></div>
  <button id="wcThemeBtn" class="themebtn" onclick="wcTheme()" title="Hell oder dunkel"></button>
</div>

<div class="tabs" id="tabs">
  <a href="#anzeige" data-t="anzeige">Anzeige</a>
  <a href="#wetter" data-t="wetter">Wetter &amp; Daten</a>
  <a href="#sensoren" data-t="sensoren">Sensoren</a>
  <a href="#ampel" data-t="ampel">Smart Home</a>
  <a href="#system" data-t="system">System</a>
</div>

<form action="/speichern" method="post" id="einst">

<div class="tab" id="t-anzeige">
<div class="card">
  <h2>Display</h2>
  <label>Helligkeit: <span id="bv">%BRIGHT%</span>%</label>
  <input type="range" name="brightness" min="10" max="100" value="%BRIGHT%"
    oninput="document.getElementById('bv').textContent=this.value">
  <label>Dimm-Timeout</label>
  <select name="dim_time">
    <option value="0" %DT0%>Aus</option>
    <option value="-1" %DTT%>30 Sekunden (Test)</option>
    <option value="1" %DT1%>1 Minute</option>
    <option value="3" %DT3%>3 Minuten (Standard)</option>
    <option value="5" %DT5%>5 Minuten</option>
    <option value="10" %DT10%>10 Minuten</option>
  </select>
  <hr style="margin:14px 0;border-color:var(--bd)">
  <h3 style="margin:0 0 4px;font-size:1em">Tag- und Nachtmodus</h3>
  <label>Nachtmodus</label>
  <select name="night_modus" id="nm_sel" onchange="nachtUi()">
    <option value="0" %NMO0%>Aus</option>
    <option value="1" %NMO1%>Feste Uhrzeiten</option>
    <option value="2" %NMO2%>Nach Sonnenuntergang und Sonnenaufgang</option>
  </select>
  <div id="nm_zeit" style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:8px">
    <div>
      <label>Von</label>
      <select name="night_from">%NIGHT_FROM_OPTIONS%</select>
    </div>
    <div>
      <label>Bis</label>
      <select name="night_to">%NIGHT_TO_OPTIONS%</select>
    </div>
  </div>
  <div id="nm_sonne" style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:8px">
    <div>
      <label>Nacht beginnt (Minuten nach Sonnenuntergang)</label>
      <input type="number" name="night_sonne_ab" min="-180" max="180" step="5" value="%NSA%">
    </div>
    <div>
      <label>Nacht endet (Minuten nach Sonnenaufgang)</label>
      <input type="number" name="night_sonne_bis" min="-180" max="180" step="5" value="%NSB%">
    </div>
  </div>
  <p class="hint" style="margin:6px 0 0">%NIGHT_INFO%</p>
  <label style="margin-top:8px;display:block">Helligkeit nachts: <span id="nbv">%NBR%</span>%</label>
  <input type="range" name="night_bright" min="0" max="50" value="%NBR%"
    oninput="document.getElementById('nbv').textContent=this.value">
  <div class="row" style="margin-top:10px"><span>Nacht-Uhr: nachts nur die gro&szlig;e Uhr mit Datum und Au&szlig;entemperatur zeigen (Antippen zeigt den Hauptscreen)</span>
    <label class="toggle"><input type="checkbox" name="night_uhr" %NU%><span class="slider"></span></label></div>
  <div class="row" style="margin-top:10px"><span>Nachts dunkles Design (danach wieder das vorherige)</span>
    <label class="toggle"><input type="checkbox" name="night_theme" %NT%><span class="slider"></span></label></div>
  <label style="margin-top:10px">Sensor-Seite: Kacheln pro Seite</label>
  <select name="kacheln_seite">
    <option value="8" %KS8%>8 (gr&ouml;&szlig;er, besser lesbar)</option>
    <option value="12" %KS12%>12 (kompakt, weniger bl&auml;ttern)</option>
  </select>
  <p class="hint" style="margin:6px 0 0">Sind es mehr Sensoren und Schalter, gibt es mehrere Seiten: unten &bdquo;&lt; 1 / 2 &gt;&ldquo; antippen oder auf den Kacheln nach links und rechts wischen. Ein roter Punkt dort zeigt einen Alarm auf einer anderen Seite.</p>
  <div style="display:%ANIMD%">
  <div class="row" style="margin-top:10px"><span>Animierte Wettersymbole (Hauptsymbol und Nacht-Uhr)</span>
    <label class="toggle"><input type="checkbox" name="anim_symbole" %AS%><span class="slider"></span></label></div>
  <div class="row" style="margin-top:10px"><span>Auch die vier Stundensymbole animieren</span>
    <label class="toggle"><input type="checkbox" name="anim_stunden" %AH%><span class="slider"></span></label></div>
  </div>
  <hr style="margin:14px 0;border-color:var(--bd)">
  <h3 style="margin:0 0 4px;font-size:1em">Ruhezeit (keine Warnungen auf dem Display)</h3>
  <p class="hint" style="margin:0 0 6px">In der Ruhezeit blinkt nichts und es erscheinen keine Warnbildschirme, Hinweis-Kacheln oder DWD-Warnungen. Danach werden noch bestehende Warnungen wie gewohnt gezeigt. Handy-Meldungen (ntfy) laufen weiter.</p>
  <select name="ruhe_modus" id="rm_sel" onchange="ruheUi()">
    <option value="0" %RMO0%>Aus</option>
    <option value="1" %RMO1%>Feste Uhrzeiten</option>
    <option value="2" %RMO2%>Wie der Nachtmodus</option>
  </select>
  <div id="rm_zeit" style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:8px">
    <div><label>Von</label><select name="ruhe_von">%RUHE_VON_OPTIONS%</select></div>
    <div><label>Bis</label><select name="ruhe_bis">%RUHE_BIS_OPTIONS%</select></div>
  </div>
  <p class="hint" style="margin:6px 0 0">%RUHE_INFO%</p>
  <script>function ruheUi(){document.getElementById('rm_zeit').style.display=document.getElementById('rm_sel').value=='1'?'grid':'none';}ruheUi();</script>
  <script>function nachtUi(){var m=document.getElementById('nm_sel').value;document.getElementById('nm_zeit').style.display=m=='1'?'grid':'none';document.getElementById('nm_sonne').style.display=m=='2'?'grid':'none';}nachtUi();</script>
</div>

<div class="card">
  <h2>Hinweise auf dem Hauptscreen</h2>
  <p class="hint">Eine Kachel oben links zeigt Regen, Frost und starke UV-Strahlung. Neue Hinweise blinken, bis du die Kachel antippst; danach leuchtet sie dauerhaft, solange der Hinweis gilt. Bei mehreren Hinweisen hat Regen Vorrang, dann Frost, dann UV.</p>
  <div class="row"><span>Sensor-Grenzwerte (ein Sensor liegt &uuml;ber oder unter seiner Grenze, einstellbar auf der Sensoren-Seite)</span>
    <label class="toggle"><input type="checkbox" name="hw_sens" %HS%><span class="slider"></span></label></div>
  <div class="row"><span>Luftdruck f&auml;llt schnell (Wetterumschwung, 4 hPa in 3 Stunden)</span>
    <label class="toggle"><input type="checkbox" name="hw_druck" %HD%><span class="slider"></span></label></div>
  <div class="row"><span>Hei&szlig;er Sonnentag: Rolll&auml;den und Sonnenschutz (ab Tageshöchsttemperatur und UV 6)</span>
    <label class="toggle"><input type="checkbox" name="hw_schat" %HSC%><span class="slider"></span></label></div>
  <label>Hitze-Hinweis ab <span id="stv">%STT%</span> &deg;C Tageshöchsttemperatur</label>
  <input type="range" name="schatten_temp" min="22" max="35" value="%STT%" oninput="document.getElementById('stv').textContent=this.value">
  <div class="row"><span>Garten: Gie&szlig;en lohnt sich (April bis September, wenig Regen in den letzten 3 Tagen und keiner in Sicht)</span>
    <label class="toggle"><input type="checkbox" name="hw_giess" %HG%><span class="slider"></span></label></div>
  <div class="row"><span>Gef&uuml;hlte Temperatur deutlich k&auml;lter als gemessen (5 &deg;C oder mehr, zum Beispiel durch Wind)</span>
    <label class="toggle"><input type="checkbox" name="hw_gef" %HGF%><span class="slider"></span></label></div>
  <div class="row"><span>Regen</span>
    <label class="toggle"><input type="checkbox" name="hw_regen" %HR%><span class="slider"></span></label></div>
  <div class="row"><span>Frost (0 &deg;C oder darunter in den n&auml;chsten 12 Stunden)</span>
    <label class="toggle"><input type="checkbox" name="hw_frost" %HF%><span class="slider"></span></label></div>
  <div class="row"><span>UV-Index 6 oder h&ouml;her</span>
    <label class="toggle"><input type="checkbox" name="hw_uv" %HU%><span class="slider"></span></label></div>
</div>

<div class="card">
  <h2>Lüften und Raumklima</h2>
  <p class="hint">Die <b>Lüften-Ampel</b> vergleicht die Luft in deinen Räumen mit der Außenluft und sagt, ob Lüften jetzt sinnvoll ist (zum Beispiel: Raum feucht, draußen trockener und die Luft gut). Dafür braucht jeder Raum einen <b>Temperatur- und einen Feuchtesensor mit demselben Raumnamen</b> (Seite Sensoren). Die <b>Warnung</b> ist ein eigener Punkt: Bleibt ein Raum länger zu feucht, erscheint wie bei der Pollenwarnung ein Warnbildschirm mit dem Raum, der Feuchte und einem Rat; zum Bestätigen antippen.</p>
  <div class="row"><span>Lüften-Ampel auf der Sensor-Seite anzeigen</span>
    <label class="toggle"><input type="checkbox" name="lueft_ampel" %LA%><span class="slider"></span></label></div>
  <div class="row"><span>Warnbildschirm, wenn ein Raum zu feucht bleibt</span>
    <label class="toggle"><input type="checkbox" name="lueft_warnung" %LW%><span class="slider"></span></label></div>
  <div class="row"><span>Kühllüften im Sommer (warmer Raum: Fenster auf, wenn es draußen mindestens 2 &deg;C kühler ist; sonst Fenster und Rollläden zu)</span>
    <label class="toggle"><input type="checkbox" name="lueft_kuehlen" %LK%><span class="slider"></span></label></div>
  <label>Ein Raum gilt als warm ab <span id="lhv">%LH%</span> &deg;C</label>
  <input type="range" name="lueft_hitze" min="20" max="30" value="%LH%" oninput="document.getElementById('lhv').textContent=this.value">
  <label>Feuchte-Grenze: <span id="lfv">%LF%</span> % (ab hier &bdquo;zu feucht&ldquo;, 10 Punkte darüber &bdquo;Schimmelgefahr&ldquo;)</label>
  <input type="range" name="lueft_feuchte" min="50" max="80" value="%LF%"
    oninput="document.getElementById('lfv').textContent=this.value">
  <p class="hint" style="margin-top:6px">Die Warnung kommt erst, wenn die Grenze 15 Minuten überschritten ist (Schimmelgefahr nach 5 Minuten), damit Duschen oder Kochen keinen Alarm auslösen. Aufs Handy meldet sie ntfy (Reiter Smart Home).</p>
</div>

<div class="card">
  <h2>Temperaturverlauf</h2>
  <p class="hint">Der Screen nach dem Hauptscreen zeigt die Temperatur der n&auml;chsten 24 Stunden als Linie. Darunter zeigen Balken, wie viel es in jeder Stunde regnet.</p>
  <div class="row"><span>Regenbalken anzeigen (aus: das Temperaturdiagramm wird h&ouml;her)</span>
    <label class="toggle"><input type="checkbox" name="vl_regen" %VR%><span class="slider"></span></label></div>
</div>
<div class="card">
  <h2>Allergie-Tagebuch</h2>
  <input type="hidden" name="tagebuch_form" value="1">
  <div class="row"><span>Seite &bdquo;Allergie-Tagebuch&ldquo; anzeigen (aus: sie fehlt in der Reihe der Bildschirme; die Eintr&auml;ge bleiben erhalten)</span>
    <label class="toggle"><input type="checkbox" name="tagebuch_an" %TBA%><span class="slider"></span></label></div>
  <p class="hint">Eintr&auml;ge ansehen, erg&auml;nzen, l&ouml;schen und als CSV laden: <a href="/tagebuch">Tagebuch</a>.</p>
</div>
<div class="card">
  <h2>T&ouml;ne (Lautsprecher)</h2>
  <h3 style="margin:0 0 4px;font-size:1em">Wetter- und Pollenwarnung</h3>
  <div class="row"><span>Warnton bei neuer Warnung (nicht in der Ruhezeit)</span>
    <label class="toggle"><input type="checkbox" name="warn_ton_an" %WTA%><span class="slider"></span></label></div>
  <label>Warnton</label>
  <select name="warn_ton" id="tw_ton">%WARN_TON_OPTIONS%</select>
  <label>Lautst&auml;rke: <span id="wl_v">%WARN_LAUT%</span>%</label>
  <input type="range" name="warn_laut" id="tw_laut" min="0" max="100" value="%WARN_LAUT%" oninput="document.getElementById('wl_v').textContent=this.value">
  <div style="margin-top:10px" id="tw_test_zeile">
    <button type="button" class="btn" onclick="tonTest(true,'w')">Ton probeh&ouml;ren</button>
    <button type="button" class="btn" onclick="tonTest(false,'w')">Stopp</button>
  </div>
  <hr style="margin:14px 0;border-color:var(--bd)">
  <h3 style="margin:0 0 4px;font-size:1em">Timer und Stoppuhr</h3>
  <p class="hint">Erreichbar &uuml;ber das Men&uuml; (Taste Timer). Wenn die Zeit abl&auml;uft, blinkt das Display; mit Lautsprecher (P4) ert&ouml;nt zus&auml;tzlich ein Ton, bis du den Bildschirm antippst.</p>
  <label>Alarmton</label>
  <select name="timer_ton" id="tt_ton">%TIMER_TON_OPTIONS%</select>
  <label>Lautst&auml;rke: <span id="tl_v">%TIMER_LAUT%</span>%</label>
  <input type="range" name="timer_laut" id="tt_laut" min="0" max="100" value="%TIMER_LAUT%" oninput="document.getElementById('tl_v').textContent=this.value">
  <div style="margin-top:10px" id="tt_test_zeile">
    <button type="button" class="btn" onclick="tonTest(true,'t')">Ton probeh&ouml;ren</button>
    <button type="button" class="btn" onclick="tonTest(false,'t')">Stopp</button>
    <span class="hint" id="tt_msg"></span>
  </div>
  <p class="hint" id="tt_kein" style="display:none">Dieses Board hat keinen Lautsprecher: Der Alarm bleibt optisch (blinkendes Display).</p>
</div>
</div>

<div class="tab" id="t-wetter">
<div class="card">
  <h2>Standort &amp; Region</h2>
  <label>Stadt (ohne Umlaute: Munchen, Koeln)</label>
  <input type="text" name="location" value="%LOCATION%">
  <label>DWD Pollenregion</label>
  <select name="dwd_region">%DWD_REGIONEN%</select>
</div>

<!--ST2--><div class="card">
  <h2>Zweiter Standort</h2>
  <p class="hint">Optional, zum Beispiel Wohnort und Ferienhaus. Auf dem Hauptscreen schaltest du mit einem Tipp auf die Pille oben rechts zwischen beiden um. Wetter, 7-Tage-Vorschau, Pollen, Warnungen und Biowetter folgen dem gew&auml;hlten Ort. Die Sensoren und der Verlauf bleiben beim ersten Standort. Nach einem Neustart gilt wieder der erste Standort. Feld leeren = zweiten Standort entfernen.</p>
  <label>Stadt (ohne Umlaute: Munchen, Koeln)</label>
  <input type="text" name="location2" value="%LOCATION2%" placeholder="leer = kein zweiter Standort">
  <label>DWD Pollenregion</label>
  <select name="dwd_region2">%DWD_REGIONEN2%</select>
  <label>Biowetter-Zone</label>
  <select name="bio_zone2">%BIO_ZONE_OPTIONS2%</select>
  <p class="hint" id="so_status">%STANDORT_STATUS%</p>
  <button type="button" class="btn btn-blau" onclick="standortWechseln(0)">Standort 1 anzeigen</button>
  <button type="button" class="btn btn-blau" onclick="standortWechseln(1)">Standort 2 anzeigen</button>
</div>
<!--/ST2-->
<div class="card">
  <h2>Meine Allergene</h2>
  <p class="hint">Die Pollenwarnung, die Pollen-Ampel und die beiden Pollen auf dem Hauptscreen ber&uuml;cksichtigen nur die gew&auml;hlten Pollen. Hasel, Esche und Roggen kommen aus der DWD-Tagesvorhersage, die &uuml;brigen aus der Stundenvorhersage.</p>
  %POL_AUSWAHL%
</div>

<div class="card">
  <h2>Warnungen</h2>
  <div class="row"><span>Pollenwarnung</span>
    <label class="toggle"><input type="checkbox" name="pollen_warn" %PW%><span class="slider"></span></label></div>
  <div class="row"><span>DWD-Wetterwarnungen</span>
    <label class="toggle"><input type="checkbox" name="warn_region" %WR%><span class="slider"></span></label></div>
  <label>Pollen-Warnschwelle</label>
  <select name="pol_schw">
    <option value="0" %PS0%>Gering</option>
    <option value="1" %PS1%>Mittel</option>
    <option value="2" %PS2%>Hoch (Standard)</option>
    <option value="3" %PS3%>Stark</option>
  </select>
</div>

<div class="card">
  <h2>Biowetter</h2>
  <label>DWD Biowetter-Zone</label>
  <select name="bio_zone">%BIO_ZONE_OPTIONS%</select>
  <hr style="margin:14px 0;border-color:var(--bd)">
  <h3 style="margin:0 0 6px">Wetterfühligkeit – angezeigte Kategorien</h3>
  <p class="hint">Wähle die 4 Kategorien für den Gesundheitsscreen.</p>
  <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:8px">
    <div><label>Zeile 1</label><select name="ws_cat0">%WS_CAT_OPTIONS0%</select></div>
    <div><label>Zeile 2</label><select name="ws_cat1">%WS_CAT_OPTIONS1%</select></div>
    <div><label>Zeile 3</label><select name="ws_cat2">%WS_CAT_OPTIONS2%</select></div>
    <div><label>Zeile 4</label><select name="ws_cat3">%WS_CAT_OPTIONS3%</select></div>
  </div>
</div>
</div>

<div class="savebar" id="savebar">
  <button type="submit" class="btn btn-gruen">Einstellungen speichern</button>
  <span id="dirty">Ungespeicherte &Auml;nderungen</span>
</div>
</form>

<div class="tab" id="t-sensoren">
<div class="card">
  <h2>Sensoren</h2>
  <p class="hint">Interner Umweltsensor und externe Sensoren per MQTT (z. B. ESPHome): ausw&auml;hlen, benennen, korrigieren.</p>
  <div class="status">%SENS%</div>
  <a href="/sensoren" class="btn btn-blau">Sensoren einrichten</a>
</div>

<div class="card">
  <h2>Next-Displays</h2>
  <p class="hint">Alle WetterCube Next an einer Stelle einstellen: ein Reiter je Next (Seiten, R&auml;ume, Helligkeit, Nachtmodus). Auch erreichbar unter <b>wettercube-next.local</b>.</p>
  <div class="status">%NEXTS%</div>
  <a href="/next" class="btn btn-blau">Next-Displays einstellen</a>
</div>

<div class="card">
  <h2>Speicherkarte</h2>
  <p class="hint">Eine microSD-Karte (FAT32) im Einschub speichert den Verlauf der Sensoren und das Log. Status: <b>%SD%</b></p>
  <a href="/karte" class="btn btn-blau">Speicherkarte auslesen</a>
</div>
</div>

<div class="tab" id="t-ampel">
<div class="card">
  <h2>Ampel-Konfiguration</h2>
  <p class="hint">Einstellungen fuer das Hardware-Addon (Ampel). Aktuell: <strong id="amp_d">…</strong> &nbsp;|&nbsp; Aktiv: <strong id="amp_a">…</strong></p>
  <form action="/api/ampel_save" method="post">
  <label>Was die Ampel anzeigt</label>
  <select name="amp_modus">
    <option value="0" %AMP_M0%>Temperatur</option>
    <option value="1" %AMP_M1%>Pollen (meine Allergene)</option>
    <option value="2" %AMP_M2%>Luftqualit&auml;t</option>
  </select>
  <p class="hint">Pollen: Gr&uuml;n f&uuml;r keine bis geringe, Gelb f&uuml;r mittlere bis hohe, Rot f&uuml;r starke Belastung (schlimmstes der gew&auml;hlten Allergene). Luftqualit&auml;t (europ&auml;ischer AQI): Gr&uuml;n bis 40, Gelb bis 60, Rot dar&uuml;ber. Die Temperaturschwellen unten gelten nur im Modus Temperatur.</p>
  <div class="row">
    <div class="col">
      <label>Gruen min (°C)</label>
      <input type="number" name="amp_gn_min" value="%AMP_GN_MIN%" min="-30" max="60">
    </div>
    <div class="col">
      <label>Gruen max (°C)</label>
      <input type="number" name="amp_gn_max" value="%AMP_GN_MAX%" min="-30" max="60">
    </div>
  </div>
  <div class="row">
    <div class="col">
      <label>Gelb min (°C)</label>
      <input type="number" name="amp_ge_min" value="%AMP_GE_MIN%" min="-30" max="60">
    </div>
    <div class="col">
      <label>Gelb max (°C)</label>
      <input type="number" name="amp_ge_max" value="%AMP_GE_MAX%" min="-30" max="60">
    </div>
  </div>
  <div class="row">
    <div class="col">
      <label>Rot min (°C)</label>
      <input type="number" name="amp_ro_min" value="%AMP_RO_MIN%" min="-30" max="60">
    </div>
    <div class="col">
      <label>Rot max (°C)</label>
      <input type="number" name="amp_ro_max" value="%AMP_RO_MAX%" min="-30" max="60">
    </div>
  </div>
  <button type="submit" class="btn btn-gruen">Ampel-Schwellen speichern</button>
  </form>
</div>

<div class="card">
  <h2>Home Assistant (MQTT)</h2>
  <p class="hint">Der WetterCube meldet sich bei <b>Home Assistant</b> als Ger&auml;t &bdquo;WetterCube Fusion&ldquo; an, und zwar mit Au&szlig;enwetter, Mond, UV, Luftqualit&auml;t, Pollen, L&uuml;ften, Alarmen und dem internen Sensor. Home Assistant legt die Sensoren selbst an, ohne YAML. Das l&auml;uft &uuml;ber dieselbe MQTT-Verbindung wie die Sensoren (Seite <a href="/sensoren">Sensoren</a>, Schritt 1); in Home Assistant muss die MQTT-Integration mit demselben Broker eingerichtet sein. Ohne MQTT geht es auch &uuml;ber die Adresse <a href="/api/status" target="_blank"><code>/api/status</code></a> (REST-Sensor in Home Assistant).</p>
  <form action="/api/ha_save" method="post">
  <div class="row"><span>Home Assistant melden</span>
    <label class="toggle"><input type="checkbox" name="ha_aktiv" %HAA%><span class="slider"></span></label></div>
  <label>Topic-Pr&auml;fix (f&uuml;r Status und Zustand, normalerweise nicht &auml;ndern)</label>
  <input type="text" name="ha_praefix" value="%HAP%" maxlength="30" autocapitalize="off" autocorrect="off">
  <p class="hint" style="margin:6px 0 0"><b>Stand:</b> %HAS%</p>
  <button type="submit" class="btn btn-gruen">Speichern</button>
  <button type="submit" formaction="/api/ha_neu" class="btn">Jetzt neu melden</button>
  </form>
</div>

<div class="card">
  <h2>Benachrichtigung aufs Handy (ntfy)</h2>
  <p class="hint"><b>ntfy</b> ist ein kostenloser Dienst, der Nachrichten aufs Handy schickt. Die Meldungen laufen &uuml;ber den Server von ntfy.sh (ein Dienst eines Dritten); wer das nicht m&ouml;chte, tr&auml;gt unten einen eigenen Server ein. So richtest du es ein: Installiere die App <b>ntfy</b> (Android oder iPhone), tippe auf &bdquo;+&ldquo; und trage ein <b>Topic</b> ein, am besten einen langen, nicht erratbaren Namen wie <i>wettercube-k7x2m9q4</i>. Dasselbe Topic tr&auml;gst du hier ein. Das Topic wirkt wie ein Passwort: Wer es kennt, kann mitlesen. Es steht deshalb nicht in der Einstellungssicherung und nicht im Log.</p>
  <form action="/api/ntfy_save" method="post">
  <div class="row"><span>Benachrichtigungen aktiv</span>
    <label class="toggle"><input type="checkbox" name="ntfy_aktiv" %NA%><span class="slider"></span></label></div>
  <label>Server</label>
  <input type="text" name="ntfy_server" value="%NSRV%" autocapitalize="off" autocorrect="off">
  <label>Topic</label>
  <input type="text" name="ntfy_topic" value="%NTOP%" autocapitalize="off" autocorrect="off" autocomplete="off">
  <details><summary>Gesch&uuml;tztes Topic oder eigener Server mit Anmeldung</summary>
    <label>Zugangstoken</label>
    <input type="password" name="ntfy_token" autocomplete="new-password" placeholder="%NTOKPH%">
    <label style="display:flex;gap:8px;align-items:center"><input type="checkbox" name="ntfy_token_weg" style="width:auto"> gespeicherten Token l&ouml;schen</label>
  </details>
  <label style="margin-top:12px">Das wird gemeldet</label>
  <div class="row"><span>Sensor-Grenzwerte (&Uuml;ber- oder Unterschreitung eines Sensors)</span>
    <label class="toggle"><input type="checkbox" name="ntfy_q_sensor" %NQS%><span class="slider"></span></label></div>
  <div class="row"><span>Lüften-Warnung (ein Raum bleibt zu feucht)</span>
    <label class="toggle"><input type="checkbox" name="ntfy_q_lueften" %NQL%><span class="slider"></span></label></div>
  <div class="row"><span>Entwarnung senden, wenn ein Alarm endet</span>
    <label class="toggle"><input type="checkbox" name="ntfy_ent" %NEN%><span class="slider"></span></label></div>
  <button type="submit" class="btn btn-gruen">Speichern</button>
  <button type="button" class="btn" onclick="ntfyTest()">Testnachricht senden</button>
  <p class="hint" id="ntfy_msg" style="margin-top:8px"></p>
  </form>
  <script>
  function ntfyTest(){var m=document.getElementById('ntfy_msg');m.textContent='Wird gesendet ...';
    fetch('/api/ntfy_test',{method:'POST'}).then(function(){var n=0;var t=setInterval(function(){
      fetch('/api/ntfy_status').then(function(r){return r.text();}).then(function(s){m.textContent=s;});if(++n>4)clearInterval(t);},1500);});}
  </script>
</div>
</div>

<div class="tab" id="t-system">
<div class="card">
  <h2>WLAN-Zugangsdaten aendern</h2>
  <p class="hint">SSID/Passwort aendern ohne neu zu flashen.</p>
  <a href="/wlan" class="btn btn-blau">WLAN aendern</a>
</div>

<div class="card">
  <h2>Passwort f&uuml;r die Web-Oberfl&auml;che</h2>
  <p class="hint">%WPSTATUS%</p>
  <p class="hint">Mit Passwort fragt das Ger&auml;t beim &Ouml;ffnen dieser Seite nach Benutzername (<b>admin</b>) und Passwort. Das <b>Next</b>, die <b>WetterAmpel</b> und <b>Home Assistant</b> lesen weiter die Werte (<code>/api/next</code>, <code>/api/ampel</code>, <code>/api/status</code>). Das Passwort wird im Heimnetz unverschl&uuml;sselt (HTTP) &uuml;bertragen: Es sch&uuml;tzt vor Zugriffen anderer Ger&auml;te und fremder Webseiten, nicht vor jemandem, der den Netzverkehr mitliest. <b>Vergessen?</b> Die Notfall-App kann es entfernen (&bdquo;Web-Passwort entfernen&ldquo;).</p>
  <form action="/api/web_pw_save" method="post" onsubmit="return wpBestaetigen()">
    <label>Neues Passwort (mindestens 6 Zeichen)</label>
    <input type="password" name="pw1" autocomplete="new-password">
    <label>Wiederholen</label>
    <input type="password" name="pw2" autocomplete="new-password">
    <button type="submit" class="btn btn-blau" style="margin-top:8px">Passwort speichern</button>
    <label style="display:flex;gap:8px;align-items:center;margin-top:10px"><input type="checkbox" name="pw_weg" style="width:auto"> Passwort entfernen</label>
  </form>
</div>

<div class="card">
  <h2>Firmware-Update</h2>
  <div class="status">Aktuelle Version: %VERSION%</div>
  <form action="/api/update_modus_save" method="post" onsubmit="return umBestaetigen()">
    <label>Updates</label>
    <label style="display:flex;gap:8px;align-items:flex-start"><input type="radio" name="update_modus" value="2" style="width:auto;margin-top:4px" %UMS2% onchange="umWarnung()"><span><b>Hinweis am Display und in der Web-Oberfl&auml;che</b> (empfohlen). Eine neue Version erscheint als Update-Symbol auf dem Hauptscreen.</span></label>
    <label style="display:flex;gap:8px;align-items:flex-start"><input type="radio" name="update_modus" value="1" style="width:auto;margin-top:4px" %UMS1% onchange="umWarnung()"><span><b>Nur in der Web-Oberfl&auml;che.</b> Updates sind &uuml;ber diese Seite m&ouml;glich, aber das Display zeigt keinen Hinweis.</span></label>
    <label style="display:flex;gap:8px;align-items:flex-start"><input type="radio" name="update_modus" value="0" style="width:auto;margin-top:4px" %UMS0% onchange="umWarnung()"><span><b>Ganz aus.</b> Das Ger&auml;t fragt nicht nach neuen Versionen.</span></label>
    <label style="margin-top:8px">Nach neuen Versionen fragen alle</label>
    <select name="update_std"><option value="1" %UST1%>1 Stunde</option><option value="6" %UST6%>6 Stunden (Standard)</option><option value="24" %UST24%>24 Stunden</option></select>
    <div id="um_warn" class="status" style="display:none;margin-top:8px"><b>Achtung:</b> Ohne Updates kommen auch <b>keine Sicherheitsupdates</b> mehr. Du musst die Firmware dann selbst aktuell halten (neue Datei von GitHub laden und unten installieren).</div>
    <button type="submit" class="btn btn-blau" style="margin-top:8px">Speichern</button>
  </form>
  <div class="status">%UINFO%</div>
  <div %UMHIDE%>
  <button type="button" class="btn btn-blau" onclick="checkOta()">Auf Updates pruefen</button>
  <div id="ota_status" class="status"></div>
  </div>
  <hr style="margin:14px 0;border-color:var(--bd)">
  <label>Firmware aus einer Datei installieren (z. B. firmware.bin)</label>
  <input type="file" id="fw_file" accept=".bin,application/octet-stream">
  <button type="button" class="btn btn-rot" onclick="firmwareDatei()">Datei installieren</button>
  <div id="fw_status" class="status"></div>
  <div class="status" style="margin-top:10px">Hinweis: %UPDATE_HINWEIS% Das Geraet dabei <b>nicht vom Strom trennen</b> &ndash; es startet danach selbst neu (ca. 1 Minute).</div>
</div>

<div class="card">
  <h2>Einstellungen sichern</h2>
  <p class="hint">Speichert alle Einstellungen (ohne WLAN-Passwort) als Datei und spielt sie bei Bedarf wieder ein, zum Beispiel nach einer Neuinstallation mit gel&ouml;schtem Speicher. Auch Sicherungen des WetterCubePlus werden akzeptiert.</p>
  <a href="/backup" class="btn btn-blau">Einstellungen herunterladen</a>
  <label>Sicherung wiederherstellen</label>
  <input type="file" id="restore_file" accept=".json,application/json">
  <button type="button" class="btn btn-rot" onclick="restoreSettings()">Wiederherstellen</button>
  <div id="restore_status" class="status"></div>
</div>

<div class="card">
  <h2>Wiederherstellung</h2>
  <p class="hint">Falls das Ger&auml;t nicht mehr richtig l&auml;uft oder ein Update schiefgegangen ist: Die Notfall-App erneuert die Firmware (von der Speicherkarte oder aus dem Ger&auml;t) oder setzt das Ger&auml;t in den Werkszustand zur&uuml;ck. Gew&auml;hlt wird <b>am Display</b>, nichts passiert ohne deine Best&auml;tigung. %NOTFALL%.</p>
  <button type="button" class="btn btn-rot" %NOTFALL_KNOPF% onclick="wiederherstellung()">Wiederherstellung starten</button>
  <div id="wh_status" class="status"></div>
  <hr style="margin:14px 0;border-color:var(--bd)">
  <p class="hint">Die Einstellungen werden bei jeder &Auml;nderung ohne Passw&ouml;rter auf die Speicherkarte gesichert (notfall/einstellungen.json). %SICHERUNG%.</p>
  <button type="button" class="btn btn-blau" %SICHERUNG_KNOPF% onclick="sicherungKarte()">Einstellungen von der Karte wiederherstellen</button>
  <div id="sk_status" class="status"></div>
</div>

<div class="card">
  <h2>Diagnose</h2>
  <p class="hint">Das Log zeigt Start, WLAN, Datenabrufe und Fehler. Bei Problemen hilft es bei der Suche.</p>
  <a href="/log" target="_blank" class="btn btn-blau">Log ansehen</a>
  <p class="hint" style="margin-top:14px">Das Bildschirmfoto zeigt, was das Display gerade anzeigt. Es wird als PNG gespeichert (dauert einige Sekunden).</p>
  <a href="/screenshot.png" download="wettercube-bildschirm.png" class="btn btn-blau">Bildschirmfoto speichern</a>
</div>

<div class="card">
  <h2>Datenquellen</h2>
  <p class="hint">Wetterdaten: <a href="https://open-meteo.com" target="_blank" rel="noopener">Weather data by Open-Meteo.com</a> (<a href="https://open-meteo.com/en/licence" target="_blank" rel="noopener">CC BY 4.0</a>; die kostenlose Schnittstelle ist nur f&uuml;r nicht-kommerzielle Nutzung). Die Werte werden im WetterCube teils umgerechnet und zusammengefasst, zum Beispiel zu Pollenstufen und Tagesh&ouml;chstwerten. Pollen und Luftqualit&auml;t: Contains modified Copernicus Atmosphere Monitoring Service information, &uuml;ber Open-Meteo. Ortssuche: Location data by <a href="https://www.geonames.org" target="_blank" rel="noopener">GeoNames</a> via Open-Meteo. Warnungen, Pollenflug, Biowetter und Radar: Quelle: <a href="https://opendata.dwd.de" target="_blank" rel="noopener">Deutscher Wetterdienst</a> (<a href="https://www.dwd.de/DE/service/rechtliche_hinweise/rechtliche_hinweise_node.html" target="_blank" rel="noopener">CC BY 4.0</a>).</p>
</div>
</div>

<script>
function standortWechseln(n){
  var s=document.getElementById('so_status');
  fetch('/api/standort?nr='+n,{method:'POST'}).then(function(r){return r.text();}).then(function(t){if(s)s.textContent=t;}).catch(function(){if(s)s.textContent='Fehler beim Umschalten';});
}
function wpBestaetigen(){
  var f=document.querySelector('input[name=pw_weg]');
  if(f&&f.checked) return confirm('Passwort entfernen? Dann kann jeder im Heimnetz die Einstellungen aendern.');
  return true;
}
function umWarnung(){
  var r=document.querySelector('input[name=update_modus]:checked');
  document.getElementById('um_warn').style.display=(r&&r.value==='0')?'block':'none';
}
function umBestaetigen(){
  var r=document.querySelector('input[name=update_modus]:checked');
  if(r&&r.value==='0') return confirm('Updates ganz ausschalten?\n\nDann kommen auch keine Sicherheitsupdates mehr. Du musst die Firmware selbst aktuell halten.');
  return true;
}
document.addEventListener('DOMContentLoaded',umWarnung);
function checkOta(){
  document.getElementById('ota_status').textContent='Pruefe...';
  fetch('/ota_check').then(function(r){return r.json();}).then(function(d){
    if(d.aus){ document.getElementById('ota_status').textContent='Updates sind ausgeschaltet.'; return; }
    if(d.update_available){
      document.getElementById('ota_status').innerHTML=
        'Neue Version: '+d.latest+' &nbsp;'+
        '<button class="btn btn-rot" onclick="doUpdate()">Jetzt installieren</button>';
    } else {
      document.getElementById('ota_status').textContent='Bereits aktuell ('+d.latest+')';
    }
  }).catch(function(){document.getElementById('ota_status').textContent='Fehler beim Pruefen';});
}
function doUpdate(){
  if(!confirm('Update jetzt installieren?\n\n%UPDATE_CONFIRM% Das Geraet NICHT vom Strom trennen, es startet danach selbst neu.')) return;
  document.getElementById('ota_status').textContent='Update laeuft... %UPDATE_LAEUFT% Geraet NICHT ausschalten (ca. 1 Minute).';
  fetch('/ota_update',{method:'POST'}).then(function(r){return r.text();}).then(function(t){
    document.getElementById('ota_status').textContent=(t==='ok')?'Update installiert. Das Geraet startet neu ...':t;
  }).catch(function(){
    document.getElementById('ota_status').textContent='Verbindung beendet. Wenn das Geraet gerade neu startet, ist das normal.';
  });
}
function firmwareDatei(){
  var f=document.getElementById('fw_file').files[0], st=document.getElementById('fw_status');
  if(!f){st.textContent='Bitte zuerst eine Datei waehlen.';return;}
  if(!confirm('Firmware aus "'+f.name+'" jetzt installieren?\n\nDas Geraet NICHT vom Strom trennen, es startet danach selbst neu.')) return;
  var x=new XMLHttpRequest();
  x.upload.onprogress=function(e){if(e.lengthComputable)st.textContent='Wird hochgeladen und installiert: '+Math.round(e.loaded*100/e.total)+' %';};
  x.onload=function(){st.textContent=(x.status===200)?'Installiert. Das Geraet startet neu (Seite in ca. 1 Minute neu laden).':'Fehler: '+x.responseText;};
  x.onerror=function(){st.textContent='Verbindung beendet. Wenn das Geraet gerade neu startet, ist das normal.';};
  st.textContent='Wird hochgeladen ...';
  x.open('POST','/update_datei'); x.setRequestHeader('Content-Type','application/octet-stream'); x.send(f);
}
function wiederherstellung(){
  if(!confirm('Wiederherstellung starten?\n\nDas Geraet startet in die Notfall-App. Dort waehlst du am Display, was passieren soll. Bis dahin wird nichts veraendert.')) return;
  var st=document.getElementById('wh_status'); st.textContent='Starte die Wiederherstellung ... Bitte jetzt auf das Display schauen.';
  fetch('/wiederherstellung',{method:'POST'}).then(function(r){return r.text();}).then(function(t){if(t!=='ok')st.textContent='Fehler: '+t;}).catch(function(){});
}
function sicherungKarte(){
  if(!confirm('Die Einstellungen von der Speicherkarte jetzt einspielen? Das Geraet startet danach neu.')) return;
  var st=document.getElementById('sk_status'); st.textContent='Wird eingespielt ...';
  fetch('/restore_karte',{method:'POST'}).then(function(r){return r.text().then(function(t){return {ok:r.ok,t:t};});})
    .then(function(o){st.textContent=o.ok?'Eingespielt. Das Geraet startet neu (Seite in ca. 30 Sekunden neu laden).':'Fehler: '+o.t;})
    .catch(function(){st.textContent='Verbindung beendet. Wenn das Geraet gerade neu startet, ist das normal.';});
}
function restoreSettings(){
  var f=document.getElementById('restore_file').files[0];
  var st=document.getElementById('restore_status');
  if(!f){st.textContent='Bitte zuerst eine Datei waehlen.';return;}
  if(!confirm('Die Einstellungen aus der Datei jetzt einspielen? Das Geraet startet danach neu.')) return;
  var r=new FileReader();
  r.onload=function(){
    st.textContent='Wird eingespielt ...';
    fetch('/restore',{method:'POST',headers:{'Content-Type':'application/json'},body:r.result})
      .then(function(x){return x.text().then(function(tx){return {ok:x.ok,t:tx};});})
      .then(function(o){st.textContent=o.ok?'Eingespielt. Das Geraet startet neu (Seite in ca. 30 Sekunden neu laden).':'Fehler: '+o.t;})
      .catch(function(){st.textContent='Verbindung beendet. Wenn das Geraet gerade neu startet, ist das normal.';});
  };
  r.readAsText(f);
}
function ladeAmpel(){
  fetch('/api/ampel').then(function(r){return r.json();}).then(function(d){
    document.getElementById('amp_d').textContent=d.detail||(d.temperature.toFixed(1)+' °C');
    var farben={'green':'Gruen','yellow':'Gelb','red':'Rot','none':'keine'};
    document.getElementById('amp_a').textContent=farben[d.active]||d.active;
  }).catch(function(){});
}
ladeAmpel();

var TABS=['anzeige','wetter','sensoren','ampel','system'];
function zeige(t){
  if(TABS.indexOf(t)<0)t='anzeige';
  for(var i=0;i<TABS.length;i++){
    var s=document.getElementById('t-'+TABS[i]);if(s)s.style.display=(TABS[i]===t)?'block':'none';
    var a=document.querySelector('.tabs a[data-t="'+TABS[i]+'"]');if(a)a.className=(TABS[i]===t)?'aktiv':'';
  }
  var sb=document.getElementById('savebar');if(sb)sb.style.display=(t==='anzeige'||t==='wetter')?'flex':'none';
  try{localStorage.setItem('wcf-tab',t);}catch(e){}
  if(location.hash!=='#'+t&&history.replaceState)history.replaceState(null,'','#'+t);
}
function startTab(){
  var t=location.hash.replace('#','');
  if(!t){try{t=localStorage.getItem('wcf-tab')||'';}catch(e){}}
  zeige(t);
}
window.addEventListener('hashchange',function(){zeige(location.hash.replace('#',''));});
document.getElementById('tabs').addEventListener('click',function(e){var a=e.target.closest?e.target.closest('a[data-t]'):null;if(a){e.preventDefault();zeige(a.getAttribute('data-t'));}});
document.getElementById('einst').addEventListener('input',function(){var d=document.getElementById('dirty');if(d)d.style.display='inline';});
wcThemeLabel();startTab();
function tonTest(an,w){
  var m=document.getElementById('tt_msg');
  if(!an){fetch('/api/tontest?aus=1',{method:'POST'});m.textContent='';return;}
  var ton=document.getElementById('t'+w+'_ton').value,laut=document.getElementById('t'+w+'_laut').value;
  fetch('/api/tontest?ton='+ton+'&laut='+laut,{method:'POST'}).then(function(r){return r.text();}).then(function(t){m.textContent=t==='ok'?'Ton l\u00e4uft (Stopp beendet ihn)':t;}).catch(function(){m.textContent='Fehler';});
}
</script>
</body></html>
)rawhtml";

const char WLAN_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="de"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WLAN aendern</title>
)rawhtml" WEBUI_THEME_HEAD WEBUI_THEME_CSS R"rawhtml(
<style>
  body{font-family:Arial;background:var(--bg);color:var(--txt);padding:20px}
  .card{background:var(--card);border:1px solid var(--bd);border-radius:8px;padding:20px;max-width:420px;margin:auto}
  h1{color:var(--acc);font-size:1.3em}
  label{display:block;color:var(--mut);font-size:.85em;margin-top:10px}
  input,select{width:100%;padding:8px;border-radius:4px;border:1px solid var(--bd);
    background:var(--in);color:var(--txt);box-sizing:border-box;margin-top:3px}
  .btn{width:100%;padding:10px;background:#238636;color:#fff;border:none;border-radius:4px;font-size:1em;cursor:pointer;margin-top:16px}
  .btn:hover{background:#2ea043}
  a{color:var(--acc)}
</style></head><body>
<div class="wcnav"><a href="/">Einstellungen</a><span class="rechts"><button id="wcThemeBtn" class="themebtn" onclick="wcTheme()"></button></span></div>
<script>wcThemeLabel();</script>
<div class="card">
  <h1>WLAN-Zugangsdaten aendern</h1>
  <form action="/wlan_speichern" method="post">
    <label>Netzwerk</label>
    <select name="ssid">%NETZWERKE%</select>
    <label>Passwort</label>
    <input type="password" name="pass">
    <button type="submit" class="btn">Speichern &amp; Neustart</button>
  </form>
  <br><a href="/">Zurueck</a>
</div></body></html>
)rawhtml";