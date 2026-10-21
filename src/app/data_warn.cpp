// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// DWD-Wetterwarnungen (WFS) und Warnkarte (WMS, PNG → RGB565A8 im PSRAM)
#include "app_state.h"
#include <math.h>
#include <ArduinoJson.h>

// lodepng wird von LVGL mitgebracht (Dekoder für lv_png)
extern "C" unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                                     const unsigned char* in, size_t insize);
extern "C" unsigned lodepng_decode24(unsigned char** out, unsigned* w, unsigned* h,
                                     const unsigned char* in, size_t insize);

// lodepng soll nicht den kleinen LVGL-Pool, sondern den PSRAM nutzen (Kartenbild: mehrere MB).
// Passend dazu steht -DLODEPNG_NO_COMPILE_ALLOCATORS in build_opt.h.
extern "C" {
static void lodepngFehler(const char* was, size_t gr) {
  logDiag("[Karte] lodepng %s fehlgeschlagen: %u Bytes (PSRAM frei %u, groesster Block %u)", was, (unsigned)gr,
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}
void* lodepng_malloc(size_t size) { void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM); if (!p && size) lodepngFehler("malloc", size); return p; }
void* lodepng_realloc(void* ptr, size_t newSize) { void* p = heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM); if (!p && newSize) lodepngFehler("realloc", newSize); return p; }
void  lodepng_free(void* ptr)                    { heap_caps_free(ptr); }
}

lv_img_dsc_t warnkarteDsc = {};
uint8_t*     warnkarteBuf = nullptr;
bool         warnkarteNeu = false;
int          warnkarteW = 0, warnkarteH = 0;

