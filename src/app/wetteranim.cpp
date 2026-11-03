// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Animierte Wettersymbole, siehe wetteranim.h. Aufbau der Datei: tools/anim_pack.py.
#ifdef FUSION_WETTERANIM
#include "wetteranim.h"
#include "app_state.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_partition.h"

extern "C" unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h, const unsigned char* in, size_t insize);

namespace {
struct Kopf    { char magic[4]; uint16_t version, anzahl, fps, breite, hoehe, rsv; };
struct Eintrag { char name[32]; uint32_t bilder, tabelle; };
struct Bild    { uint32_t offset, groesse; };

const uint8_t* D = nullptr;                   // Animationspaket im PSRAM (aus der Partition "anim" gelesen)
size_t s_laenge = 0;
Kopf s_kopf; bool s_ok = false, s_geprueft = false;

// Liest die Partition "anim" einmal in den PSRAM (kein mmap: Flash-Zugriffe waehrend eines Updates halten sonst den Bildaufbau an).
bool laden() {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "anim");
  if (!p) return false;
  Kopf k;
  if (esp_partition_read(p, 0, &k, sizeof(k)) != ESP_OK || memcmp(k.magic, "WXAN", 4) != 0) return false;   // leer oder fremd
  uint8_t* puf = (uint8_t*)heap_caps_malloc(p->size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!puf) return false;
  if (esp_partition_read(p, 0, puf, p->size) != ESP_OK) { heap_caps_free(puf); return false; }
  D = puf; s_laenge = p->size;
  return true;
}

bool pruefen() {
  if (s_geprueft) return s_ok;
  s_geprueft = true;
  if (!laden()) return false;
  const size_t n = s_laenge;
  if (n < sizeof(Kopf)) return false;
  memcpy(&s_kopf, D, sizeof(Kopf));
  s_ok = memcmp(s_kopf.magic, "WXAN", 4) == 0 && s_kopf.version == 1 && s_kopf.anzahl > 0 && s_kopf.fps > 0 && s_kopf.fps <= 30 &&
         s_kopf.breite > 0 && s_kopf.breite <= 256 && s_kopf.hoehe == s_kopf.breite &&
         16 + (size_t)s_kopf.anzahl * sizeof(Eintrag) <= n;
  return s_ok;
}

bool finde(const char* name, Eintrag* e) {
  if (!pruefen() || !name) return false;
  for (int i = 0; i < s_kopf.anzahl; i++) {
    memcpy(e, D + 16 + i * sizeof(Eintrag), sizeof(Eintrag));
    e->name[31] = 0;
    if (strcmp(e->name, name) == 0 && e->bilder > 0) return true;
  }
  return false;
}

struct Platz {
  lv_obj_t* obj = nullptr;
  Eintrag e = {};
  uint8_t* puf[2] = {nullptr, nullptr};      // RGB565A8: erst Farbebene (2 Byte je Pixel), dann Alphaebene
  lv_img_dsc_t dsc[2] = {};
  int vorn = 0;                               // Puffer, der gerade angezeigt wird
  uint32_t bild = 0xFFFFFFFFu;                // zuletzt gezeigter Bildindex
  int64_t start = 0;                          // Startzeit (us)
  int ziel = 0, x = 0, y = 0;
};
Platz s_platz[6];                            // grosses Symbol + 4 Stunden der Vorschau + Nachtanzeige
lv_timer_t* s_timer = nullptr;

bool dekodieren(Platz& p, uint32_t idx, int in) {
  Bild b; memcpy(&b, D + p.e.tabelle + idx * sizeof(Bild), sizeof(Bild));
  unsigned char* rgba = nullptr; unsigned w = 0, h = 0;
  if (lodepng_decode32(&rgba, &w, &h, D + b.offset, b.groesse) != 0 || !rgba) return false;
  if (w != s_kopf.breite || h != s_kopf.hoehe) { heap_caps_free(rgba); return false; }
  uint16_t* farbe = (uint16_t*)p.puf[in];
  uint8_t* alpha = p.puf[in] + (size_t)w * h * 2;
  const uint8_t* q = rgba;
  for (size_t i = 0, n = (size_t)w * h; i < n; i++, q += 4) {
    farbe[i] = (uint16_t)(((q[0] & 0xF8) << 8) | ((q[1] & 0xFC) << 3) | (q[2] >> 3));
    alpha[i] = q[3];
  }
  heap_caps_free(rgba);
  return true;
}

