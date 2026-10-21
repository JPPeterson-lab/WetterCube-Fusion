// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Diagnose: aktuellen Bildschirminhalt als BMP ausliefern (http://wettercube-fusion.local/screenshot.bmp).
// Hilfreich, um Layout und Animationen ohne Foto zu pruefen. Mit ?s=main|verlauf|bio|allergen|karte|warnliste|luft|sensoren|menu|pollenwarnung|boot|kachel30|kachel32 auch andere Seiten. Rendert den aktiven Screen einmal in einen PSRAM-Puffer.
#include "app_state.h"
#include "lueften.h"
#include "../platform/WebServer.h"
#include "esp_heap_caps.h"
#include "../ui/screens.h"
#include "../ui/colors.h"

void screenshotRegistrieren(WebServer& server) {
  server.on("/screenshot.bmp", HTTP_GET, [&server]() {
    int W = 1024, H = 600;
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
      String s = server.arg("s");                          // ?s=main|verlauf|bio|allergen|karte|warnliste|luft|sensoren|menu|pollenwarnung|boot: andere Seite zeigen, ohne umzuschalten
      if (s == "main") scr = objects.screen_1; else if (s == "bio") scr = objects.screenbiowetter;
      else if (s == "allergen") scr = objects.screenallergen; else if (s == "sensoren") { scr = objects.screensensoren; uiSensorenErzwingen(); }
      else if (s == "menu") { scr = objects.screenmenu; uiSystemErzwingen(); } else if (s == "nachtuhr") { scr = objects.screennachtuhr; uiNachtUhrErzwingen(); } else if (s == "luft") { scr = objects.screenairquality; uiLuftErzwingen(); }
      else if (s == "verlauf") scr = objects.screenverlauf; else if (s == "karte") scr = objects.screenwarnkarte1;
      else if (s == "warnliste") scr = objects.screenwarnkarte2; else if (s == "pollenwarnung") scr = objects.screenwarnungpollen; else if (s == "lueftenwarnung") { lueftenDemoSetzen(); aktualisiereLueftenWarnScreen(); scr = objects.screenwarnunglueften; } else if (s == "boot") scr = objects.screenboot;
      else if (s == "kachel30") scr = objects.container_30; else if (s == "kachel32") scr = objects.container_32;   // einzelne Kacheln: brauchen kaum Speicher
      uint32_t bedarf = scr ? lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR) : 0;
      if (bedarf) puffer = (uint8_t*)heap_caps_malloc(bedarf, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (puffer) ok = lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &dsc, puffer, bedarf) == LV_RES_OK;
      if (designGewechselt) { change_color_theme(altesDesign); aktualisiereUI(); }
    }
    if (ok) { W = dsc.header.w; H = dsc.header.h; }
    if (!ok) {
      if (puffer) heap_caps_free(puffer);
      server.send(500, "text/plain", "Bildschirmfoto nicht moeglich (zu wenig Speicher?)");
      return;
    }
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
