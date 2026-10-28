// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Gemeinsames Design der Web-Seiten: Hell/Dunkel über CSS-Variablen, Umschalter (gemerkt im Browser unter "wcf-theme"),
// Navigationsleiste für Seiten ohne Reiter. Als Makros, damit alle Seiten dasselbe Aussehen teilen.

// im <head> ganz vorn: setzt das Design vor dem ersten Zeichnen (kein Aufblitzen); ohne gemerkte Wahl gilt die Einstellung des Browsers
#define WEBUI_THEME_HEAD R"rawjs(<script>
(function(){var t=null;try{t=localStorage.getItem('wcf-theme');}catch(e){}
if(t!=='light'&&t!=='dark')t=(window.matchMedia&&window.matchMedia('(prefers-color-scheme: light)').matches)?'light':'dark';
document.documentElement.setAttribute('data-theme',t);})();
function wcThemeLabel(){var b=document.getElementById('wcThemeBtn');if(!b)return;
b.textContent=document.documentElement.getAttribute('data-theme')==='light'?'☾ Dunkel':'☀ Hell';}
function wcTheme(){var t=document.documentElement.getAttribute('data-theme')==='light'?'dark':'light';
document.documentElement.setAttribute('data-theme',t);try{localStorage.setItem('wcf-theme',t);}catch(e){}wcThemeLabel();}
</script>)rawjs"

// Farben: dunkel (Standard) und hell
#define WEBUI_THEME_CSS R"rawcss(<style>
:root,[data-theme=dark]{--bg:#0d1117;--card:#161b22;--bd:#30363d;--bd2:#21262d;--txt:#e6edf3;--mut:#8b949e;--in:#21262d;--acc:#58a6ff;--slider:#30363d;--tab:#161b22;color-scheme:dark}
[data-theme=light]{--bg:#f3f5f8;--card:#ffffff;--bd:#d0d7de;--bd2:#e6eaef;--txt:#1f2328;--mut:#59636e;--in:#f6f8fa;--acc:#0969da;--slider:#b8c0c8;--tab:#e6eaef;color-scheme:light}
.wcnav{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:0 0 16px}
.wcnav a{padding:8px 14px;border-radius:8px;background:var(--tab);color:var(--txt);text-decoration:none;font-size:.92em;border:1px solid var(--bd)}
.wcnav a.aktiv{background:#1f6feb;border-color:#1f6feb;color:#fff}
.wcnav .rechts{margin-left:auto}
.themebtn{background:var(--tab);color:var(--txt);border:1px solid var(--bd);border-radius:8px;padding:8px 14px;font-size:.92em;cursor:pointer;margin:0;width:auto}
</style>)rawcss"

// Navigationsleiste für Sensoren und Speicherkarte (die Einstellungsseite hat Reiter); markiert die aktuelle Seite
#define WEBUI_NAVBAR R"rawhtml(<div class="wcnav"><a href="/">Einstellungen</a><a href="/sensoren" data-p="/sensoren">Sensoren</a><a href="/schalter" data-p="/schalter">Schalter</a><a href="/next" data-p="/next">Next-Displays</a><a href="/tagebuch" data-p="/tagebuch">Tagebuch</a><a href="/karte" data-p="/karte">Speicherkarte</a><a href="/log" target="_blank">Log</a><span class="rechts"><button id="wcThemeBtn" class="themebtn" onclick="wcTheme()"></button></span></div>
<script>wcThemeLabel();(function(){var l=document.querySelectorAll('.wcnav a[data-p]');for(var i=0;i<l.length;i++)if(location.pathname.indexOf(l[i].getAttribute('data-p'))===0)l[i].className='aktiv';})();</script>)rawhtml"
