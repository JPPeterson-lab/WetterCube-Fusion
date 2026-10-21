#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jan Althaus (JPPeterson-lab)
# Baut WetterCube Fusion (ESP-IDF 5.5.5 muss unter ~/esp/esp-idf-v5.5.5 installiert sein).
#   bash build.sh              bauen (Ziel esp32s3, solange das P4-Board fehlt)
#   bash build.sh flash PORT   bauen und flashen (z. B. /dev/cu.usbmodem1234); mit --nur-firmware nur die Firmware nach app0
#   bash build.sh recovery [-DNOTFALL_SNAPSHOT=1]   Notfall-App bauen (recovery/build/recovery.bin; mit dem Schalter gibt sie Bildschirme seriell aus, fuer Tests)
#   bash build.sh erase PORT   kompletten Flash des Boards loeschen (auch Einstellungen und WLAN), danach neu flashen
#   bash build.sh clean        build/ und sdkconfig loeschen (z. B. nach Aenderungen an sdkconfig.defaults*)
#   TARGET=esp32p4 bash build.sh   P4-Board bauen (Ordner build_p4, sdkconfig_p4; noch ohne Hardware erprobt)
source ~/esp/esp-idf-v5.5.5/export.sh > /dev/null 2>&1 || { echo "ESP-IDF 5.5.5 nicht gefunden"; exit 1; }
cd "$(dirname "$0")" || exit 1
TARGET="${TARGET:-esp32s3}"
case "$1" in
  clean) rm -rf build build_p4 sdkconfig sdkconfig.old sdkconfig_p4; echo "aufgeraeumt"; exit 0 ;;
esac
# P4: eigener Bauordner und eigene sdkconfig, damit der S3-Stand unberuehrt bleibt
P4=""
if [ "$TARGET" = "esp32p4" ]; then P4="-B build_p4 -DIDF_TARGET=esp32p4 -DSDKCONFIG=sdkconfig_p4"
else [ -f sdkconfig ] || idf.py set-target "$TARGET"; fi
DEFS=""; [ -n "$PCLK" ] && DEFS="-DFUSION_PIXEL_CLOCK_MHZ=$PCLK"
# Testschalter immer ausdruecklich setzen: CMake merkt sich -D-Werte im Cache, ein frueherer Testbau wuerde sonst weiterleben
#   TESTHANG=1 TESTVERSION=9.9.9-test bash build.sh   baut eine absichtlich haengende Firmware fuer die Rollback-Pruefung
DEFS="$DEFS -DFUSION_TEST_HANG=${TESTHANG:-OFF} -DFUSION_TEST_VERSION=${TESTVERSION:-}"
if [ "$1" = "recovery" ]; then
  if [ "$TARGET" = "esp32p4" ]; then
    (cd recovery && idf.py -B build_p4 -DIDF_TARGET=esp32p4 -DSDKCONFIG=sdkconfig_p4 ${2:--DNOTFALL_SNAPSHOT=OFF -DNOTFALL_DEMO=OFF} build) || exit 1
    cp recovery/build_p4/wettercube_fusion_notfall.bin recovery/build_p4/recovery.bin
    ls -la recovery/build_p4/recovery.bin
  else
    (cd recovery && { [ -f sdkconfig ] || idf.py set-target "$TARGET"; } && idf.py ${2:--DNOTFALL_SNAPSHOT=OFF -DNOTFALL_DEMO=OFF} build) || exit 1
    cp recovery/build/wettercube_fusion_notfall.bin recovery/build/recovery.bin
    ls -la recovery/build/recovery.bin
  fi
elif [ "$1" = "erase" ]; then
  [ -n "$2" ] || { echo "Verwendung: bash build.sh erase <PORT>"; exit 1; }
  idf.py -p "$2" erase-flash
elif [ "$1" = "flash" ]; then
  [ -n "$2" ] || { echo "Verwendung: bash build.sh flash <PORT> [--nur-firmware]"; exit 1; }
  idf.py $P4 $DEFS build || exit 1
  python3 tools/flash_usb.py "$2" $3        # schreibt Firmware nach app0 und die Notfall-App nach factory (siehe Skript)
else
  idf.py $P4 $DEFS build
fi
