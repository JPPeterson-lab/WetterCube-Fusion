// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Bildschirmfoto: aktuellen Bildschirminhalt ausliefern, als PNG (/screenshot.png, Knopf in der Web-Oberflaeche) oder BMP (/screenshot.bmp, Diagnose).
// Hilfreich, um Layout und Animationen ohne Foto zu pruefen. Mit ?s=main|woche|verlauf|bio|allergen|karte|warnliste|luft|sensoren|menu|pollenwarnung|boot|kachel30|kachel32 auch andere Seiten. Rendert den aktiven Screen einmal in einen PSRAM-Puffer.
#include "app_state.h"
#include "lueften.h"
#include "../platform/WebServer.h"
#include "esp_heap_caps.h"
#include "../ui/screens.h"
#include "../ui/colors.h"

// Rendert den gewuenschten Screen einmal in einen PSRAM-Puffer (RGB565). Der Puffer ist rund 1,2 MB gross; ist der PSRAM gerade zu
// zerstueckelt, folgen bis zu drei weitere Versuche. Rueckgabe: Puffer (mit heap_caps_free freigeben) oder nullptr.
static uint8_t* fotoNehmen(WebServer& server, int& W, int& H) {
  for (int versuch = 0; versuch < 4; versuch++) {
    if (versuch) delay(400);
    lv_img_dsc_t dsc;
    uint8_t* puffer = nullptr;
    bool ok = false;
    uint32_t altesDesign = active_theme_index;
    bool designGewechselt = false;
    {
      LvLock lock;
      String theme = server.arg("theme");                  // ?theme=dark|light: Design nur fuer dieses Foto wechseln
      uint32_t neu = theme == "dark" ? THEME_DARK : theme == "light" ? THEME_LIGHT : altesDesign;
      if (neu != altesDesign) { change_color_theme(neu); aktualisiereUI(); designGewechselt = true; }
      lv_obj_t* scr = lv_scr_act();
      String s = server.arg("s");                          // ?s=main|woche|verlauf|bio|allergen|karte|warnliste|luft|sensoren|menu|pollenwarnung|boot: andere Seite zeigen, ohne umzuschalten
      if (s == "main") scr = objects.screen_1; else if (s == "bio") scr = objects.screenbiowetter;
      else if (s == "allergen") scr = objects.screenallergen; else if (s == "sensoren") { scr = objects.screensensoren; uiSensorenErzwingen(); }
      else if (s == "menu") { scr = objects.screenmenu; uiSystemErzwingen(); } else if (s == "nachtuhr") { scr = objects.screennachtuhr; uiNachtUhrErzwingen(); } else if (s == "luft") { scr = objects.screenairquality; uiLuftErzwingen(); }
      else if (s == "woche") { scr = objects.screenwoche; aktualisiereWoche(); }
      else if (s == "verlauf") scr = objects.screenverlauf; else if (s == "karte") scr = objects.screenwarnkarte1;
      else if (s == "warnliste") scr = objects.screenwarnkarte2; else if (s == "pollenwarnung") scr = objects.screenwarnungpollen; else if (s == "lueftenwarnung") { lueftenDemoSetzen(); aktualisiereLueftenWarnScreen(); scr = objects.screenwarnunglueften; } else if (s == "boot") scr = objects.screenboot;
      else if (s == "kachel30") scr = objects.container_30; else if (s == "kachel32") scr = objects.container_32;   // einzelne Kacheln: brauchen kaum Speicher
      uint32_t bedarf = scr ? lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR) : 0;
      if (bedarf) puffer = (uint8_t*)heap_caps_malloc(bedarf, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (puffer) ok = lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &dsc, puffer, bedarf) == LV_RES_OK;
      if (designGewechselt) { change_color_theme(altesDesign); aktualisiereUI(); }
    }
    if (ok) { W = dsc.header.w; H = dsc.header.h; return puffer; }
    if (puffer) heap_caps_free(puffer);
  }
  return nullptr;
}

static inline void rgb565zu888(uint16_t p, uint8_t* o) {
  o[0] = ((p >> 11) & 0x1F) * 255 / 31;   // R
  o[1] = ((p >> 5) & 0x3F) * 255 / 63;    // G
  o[2] = (p & 0x1F) * 255 / 31;           // B
}

static uint32_t crc32Teil(uint32_t crc, const uint8_t* d, size_t n) {   // PNG-Pruefsumme, ohne Tabelle (spart RAM, Tempo reicht)
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}

static void be32(uint8_t* o, uint32_t v) { o[0] = v >> 24; o[1] = v >> 16; o[2] = v >> 8; o[3] = v; }

