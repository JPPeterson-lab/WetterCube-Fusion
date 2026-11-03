// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Bilderrahmen: Dateiliste, JPEG-Dekodierung und Web-Schnittstelle, siehe bilderrahmen.h.
// Dekodierung mit TJpgDec (kommt mit LVGL, LV_USE_SJPG): Baseline-JPEG, Verkleinerung beim Dekodieren auf 1/2, 1/4 oder 1/8, danach glattes
// Anpassen auf die Bildschirmgroesse (bilinear). Progressive JPEG kann TJpgDec nicht; das Hochladen in der Web-Oberflaeche wandelt Fotos im
// Browser in passende Baseline-JPEG um (auch die Ausrichtung nach EXIF), Dateien, die du direkt auf die Karte kopierst, sollten Baseline-JPEG sein.
#include "app_state.h"
#include "bilderrahmen.h"
#include "../platform/WebServer.h"
#ifdef FUSION_BILDERRAHMEN   // nur ESP32-P4: der S3 hat zu wenig internen RAM fuer Dekodierung und Lade-Task
#include "../storage/storage.h"
extern "C" {
#include "src/extra/libs/sjpg/tjpgd.h"
}
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mutex>
#include <math.h>
#include <ctype.h>
#include "esp_heap_caps.h"

#define ORDNER      "/sdcard/bilder"
#define MAX_BILDER  200
#define MAX_DATEI   (8u * 1024u * 1024u)    // groessere Dateien werden nicht geladen
#define NAME_LEN    40

static std::mutex s_mtx;
static char (*s_namen)[NAME_LEN] = nullptr;   // PSRAM, sortiert
static int s_n = 0;
static unsigned long s_stand = 0;

static bool istJpeg(const char* n) {
  if (n[0] == '.') return false;                                  // "._bild.jpg" und Ordnermarken vom Mac
  const size_t l = strlen(n);
  auto ende = [&](const char* e) { const size_t el = strlen(e); if (l < el) return false; for (size_t i = 0; i < el; i++) if (tolower((unsigned char)n[l - el + i]) != e[i]) return false; return true; };
  return ende(".jpg") || ende(".jpeg");
}

int rahmenAnzahl(bool neuLesen) {
  std::lock_guard<std::mutex> g(s_mtx);
  if (!speicherBereit()) { s_n = 0; return 0; }
  if (!neuLesen && s_namen && millis() - s_stand < 10UL * 60000UL) return s_n;
  if (!s_namen) s_namen = (char(*)[NAME_LEN])heap_caps_calloc(MAX_BILDER, NAME_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_namen) return 0;
  s_n = 0;
  DIR* d = opendir(ORDNER);
  if (d) {
    struct dirent* e;
    while ((e = readdir(d)) != nullptr && s_n < MAX_BILDER) {
      if (e->d_type == DT_DIR || strlen(e->d_name) >= NAME_LEN || !istJpeg(e->d_name)) continue;
      strlcpy(s_namen[s_n++], e->d_name, NAME_LEN);
    }
    closedir(d);
  }
  for (int a = 1; a < s_n; a++) {                                  // nach Namen sortieren (Einfuegesortierung)
    char t[NAME_LEN]; memcpy(t, s_namen[a], NAME_LEN);
    int b = a - 1;
    while (b >= 0 && strcasecmp(s_namen[b], t) > 0) { memcpy(s_namen[b + 1], s_namen[b], NAME_LEN); b--; }
    memcpy(s_namen[b + 1], t, NAME_LEN);
  }
  s_stand = millis();
  return s_n;
}

bool rahmenName(int index, char* out, size_t n) {
  rahmenAnzahl();
  std::lock_guard<std::mutex> g(s_mtx);
  if (!s_namen || index < 0 || index >= s_n) return false;
  strlcpy(out, s_namen[index], n);
  return true;
}

// ── Dekodieren ───────────────────────────────────────────────────────────────────────────
struct JCtx { const uint8_t* d; size_t n, pos; uint16_t* px; int bw, bh; };

static size_t jEingabe(JDEC* jd, uint8_t* buf, size_t len) {
  JCtx* c = (JCtx*)jd->device;
  const size_t r = len < c->n - c->pos ? len : c->n - c->pos;
  if (buf) memcpy(buf, c->d + c->pos, r);
  c->pos += r;
  return r;
}

static int jAusgabe(JDEC* jd, void* bitmap, JRECT* rc) {
  JCtx* c = (JCtx*)jd->device;
  const uint8_t* s = (const uint8_t*)bitmap;                       // RGB888 (JD_FORMAT 0)
  for (int y = rc->top; y <= rc->bottom; y++) {
    for (int x = rc->left; x <= rc->right; x++, s += 3) {
      if (x < c->bw && y < c->bh) c->px[(size_t)y * c->bw + x] = (uint16_t)(((s[0] & 0xF8) << 8) | ((s[1] & 0xFC) << 3) | (s[2] >> 3));
    }
  }
  return 1;
}

