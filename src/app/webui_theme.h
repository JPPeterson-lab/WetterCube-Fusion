// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Gemeinsames Design der Web-Seiten im WetterCube-Markendesign (wie wettercube.de und der Web-Installer): Montserrat, Himmelblau, Navy, Orange, Gruen;
// hell und dunkel ueber CSS-Variablen, Umschalter (gemerkt im Browser unter "wcf-theme"). Ohne gemerkte Wahl gilt die Einstellung des Browsers.
// Die Schrift liefert das Geraet selbst (Route /marke/montserrat.woff2), es wird nichts aus dem Internet geladen.
// Aufbau jeder Seite:  WEBUI_THEME_HEAD WEBUI_THEME_CSS <Seiten-CSS> ... <body> WEBUI_NAVBAR   (WEBUI_MARKE_CSS steht in WEBUI_NAVBAR und gilt damit nach dem Seiten-CSS)

// im <head> ganz vorn: setzt das Design vor dem ersten Zeichnen (kein Aufblitzen)
#define WEBUI_THEME_HEAD R"rawjs(<script>
(function(){var t=null;try{t=localStorage.getItem('wcf-theme');}catch(e){}
if(t!=='light'&&t!=='dark')t=(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches)?'dark':'light';
document.documentElement.setAttribute('data-theme',t);})();
function wcThemeLabel(){var b=document.getElementById('wcThemeBtn');if(!b)return;
b.textContent=document.documentElement.getAttribute('data-theme')==='light'?'☾ Dunkel':'☀ Hell';}
function wcTheme(){var t=document.documentElement.getAttribute('data-theme')==='light'?'dark':'light';
document.documentElement.setAttribute('data-theme',t);try{localStorage.setItem('wcf-theme',t);}catch(e){}wcThemeLabel();}
</script>)rawjs"

// Farben (Marken-Tokens von wettercube.de): hell und dunkel
#define WEBUI_THEME_CSS R"rawcss(<style>
@font-face{font-family:Montserrat;font-style:normal;font-weight:100 900;font-display:swap;src:url(/marke/montserrat.woff2) format("woff2")}
:root,[data-theme=light]{--himmel:#87ceeb;--himmel-dunkel:#2b7fa3;--kachel:#e0f7ff;--flaeche:#f5fbfe;--navy:#0b2545;
--bg:#f5fbfe;--card:#ffffff;--bd:#b9d6e4;--bd2:#dcebf3;--txt:#111827;--mut:#4b5563;--in:#f5fbfe;--acc:#2b7fa3;--slider:#b9d6e4;--tab:#e0f7ff;
--btn:#0b2545;--btn-h:#13355f;--blau:#2b7fa3;--gruen:#0fa64a;--rot:#ce4b11;--amber:#d9930d;--grau:#6b7c8d;
--gruen-t:#0c8a3d;--rot-t:#b83e0c;--amber-t:#9a6700;color-scheme:light}
[data-theme=dark]{--bg:#06203b;--card:#0e3358;--bd:#2f6aa3;--bd2:#1d4d7d;--txt:#ffffff;--mut:#bcd3e6;--in:#0a2a4b;--acc:#87ceeb;--slider:#2f6aa3;--tab:#184a7e;
--btn:#2b7fa3;--btn-h:#3d8fb8;--blau:#4a9cc5;--gruen-t:#4cd987;--rot-t:#ff9166;--amber-t:#f0b429;--grau:#7f93a6;color-scheme:dark}
.wcnav{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:0 0 16px}
.wcnav a{padding:7px 14px;border-radius:999px;background:var(--tab);color:var(--txt);text-decoration:none;font-size:.88em;font-weight:500;border:1px solid var(--bd)}
.wcnav a:hover{border-color:var(--himmel-dunkel)}
.wcnav a.aktiv{background:var(--btn);border-color:var(--btn);color:#fff}
.wcnav .rechts{margin-left:auto}
.themebtn{background:var(--tab);color:var(--txt);border:1px solid var(--bd);border-radius:999px;padding:7px 14px;font-size:.88em;cursor:pointer;margin:0;width:auto}
</style>)rawcss"

// Markenkopf, Navigationsleiste und Markenstil (gilt nach dem Seiten-CSS): Montserrat, runde Karten und Knoepfe, Marken-Farben fuer Zustaende
#define WEBUI_MARKE_CSS R"rawcss(<style>
body{font-family:Montserrat,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif!important;-webkit-font-smoothing:antialiased;background:var(--bg);color:var(--txt)}
h1{font-weight:600;letter-spacing:-.5px;color:var(--txt)}h2,h3{font-weight:600;letter-spacing:-.2px}
a{text-underline-offset:3px}
.card,.g,.hb,.k,.schritt{border-radius:16px!important;border-color:var(--bd)}
button,.btn,a.knopf,.themebtn{border-radius:999px!important;font-family:inherit;font-weight:600}
input,select,textarea{border-radius:10px!important;font-family:inherit}
input[type=checkbox],input[type=radio],input[type=range]{accent-color:var(--himmel-dunkel)}
details summary{color:var(--acc);font-weight:500}
.marke-kopf{display:flex;align-items:baseline;gap:10px;margin:0 0 12px}
.marke-kopf a{font-weight:600;font-size:26px;letter-spacing:-.5px;color:var(--txt);text-decoration:none}.marke-kopf a span{color:var(--himmel)}
.marke-kopf em{font-style:normal;font-size:26px;letter-spacing:-.5px;color:var(--himmel);font-weight:500}
.ok{color:var(--gruen-t)!important}.aus,.fehler{color:var(--rot-t)}.warn{color:var(--amber-t)}
</style>)rawcss"

// Schimmelwaechter nur beim P4 in der Leiste
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
#define WEBUI_NAV_SCHIMMEL R"rawhtml(<a href="/schimmel" data-p="/schimmel">Schimmel</a>)rawhtml"
#else
#define WEBUI_NAV_SCHIMMEL ""
#endif

// Navigationsleiste fuer die Seiten (die Einstellungsseite hat Reiter); markiert die aktuelle Seite
#define WEBUI_NAVBAR WEBUI_MARKE_CSS R"rawhtml(<div class="marke-kopf"><a href="/">WetterCube</a><em>Fusion</em></div><div class="wcnav"><a href="/">Einstellungen</a><a href="/sensoren" data-p="/sensoren">Sensoren</a><a href="/schalter" data-p="/schalter">Schalter</a><a href="/next" data-p="/next">Next-Displays</a><a href="/tagebuch" data-p="/tagebuch">Tagebuch</a>)rawhtml" WEBUI_NAV_SCHIMMEL R"rawhtml(<a href="/karte" data-p="/karte">Speicherkarte</a><a href="/log" target="_blank">Log</a><span class="rechts"><button id="wcThemeBtn" class="themebtn" onclick="wcTheme()"></button></span></div>
<script>wcThemeLabel();(function(){var l=document.querySelectorAll('.wcnav a[data-p]');for(var i=0;i<l.length;i++){var q=l[i].getAttribute('data-p'),w=location.pathname;if(w===q||w.indexOf(q+'/')===0)l[i].className='aktiv';}})();</script>)rawhtml"
