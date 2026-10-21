// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Farben, Texte und Icon-Zuordnung für die Darstellung der Daten
#include "app_state.h"
#include "../icons/wetter.h"
#include "../ui/images.h"
#include "../ui/colors.h"

// Gemeinsame Farbpalette der Wertfarben (Temperatur, Pollen, Luftqualität, UV, Biowetter): satte Töne,
// die auf hellem wie dunklem Hintergrund gut lesbar bleiben
static const uint32_t C_BLAU       = 0x0080E8;
static const uint32_t C_GRUEN      = 0x00A838;
static const uint32_t C_GELBGRUEN  = 0x8CB400;
static const uint32_t C_GELB       = 0xE8A000;   // Goldgelb
static const uint32_t C_ORANGE     = 0xF06400;
static const uint32_t C_ROT        = 0xE01E00;
static const uint32_t C_VIOLETT    = 0x8B008B;
static const uint32_t C_GRAU       = 0x888888;

// Konvertiert Open-Meteo-Pollenwerte (Körner/m³) auf die DWD-Skala 0–3
float openMeteoToDwd(float grains) {
  if (grains < 0)    return -1.0f;
  if (grains == 0)   return  0.0f;
  if (grains <= 10)  return  1.0f;   // gering
  if (grains <= 30)  return  2.0f;   // mittel
  if (grains <= 100) return  2.5f;   // hoch
  return                     3.0f;   // stark
}

const char* pollenText(float v) {
  if (v < 0)    return "--";
  if (v == 0)   return "keine";
  if (v <= 1.0) return "gering";
  if (v <= 2.0) return "mittel";
  if (v <= 2.5) return "hoch";
  return               "stark";
}

lv_color_t pollenColor(float v) {
  if (v <= 0)   return lv_color_hex(C_GRAU);
  if (v <= 1.0) return lv_color_hex(C_GRUEN);
  if (v <= 2.0) return lv_color_hex(C_GELB);
  if (v <= 2.5) return lv_color_hex(C_ORANGE);
  return               lv_color_hex(C_ROT);
}

// Temperaturfarben: kräftig auf dem hellen Theme, etwas heller auf dem dunklen (sonst zu wenig Kontrast)
lv_color_t tempColor(float t) {
  const bool dunkel = (active_theme_index == THEME_DARK);
  if (t < 8)   return lv_color_hex(dunkel ? 0x3FA0FF : 0x0A5CE0);  // blau
  if (t < 15)  return lv_color_hex(dunkel ? 0x1FD46A : 0x00A040);  // grün
  if (t < 24)  return lv_color_hex(dunkel ? 0xFFC41A : 0xEE9B00);  // gelb
  return              lv_color_hex(dunkel ? 0xFF4A2E : 0xE01E0A);  // rot
}

// Europäischer AQI: 0–20 gut, –40 mittel, –60 mäßig, –80 schlecht, –100 sehr schlecht, darüber extrem
lv_color_t aqiColor(int aqi) {
  if (aqi < 0)    return lv_color_hex(C_GRAU);
  if (aqi <= 20)  return lv_color_hex(C_GRUEN);
  if (aqi <= 40)  return lv_color_hex(C_GELBGRUEN);
  if (aqi <= 60)  return lv_color_hex(C_GELB);
  if (aqi <= 80)  return lv_color_hex(C_ORANGE);
  if (aqi <= 100) return lv_color_hex(C_ROT);
  return lv_color_hex(C_VIOLETT);
}

