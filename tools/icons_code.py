#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jan Althaus (JPPeterson-lab)
"""Wandelt PNG-Icons in LVGL-Bilder (RGB565A8) fuer die Firmware um (ohne PicoPixel, kein Limit fuer Bilder dort).

Aufruf:  python3 tools/icons_code.py
Quellen: assets/icons/meteocons/*.png (Meteocons, MIT) und assets/icons/eigene/*.png (eigene Werke), 128x128 mit Alpha.
Ausgabe: assets/icons/code/ic_<name>.inc (je Icon) und src/icons/icons.cpp (Verzeichnis name -> lv_img_dsc_t).
Neue Icons: PNG in einen der Ordner legen, hier in ICONS eintragen (Name, Datei, Zielgroesse), Skript ausfuehren.
"""
import os, sys
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
M = "assets/icons/meteocons/"
E = "assets/icons/eigene/"
# (Name im Code, Quelle, Kantenlaenge in Pixeln)
ICONS = [
    ("thermometer",   M + "thermometer.png",   64),
    ("humidity",      M + "humidity.png",      64),
    ("barometer",     M + "barometer.png",     64),
    ("wind",          M + "wind.png",          64),
    ("compass",       M + "compass.png",       64),
    ("uv-index",      M + "uv-index.png",      64),
    ("raindrop",      M + "raindrop.png",      64),
    ("sunrise",       M + "sunrise.png",       64),
    ("sunset",        M + "sunset.png",        64),
    ("pollen",        M + "pollen.png",        64),
    ("pollen-flower", M + "pollen-flower.png", 64),
    ("pollen-tree",   M + "pollen-tree.png",   64),
    ("pollen-grass",  M + "pollen-grass.png",  64),
    ("pollen-weed",   M + "pollen-weed.png",   64),
    ("windpfeil",     E + "windpfeil.png",     64),
    ("bio-befinden",    E + "bio-befinden.png",    64),
    ("bio-bp-niedrig",  E + "bio-bp-niedrig.png",  64),
    ("bio-bp-hoch",     E + "bio-bp-hoch.png",     64),
    ("bio-rheuma-entz", E + "bio-rheuma-entz.png", 64),
    ("bio-rheuma-deg",  E + "bio-rheuma-deg.png",  64),
    ("bio-asthma",      E + "bio-asthma.png",      64),
    ("bio-waerme",      M + "thermometer-sun.png", 64),
    ("humidity-36",  M + "humidity.png",      36),   # exakte Groessen fuer die Luft-Kachel (kein Zoom noetig, keine Verschiebung)
    ("barometer-36", M + "barometer.png",     36),
    ("windpfeil-40", E + "windpfeil.png",     40),
    ("moon-new", M + "moon-new.png", 88),
    ("moon-waxing-crescent", M + "moon-waxing-crescent.png", 88),
    ("moon-first-quarter", M + "moon-first-quarter.png", 88),
    ("moon-waxing-gibbous", M + "moon-waxing-gibbous.png", 88),
    ("moon-full", M + "moon-full.png", 88),
    ("moon-waning-gibbous", M + "moon-waning-gibbous.png", 88),
    ("moon-last-quarter", M + "moon-last-quarter.png", 88),
    ("moon-waning-crescent", M + "moon-waning-crescent.png", 88),
]

def ident(name):
    return "ic_" + name.replace("-", "_")

def rgb565a8(im):
    w, h = im.size
    px = list(im.getdata())
    col = bytearray(); alpha = bytearray()
    for r, g, b, a in px:
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        col += bytes((v & 0xFF, v >> 8))
        alpha.append(a)
    return bytes(col + alpha)

def main():
    outdir = os.path.join(ROOT, "assets/icons/code")
    os.makedirs(outdir, exist_ok=True)
    total = 0
    for name, src, size in ICONS:
        im = Image.open(os.path.join(ROOT, src)).convert("RGBA").resize((size, size), Image.LANCZOS)
        data = rgb565a8(im)
        total += len(data)
        idn = ident(name)
        lines = [f"// Erzeugt von tools/icons_code.py aus {src} ({size}x{size}, RGB565A8). Nicht von Hand aendern.",
                 "#include <lvgl.h>",
                 f"static const uint8_t {idn}_map[] = {{"]
        for i in range(0, len(data), 32):
            lines.append("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 32]) + ",")
        lines += ["};",
                  f"extern const lv_img_dsc_t {idn};",
                  f"const lv_img_dsc_t {idn} = {{",
                  f"  .header = {{ .cf = LV_IMG_CF_RGB565A8, .always_zero = 0, .reserved = 0, .w = {size}, .h = {size} }},",
                  f"  .data_size = {len(data)},",
                  f"  .data = {idn}_map,",
                  "};", ""]
        open(os.path.join(outdir, idn + ".inc"), "w").write("\n".join(lines))
    reg = ["// Erzeugt von tools/icons_code.py. Nicht von Hand aendern.",
           '#include "icons.h"', "#include <string.h>", ""]
    for name, _, _ in ICONS:
        reg.append(f'#include "assets/icons/code/{ident(name)}.inc"')
    reg += ["", "struct IconEintrag { const char* name; const lv_img_dsc_t* dsc; };", "static const IconEintrag ICONS[] = {"]
    for name, _, _ in ICONS:
        reg.append(f'  {{"{name}", &{ident(name)}}},')
    reg += ["};", "",
            "int iconAnzahl() { return (int)(sizeof(ICONS) / sizeof(ICONS[0])); }",
            "const char* iconName(int i) { return (i >= 0 && i < iconAnzahl()) ? ICONS[i].name : \"\"; }",
            "const lv_img_dsc_t* iconHolen(const char* name) {",
            "  if (!name) return nullptr;",
            "  for (const IconEintrag& e : ICONS) if (strcmp(e.name, name) == 0) return e.dsc;",
            "  return nullptr;", "}", ""]
    open(os.path.join(ROOT, "src/icons/icons.cpp"), "w").write("\n".join(reg))
    print(f"{len(ICONS)} Icons, {total/1024:.0f} KB Bilddaten")

if __name__ == "__main__":
    sys.exit(main())
