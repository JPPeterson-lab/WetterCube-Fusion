#!/bin/bash
sedi() { if sed --version >/dev/null 2>&1; then sed -i "$@"; else sed -i '' "$@"; fi; }   # GNU- und BSD-sed (Linux/WSL und macOS)
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jan Althaus (JPPeterson-lab)
# PicoPixel UI-Export vorbereiten
# PicoPixel Exportziel: src/ui/  (der IDF-Build uebersetzt src/ rekursiv)
SRC="$(cd "$(dirname "$0")/.." && pwd)/src/ui"

if [ ! -d "$SRC" ]; then
  echo "FEHLER: src/ui/ nicht gefunden."
  echo "PicoPixel Export-Pfad auf: $(realpath "$SRC") setzen."
  exit 1
fi

# PicoPixel generiert "lvgl/lvgl.h" – hier heisst der Header <lvgl.h>
find "$SRC" -name "*.c" -o -name "*.h" | while read f; do
  if grep -q '"lvgl/lvgl.h"' "$f"; then
    sedi 's|#include "lvgl/lvgl.h"|#include <lvgl.h>|g' "$f"
    echo "  lvgl-Include gepatcht: $(basename $f)"
  fi
done

# Wetter-Icons (Meteocons, MIT) liegen unter assets/icons/weather/ und gehen dem PicoPixel-Export vor: so bleibt der Austausch
# gegen die frueheren Dovora-Icons (CC BY-SA 4.0) auch dann bestehen, wenn in PicoPixel noch die alten Bilder stecken.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WICON_DIR="$ROOT/assets/icons/weather"
WETTER_ICONS=" alert day_clear day_partial_cloud day_rain day_snow fog mist night_full_moon_clear night_full_moon_partial_cloud night_full_moon_rain night_full_moon_snow overcast rain rain_thunder sleet snow thunder "