// Einzelwerte, Farbkodierung nach EU-Grenzwerten (eine Definition für alle Screens)
lv_color_t pm25Color(float v) {
  if (v < 0)   return lv_color_hex(C_GRAU);
  if (v <= 10) return lv_color_hex(C_GRUEN);
  if (v <= 25) return lv_color_hex(C_GELB);
  if (v <= 50) return lv_color_hex(C_ORANGE);
  return lv_color_hex(C_ROT);
}
lv_color_t pm10Color(float v) {
  if (v < 0)    return lv_color_hex(C_GRAU);
  if (v <= 20)  return lv_color_hex(C_GRUEN);
  if (v <= 50)  return lv_color_hex(C_GELB);
  if (v <= 100) return lv_color_hex(C_ORANGE);
  return lv_color_hex(C_ROT);
}
lv_color_t no2Color(float v) {
  if (v < 0)    return lv_color_hex(C_GRAU);
  if (v <= 40)  return lv_color_hex(C_GRUEN);
  if (v <= 100) return lv_color_hex(C_GELB);
  if (v <= 200) return lv_color_hex(C_ORANGE);
  return lv_color_hex(C_ROT);
}
lv_color_t o3Color(float v) {
  if (v < 0)     return lv_color_hex(C_GRAU);
  if (v <= 60)   return lv_color_hex(C_GRUEN);
  if (v <= 120)  return lv_color_hex(C_GELB);
  if (v <= 180)  return lv_color_hex(C_ORANGE);
  return lv_color_hex(C_ROT);
}
lv_color_t so2Color(float v) {      // Schwefeldioxid (ug/m3), Stufen nach dem EU-Luftqualitaetsindex
  if (v < 0)    return lv_color_hex(C_GRAU);
  if (v <= 100) return lv_color_hex(C_GRUEN);
  if (v <= 200) return lv_color_hex(C_GELB);
  if (v <= 350) return lv_color_hex(C_ORANGE);
  return lv_color_hex(C_ROT);
}
lv_color_t coColor(float v) {       // Kohlenmonoxid (ug/m3), grobe Stufen (WHO-Richtwert 4 mg/m3 im Tagesmittel)
  if (v < 0)      return lv_color_hex(C_GRAU);
  if (v <= 2000)  return lv_color_hex(C_GRUEN);
  if (v <= 4000)  return lv_color_hex(C_GELB);
  if (v <= 10000) return lv_color_hex(C_ORANGE);
  return lv_color_hex(C_ROT);
}
lv_color_t staubColor(float v) {    // Staub inkl. Saharastaub (ug/m3), grobe Stufen zur Orientierung
  if (v < 0)    return lv_color_hex(C_GRAU);
  if (v <= 30)  return lv_color_hex(C_GRUEN);
  if (v <= 60)  return lv_color_hex(C_GELB);
  if (v <= 120) return lv_color_hex(C_ORANGE);
  return lv_color_hex(C_ROT);
}
lv_color_t uvColor(float uv) {
  if (uv < 0)    return lv_color_hex(C_GRAU);
  if (uv >= 8)   return lv_color_hex(C_ROT);
  if (uv >= 6)   return lv_color_hex(C_ORANGE);
  if (uv >= 3)   return lv_color_hex(C_GELB);
  return lv_color_hex(C_GRUEN);
}

const char* aqiStatusText(int aqi) {
  if (aqi < 0)    return "--";
  if (aqi <= 20)  return "Gut";
  if (aqi <= 40)  return "Mittel";
  if (aqi <= 60)  return "Mäßig";
  if (aqi <= 80)  return "Schlecht";
  if (aqi <= 100) return "Sehr schlecht";
  return "Extrem";
}

lv_color_t bioWertColor(int v) {
  switch (v) {
    case 1:  return lv_color_hex(C_GRUEN);  // positiv
    case 2:  return lv_color_hex(C_GELB);  // geringe Belastung
    case 3:  return lv_color_hex(C_ROT);  // hohe Belastung
    default: return lv_color_hex(C_GRAU);  // kein Einfluss
  }
}

const char* bioWertText(int v) {
  switch (v) {
    case 1:  return "positiver Einfluss";
    case 2:  return "geringe Belastung";
    case 3:  return "hohe Belastung";
    default: return "kein Einfluss";
  }
}

