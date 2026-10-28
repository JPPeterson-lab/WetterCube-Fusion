#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jan Althaus (JPPeterson-lab)
"""Nimmt alle Bildschirme vom Geraet auf (hell und dunkel) und baut die Uebersicht.

Aufruf:  python3 tools/screens_aufnehmen.py <IP oder Name des Geraets> [Ausgabeordner]
Voraussetzung: Firmware mit /screenshot.png (ab v0.9.9-beta12). Die Bilder entstehen aus dem LVGL-Abbild, sind also unabhaengig davon, wie das Display im Gehaeuse sitzt.
Ablauf: erst echte Daten (Hauptscreen, 7 Tage, Verlauf, Karte, Sensoren, Menue, Nacht-Uhr, Timer, Boot), dann mit /demo Beispieldaten fuer Seiten,
die sonst leer waeren (Allergene, Biowetter, Luft, Lueftenwarnung, Pollenwarnung, Warnliste, Regen- und Frost-Hinweis); am Ende lädt /demo?an=0 die echten Daten neu.
Achtung: Die Demo-Warnungen loesen auf dem P4 kurz den Warnton aus.
"""
import os, sys, time, urllib.request
from PIL import Image

ip = sys.argv[1] if len(sys.argv) > 1 else "wettercube-fusion.local"
aus = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "screens")
os.makedirs(aus, exist_ok=True)

def hole(pfad, versuche=6):
    url = "http://%s%s" % (ip, pfad)
    for v in range(versuche):
        try:
            with urllib.request.urlopen(url, timeout=40) as r:
                return r.read()
        except Exception as e:
            time.sleep(1.5)
    raise SystemExit("Abruf fehlgeschlagen: " + url)

def foto(name, s, extra=""):
    for thema, suffix in (("light", "hell"), ("dark", "dunkel")):
        daten = hole("/screenshot.png?s=%s&theme=%s%s" % (s, thema, extra))
        pfad = os.path.join(aus, "%s_%s.png" % (name, suffix))
        open(pfad, "wb").write(daten)
        print("  ", os.path.basename(pfad), len(daten) // 1024, "KB")

def demo(an, v=0):
    hole("/demo?an=%d&v=%d" % (an, v)); time.sleep(2.5)

print("Echte Daten:")
for name, s in (("main", "main"), ("woche", "woche"), ("verlauf", "verlauf"), ("karte", "karte"), ("sensoren", "sensoren"), ("menu", "menu"),
                ("nachtuhr", "nachtuhr"), ("timer", "timer"), ("boot", "boot")):
    foto(name, s)
print("Beispieldaten (Variante 0):")
demo(1, 0)
for name, s in (("allergen", "allergen"), ("tagebuch", "tagebuch"), ("bio", "bio"), ("luft", "luft"), ("lueftenwarnung", "lueftenwarnung"), ("pollenwarnung", "pollenwarnung")):
    foto(name, s)
print("Beispieldaten (Variante 1: Regenhinweis, DWD-Warnung, Update-Symbol):")
demo(1, 1)
foto("main_regen", "main"); foto("warnliste", "warnliste")
print("Beispieldaten (Variante 2: Frost und UV):")
demo(1, 2)
foto("main_frost", "main")
demo(0)

# Die Geraet liefert unkomprimierte PNG (je 1,8 MB): hier verlustfrei kleiner machen
for f in sorted(os.listdir(aus)):
    if (f.endswith("_hell.png") or f.endswith("_dunkel.png")) and not f.startswith("uebersicht"):
        pf = os.path.join(aus, f); Image.open(pf).convert("RGB").save(pf, optimize=True, compress_level=9)

# Uebersicht: 3 x 4 Kacheln (verkleinert), je Design eine Datei
FOLGE = ("main", "woche", "verlauf", "bio", "allergen", "karte", "luft", "sensoren", "menu", "nachtuhr", "pollenwarnung", "boot")
for suffix in ("hell", "dunkel"):
    kw, kh, rand = 512, 300, 14
    bild = Image.new("RGB", (3 * kw + 4 * rand, 4 * kh + 5 * rand), (239, 239, 239))
    for i, n in enumerate(FOLGE):
        k = Image.open(os.path.join(aus, "%s_%s.png" % (n, suffix))).convert("RGB").resize((kw, kh), Image.LANCZOS)
        bild.paste(k, (rand + (i % 3) * (kw + rand), rand + (i // 3) * (kh + rand)))
    bild.save(os.path.join(aus, "uebersicht_%s.png" % suffix), optimize=True)
    print("   uebersicht_%s.png" % suffix)
