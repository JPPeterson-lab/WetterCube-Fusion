#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jan Althaus (JPPeterson-lab)
"""Flasht Fusion per USB mit der Notfall-Partitionstabelle.

Mit einer Partition vom Typ "factory" legt `idf.py flash` die Firmware auf die Notfall-Partition und der leere otadata-Bereich
startet sie von dort. Fusion will die Firmware aber in "app0" (ota_0) und die Notfall-App in "factory". Darum schreibt dieses Skript
alle Teile selbst: Bootloader, Partitionstabelle, otadata (zeigt auf ota_0), Notfall-App (falls gebaut) und die Firmware.

Aufruf (aus build.sh):  python3 tools/flash_usb.py PORT [--nur-firmware]   (TARGET=esp32p4 fuer das P4-Board: Ordner build_p4, partitions_p4.csv)
  --nur-firmware  schreibt nur die Firmware nach ota_0 und stellt otadata auf ota_0 (Bootloader, Tabelle und Notfall-App bleiben)
"""
import binascii, csv, json, os, struct, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
P4 = os.environ.get("TARGET") == "esp32p4"
B = os.path.join(ROOT, "build_p4" if P4 else "build")

def offsets():
    o = {}
    for row in csv.reader(l for l in open(os.path.join(ROOT, "partitions_p4.csv" if P4 else "partitions.csv")) if not l.startswith("#") and l.strip()):
        row = [c.strip() for c in row]
        o[row[0]] = (int(row[3], 0), int(row[4], 0))
    return o

def otadata_ota0():
    """otadata mit Eintrag 'Sequenz 1' = ota_0 (Aufbau wie in components/app_update/otatool.py)"""
    d = bytearray(b"\xff" * 0x2000)
    seq = struct.pack("<I", 1)
    d[0:4] = seq
    d[28:32] = struct.pack("<I", binascii.crc32(seq, 0xFFFFFFFF) % (1 << 32))
    path = os.path.join(B, "otadata_ota0.bin")
    open(path, "wb").write(d)
    return path

def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    if sys.argv[1] == "--nur-otadata":
        print(otadata_ota0()); return
    port = sys.argv[1]; nur_fw = "--nur-firmware" in sys.argv
    pt = offsets()
    fa = json.load(open(os.path.join(B, "flasher_args.json")))
    s = fa["flash_settings"]; chip = fa["extra_esptool_args"]["chip"]
    app = os.path.join(B, "wettercube_fusion.bin")
    teile = []
    if not nur_fw:
        teile.append((fa["bootloader"].get("offset", "0x0"), os.path.join(B, fa["bootloader"]["file"])))   # S3: 0x0, P4: 0x2000
        teile.append((hex(0x8000), os.path.join(B, fa["partition-table"]["file"])))
        rec = os.path.join(ROOT, "recovery", "build_p4" if P4 else "build", "recovery.bin")
        if os.path.exists(rec): teile.append((hex(pt["factory"][0]), rec))
        else: print("Hinweis: recovery.bin fehlt (bash build.sh recovery), die Notfall-Partition bleibt leer")
    teile.append((hex(pt["otadata"][0]), otadata_ota0()))
    teile.append((hex(pt["app0"][0]), app))
    cmd = [sys.executable, "-m", "esptool", "--chip", chip, "-p", port, "-b", "460800", "--before", "default_reset", "--after", "hard_reset",
           "write_flash", "--flash_mode", s["flash_mode"], "--flash_size", s["flash_size"], "--flash_freq", s["flash_freq"]]
    for off, f in teile: cmd += [off, f]
    print(" ".join(cmd[2:]))
    sys.exit(subprocess.call(cmd))

main()
