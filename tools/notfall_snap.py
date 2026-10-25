#!/usr/bin/env python3
"""Liest die Bildschirm-Ausgabe der Notfall-App (NOTFALL_SNAPSHOT-Build) von der seriellen Schnittstelle und speichert PNGs.
Aufruf: python3 tools/notfall_snap.py PORT AUSGABE_PREFIX [SEKUNDEN] [--reset]"""
import base64, struct, sys, time, zlib, serial

def png(pfad, w, h, rgb):
    roh = b''.join(b'\x00' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(pfad, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(roh)) + chunk(b'IEND', b''))
port, prefix = sys.argv[1], sys.argv[2]
dauer = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 40
s = serial.Serial(port, 115200, timeout=1)
if "--reset" in sys.argv:
    s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
rows = None; n = 0; t0 = time.time(); buf = b""
while time.time() - t0 < dauer:
    buf += s.read(65536)
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        line = line.decode(errors="replace").strip()
        if line.startswith("SNAP"): rows = []
        elif line.startswith("S:") and rows is not None:
            try:   # Zeile = Lauflängenkodierung: je Lauf [Anzahl, Pixel niedrig, Pixel hoch]
                d = base64.b64decode(line[2:]); z = bytearray()
                for i in range(0, len(d) - 2, 3): z += bytes((d[i+1], d[i+2])) * d[i]
                rows.append(bytes(z).ljust(1024, b"\0")[:1024])
            except Exception: rows.append(b"\0" * 1024)   # beschädigte Zeile: schwarz
        elif line == "SEND" and rows:
            w, h = 512, len(rows)
            rgb = bytearray()
            for r in rows:
                for x in range(w):
                    p = r[x*2] | (r[x*2+1] << 8)
                    rgb += bytes((((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31))
            n += 1; png(f"{prefix}{n}.png", w, h, bytes(rgb)); print("gespeichert", f"{prefix}{n}.png"); rows = None