// PNG ohne Kompression (gespeicherte Deflate-Bloecke, je Bildzeile einer): braucht keinen Zusatzspeicher und wird Zeile fuer Zeile gesendet.
static void sendePng(WebServer& server, const uint16_t* px, int W, int H) {
  const uint32_t zeile = 1 + (uint32_t)W * 3;                    // Filterbyte + RGB
  const uint32_t idat  = 2 + (uint32_t)H * (5 + zeile) + 4;      // zlib-Kopf + Bloecke + Adler32
  const uint32_t gesamt = 8 + 25 + 12 + idat + 12;
  server.setContentLength(gesamt);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "image/png", "");
  uint8_t kopf[8 + 25 + 8 + 2] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  be32(kopf + 8, 13); memcpy(kopf + 12, "IHDR", 4);
  be32(kopf + 16, W); be32(kopf + 20, H); kopf[24] = 8; kopf[25] = 2; kopf[26] = 0; kopf[27] = 0; kopf[28] = 0;   // 8 Bit, RGB
  be32(kopf + 29, crc32Teil(0xFFFFFFFFu, kopf + 12, 17) ^ 0xFFFFFFFFu);
  be32(kopf + 33, idat); memcpy(kopf + 37, "IDAT", 4);
  kopf[41] = 0x78; kopf[42] = 0x01;                              // zlib-Kopf (ohne Kompression)
  uint32_t crc = crc32Teil(0xFFFFFFFFu, kopf + 37, 4);
  crc = crc32Teil(crc, kopf + 41, 2);
  server.sendContent((const char*)kopf, 43);
  uint8_t* z = (uint8_t*)heap_caps_malloc(5 + zeile + 24, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!z) return;
  uint32_t a = 1, b = 0;                                         // Adler32 ueber die Rohdaten
  for (int y = 0; y < H; y++) {
    z[0] = (y == H - 1) ? 1 : 0;                                 // letzter Block: BFINAL
    z[1] = zeile & 0xFF; z[2] = zeile >> 8; z[3] = ~zeile & 0xFF; z[4] = (~zeile >> 8) & 0xFF;
    uint8_t* d = z + 5;
    d[0] = 0;                                                    // Filter "keiner"
    for (int x = 0; x < W; x++) rgb565zu888(px[y * W + x], d + 1 + x * 3);
    for (uint32_t i = 0; i < zeile; i++) { a = (a + d[i]) % 65521; b = (b + a) % 65521; }
    uint32_t n = 5 + zeile;
    if (y == H - 1) { be32(d + zeile, (b << 16) | a); n += 4; }  // Adler32 haengt am Ende der IDAT-Daten
    crc = crc32Teil(crc, z, n);
    if (y == H - 1) {                                            // CRC des IDAT-Blocks und IEND anhaengen
      be32(z + n, crc ^ 0xFFFFFFFFu); n += 4;
      be32(z + n, 0); memcpy(z + n + 4, "IEND", 4); be32(z + n + 8, 0xAE426082u); n += 12;
    }
    server.sendContent((const char*)z, n);
  }
  heap_caps_free(z);
}

void screenshotRegistrieren(WebServer& server) {
  server.on("/screenshot.png", HTTP_GET, [&server]() {
    if (!server.hasArg("s") && !server.hasArg("theme")) {   // Knopf in der Web-Oberflaeche: Framebuffer lesen, braucht keinen Zusatzspeicher
      const uint16_t* fb = lvglPortFramebuffer();
      if (fb) { sendePng(server, fb, LVGL_PORT_H_RES, LVGL_PORT_V_RES); return; }
    }
    int W = 0, H = 0;
    uint8_t* puffer = fotoNehmen(server, W, H);
    if (!puffer) { server.send(500, "text/plain", "Bildschirmfoto nicht moeglich (zu wenig Speicher?)"); return; }
    sendePng(server, (const uint16_t*)puffer, W, H);
    heap_caps_free(puffer);
  });
  server.on("/screenshot.bmp", HTTP_GET, [&server]() {
    int W = 0, H = 0;
    uint8_t* puffer = fotoNehmen(server, W, H);
    if (!puffer) { server.send(500, "text/plain", "Bildschirmfoto nicht moeglich (zu wenig Speicher?)"); return; }
    const uint32_t zeile = (W * 3 + 3) & ~3u, daten = zeile * H, gesamt = 54 + daten;
    uint8_t kopf[54] = {'B', 'M'};
    auto le32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) kopf[o + i] = (v >> (8 * i)) & 0xFF; };
    le32(2, gesamt); le32(10, 54); le32(14, 40); le32(18, W); le32(22, H); kopf[26] = 1; kopf[28] = 24; le32(34, daten);
    server.setContentLength(gesamt);
    server.send(200, "image/bmp", "");
    server.sendContent((const char*)kopf, 54);
    uint8_t* z = (uint8_t*)heap_caps_malloc(1024 * 3 + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!z) { heap_caps_free(puffer); return; }
    const uint16_t* px = (const uint16_t*)puffer;
    memset(z, 0, 1024 * 3 + 4);
    for (int y = H - 1; y >= 0; y--) {                     // BMP speichert von unten nach oben
      for (int x = 0; x < W; x++) {
        uint16_t p = px[y * W + x];
        z[x * 3 + 0] = ((p & 0x1F) * 255) / 31;            // B
        z[x * 3 + 1] = (((p >> 5) & 0x3F) * 255) / 63;     // G
        z[x * 3 + 2] = (((p >> 11) & 0x1F) * 255) / 31;    // R
      }
      server.sendContent((const char*)z, zeile);
    }
    heap_caps_free(z);
    heap_caps_free(puffer);
  });
}