static inline void rgb(uint16_t p, int* r, int* g, int* b) { *r = (p >> 11) & 31; *g = (p >> 5) & 63; *b = p & 31; }

// bilinear verkleinern oder (bei gleicher Groesse) uebernehmen
static uint16_t* anpassen(const uint16_t* q, int qw, int qh, int zw, int zh) {
  uint16_t* z = (uint16_t*)heap_caps_malloc((size_t)zw * zh * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!z) return nullptr;
  const uint32_t sx = (uint32_t)((qw << 16) / zw), sy = (uint32_t)((qh << 16) / zh);
  for (int y = 0; y < zh; y++) {
    const uint32_t fy = y * sy; const int y0 = fy >> 16; const int y1 = y0 + 1 < qh ? y0 + 1 : y0; const int wy = (fy >> 8) & 255;
    for (int x = 0; x < zw; x++) {
      const uint32_t fx = x * sx; const int x0 = fx >> 16; const int x1 = x0 + 1 < qw ? x0 + 1 : x0; const int wx = (fx >> 8) & 255;
      int r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
      rgb(q[(size_t)y0 * qw + x0], &r00, &g00, &b00); rgb(q[(size_t)y0 * qw + x1], &r10, &g10, &b10);
      rgb(q[(size_t)y1 * qw + x0], &r01, &g01, &b01); rgb(q[(size_t)y1 * qw + x1], &r11, &g11, &b11);
      auto mix = [&](int a, int b, int c, int d) { const int o = a * (256 - wx) + b * wx, u = c * (256 - wx) + d * wx; return (o * (256 - wy) + u * wy) >> 16; };
      z[(size_t)y * zw + x] = (uint16_t)((mix(r00, r10, r01, r11) << 11) | (mix(g00, g10, g01, g11) << 5) | mix(b00, b10, b01, b11));
    }
  }
  return z;
}