# images/*.c werden von images.c per #include eingebunden UND vom Build direkt kompiliert
# → doppelte Definitionen; Lösung: zu .inc umbenennen
if [ -d "$SRC/images" ]; then
  for f in "$SRC/images"/*.c; do
    [ -f "$f" ] || continue
    name="$(basename "$f" .c)"
    if [[ "$WETTER_ICONS" == *" $name "* && -f "$WICON_DIR/$name.inc" ]]; then
      rm -f "$f"
      echo "  Wetter-Icon aus assets/icons/weather verwendet: $name"
      continue
    fi
    base="${f%.c}"
    mv "$f" "${base}.inc"
    echo "  Image umbenannt: $(basename "$f") → $(basename "${base}.inc")"
  done
  if [ -f "$SRC/images.c" ]; then
    sedi 's|#include "images/\(.*\)\.c"|#include "src/ui/images/\1.inc"|g' "$SRC/images.c"
    sedi 's|#include "images/\(.*\)\.inc"|#include "src/ui/images/\1.inc"|g' "$SRC/images.c"
    for n in $WETTER_ICONS; do
      [ -f "$WICON_DIR/$n.inc" ] && sedi "s|src/ui/images/$n.inc|assets/icons/weather/$n.inc|g" "$SRC/images.c"
    done
    echo "  images.c Include-Pfade aktualisiert (Wetter-Icons aus assets/icons/weather)"
  fi
fi

# Namenskonflikt: das PicoPixel-Bild "pollen" heisst wie die globale Variable `pollen` (app_state.h) → Symbol in "pollen_img" umbenennen
if [ -f "$SRC/images.h" ] && grep -q "lv_img_dsc_t pollen;" "$SRC/images.h"; then
  sedi 's/extern const lv_img_dsc_t pollen;/extern const lv_img_dsc_t pollen_img;/' "$SRC/images.h"
  sedi 's/&pollen }/\&pollen_img }/' "$SRC/images.c"
  [ -f "$SRC/images/pollen.inc" ] && sedi 's/const lv_img_dsc_t pollen = {/const lv_img_dsc_t pollen_img = {/' "$SRC/images/pollen.inc"
  sedi 's/lv_img_set_src(obj, &pollen);/lv_img_set_src(obj, \&pollen_img);/' "$SRC/screens.c"
  echo "  Bild pollen → pollen_img umbenannt (Namenskonflikt mit der Variable pollen)"
fi

# PicoPixel exportiert nur Bilder, die ein Widget benutzt. Die Wetter-Icons setzt die Firmware zur Laufzeit: Deklaration und Einbindung ergaenzen
python3 - "$SRC" "$WICON_DIR" $WETTER_ICONS <<'PYEOF'
import sys, os
src, wdir, names = sys.argv[1], sys.argv[2], sys.argv[3:]
h = open(os.path.join(src, "images.h")).read()
c = open(os.path.join(src, "images.c")).read()
for n in names:
    if not os.path.isfile(os.path.join(wdir, n + ".inc")): continue
    decl = "extern const lv_img_dsc_t %s;" % n
    if decl not in h:
        h = h.replace("extern const ext_img_desc_t images", decl + "\nextern const ext_img_desc_t images", 1)
    inc = '#include "assets/icons/weather/%s.inc"' % n
    if inc not in c:
        c = c.replace("const ext_img_desc_t images", inc + "\nconst ext_img_desc_t images", 1)
open(os.path.join(src, "images.h"), "w").write(h)
open(os.path.join(src, "images.c"), "w").write(c)
PYEOF
echo "  Wetter-Icons in images.h/images.c vervollstaendigt"

# Platzhalter-Bilder: Die Icons der Allergen-/Biowetter-Screens setzt die Firmware zur Laufzeit aus dem Code (icons.h); in PicoPixel stehen
# dort nur Stand-ins. Wird ein Bild ausschliesslich von solchen Widgets (imagepollen*, imagebio*, imagewind*, imageverlauf*) benutzt, ersetzt das Skript es durch ein
# 1x1-Bild: spart je 128x128-Bild 48 KB Flash.
python3 - "$SRC" <<'PYEOF'
import sys, os, re
src = sys.argv[1]
scr = open(os.path.join(src, "screens.c")).read()
img_dir = os.path.join(src, "images")
for f in sorted(os.listdir(img_dir)):
    if not f.endswith(".inc"): continue
    name = f[:-4]
    sym = "pollen_img" if name == "pollen" else name
    uses = [m.start() for m in re.finditer(r"lv_img_set_src\(obj, &%s\)" % re.escape(sym), scr)]
    if not uses: continue
    owners = []
    for u in uses:
        j = scr.rfind("// ", 0, u)
        owners.append(scr[j+3:scr.index("\n", j)].strip())
    if all(re.match(r"image(pollen|bio|wind|verlauf)", o) for o in owners):
        stub = (f"// Platzhalter (tools/deploy_ui.sh): die Firmware setzt das Icon zur Laufzeit aus icons.h\n"
                f'#include "src/ui/screens.h"\n'
                f"static const uint8_t {sym}_stub_map[] = {{0x00, 0x00, 0x00}};\n"
                f"const lv_img_dsc_t {sym} = {{\n"
                f"  .header = {{ .cf = LV_IMG_CF_RGB565A8, .always_zero = 0, .reserved = 0, .w = 1, .h = 1 }},\n"
                f"  .data_size = 3,\n  .data = {sym}_stub_map,\n}};\n")
        open(os.path.join(img_dir, f), "w").write(stub)
        print("  Platzhalter-Bild ersetzt: " + sym)
PYEOF

# screens.c: PicoPixel exportiert Farben als lv_color_hex(0xffRRGGBB)
if [ -f "$SRC/screens.c" ]; then
  sedi 's/lv_color_hex(0xff/lv_color_hex(0x/g' "$SRC/screens.c"
  echo "  screens.c: lv_color_hex 0xff-Prefix entfernt (PicoPixel ARGB→RGB Fix)"
fi

# ui.c: FADE_IN-Animation → NONE (Crash-Fix)
if [ -f "$SRC/ui.c" ]; then
  sedi 's|lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false)|lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false)|g' "$SRC/ui.c"
  echo "  ui.c: FADE_IN-Animation → NONE (Crash-Fix)"
fi

# screens.c: Sonderzeichen ² (U+00B2) → ASCII (LVGL Montserrat-Font hat kein ²)
if [ -f "$SRC/screens.c" ]; then
  if grep -q 'mg/m²' "$SRC/screens.c"; then
    sedi 's|mg/m²|ug/m3|g' "$SRC/screens.c"
    echo "  screens.c: Sonderzeichen mg/m² → ug/m3 ersetzt (Font-Fix)"
  fi
fi

# ── Verifikation: Prüfe ob alle vom Code verwendeten Objekt-Namen noch vorhanden sind ──
echo ""
echo "Verifikation screens.h – erwartete Objekte:"
EXPECTED=(
  "screenallergen" "button_1" "button_1_1" "button_1_2"
  "screensensoren" "containersensoren" "buttonsensorenzurueck" "buttonsensorenhome" "buttonsensorenweiter"
  "imagewindpfeil" "screenverlauf" "chartverlauf" "chartregen" "labelverlauftitel" "labelverlaufminmax" "labelverlaufregen" "imageverlauftemp" "imageverlaufregen" "buttonverlaufzurueck" "buttonverlaufweiter" "buttonverlaufhome" "imagepollenhasel" "imagepollenerle" "imagepollenesche" "imagepollenbirke" "imagepollengraeser" "imagepollenroggen" "imagepollenbeifuss" "imagepollenambrosia"
  "labelgruppestunden" "labelgruppetage" "labelgruppeheute" "labelgruppemorgen"
  "imagebiobefinden" "imagebiobpniedrig" "imagebiobphoch" "imagebiorheumaentz" "imagebiorheumadeg" "imagebioasthma" "imagebiowaerme"
  "screenboot" "labelstatus" "barwifi"
  "screen_1" "labelbuttonforward" "labelbuttonbackward" "fc_settings"
  "screenbiowetter" "labelbuttonbackward_4" "labelbuttonforward_6" "labelbuttonhome_7"
  "screenwarnkarte1" "labelbuttonbackward_1" "labelbuttonforward_1" "labelbuttonhome" "labelbuttonscreenhub"
  "screenwarnkarte2" "labelbuttonscreenhubback" "labelbuttonhome_1"
  "screenairquality" "labelbuttonbackward_2" "labelbuttonforward_2" "labelbuttonhome_2"
  "screenmenu" "labelbuttonmenu_2"
  "screenwarnungpollen"
)

MISSING=0
if [ -f "$SRC/screens.h" ]; then
  for obj in "${EXPECTED[@]}"; do
    if ! grep -q "\b${obj}\b" "$SRC/screens.h"; then
      echo "  WARNUNG: '$obj' nicht in screens.h gefunden – Button/Objekt umbenannt?"
      MISSING=$((MISSING+1))
    fi
  done
  if [ $MISSING -eq 0 ]; then
    echo "  Alle erwarteten Objekte vorhanden."
  else
    echo ""
    echo "  !! $MISSING Objekte fehlen – navigation.cpp (nav_init) / setLabel pruefen !!"
  fi
fi

echo ""
echo "Fertig. src/ui/ bereit fuer den Build (bash build.sh)."