// WMO-Wettercode → Symbol (Meteocons, tools/wetter_icons.py), je mit Tag- und Nachtvariante. Die Bewoelkung des Symbols folgt der Art der
// Meldung (Schauer: aufgelockert, Dauerregen: bedeckt), die Staerke der Wolkenfarbe: leicht = helle Wolke, maessig = bedeckt, stark = dunkle Wolke.
struct WxSymbol { const lv_img_dsc_t* dsc; const char* name; };    // statisches Bild und Name (auch der der Animation, tools/anim_pack.py)
#define WX(n) (day ? WxSymbol{&wx_##n##_day, #n "_day"} : WxSymbol{&wx_##n##_night, #n "_night"})                       // ohne Niederschlag: wx_<Wolke>_day / _night
#define WXN(wolke, nied) (day ? WxSymbol{&wx_##wolke##_day_##nied, #wolke "_day_" #nied} : WxSymbol{&wx_##wolke##_night_##nied, #wolke "_night_" #nied})   // mit Niederschlag: wx_<Wolke>_day_<Art>
static WxSymbol wmoZuSymbol(int wmo, bool day) {
  switch (wmo) {
    case 0:  return WX(clear);                          // klar
    case 1:  return WX(mostly_clear);                   // ueberwiegend klar
    case 2:  return WX(partly_cloudy);                  // teils bewoelkt
    case 3:  return WX(overcast);                       // bedeckt
    case 45: return WX(fog);                            // Nebel
    case 48: return WX(fog);                            // Reifnebel
    case 51: return WXN(partly_cloudy, drizzle);          // leichter Nieselregen
    case 53: return WXN(overcast, drizzle);               // Nieselregen
    case 55: return WXN(overcast, rain);                  // dichter Nieselregen
    case 56: case 57: case 66: case 67: return WXN(overcast, sleet);   // gefrierender Nieselregen / Regen (auch stark)
    case 61: return WXN(partly_cloudy, rain);             // leichter Regen
    case 63: return WXN(overcast, rain);                  // Regen
    case 65: return WXN(extreme, rain);                   // starker Regen
    case 71: return WXN(partly_cloudy, snow);             // leichter Schneefall
    case 73: return WXN(overcast, snow);                  // Schneefall
    case 75: return WXN(extreme, snow);                   // starker Schneefall
    case 77: return WXN(partly_cloudy, snow);             // Schneegriesel
    case 80: return WXN(partly_cloudy, rain);             // leichte Regenschauer
    case 81: return WXN(overcast, rain);                  // Regenschauer
    case 82: return WXN(extreme, rain);                   // heftige Regenschauer
    case 85: return WXN(partly_cloudy, snow);             // leichte Schneeschauer
    case 86: return WXN(extreme, snow);                   // starke Schneeschauer
    case 95: return WXN(thunderstorms, rain);    // Gewitter
    case 96: case 99: return WXN(thunderstorms, hail);   // Gewitter mit Hagel
  }
  // unbekannter Code: nach Bereich
  if (wmo >= 95) return WXN(thunderstorms, rain);
  if (wmo >= 71) return WXN(overcast, snow);
  if (wmo >= 51) return WXN(overcast, rain);
  return WX(overcast);
}

const lv_img_dsc_t* wmoZuImage(int wmo, bool day) { return wmoZuSymbol(wmo, day).dsc; }
const char* wmoZuAnimName(int wmo, bool day) { return wmoZuSymbol(wmo, day).name; }

// Wettersymbole (112 px Quelle) in einem Widget zeigen: auf zielPx skalieren und so setzen, dass links oben (xOben, yOben) der angezeigte
// Bereich beginnt. LVGL skaliert im Modus REAL um die Mitte des Quellbilds, deshalb wird das Widget um die Differenz verschoben.
void wetterIconZeichnen(lv_obj_t* obj, const lv_img_dsc_t* d, int zielPx, int xOben, int yOben) {
  if (!obj || !d) return;
  const int w = d->header.w;
  lv_img_set_src(obj, d);
  lv_img_set_size_mode(obj, LV_IMG_SIZE_MODE_REAL);
  lv_img_set_antialias(obj, true);
  lv_img_set_zoom(obj, (uint16_t)(zielPx * 256 / w));
  lv_obj_set_size(obj, zielPx, zielPx);     // genau die angezeigte Groesse: ist das Widget groesser als das skalierte Bild, kachelt LVGL das Bild
  lv_obj_set_pos(obj, xOben + zielPx / 2 - w / 2, yOben + zielPx / 2 - w / 2);
}