void freigeben(Platz& p) {
  if (p.obj) lv_img_cache_invalidate_src(&p.dsc[0]), lv_img_cache_invalidate_src(&p.dsc[1]);
  for (auto& b : p.puf) { if (b) heap_caps_free(b); b = nullptr; }
  p = Platz();
}

void takt(lv_timer_t*) {
  if (!cfg.anim_symbole && !cfg.anim_stunden) return;                 // abgeschaltet: das letzte Bild bleibt stehen, bis die Oberflaeche das statische Symbol setzt
  const int64_t jetzt = esp_timer_get_time();
  for (auto& p : s_platz) {
    if (!p.obj) continue;
    if (lv_obj_get_screen(p.obj) != lv_scr_act()) continue;           // nur arbeiten, wenn das Bild zu sehen ist
    const uint32_t idx = (uint32_t)(((jetzt - p.start) * s_kopf.fps / 1000000) % p.e.bilder);
    if (idx == p.bild) continue;
    const int hinten = 1 - p.vorn;
    if (!dekodieren(p, idx, hinten)) { p.bild = idx; continue; }       // Bild uebersprungen, naechstes Mal wieder
    p.vorn = hinten; p.bild = idx;
    lv_img_cache_invalidate_src(&p.dsc[p.vorn]);
    lv_img_set_src(p.obj, &p.dsc[p.vorn]);
  }
}
}  // namespace

bool wetterAnimVerfuegbar() { return pruefen(); }

bool wetterAnimSetzen(lv_obj_t* obj, const char* name, int zielPx, int xOben, int yOben) {
  if (!obj) return false;
  Eintrag e;
  if (!finde(name, &e)) return false;
  Platz* p = nullptr; Platz* frei = nullptr;
  for (auto& s : s_platz) { if (s.obj == obj) p = &s; else if (!s.obj && !frei) frei = &s; }
  if (p && strcmp(p->e.name, e.name) == 0) {                          // gleiche Animation: nur die Lage nachziehen
    if (p->ziel != zielPx || p->x != xOben || p->y != yOben) {
      p->ziel = zielPx; p->x = xOben; p->y = yOben;
      wetterIconZeichnen(obj, &p->dsc[p->vorn], zielPx, xOben, yOben);
    }
    return true;
  }
  if (!p) p = frei;
  if (!p) return false;
  const size_t pro = (size_t)s_kopf.breite * s_kopf.hoehe * 3;
  if (!p->puf[0]) { p->puf[0] = (uint8_t*)heap_caps_malloc(pro, MALLOC_CAP_SPIRAM); p->puf[1] = (uint8_t*)heap_caps_malloc(pro, MALLOC_CAP_SPIRAM); }
  if (!p->puf[0] || !p->puf[1]) { freigeben(*p); return false; }
  p->obj = obj; p->e = e; p->vorn = 0; p->bild = 0; p->start = esp_timer_get_time();
  p->ziel = zielPx; p->x = xOben; p->y = yOben;
  for (int i = 0; i < 2; i++) {
    p->dsc[i].header.cf = LV_IMG_CF_RGB565A8; p->dsc[i].header.always_zero = 0; p->dsc[i].header.reserved = 0;
    p->dsc[i].header.w = s_kopf.breite; p->dsc[i].header.h = s_kopf.hoehe;
    p->dsc[i].data_size = pro; p->dsc[i].data = p->puf[i];
  }
  if (!dekodieren(*p, 0, 0)) { freigeben(*p); return false; }
  lv_img_cache_invalidate_src(&p->dsc[0]);
  wetterIconZeichnen(obj, &p->dsc[0], zielPx, xOben, yOben);
  if (!s_timer) s_timer = lv_timer_create(takt, 1000 / 30, nullptr);
  return true;
}

void wetterAnimStoppen(lv_obj_t* obj) {
  for (auto& p : s_platz) if (p.obj == obj) freigeben(p);
}
#endif