bool rahmenDekodieren(int index, int maxW, int maxH, RahmenBild* out) {
  char name[NAME_LEN];
  if (!rahmenName(index, name, sizeof(name))) return false;
  char pfad[96]; snprintf(pfad, sizeof(pfad), ORDNER "/%s", name);
  struct stat st;
  if (stat(pfad, &st) != 0 || st.st_size < 200) return false;
  if ((size_t)st.st_size > MAX_DATEI) { logDiag("[Rahmen] %s ist zu gross (%u Bytes)", name, (unsigned)st.st_size); return false; }
  uint8_t* datei = (uint8_t*)heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!datei) return false;
  bool ok = false;
  FILE* f = fopen(pfad, "rb");
  if (f) { ok = fread(datei, 1, st.st_size, f) == (size_t)st.st_size; fclose(f); }
  if (!ok) { heap_caps_free(datei); return false; }

  const size_t POOL = 16 * 1024;
  void* pool = heap_caps_malloc(POOL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  JDEC* jd = (JDEC*)heap_caps_malloc(sizeof(JDEC), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint16_t* roh = nullptr; uint16_t* fertig = nullptr; int zw = 0, zh = 0;
  JCtx c = {datei, (size_t)st.st_size, 0, nullptr, 0, 0};
  ok = false;
  if (pool && jd) {
    JRESULT r = jd_prepare(jd, jEingabe, pool, POOL, &c);
    if (r != JDR_OK) logDiag("[Rahmen] %s: JPEG nicht lesbar (Fehler %d, progressive Dateien gehen nicht)", name, (int)r);
    else {
      const int w = jd->width, h = jd->height;
      float fkt = fminf(fminf((float)maxW / w, (float)maxH / h), 1.0f);
      int s = 0;
      while (s < 3 && (1 << (s + 1)) <= 1.0f / fkt) s++;
      c.bw = ((w - 1) >> s) + 1; c.bh = ((h - 1) >> s) + 1;
      roh = (uint16_t*)heap_caps_malloc((size_t)c.bw * c.bh * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      c.px = roh;
      if (roh) {
        const unsigned long t0 = millis();
        r = jd_decomp(jd, jAusgabe, (uint8_t)s);
        if (r != JDR_OK) logDiag("[Rahmen] %s: Dekodieren fehlgeschlagen (Fehler %d)", name, (int)r);
        else {
          zw = (int)lroundf(w * fkt); zh = (int)lroundf(h * fkt);
          if (zw < 1) zw = 1;
          if (zh < 1) zh = 1;
          if (zw == c.bw && zh == c.bh) { fertig = roh; roh = nullptr; }
          else fertig = anpassen(roh, c.bw, c.bh, zw, zh);
          logDiag("[Rahmen] %s: %dx%d, 1/%d dekodiert, %dx%d in %u ms", name, w, h, 1 << s, zw, zh, (unsigned)(millis() - t0));
        }
      }
    }
  }
  if (roh) heap_caps_free(roh);
  if (pool) heap_caps_free(pool);
  if (jd) heap_caps_free(jd);
  heap_caps_free(datei);
  if (!fertig) return false;
  out->px = fertig; out->w = zw; out->h = zh; strlcpy(out->name, name, sizeof(out->name));
  return true;
}

// ── Web-Schnittstelle ─────────────────────────────────────────────────────────────────────
static bool nameOk(const String& n) {
  if (n.length() < 5 || n.length() >= NAME_LEN) return false;
  for (size_t i = 0; i < n.length(); i++) { const char ch = n[i]; if (!(isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.')) return false; }
  return !strstr(n.c_str(), "..") && istJpeg(n.c_str());
}

void rahmenWebuiRegistrieren(WebServer& server) {
  server.on("/api/bilder", HTTP_GET, [&server]() {
    const int n = rahmenAnzahl(true);
    String s = "{\"karte\":" + String(speicherBereit() ? "true" : "false") + ",\"anzahl\":" + String(n) + ",\"namen\":[";
    char nm[NAME_LEN];
    for (int i = 0; i < n; i++) if (rahmenName(i, nm, sizeof(nm))) { if (i) s += ","; s += "\"" + String(nm) + "\""; }
    s += "]}";
    server.send(200, "application/json", s);
  });
  // Hochladen: der Rumpf ist das JPEG selbst, der Dateiname steht im Kopf X-Name (der Rumpf wird hier nicht vorab als Text gelesen)
  server.on("/api/bild", HTTP_POST, [&server]() {
    if (!speicherBereit()) { server.send(409, "text/plain; charset=utf-8", "Keine Speicherkarte gefunden."); return; }
    const String name = server.header("X-Name");
    const size_t gesamt = server.clientContentLength();
    if (!nameOk(name)) { server.send(400, "text/plain; charset=utf-8", "Ungültiger Dateiname."); return; }
    if (gesamt < 200 || gesamt > 4u * 1024u * 1024u) { server.send(413, "text/plain; charset=utf-8", "Das Bild ist zu groß (höchstens 4 MB)."); return; }
    if (rahmenAnzahl(true) >= MAX_BILDER) { server.send(409, "text/plain; charset=utf-8", "Es sind schon 200 Bilder auf der Karte."); return; }
    mkdir("/sdcard/bilder", 0775);
    char tmp[96]; snprintf(tmp, sizeof(tmp), ORDNER "/%s", "hochladen.tmp");
    FILE* f = fopen(tmp, "wb");
    uint8_t* buf = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    bool ok = f && buf;
    size_t rest = gesamt;
    while (ok && rest > 0) {
      const int n = server.bodyLesen(buf, rest < 4096 ? rest : 4096);
      if (n <= 0) { ok = false; break; }
      if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) ok = false;
      rest -= (size_t)n;
    }
    if (f) fclose(f);
    if (buf) heap_caps_free(buf);
    char ziel[96]; snprintf(ziel, sizeof(ziel), ORDNER "/%s", name.c_str());
    if (ok) { unlink(ziel); ok = rename(tmp, ziel) == 0; }
    if (!ok) { unlink(tmp); server.send(500, "text/plain; charset=utf-8", "Das Bild konnte nicht gespeichert werden."); return; }
    rahmenAnzahl(true);
    logDiag("[Rahmen] Bild gespeichert: %s (%u Bytes)", name.c_str(), (unsigned)gesamt);
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/bild_loeschen", HTTP_POST, [&server]() {
    if (!speicherBereit()) { server.send(409, "text/plain; charset=utf-8", "Keine Speicherkarte gefunden."); return; }
    int weg = 0;
    if (server.arg("alle") == "1") {
      const int n = rahmenAnzahl(true); char nm[NAME_LEN];
      for (int i = 0; i < n; i++) if (rahmenName(i, nm, sizeof(nm))) { char p[96]; snprintf(p, sizeof(p), ORDNER "/%s", nm); if (unlink(p) == 0) weg++; }
    } else {
      const String name = server.arg("name");
      if (!nameOk(name)) { server.send(400, "text/plain; charset=utf-8", "Ungültiger Dateiname."); return; }
      char p[96]; snprintf(p, sizeof(p), ORDNER "/%s", name.c_str());
      if (unlink(p) == 0) weg = 1;
    }
    rahmenAnzahl(true);
    server.send(200, "text/plain", String(weg));
  });
}
#else
int  rahmenAnzahl(bool) { return 0; }
bool rahmenName(int, char*, size_t) { return false; }
bool rahmenDekodieren(int, int, int, RahmenBild*) { return false; }
void rahmenWebuiRegistrieren(WebServer&) {}
#endif