const char* windRichtung(int deg) {
  static const char* dirs[] = {"N","NO","O","SO","S","SW","W","NW"};
  return dirs[((deg + 22) % 360) / 45];
}

// Die Schriften enthalten ASCII (32–126) und genau diese Sonderzeichen: ° Ä Ö Ü ß ä ö ü.
// Alles andere wird ersetzt (², Gedankenstriche, typografische Anführungszeichen ...) oder verworfen.
static bool schriftHatZeichen(uint32_t cp) {
  return cp == 176 || cp == 196 || cp == 214 || cp == 220 || cp == 223 || cp == 228 || cp == 246 || cp == 252;
}
static void anhaengen(String& r, uint32_t cp) {
  if (cp < 0x80) r += (char)cp;
  else { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3F)); }   // Latin-1 als UTF-8
}
String bereinigeText(const String& s) {
  String r;
  const uint8_t* p = (const uint8_t*)s.c_str();
  while (*p) {
    uint32_t cp;
    int n;
    if (p[0] < 0x80)                        { cp = p[0]; n = 1; }
    else if ((p[0] & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); n = 2; }
    else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); n = 3; }
    else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { cp = 0; n = 4; }
    else                                    { cp = '?'; n = 1; }

    if (cp == '\n' || (cp >= 32 && cp <= 126) || schriftHatZeichen(cp)) anhaengen(r, cp);
    else switch (cp) {
      case 0xB2: r += '2'; break;                       // ² (z. B. l/m²)
      case 0xB3: r += '3'; break;                       // ³
      case 0xB5: r += 'u'; break;                       // µ
      case 0xB1: r += "+-"; break;                      // ±
      case 0xD7: r += 'x'; break;                       // ×
      case 0xA0: r += ' '; break;                       // geschütztes Leerzeichen
      case 0xE9: case 0xE8: case 0xEA: r += 'e'; break; // é è ê
      case 0xE0: case 0xE1: case 0xE2: r += 'a'; break; // à á â
      case 0x2013: case 0x2014: r += '-'; break;        // Gedankenstriche
      case 0x2018: case 0x2019: r += '\''; break;       // typografische Apostrophe
      case 0x201C: case 0x201D: case 0x201E: r += '"'; break;
      case 0x2026: r += "..."; break;
      default: break;                                   // alles andere weglassen
    }
    p += n;
  }
  return r;
}

// ── Pollen-Gesamtwert und WetterAmpel ────────────────────────────────────────
const char* const POLLEN_ANZEIGE[8] = {"Hasel", "Erle", "Esche", "Birke", "Gräser", "Roggen", "Beifuß", "Ambrosia"};

bool pollenGewaehlt(int i) { return (cfg.pollen_auswahl >> i) & 1; }

// Wert eines Allergens (DWD-Skala 0–3): Open-Meteo (nächste Stunde), wo vorhanden, sonst der DWD-Tageswert
// (Hasel, Esche und Roggen liefert Open-Meteo nicht)
float pollenWertWarnung(int i) {
  switch (i) {
    case 0:  return pollen.dwd_hasel;
    case 1:  return openMeteoToDwd(pollen.erle);
    case 2:  return pollen.dwd_esche;
    case 3:  return openMeteoToDwd(pollen.birke);
    case 4:  return openMeteoToDwd(pollen.graeser);
    case 5:  return pollen.dwd_roggen;
    case 6:  return openMeteoToDwd(pollen.beifuss);
    default: return openMeteoToDwd(pollen.ambrosia);
  }
}

