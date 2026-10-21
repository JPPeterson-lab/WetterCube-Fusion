#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jan Althaus (JPPeterson-lab)
"""Packt die gerenderten Animationsbilder der Wettersymbole zu assets/icons/anim/wetteranim.bin (wird nur beim ESP32-P4 eingebunden).

Aufruf:  python3 tools/anim_pack.py <frames-Ordner> [--fps 12] [--farben 0]
  <frames-Ordner>  enthaelt je Symbol einen Ordner <name>/000.png, 001.png ... (128x128 RGBA, aus tools/anim_render)
  --farben N       N > 0: Bilder auf N Farben (mit Transparenz) verkleinern, spart Platz (0 = verlustfrei)

Je Symbol wird die kleinste Periode gesucht (z. B. die Sonne mit ihren 8 gleichen Strahlen wiederholt sich nach einem Achtel der
Drehung), nur diese Bilder kommen in die Datei. Namen mit Unterstrich wie in src/icons/wetter.h (clear_day).

Aufbau der Datei (little endian):
  Kopf    16 Byte: "WXAN", u16 Version (1), u16 Anzahl, u16 fps, u16 Breite, u16 Hoehe, u16 0
  Eintrag 40 Byte je Symbol: char name[32], u32 Bilderzahl, u32 Offset der Bildtabelle
  Tabelle je Symbol: je Bild u32 Offset (ab Dateianfang), u32 Groesse; danach die PNG-Daten
"""
import io, os, struct, sys
import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
AUS = os.path.join(ROOT, "assets/icons/anim/wetteranim.bin")

def periode(frames):
    n = len(frames)
    a = np.stack([np.asarray(f, dtype=np.int16) for f in frames])
    for k in range(1, n):
        if n % k: continue
        if all(np.abs(a[(i + k) % n] - a[i]).max() <= 6 for i in range(n)): return k
    return n

def png(im, farben):
    if farben: im = im.quantize(colors=farben, method=Image.Quantize.FASTOCTREE)   # Palette mit Transparenz
    b = io.BytesIO(); im.save(b, "PNG", optimize=True); return b.getvalue()

def main():
    if len(sys.argv) < 2: print(__doc__); return 1
    ordner = sys.argv[1]
    fps = int(sys.argv[sys.argv.index("--fps") + 1]) if "--fps" in sys.argv else 12
    farben = int(sys.argv[sys.argv.index("--farben") + 1]) if "--farben" in sys.argv else 0
    namen = sorted(d for d in os.listdir(ordner) if os.path.isdir(os.path.join(ordner, d)))
    daten = []; gesamt = 0
    for n in namen:
        dat = sorted(f for f in os.listdir(os.path.join(ordner, n)) if f.endswith(".png"))
        ims = [Image.open(os.path.join(ordner, n, f)).convert("RGBA") for f in dat]
        k = periode(ims)
        pngs = [png(im, farben) for im in ims[:k]]
        daten.append((n.replace("-", "_"), pngs)); gesamt += sum(len(p) for p in pngs)
        print(f"{n:34s} {len(ims):3d} -> {k:3d} Bilder, {sum(len(p) for p in pngs)/1024:6.0f} KB")
    breite, hoehe = ims[0].size
    kopf = struct.pack("<4sHHHHHH", b"WXAN", 1, len(daten), fps, breite, hoehe, 0)
    pos = 16 + 40 * len(daten); eintr = b""; tabellen = b""; blob = b""
    # Tabellen zuerst, danach die Bilddaten
    tab_groesse = sum(8 * len(p) for _, p in daten)
    daten_start = pos + tab_groesse; tpos = pos; dpos = daten_start
    for name, pngs in daten:
        eintr += struct.pack("<32sII", name.encode(), len(pngs), tpos)
        t = b""
        for p in pngs:
            t += struct.pack("<II", dpos, len(p)); blob += p; dpos += len(p)
        tabellen += t; tpos += len(t)
    os.makedirs(os.path.dirname(AUS), exist_ok=True)
    open(AUS, "wb").write(kopf + eintr + tabellen + blob)
    print(f"{len(daten)} Symbole, {gesamt/1024/1024:.2f} MB Bilddaten, Datei {os.path.getsize(AUS)/1024/1024:.2f} MB -> {AUS}")

sys.exit(main())