// ── Warnungen ────────────────────────────────────────────────────────────────
void fetchDwdWarnungen() {
  spur("DWD-Warnungen: Abruf");
  if (WiFi.status() != WL_CONNECTED || !cfg.warn_region) return;
  WiFiClientSecure sc;
  HTTPClient http;

  const DwdBbox* bb = getDwdBbox();
  // WFS mit BBOX-Filter: nur Warnungen im Ausschnitt des Bundeslandes
  String url = "https://maps.dwd.de/geoserver/dwd/ows?service=WFS&version=1.1.0"
               "&request=GetFeature&typeName=dwd:Warnungen_Gemeinden"
               "&outputFormat=application/json&maxFeatures=20";
  url += "&BBOX=" + String(bb->minLon, 4) + "," + String(bb->minLat, 4) + ","
                  + String(bb->maxLon, 4) + "," + String(bb->maxLat, 4) + ",EPSG:4326";

  http.begin(sc, url);
  http.addHeader("User-Agent", "WetterCubeFusion/" FIRMWARE_VERSION);
  http.setTimeout(15000);
  int code = http.GET();
  if (code != 200) {
    logHttpFehler("DWD-Warn", code, sc);
    http.end();
    return;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  body = String();
  if (err != DeserializationError::Ok && err != DeserializationError::NoMemory) {
    logDiag("[DWD-Warn] JSON: %s", err.c_str());
    return;
  }

  WarnDaten neu[5];
  int count = 0;
  for (JsonObject feat : doc["features"].as<JsonArray>()) {
    if (count >= 5) break;
    JsonObject p = feat["properties"];

    const char* sevStr = p["SEVERITY"] | p["severity"] | p["Severity"] | "Minor";
    int sev = 1;
    if      (strcmp(sevStr, "Extreme")  == 0) sev = 4;
    else if (strcmp(sevStr, "Severe")   == 0) sev = 3;
    else if (strcmp(sevStr, "Moderate") == 0) sev = 2;

    String titel = String(p["HEADLINE"] | p["headline"] | p["Headline"] | p["NAME"] | "");
    bool dup = false;
    for (int i = 0; i < count; i++) if (neu[i].titel == titel) { dup = true; break; }
    if (dup) continue;

    neu[count].aktiv        = true;
    neu[count].level        = sev;
    neu[count].typ          = String(p["EVENT"]       | p["event"]       | p["Event"]       | "");
    neu[count].titel        = titel;
    neu[count].beschreibung = String(p["DESCRIPTION"] | p["description"] | p["Description"] | "");
    count++;
  }

  LvLock lock;
  for (int i = 0; i < 5; i++) warnungen[i] = neu[i];
  anzahl_warnungen = count;
  abrufOk(AB_WARNUNGEN);
  logDiag("[DWD-Warn] %d Warnung(en) fuer Region %d", count, cfg.dwd_region);
}

// ── Warnkarte ────────────────────────────────────────────────────────────────

// Lädt ein PNG von der WMS-Schnittstelle (leer bei Fehler oder falschem Format)
static String ladeKartenPng(const String& url) {
  WiFiClientSecure sc;
  HTTPClient http;
  http.begin(sc, url);
  http.addHeader("User-Agent", "WetterCubeFusion/" FIRMWARE_VERSION);
  http.setTimeout(30000);
  String body;
  int code = http.GET();
  if (code == 200) body = http.getString();
  else logHttpFehler("Karte", code, sc);
  http.end();
  // PNG-Signatur prüfen (WMS liefert bei Fehlern XML mit Status 200)
  if (body.length() < 512 || (uint8_t)body[0] != 0x89 || body[1] != 'P' || body[2] != 'N' || body[3] != 'G') return String();
  return body;
}

bool fetchWarnkarte() {
  spur("Karte: Abruf");
  if (WiFi.status() != WL_CONNECTED) return false;
  const DwdBbox* bb = getDwdBbox();

  // Bildgröße aus dem Seitenverhältnis des Ausschnitts (Breitengrad-korrigiert), eingepasst in KARTE_MAX_W x KARTE_MAX_H
  float lonSpan = bb->maxLon - bb->minLon, latSpan = bb->maxLat - bb->minLat;
  float midLat  = (bb->minLat + bb->maxLat) * 0.5f * (float)M_PI / 180.0f;
  float aspect  = (lonSpan * cosf(midLat)) / latSpan;
  int w, h;
  if (aspect >= (float)KARTE_MAX_W / KARTE_MAX_H) { w = KARTE_MAX_W; h = (int)(w / aspect); }
  else                                            { h = KARTE_MAX_H; w = (int)(h * aspect); }

  String bbox = String(bb->minLon, 4) + "," + String(bb->minLat, 4) + "," + String(bb->maxLon, 4) + "," + String(bb->maxLat, 4);
  String basis = "https://maps.dwd.de/geoserver/dwd/ows?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&SRS=EPSG:4326&FORMAT=image/png";
  basis += "&WIDTH=" + String(w) + "&HEIGHT=" + String(h) + "&BBOX=" + bbox;

  unsigned long t0 = millis();
  // Erst mit Bundesland-Grenzen, sonst nur der Radar-Layer
  String png = ladeKartenPng(basis + "&LAYERS=dwd:KV_VG250_BUNDESLAENDER_2020,dwd:Niederschlagsradar");
  if (png.isEmpty()) png = ladeKartenPng(basis + "&LAYERS=dwd:Niederschlagsradar");
  if (png.isEmpty()) { logDiag("[Karte] Download fehlgeschlagen"); return false; }
  size_t pngSize = png.length();

  // Das Dekodieren braucht ca. 3 MB am Stueck (RGBA-Bild plus Zeilenpuffer von lodepng): mit der alten Karte (1 MB) im PSRAM reicht es nicht
  // (Fehler 83 bei jeder Aktualisierung nach dem Start). Darum die alte Karte vorher freigeben; sie fehlt nur wenige Sekunden.
  warnkarteFreigeben();

  unsigned char* rgba = nullptr;
  unsigned imgW = 0, imgH = 0;
  spur("Karte: PNG dekodieren");
  logDiag("[Karte] Dekodiere %dx%d, PNG %u Bytes, PSRAM frei %u (groesster Block %u)", w, h, (unsigned)pngSize,
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  // Die WMS-Karte ist meist ein RGB-PNG ohne Transparenz (IHDR: Bittiefe 8, Farbtyp 2). Als RGBA dekodiert braucht lodepng nach dem
  // entpackten Bild (1 MB) einen zweiten 1,4-MB-Block, der im zerstückelten PSRAM nicht mehr am Stück frei ist (Fehler 83).
  // Als RGB dekodiert reicht ein Block, der Rest geschieht an Ort und Stelle.
  const unsigned char* pngDaten = (const unsigned char*)png.c_str();
  const bool ohneAlpha = pngSize > 33 && pngDaten[24] == 8 && pngDaten[25] == 2;
  unsigned err = ohneAlpha ? lodepng_decode24(&rgba, &imgW, &imgH, pngDaten, pngSize)
                           : lodepng_decode32(&rgba, &imgW, &imgH, pngDaten, pngSize);
  png = String();
  if (err || !rgba) {
    logDiag("[Karte] PNG-Dekodierung fehlgeschlagen (%u)", err);
    if (rgba) lodepng_free(rgba);
    return false;
  }

  // RGBA8888 → RGB565A8 ([RGB565-Block][Alpha-Block]); LV_COLOR_16_SWAP ist 0, keine Byte-Vertauschung
  size_t px = (size_t)imgW * imgH;
  uint8_t* decoded;
  if (ohneAlpha) {
    // RGB888 (3 Byte je Pixel) → RGB565 + Alpha-Block (zusammen wieder 3 Byte je Pixel) im selben Puffer: vorwärts gelesen und
    // geschrieben, der Schreibzeiger (2·i) bleibt hinter dem Lesezeiger (3·i); der Alpha-Block kommt erst danach
    decoded = rgba;
    uint16_t* rgbPart = (uint16_t*)decoded;
    for (size_t i = 0; i < px; i++) {
      uint8_t r = decoded[i*3], g = decoded[i*3+1], b = decoded[i*3+2];
      rgbPart[i] = ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
    }
    memset(decoded + px * 2, 255, px);
  } else {
    decoded = (uint8_t*)heap_caps_malloc(px * 3, MALLOC_CAP_SPIRAM);
    if (!decoded) { logDiag("[Karte] Kein PSRAM fuer %ux%u", imgW, imgH); lodepng_free(rgba); return false; }
    uint16_t* rgbPart   = (uint16_t*)decoded;
    uint8_t*  alphaPart = decoded + px * 2;
    for (size_t i = 0; i < px; i++) {
      uint8_t r = rgba[i*4], g = rgba[i*4+1], b = rgba[i*4+2];
      rgbPart[i]   = ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
      alphaPart[i] = rgba[i*4+3];
    }
    lodepng_free(rgba);
  }

  {
    LvLock lock;
    uint8_t* alt = warnkarteBuf;
    warnkarteBuf = decoded;
    memset(&warnkarteDsc, 0, sizeof(warnkarteDsc));
    warnkarteDsc.header.cf = LV_IMG_CF_RGB565A8;
    warnkarteDsc.header.w  = imgW;
    warnkarteDsc.header.h  = imgH;
    warnkarteDsc.data_size = px * 3;
    warnkarteDsc.data      = warnkarteBuf;
    warnkarteW = imgW; warnkarteH = imgH;
    warnkarteNeu = true;
    aktualisiereWarnkarte();     // zeigt die neue Karte, bevor der alte Puffer freigegeben wird
    if (alt) free(alt);
  }
  abrufOk(AB_KARTE);
  logDiag("[Karte] %ux%u in %lu ms", imgW, imgH, millis() - t0);
  return true;
}