// Name des stärksten der gewählten Allergene
String schlimmstesPollen() {
  float maxV = -1.0f; const char* name = "Pollen";
  for (int i = 0; i < 8; i++) {
    if (!pollenGewaehlt(i)) continue;
    float v = pollenWertWarnung(i);
    if (v > maxV) { maxV = v; name = POLLEN_ANZEIGE[i]; }
  }
  return String(name);
}

// Ampelfarbe je nach Modus (Temperatur / Pollen / Luftqualität); detail = Klartext für die WebUI
void ampelBerechnen(const char*& active, const char*& modus, String& detail) {
  float t = wetter.temp;
  active = "none";
  modus  = "temperature";
  detail = String(t, 1) + " °C";
  if (cfg.ampel_modus == 1) {
    // Pollen: schlimmstes der gewählten Allergene (Skala 0–3), Stufen wie pollenText(): Grün = keine/gering (≤ 1),
    // Gelb = mittel/hoch (≤ 2,5), Rot = stark (> 2,5)
    modus = "pollen";
    float v = -1.0f; const char* name = "";
    for (int i = 0; i < 8; i++) {
      if (!pollenGewaehlt(i)) continue;
      float w = pollenWertWarnung(i);
      if (w > v) { v = w; name = POLLEN_ANZEIGE[i]; }
    }
    if (v >= 0.0f) {
      active = (v > 2.5f) ? "red" : (v > 1.0f) ? "yellow" : "green";
      detail = String(name) + " (Stufe " + String(v, 1) + " von 3)";
    } else detail = "keine Pollendaten";
  } else if (cfg.ampel_modus == 2) {
    // Luftqualität: europäischer AQI, Grün bis 40 (gut/mittel), Gelb bis 60 (mäßig), Rot darüber
    modus = "air";
    int a = pollen.aqi;
    if (a >= 0) {
      active = (a <= 40) ? "green" : (a <= 60) ? "yellow" : "red";
      detail = "AQI " + String(a) + " (" + aqiStatusText(a) + ")";
    } else detail = "keine Luftqualitaetsdaten";
  } else {
    if      (t >= cfg.ampel_rot_min)   active = "red";
    else if (t >= cfg.ampel_gelb_min)  active = "yellow";
    else if (t >= cfg.ampel_gruen_min) active = "green";
  }
}

// Mondphase aus dem Datum (synodischer Monat 29,53 Tage, Neumond am 6.1.2000): Beleuchtung in Prozent und Phase 0-7 (0 Neumond, 4 Vollmond)
void mondPhase(time_t t, float* beleuchtet, int* phase8) {
  const double jd = (double)t / 86400.0 + 2440587.5;
  double ph = fmod((jd - 2451550.1) / 29.530588853, 1.0); if (ph < 0) ph += 1.0;   // 0 = Neumond, 0.5 = Vollmond
  *beleuchtet = (float)((1.0 - cos(2.0 * M_PI * ph)) / 2.0 * 100.0);
  *phase8 = ((int)floor(ph * 8.0 + 0.5)) % 8;
}
const char* mondName(int phase8) {
  static const char* N[8] = {"Neumond", "Zunehmende Sichel", "Erstes Viertel", "Zunehmender Mond", "Vollmond", "Abnehmender Mond", "Letztes Viertel", "Abnehmende Sichel"};
  return N[((phase8 % 8) + 8) % 8];
}

// WMO-Wettercode als deutscher Text (Home Assistant, /api/next)
const char* wetterText(int c) {
  if (c == 0) return "Klar";
  if (c == 1) return "Überwiegend klar";
  if (c == 2) return "Teils bewölkt";
  if (c == 3) return "Bedeckt";
  if (c == 45 || c == 48) return "Nebel";
  if (c >= 51 && c <= 57) return "Nieselregen";
  if (c >= 61 && c <= 67) return "Regen";
  if (c >= 71 && c <= 77) return "Schnee";
  if (c >= 80 && c <= 82) return "Regenschauer";
  if (c == 85 || c == 86) return "Schneeschauer";
  if (c >= 95) return "Gewitter";
  return "Unbekannt";
}

