#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jan Althaus (JPPeterson-lab)
"""Wandelt die Wetter-Symbole (Meteocons, MIT) in LVGL-Bilder (RGB565A8) fuer die Firmware um, ohne PicoPixel.

Aufruf:  python3 tools/wetter_icons.py
Quellen: assets/icons/wetter/<name>.png (112x112 mit Alpha, aus den Meteocons "fill" gerendert)
Ausgabe: assets/icons/wetter/wx_<name>.inc, src/icons/wetter.h, src/icons/wetter.cpp
         und winzige Platzhalter assets/icons/weather/<alter Name>.inc: die frueheren 16 Wetterbilder aus PicoPixel (je 49 KB) werden
         nur noch als Anfangsbild der Widgets gebraucht, die Firmware setzt zur Laufzeit ein Symbol aus diesem Satz.
"""
import os, sys, glob
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DIR = os.path.join(ROOT, "assets/icons/wetter")
ALT = ["day_clear", "day_partial_cloud", "day_rain", "day_snow", "fog", "mist", "night_full_moon_clear",
       "night_full_moon_partial_cloud", "night_full_moon_rain", "night_full_moon_snow", "overcast", "rain", "rain_thunder",
       "sleet", "snow", "thunder"]   # "alert" bleibt: es steht auf den Warnbildschirmen

def ident(name): return "wx_" + name.replace("-", "_")

def rgb565a8(im):
    col = bytearray(); alpha = bytearray()
    for r, g, b, a in im.getdata():
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        col += bytes((v & 0xFF, v >> 8)); alpha.append(a)
    return bytes(col + alpha)

def inc(idn, w, h, data, quelle):
    z = [f"// Erzeugt von tools/wetter_icons.py aus {quelle} ({w}x{h}, RGB565A8). Nicht von Hand aendern.", "#include <lvgl.h>",
         f"static const uint8_t {idn}_map[] = {{"]
    for i in range(0, len(data), 32): z.append("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 32]) + ",")
    z += ["};", f"extern const lv_img_dsc_t {idn};", f"const lv_img_dsc_t {idn} = {{",
          f"  .header = {{ .cf = LV_IMG_CF_RGB565A8, .always_zero = 0, .reserved = 0, .w = {w}, .h = {h} }},",
          f"  .data_size = {len(data)},", f"  .data = {idn}_map,", "};", ""]
    return "\n".join(z)

def main():
    namen = sorted(os.path.basename(f)[:-4] for f in glob.glob(os.path.join(DIR, "*.png")))
    gesamt = 0
    for n in namen:
        im = Image.open(os.path.join(DIR, n + ".png")).convert("RGBA")
        d = rgb565a8(im); gesamt += len(d)
        open(os.path.join(DIR, ident(n) + ".inc"), "w").write(inc(ident(n), im.width, im.height, d, f"assets/icons/wetter/{n}.png"))
    h = ["// Erzeugt von tools/wetter_icons.py. Nicht von Hand aendern.", "#pragma once", "#include <lvgl.h>", "#ifdef __cplusplus", 'extern "C" {', "#endif"]
    h += [f"extern const lv_img_dsc_t {ident(n)};" for n in namen]
    h += ["#ifdef __cplusplus", "}", "#endif", ""]
    open(os.path.join(ROOT, "src/icons/wetter.h"), "w").write("\n".join(h))
    c = ["// Erzeugt von tools/wetter_icons.py. Nicht von Hand aendern.", '#include "wetter.h"', ""]
    c += [f'#include "assets/icons/wetter/{ident(n)}.inc"' for n in namen] + [""]
    open(os.path.join(ROOT, "src/icons/wetter.cpp"), "w").write("\n".join(c))
    # Platzhalter fuer die alten Namen (1x1, durchsichtig), C-tauglich: images.c ist eine C-Datei
    stub = os.path.join(ROOT, "assets/icons/weather")
    for a in ALT:
        open(os.path.join(stub, a + ".inc"), "w").write(
            f"// Platzhalter (1x1, durchsichtig) fuer das fruehere PicoPixel-Bild {a}; die Firmware setzt zur Laufzeit ein Symbol aus tools/wetter_icons.py.\n"
            '#include "src/ui/screens.h"\n'
            f"static const uint8_t {a}_map[] = {{ 0x00, 0x00, 0x00 }};\n"
            f"const lv_img_dsc_t {a} = {{\n  .header = {{ .cf = LV_IMG_CF_RGB565A8, .always_zero = 0, .reserved = 0, .w = 1, .h = 1 }},\n"
            f"  .data_size = 3,\n  .data = {a}_map,\n}};\n")
    print(f"{len(namen)} Symbole, {gesamt/1024:.0f} KB Bilddaten, {len(ALT)} Platzhalter")

if __name__ == "__main__":
    sys.exit(main())
