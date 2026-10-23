// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Daten → Oberfläche: befüllt alle Labels, Bilder und Anzeigen des PicoPixel-Exports
#include "app_state.h"
#include <time.h>
#include "../ui/screens.h"
#include "../ui/images.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"
#include "../icons/icons.h"
#include "wetteranim.h"

static inline bool farbeGleich(lv_color_t a, lv_color_t b) { return lv_color_to32(a) == lv_color_to32(b); }

// Alle Setter ändern nur, was sich wirklich geändert hat: jede Änderung löst sonst eine
// Neuzeichnung aus, und ein Bildaufbau kostet auf diesem Display bis zu einer halben Sekunde.
static void setLabel(lv_obj_t* obj, const char* text) {
  if (obj && strcmp(lv_label_get_text(obj), text) != 0) lv_label_set_text(obj, text);
}
static void setLabelFarbe(lv_obj_t* obj, lv_color_t col, const char* text) {
  if (!obj) return;
  setLabel(obj, text);
  if (!farbeGleich(lv_obj_get_style_text_color(obj, LV_PART_MAIN), col)) lv_obj_set_style_text_color(obj, col, 0);
}
// Allergen-Screen: abgerundete Chips, Hintergrund nach Stufe (grau/gruen/gelb/orange/rot), Schrift hell oder dunkel
static void setPollenChip(lv_obj_t* obj, float v) {
  if (!obj) return;
  setLabel(obj, pollenText(v));
  const bool grau = v <= 0, gelb = v > 1.0f && v <= 2.0f;
  lv_color_t bg = grau ? lv_color_hex(0xD1D5DB) : pollenColor(v);
  lv_color_t fg = (grau || gelb) ? lv_color_hex(0x1F2937) : lv_color_hex(0xFFFFFF);
  if (!farbeGleich(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), bg)) lv_obj_set_style_bg_color(obj, bg, 0);
  if (!farbeGleich(lv_obj_get_style_text_color(obj, LV_PART_MAIN), fg)) lv_obj_set_style_text_color(obj, fg, 0);
}
// Biowetter-Screen: Chip nach Stufe (0 kein Einfluss grau, 1 positiv gruen, 2 geringe Belastung gelb, 3 hohe Belastung rot)
static void setBioChip(lv_obj_t* obj, int wert, bool leer) {
  if (!obj) return;
  setLabel(obj, leer ? "-" : bioWertText(wert));
  const bool hellerGrund = leer || wert == 0 || wert == 2;
  lv_color_t bg = hellerGrund && (leer || wert == 0) ? lv_color_hex(0xD1D5DB) : bioWertColor(wert);
  lv_color_t fg = hellerGrund ? lv_color_hex(0x1F2937) : lv_color_hex(0xFFFFFF);
  if (!farbeGleich(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), bg)) lv_obj_set_style_bg_color(obj, bg, 0);
  if (!farbeGleich(lv_obj_get_style_text_color(obj, LV_PART_MAIN), fg)) lv_obj_set_style_text_color(obj, fg, 0);
}
static void setPollenLabel(lv_obj_t* obj, float v) {
  setLabelFarbe(obj, pollenColor(v), pollenText(v));
}
// Das Zoom-Verhältnis der Bild-Widgets (128-px-Quelle) legt der Export fest, hier wird nur die Quelle getauscht
// Wettersymbol setzen. Die Symbole haben 112 px Quellgroesse; die angezeigte Groesse und Lage bleiben wie in PicoPixel entworfen
// (Hauptsymbol 128 px, Stundensymbole 112 px): beim ersten Aufruf wird links oben und Groesse aus dem Widget gelesen.
static void setWetterIcon(lv_obj_t* obj, const lv_img_dsc_t* dsc, const char* animName, bool animieren) {
  if (!obj || !dsc) return;
  struct Platz { lv_obj_t* o; int x, y, ziel; };
  static Platz plaetze[8]; static int np = 0;
  Platz* p = nullptr;
  for (int i = 0; i < np; i++) if (plaetze[i].o == obj) p = &plaetze[i];
  if (!p && np < 8) {
    p = &plaetze[np++];
    p->o = obj; p->ziel = 128 * lv_img_get_zoom(obj) / 256;                    // fruehere Anzeigegroesse (Quelle 128 px)
    p->x = lv_obj_get_x(obj); p->y = lv_obj_get_y(obj);                         // links oben des angezeigten Bereichs
  }
  // Animiert (ESP32-P4, wenn eingeschaltet und im Paket vorhanden), sonst das statische Symbol
  if (p && animieren && animName && wetterAnimSetzen(obj, animName, p->ziel, p->x, p->y)) return;
  wetterAnimStoppen(obj);
  if (lv_img_get_src(obj) == (const void*)dsc) return;
  // wetterIconZeichnen setzt das Widget um (Zielgroesse - Quellbreite) / 2 nach innen; hier ausgleichen, damit das Symbol genau im entworfenen Rechteck sitzt
  if (p) { int d = (p->ziel - (int)dsc->header.w) / 2; wetterIconZeichnen(obj, dsc, p->ziel, p->x - d, p->y - d); }
  else   lv_img_set_src(obj, dsc);
}

// Windpfeil auf dem Hauptscreen: dreht sich weich auf dem kuerzesten Weg zur neuen Richtung (Zehntelgrad)
static int s_pfeilWinkel = 0;
static bool s_pfeilBereit = false;
static int s_pfeilBasis = 0;       // Winkel der Richtungsanimation (Zehntelgrad); darauf kommt das Pendeln
static int pfeilPendeln() {
  // Der Pfeil pendelt leicht, je staerker der Wind, desto weiter (bei 20 km/h etwa 6 Grad, hoechstens 15 Grad), Periode 3 s
  float amp = wetter.geladen ? wetter.wind_speed * 3.0f : 0.0f; if (amp > 150.0f) amp = 150.0f;   // Zehntelgrad
  return (int)(amp * sinf((float)(millis() % 3000) * (2.0f * (float)M_PI / 3000.0f)));
}
static void pfeilWinkelSetzen() { if (objects.imagewindpfeil) lv_img_set_angle(objects.imagewindpfeil, (int16_t)(s_pfeilBasis + pfeilPendeln())); }
static void pfeilWinkelCb(void*, int32_t v) { s_pfeilBasis = v; pfeilWinkelSetzen(); }
static void pfeilPendelTimer(lv_timer_t*) { if (lv_scr_act() == objects.screen_1) pfeilWinkelSetzen(); }
static void windPfeil(int richtungGrad) {
  lv_obj_t* p = objects.imagewindpfeil;
  if (!p) return;
  if (!s_pfeilBereit) {
    // Groessenmodus VIRTUAL, nicht REAL: LVGL verschiebt ein gedrehtes Bild im Modus REAL um die halbe Groessenzunahme seiner
    // Begrenzungsbox (abhaengig vom Winkel, bei 20 Grad ca. 8 px), der Pfeil wandert dann beim Drehen aus der Mitte.
    // Das Bild hat genau die Widget-Groesse (64 px, Zoom 256), Drehpunkt = Mitte.
    const lv_img_dsc_t* d = iconHolen("windpfeil");
    lv_obj_t* box = p;
    int cx = lv_obj_get_x(box) + lv_obj_get_width(box) / 2, cy = lv_obj_get_y(box) + lv_obj_get_height(box) / 2;
    lv_img_set_src(p, d);
    lv_img_set_size_mode(p, LV_IMG_SIZE_MODE_VIRTUAL);
    lv_img_set_antialias(p, true);
    lv_img_set_zoom(p, 256);
    lv_obj_set_size(p, d->header.w, d->header.h);
    lv_obj_set_pos(p, cx - d->header.w / 2, cy - d->header.h / 2);
    lv_img_set_pivot(p, d->header.w / 2, d->header.h / 2);
    lv_timer_create(pfeilPendelTimer, 100, nullptr);
    s_pfeilBereit = true;
  }
  const int ziel = (((richtungGrad + 180) % 360) + 360) % 360 * 10;   // Wind kommt aus richtungGrad, der Pfeil zeigt, wohin er weht
  if (ziel == s_pfeilWinkel) return;
  const int delta = ((ziel - s_pfeilWinkel + 1800 + 3600) % 3600) - 1800;
  lv_anim_del(p, pfeilWinkelCb);
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, p); lv_anim_set_exec_cb(&a, pfeilWinkelCb);
  lv_anim_set_values(&a, s_pfeilWinkel, s_pfeilWinkel + delta);
  lv_anim_set_time(&a, 1200); lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
  s_pfeilWinkel = ziel;
}

static void setDatumUhrzeit(const struct tm& ti) {
  char buf[24];
  static const char* wt[] = {"So","Mo","Di","Mi","Do","Fr","Sa"};
  strftime(buf, sizeof(buf), "%H:%M", &ti);
  setLabel(objects.labeltime, buf);
  snprintf(buf, sizeof(buf), "%s. %02d.%02d.%04d", wt[ti.tm_wday], ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
  setLabel(objects.labeldatum, buf);
}

// Die gewählten Allergene („Meine Allergene“), stärkste zuerst; nicht gewählte stehen mit -100 am Ende.
// Wert: Open-Meteo (nächste Stunde), für Hasel, Esche und Roggen der DWD-Tageswert
struct PollenEintrag { float v; const char* name; };
static void pollenSortiert(PollenEintrag (&pe)[8]) {
  for (int i = 0; i < 8; i++) pe[i] = { pollenGewaehlt(i) ? pollenWertWarnung(i) : -100.0f, POLLEN_ANZEIGE[i] };
  for (int i = 0; i < 7; i++)
    for (int j = i + 1; j < 8; j++)
      if (pe[j].v > pe[i].v) { PollenEintrag t = pe[i]; pe[i] = pe[j]; pe[j] = t; }
}

// ── Hinweis-Kachel links oben auf dem Hauptscreen (Sensor-Alarm / Regen / Frost / UV) ───────
// Neuer Hinweis: die Kachel blinkt, Antippen bestätigt ihn (danach Dauerleuchten, solange die Bedingung gilt).
// Mehrere Hinweise: unbestätigte zuerst (Sensor-Alarm, Regen, Frost, UV, Hitze, Luftdruck, Gießen), danach Dauerleuchten des wichtigsten (gleiche Reihenfolge);
// Antippen im Dauerleuchten schaltet zum nächsten aktiven Hinweis. Ein bestätigter Hinweis blinkt erst wieder,
// wenn seine Bedingung mindestens 20 Minuten lang weg war.
// Das Label wird hier angelegt (nicht im PicoPixel-Export); wer ein eigenes Label gestalten will, setzt es hier ein.
enum { HW_SENSOR = 0, HW_REGEN = 1, HW_FROST = 2, HW_UV = 3, HW_SCHATTEN = 4, HW_DRUCK = 5, HW_GIESSEN = 6, HW_GEFUEHLT = 7, HW_ANZAHL = 8 };
struct Hinweis { bool aktiv = false; bool bestaetigt = false; unsigned long inaktivSeit = 0; char text[80] = ""; };
static Hinweis hinweise[HW_ANZAHL];
static const uint32_t HW_FARBE[HW_ANZAHL] = {0xB91C1C, 0x0A5CE0, 0x0E7490, 0xB45309, 0xC2410C, 0x475569, 0x15803D, 0x1E5AA8};   // Rot (Sensor-Alarm), Blau, Petrol, Orangebraun, Orange (Hitze), Schiefer (Druck), Gruen (Giessen); weiße Schrift
static const float    HW_UV_SCHWELLE = 6.0f;                                  // UV-Index "hoch"
static lv_obj_t* labelHinweis    = nullptr;
static int       hinweisAngezeigt = -1;
static int       hinweisManuell   = -1;
static bool      hinweisBlinkt    = false;
static bool      hinweisPhase     = false;

// Regen-Nowcast: "Regen in 25 Min." / "Regen noch 10 Min." / "Es regnet"
static bool hinweisRegen(char* buf, size_t n) {
  const float SCHWELLE = 0.1f;   // mm je 15 Min. (ab hier zählt ein Slot als Regen)
  time_t jetzt = time(nullptr);
  if (wetter.nc_n <= 0 || jetzt < 1700000000L || jetzt - wetter.nc_start[0] >= 6 * 3600L) return false;
  int aktuell = -1;   // Slot, in dem wir gerade sind
  for (int i = 0; i < wetter.nc_n; i++)
    if (jetzt >= wetter.nc_start[i] && jetzt < wetter.nc_start[i] + 900) { aktuell = i; break; }
  if (aktuell >= 0 && wetter.nc_mm[aktuell] >= SCHWELLE) {
    int ende = -1;   // erster trockener Slot danach
    for (int i = aktuell + 1; i < wetter.nc_n; i++) if (wetter.nc_mm[i] < SCHWELLE) { ende = i; break; }
    if (ende < 0) { snprintf(buf, n, "Es regnet"); return true; }
    int m = (int)((wetter.nc_start[ende] - jetzt + 59) / 60);
    snprintf(buf, n, "Regen noch %d Min.", m < 1 ? 1 : m);
    return true;
  }
  for (int i = 0; i < wetter.nc_n; i++) {
    if (wetter.nc_start[i] + 900 <= jetzt) continue;   // Slot schon vorbei
    if (wetter.nc_mm[i] >= SCHWELLE) {
      int m = (int)((wetter.nc_start[i] - jetzt + 59) / 60);
      snprintf(buf, n, "Regen in %d Min.", m < 1 ? 1 : m);
      return true;
    }
  }
  return false;
}

// Frost: tiefste Temperatur der nächsten 12 Stunden <= 0 °C
static bool hinweisFrost(char* buf, size_t n) {
  if (wetter.frost_min > 0.0f) return false;   // auch 99 = keine Daten
  int t = (int)floorf(wetter.frost_min + 0.5f);
  if (wetter.frost_jetzt)             snprintf(buf, n, "Frost, bis %d °C", t);
  else if (wetter.frost_ab.length())  snprintf(buf, n, "Frost ab %s, %d °C", wetter.frost_ab.c_str(), t);
  else                                snprintf(buf, n, "Frost, %d °C", t);
  return true;
}

// UV: Index jetzt oder in den nächsten 3 Stunden ab der Schwelle
static bool hinweisUv(char* buf, size_t n) {
  if (wetter.uv_hinweis < HW_UV_SCHWELLE) return false;
  int uv = (int)floorf(wetter.uv_hinweis + 0.5f);
  const char* stufe = wetter.uv_hinweis >= 11.0f ? "extrem" : wetter.uv_hinweis >= 8.0f ? "sehr hoch" : "hoch";
  snprintf(buf, n, "UV %s (%d), Sonnenschutz", stufe, uv);
  return true;
}

// Heisser Tag: Hoechsttemperatur ab Schwelle und starke Sonne (UV 6 oder mehr), tagsueber -> Rolllaeden / Sonnenschutz
static bool hinweisSchatten(char* buf, size_t n) {
  if (!wetter.geladen || !wetter.is_day) return false;
  struct tm ti; if (!getLocalTime(&ti, 0)) return false;
  if (ti.tm_hour < 7 || ti.tm_hour >= 17) return false;
  if (wetter.temp_max < (float)cfg.schatten_temp || wetter.uv_index < 6.0f) return false;
  snprintf(buf, n, "Heißer Tag (%.0f °C, UV %.0f)\nRollläden und Sonnenschutz", wetter.temp_max, wetter.uv_index);
  return true;
}

// Gefuehlte Temperatur deutlich kaelter als die gemessene (Wind): ab 5 Grad Unterschied
static bool hinweisGefuehlt(char* buf, size_t n) {
  if (!wetter.geladen) return false;
  if (wetter.temp - wetter.feels_like < 5.0f) return false;
  snprintf(buf, n, "Gefühlt %.0f °C, deutlich kälter\n(gemessen %.0f °C)", wetter.feels_like, wetter.temp);
  return true;
}

// Wetterumschwung: der Luftdruck ist in den letzten 3 Stunden um 4 hPa oder mehr gefallen
static bool hinweisDruck(char* buf, size_t n) {
  if (!klima.druckOk || klima.druckDelta3h > -4.0f) return false;
  snprintf(buf, n, "Luftdruck fällt schnell\n%.1f hPa in 3 Stunden", klima.druckDelta3h);
  return true;
}

// Garten: April bis September, wenig Regen in den letzten 3 Tagen und keiner in Sicht, warmer Tag
static bool hinweisGiessen(char* buf, size_t n) {
  struct tm ti; if (!getLocalTime(&ti, 0)) return false;
  if (ti.tm_mon < 3 || ti.tm_mon > 8) return false;
  if (!klima.regenOk || klima.regen3Tage >= 3.0f || klima.regenHeute + klima.regenMorgen >= 2.0f || klima.tmaxHeute < 18.0f) return false;
  snprintf(buf, n, "Garten: Gießen lohnt sich\nnur %.0f mm Regen in 3 Tagen", klima.regen3Tage);
  return true;
}

static void zeichneHinweisFarbe() {
  if (!labelHinweis || hinweisAngezeigt < 0) return;
  lv_color_t c = lv_color_hex(HW_FARBE[hinweisAngezeigt]);
  if (hinweisBlinkt && !hinweisPhase) c = lv_color_mix(lv_color_black(), c, 120);   // dunklere Phase beim Blinken
  lv_obj_set_style_bg_color(labelHinweis, c, LV_PART_MAIN);
}

static void cbHinweisBlink(lv_timer_t*) {
  hinweisPhase = !hinweisPhase;
  if (hinweisBlinkt) zeichneHinweisFarbe();
}

static void cbHinweisTap(lv_event_t*) {
  if (hinweisAngezeigt < 0) return;
  if (hinweisBlinkt) {
    hinweise[hinweisAngezeigt].bestaetigt = true;   // bestätigen: nächster unbestätigter Hinweis oder Dauerleuchten
    hinweisManuell = -1;
  } else {
    for (int k = 1; k < HW_ANZAHL; k++) {            // Dauerleuchten: zum nächsten aktiven Hinweis wechseln
      int c = (hinweisAngezeigt + k) % HW_ANZAHL;
      if (hinweise[c].aktiv) { hinweisManuell = c; break; }
    }
  }
  aktualisiereHinweise();
}

void aktualisiereHinweise() {
  LvLock lock;
  if (!objects.screen_1) return;
  if (!labelHinweis) {
    labelHinweis = lv_label_create(objects.screen_1);
    // Freie Fläche unter der Uhr (y 191–279): genauso breit wie die Uhr (325 px) und mittig dazu; lange Texte brechen um
    lv_label_set_long_mode(labelHinweis, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(labelHinweis, 325);
    lv_obj_align(labelHinweis, LV_ALIGN_TOP_MID, 0, 198);
    lv_obj_set_style_text_font(labelHinweis, &font_montserrat_22, 0);
    lv_obj_set_style_text_align(labelHinweis, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(labelHinweis, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(labelHinweis, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(labelHinweis, 10, 0);
    lv_obj_set_style_pad_ver(labelHinweis, 15, 0);
    lv_obj_set_style_pad_hor(labelHinweis, 10, 0);
    lv_obj_add_flag(labelHinweis, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(labelHinweis, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(labelHinweis, 8);
    lv_obj_add_event_cb(labelHinweis, cbHinweisTap, LV_EVENT_CLICKED, nullptr);
    lv_timer_create(cbHinweisBlink, 500, nullptr);
  }
  char buf[HW_ANZAHL][80];
  bool akt[HW_ANZAHL];
  akt[HW_SENSOR] = cfg.hinweis_sensor && hinweisSensorText(buf[HW_SENSOR], sizeof(buf[0]));
  akt[HW_REGEN] = cfg.hinweis_regen && hinweisRegen(buf[HW_REGEN], sizeof(buf[0]));
  akt[HW_FROST] = cfg.hinweis_frost && hinweisFrost(buf[HW_FROST], sizeof(buf[0]));
  akt[HW_UV]    = cfg.hinweis_uv    && hinweisUv   (buf[HW_UV],    sizeof(buf[0]));
  akt[HW_SCHATTEN] = cfg.hinweis_schatten && hinweisSchatten(buf[HW_SCHATTEN], sizeof(buf[0]));
  akt[HW_DRUCK]    = cfg.hinweis_druck    && hinweisDruck   (buf[HW_DRUCK],    sizeof(buf[0]));
  akt[HW_GIESSEN]  = cfg.hinweis_giessen  && hinweisGiessen (buf[HW_GIESSEN],  sizeof(buf[0]));
  akt[HW_GEFUEHLT] = cfg.hinweis_gefuehlt && hinweisGefuehlt (buf[HW_GEFUEHLT], sizeof(buf[0]));
  unsigned long jetzt = millis();
  for (int i = 0; i < HW_ANZAHL; i++) {
    Hinweis& h = hinweise[i];
    if (akt[i]) {
      strlcpy(h.text, buf[i], sizeof(h.text));
      h.aktiv = true;
      h.inaktivSeit = 0;
    } else {
      if (h.aktiv) { h.aktiv = false; h.inaktivSeit = jetzt ? jetzt : 1; }
      if (h.bestaetigt && h.inaktivSeit && jetzt - h.inaktivSeit >= 1200000UL) { h.bestaetigt = false; h.inaktivSeit = 0; }
    }
  }
  int zeige = -1;
  hinweisBlinkt = false;
  for (int i = 0; i < HW_ANZAHL; i++) if (hinweise[i].aktiv && !hinweise[i].bestaetigt) { zeige = i; hinweisBlinkt = true; break; }
  if (zeige < 0) {
    if (hinweisManuell >= 0 && hinweise[hinweisManuell].aktiv) zeige = hinweisManuell;
    else for (int i = 0; i < HW_ANZAHL; i++) if (hinweise[i].aktiv) { zeige = i; break; }
  }
  if (warnRuheAktiv()) zeige = -1;      // Ruhezeit: keine Hinweis-Kachel (der Zustand bleibt erhalten, sie kommt danach wieder)
  hinweisAngezeigt = zeige;
  if (zeige < 0) { lv_obj_add_flag(labelHinweis, LV_OBJ_FLAG_HIDDEN); return; }
  if (strcmp(lv_label_get_text(labelHinweis), hinweise[zeige].text) != 0) lv_label_set_text(labelHinweis, hinweise[zeige].text);
  zeichneHinweisFarbe();
  lv_obj_clear_flag(labelHinweis, LV_OBJ_FLAG_HIDDEN);
}

// Schnelle Aktualisierung ohne Daten: Uhrzeit, Datum und Regen-Nowcast (jede Minute)
void aktualisiereUhr() {
  struct tm ti;
  if (!getLocalTime(&ti, 0)) return;
  {
    LvLock lock;
    setDatumUhrzeit(ti);
  }
  aktualisiereHinweise();
}

// Allergen-Screen: Typ-Icons (Baum, Gras, Kraut) aus dem Code und Schrift der Gruppenueberschriften
static void allergenEinrichten() {
  static bool fertig = false;
  if (fertig || !objects.screenallergen) return;
  fertig = true;
  struct { lv_obj_t* o; const char* icon; } z[] = {
    {objects.imagepollenhasel, "pollen-tree"},  {objects.imagepollenerle, "pollen-tree"},
    {objects.imagepollenesche, "pollen-tree"},  {objects.imagepollenbirke, "pollen-tree"},
    {objects.imagepollengraeser, "pollen-grass"}, {objects.imagepollenroggen, "pollen-grass"},
    {objects.imagepollenbeifuss, "pollen-weed"}, {objects.imagepollenambrosia, "pollen-weed"}};
  for (auto& e : z) iconSetzen(e.o, e.icon, 40);
  // Biowetter-Screen: Icons der sieben Kategorien und Schrift der Gruppenueberschriften
  struct { lv_obj_t* o; const char* icon; } bz[] = {
    {objects.imagebiobefinden,   "bio-befinden"},   {objects.imagebiobpniedrig,  "bio-bp-niedrig"},
    {objects.imagebiobphoch,     "bio-bp-hoch"},    {objects.imagebiorheumaentz, "bio-rheuma-entz"},
    {objects.imagebiorheumadeg,  "bio-rheuma-deg"}, {objects.imagebioasthma,     "bio-asthma"},
    {objects.imagebiowaerme,     "bio-waerme"}};
  for (auto& e : bz) iconSetzen(e.o, e.icon, 40);
  if (objects.labelgruppeheute)  lv_obj_set_style_text_font(objects.labelgruppeheute, &font_montserrat_22, 0);
  if (objects.labelgruppemorgen) lv_obj_set_style_text_font(objects.labelgruppemorgen, &font_montserrat_22, 0);
  if (objects.labelgruppestunden) lv_obj_set_style_text_font(objects.labelgruppestunden, &font_montserrat_20, 0);
  if (objects.labelgruppetage)    lv_obj_set_style_text_font(objects.labelgruppetage, &font_montserrat_20, 0);
}

// ── Luftfeuchte und Luftdruck (Container 30) ────────────────────────────────────────────────────────────────────────────
// Zwei Zeilen mit Symbol statt Beschriftung: oben Luftfeuchte, unten Luftdruck (Zahl, darunter "hPa") und Trend-Pfeil.
// Positionen setzt der Code (PicoPixel verschiebt sie gern), Masse relativ zum Container (150 x 100).
static const int LD_ICON = 36, LD_PFEIL = 40;

static uint32_t themaTextfarbe() { return theme_colors[active_theme_index][THEME_COLOR_TEXT]; }

// Symbol in genau seiner Quellgroesse an eine Stelle setzen (ohne Zoom: kein Verschieben, kein Abschneiden); gedrehte Bilder: Drehpunkt Mitte
static void iconExakt(lv_obj_t* o, const char* name, int x, int y) {
  const lv_img_dsc_t* d = iconHolen(name);
  if (!o || !d) return;
  lv_img_set_src(o, d);
  lv_img_set_size_mode(o, LV_IMG_SIZE_MODE_VIRTUAL);
  lv_img_set_antialias(o, true);
  lv_img_set_zoom(o, 256);
  lv_obj_set_size(o, d->header.w, d->header.h);
  lv_obj_set_pos(o, x, y);
  lv_img_set_pivot(o, d->header.w / 2, d->header.h / 2);
}

// Raster der Kachel (150 x 100): links die Symbole (x 8), daneben links ausgerichtet die Werte (x 54), rechts der Trend-Pfeil.
// Zeile 1 Luftfeuchte (y 6 bis 42), Zeile 2 Luftdruck (y 54 bis 96): Zahl oben, "hPa" klein darunter.
static void luftLayout() {
  static bool fertig = false;
  if (fertig || !objects.container_30) return;
  fertig = true;
  auto txt = [](lv_obj_t* l, const lv_font_t* f, int x, int y, int w) {
    if (!l) return;
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_size(l, w, lv_font_get_line_height(f)); lv_obj_set_pos(l, x, y);
  };
  const int h22 = lv_font_get_line_height(&font_montserrat_22);
  txt(objects.labelhumidity,     &font_montserrat_22, 54, 6 + (LD_ICON - h22) / 2, 90);
  txt(objects.labelairpressur,   &font_montserrat_22, 54, 53, 52);
  txt(objects.labelpressureunit, &font_montserrat_latin_ext_16, 54, 53 + h22 - 3, 52);
  iconExakt(objects.imagehumidityicon,  "humidity-36",  8, 6);
  iconExakt(objects.imagebarometericon, "barometer-36", 8, 56);
  if (objects.labelpressureunit) lv_label_set_text(objects.labelpressureunit, "hPa");
}

static void luftFarben() {
  const lv_color_t c = lv_color_hex(themaTextfarbe());
  lv_obj_t* l[] = {objects.labelhumidity, objects.labelairpressur, objects.labelpressureunit};
  for (lv_obj_t* o : l) if (o && !farbeGleich(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) lv_obj_set_style_text_color(o, c, 0);
}

// Luftdruck-Trend als Pfeil rechts neben dem Luftdruck (Wind-Pfeil-Symbol, gedreht): hoch = steigend, rechts = gleichbleibend, runter = fallend
static void druckTrendZeichnen() {
  lv_obj_t* p = objects.imagedrucktrend;
  if (!p) return;
  if (!klima.druckOk) { lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN); return; }
  static bool bereit = false;
  if (!bereit) { iconExakt(p, "windpfeil-40", 150 - 6 - LD_PFEIL, 56); bereit = true; }
  const float dd = klima.druckDelta3h;
  int winkel; uint32_t farbe;
  if (dd >= 1.5f)       { winkel = 0;    farbe = 0x00A838; }   // steigt: Pfeil nach oben, gruen
  else if (dd <= -4.0f) { winkel = 1800; farbe = 0xE01E00; }   // faellt schnell: nach unten, rot
  else if (dd <= -1.5f) { winkel = 1800; farbe = 0xE8A000; }   // faellt: nach unten, gelb
  else                  { winkel = 900;  farbe = 0x888888; }   // gleichbleibend: nach rechts, grau
  lv_img_set_angle(p, winkel);
  lv_obj_set_style_img_recolor(p, lv_color_hex(farbe), 0);
  lv_obj_set_style_img_recolor_opa(p, LV_OPA_COVER, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_HIDDEN);
}

// ── Mondphase und UV-Index (Container 32) ───────────────────────────────────────────────────────────────────────────────
// Oben die Mondphase (Symbol, Name, beleuchteter Anteil), darunter der aktuelle UV-Index. Masse relativ zum Container (158 x 266).

static void mondUvZeichnen() {
  if (!objects.container_32) return;
  static const char* MOND_ICON[8] = {"moon-new", "moon-waxing-crescent", "moon-first-quarter", "moon-waxing-gibbous",
                                     "moon-full", "moon-waning-gibbous", "moon-last-quarter", "moon-waning-crescent"};
  static const char* MOND_NAME[8] = {"Neumond", "Zunehmende Sichel", "Erstes Viertel", "Zunehmender Mond",
                                     "Vollmond", "Abnehmender Mond", "Letztes Viertel", "Abnehmende Sichel"};
  static bool fertig = false;
  if (!fertig) {
    fertig = true;
    auto txt = [](lv_obj_t* l, const lv_font_t* f, uint8_t m, int x, int y, int w, int h) {
      if (!l) return;
      lv_label_set_long_mode(l, m);
      lv_obj_set_pos(l, x, y); lv_obj_set_size(l, w, h);
      lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    };
    txt(objects.labelmoonname,  &font_montserrat_18, LV_LABEL_LONG_WRAP, 4, 94, 150, 22);
    txt(objects.labelmooninfo,  &font_montserrat_latin_ext_16, LV_LABEL_LONG_CLIP, 4, 144, 150, 20);
    txt(objects.labeluvvalue,   &font_montserrat_48, LV_LABEL_LONG_CLIP, 62, 178, 92, lv_font_get_line_height(&font_montserrat_48));
    txt(objects.labeluvtext,    &font_montserrat_latin_ext_16, LV_LABEL_LONG_CLIP, 4, 236, 150, 20);
    wetterIconZeichnen(objects.imageuvicon, iconHolen("uv-index"), 56, 6, 172);
  }
  char buf[48];
  const lv_color_t text = lv_color_hex(themaTextfarbe());
  // Mond (nur mit gueltiger Uhrzeit)
  struct tm ti;
  if (getLocalTime(&ti, 0)) {
    float hell; int ph; mondPhase(time(nullptr), &hell, &ph);
    const lv_img_dsc_t* d = iconHolen(MOND_ICON[ph]);
    if (d && objects.imagemoon && lv_img_get_src(objects.imagemoon) != (const void*)d) wetterIconZeichnen(objects.imagemoon, d, 88, 35, 6);
    setLabel(objects.labelmoonname, MOND_NAME[ph]);
    snprintf(buf, sizeof(buf), "%.0f %% beleuchtet", hell);
    setLabel(objects.labelmooninfo, buf);
    // Die Prozentzeile folgt direkt dem Namen (ein oder zwei Zeilen)
    if (objects.labelmoonname && objects.labelmooninfo) {
      lv_obj_set_height(objects.labelmoonname, LV_SIZE_CONTENT);
      lv_obj_update_layout(objects.labelmoonname);
      const int y = lv_obj_get_y(objects.labelmoonname) + lv_obj_get_height(objects.labelmoonname) + 2;
      if (lv_obj_get_y(objects.labelmooninfo) != y) lv_obj_set_y(objects.labelmooninfo, y);
    }
  }
  // UV-Index jetzt (Stunde) mit Stufe und Tageshoechstwert
  if (wetter.geladen) {
    const float uv = wetter.uv_jetzt;
    const char* stufe = uv >= 11 ? "extrem" : uv >= 8 ? "sehr hoch" : uv >= 6 ? "hoch" : uv >= 3 ? "mäßig" : "niedrig";
    snprintf(buf, sizeof(buf), "%.1f", uv);
    setLabelFarbe(objects.labeluvvalue, uvColor(uv), buf);
    snprintf(buf, sizeof(buf), "%s, max. %.0f", stufe, wetter.uv_index);
    setLabel(objects.labeluvtext, buf);
  }
  // Textfarben des Themas (der Designwechsel setzt die Farben der Beschriftungen zurueck)
  lv_obj_t* l[] = {objects.labelmoonname, objects.labelmooninfo, objects.labeluvtext};
  for (lv_obj_t* o : l) if (o && !farbeGleich(lv_obj_get_style_text_color(o, LV_PART_MAIN), text)) lv_obj_set_style_text_color(o, text, 0);
}


// Namensnennung der Datenquellen: die Labels (labelquelle*) kommen aus PicoPixel und sind dort verschiebbar; hier wird nur die Schrift gesetzt
// (mit Umlauten), soweit PicoPixel keine mitgibt.
static void quellenHinweiseAnlegen() {
  static bool fertig = false;
  if (fertig) return;
  fertig = true;
  lv_obj_t* l[] = {objects.labelquellemenue_1, objects.labelquelleallergen, objects.labelquellebio, objects.labelquellekarte1, objects.labelquellekarte2};
  for (lv_obj_t* o : l) if (o) lv_obj_set_style_text_font(o, &font_montserrat_latin_ext_16, 0);
}

void aktualisiereUI() {
  spur("UI: aktualisieren");
  LvLock lock;
  allergenEinrichten();
  aktualisiereHinweise();
  aktualisiereVerlauf();
  aktualisiereWoche();
  struct tm ti;
  bool hasTime = getLocalTime(&ti, 0);
  char buf[32];

  // ── Hauptscreen ────────────────────────────────────────────────────────────
  if (hasTime) setDatumUhrzeit(ti);
  if (wetter.geladen) {
    snprintf(buf, sizeof(buf), "%.0f °C", wetter.temp);
    setLabelFarbe(objects.labeltemp, tempColor(wetter.temp), buf);
    // gefühlte Temperatur (kleine Schrift unter der großen Temperatur; Schrift hier gesetzt, weil PicoPixel sie nicht binden konnte)
    if (objects.labelgefuehlt) {
      lv_obj_set_style_text_font(objects.labelgefuehlt, &font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
      snprintf(buf, sizeof(buf), "gefühlt %.0f °C", wetter.feels_like);
      setLabel(objects.labelgefuehlt, buf);
    }
    setWetterIcon(objects.imagewetter, wmoZuImage(wetter.wmo_code, wetter.is_day), wmoZuAnimName(wetter.wmo_code, wetter.is_day), cfg.anim_symbole);
    snprintf(buf, sizeof(buf), "%.0f %%", wetter.humidity);        setLabel(objects.labelhumidity, buf);
    snprintf(buf, sizeof(buf), "%.0f km/h", wetter.wind_speed);    setLabel(objects.labelwindspeed, buf);
    setLabel(objects.labelwinddirection, windRichtung(wetter.wind_dir));
    windPfeil(wetter.wind_dir);
    luftLayout();
    snprintf(buf, sizeof(buf), "%.0f", wetter.pressure);           setLabel(objects.labelairpressur, buf);   // Einheit steht in einer eigenen Beschriftung darunter
    luftFarben();
    druckTrendZeichnen();
    mondUvZeichnen();

    // 4-Stunden-Vorhersage
    lv_obj_t* hzeit[4] = {objects.labelh1zeit, objects.labelh2zeit, objects.labelh3zeit, objects.labelh4zeit};
    lv_obj_t* htemp[4] = {objects.labelh1temp, objects.labelh2temp, objects.labelh3temp, objects.labelh4temp};
    lv_obj_t* hws[4]   = {objects.labelh1windspeed, objects.labelh2windspeed, objects.labelh3windspeed, objects.labelh4windspeed};
    lv_obj_t* hwd[4]   = {objects.labelh1winddirection, objects.labelh2winddirection, objects.labelh3winddirection, objects.labelh4winddirection};
    lv_obj_t* himg[4]  = {objects.imageh1, objects.imageh2, objects.imageh3, objects.imageh4};
    for (int i = 0; i < 4; i++) {
      setLabel(hzeit[i], wetter.time_forecast[i].c_str());
      snprintf(buf, sizeof(buf), "%.0f °C", wetter.temp_forecast[i]);       setLabelFarbe(htemp[i], tempColor(wetter.temp_forecast[i]), buf);
      snprintf(buf, sizeof(buf), "%.0f km/h", wetter.wind_speed_forecast[i]); setLabel(hws[i], buf);
      setLabel(hwd[i], windRichtung(wetter.wind_dir_forecast[i]));
      setWetterIcon(himg[i], wmoZuImage(wetter.wmo_forecast[i], wetter.tag_forecast[i]), wmoZuAnimName(wetter.wmo_forecast[i], wetter.tag_forecast[i]), cfg.anim_stunden);
    }

    // Sonne, Tages-Min/Max (der UV-Tageswert ist aus dem Layout entfallen, UV steht in der Leiste unten)
    setLabel(objects.labelsunrisetime, wetter.sunrise.isEmpty() ? "--:--" : wetter.sunrise.c_str());
    setLabel(objects.labelsundowntime, wetter.sunset.isEmpty()  ? "--:--" : wetter.sunset.c_str());
    snprintf(buf, sizeof(buf), "%.0f °C", wetter.temp_min);               setLabelFarbe(objects.labeltempminvalue, tempColor(wetter.temp_min), buf);
    snprintf(buf, sizeof(buf), "%.0f °C", wetter.temp_max);               setLabelFarbe(objects.labeltempmaxvalue, tempColor(wetter.temp_max), buf);
  } else if (objects.labelgefuehlt) {
    setLabel(objects.labelgefuehlt, "");   // bis die Wetterdaten da sind, steht kein Platzhalter da
  }
  setLabel(objects.labelversion, FIRMWARE_VERSION);
  quellenHinweiseAnlegen();

  // Top-2-Pollen (nächste Stunde)
  {
    PollenEintrag pe[8];
    pollenSortiert(pe);
    lv_obj_t* pn[2] = {objects.labelpollenforecast1main, objects.labelpollenforecast2main};
    lv_obj_t* pv[2] = {objects.labelpollenforecast1mainvalue, objects.labelpollenforecast2mainvalue};
    for (int i = 0; i < 2; i++) {
      if (pe[i].v <= -99.0f) { setLabel(pn[i], "-"); setLabel(pv[i], ""); continue; }   // weniger als 2 Allergene gewählt
      setLabel(pn[i], pe[i].v >= 0 ? pe[i].name : "--");
      setPollenLabel(pv[i], pe[i].v);
    }
  }

  // Luftqualität auf dem Hauptscreen (Bogen) und in der Kurzleiste unten
  {
    int aqi = pollen.aqi;
    if (objects.arcaqi) {
      int wert = (aqi >= 0) ? min(aqi, 150) : 0;
      lv_color_t col = aqiColor(aqi);
      if (lv_arc_get_max_value(objects.arcaqi) != 150) lv_arc_set_range(objects.arcaqi, 0, 150);
      if (lv_arc_get_value(objects.arcaqi) != wert) lv_arc_set_value(objects.arcaqi, wert);
      if (!farbeGleich(lv_obj_get_style_arc_color(objects.arcaqi, LV_PART_INDICATOR), col))
        lv_obj_set_style_arc_color(objects.arcaqi, col, LV_PART_INDICATOR);
    }
    if (aqi >= 0) snprintf(buf, sizeof(buf), "%d", aqi); else snprintf(buf, sizeof(buf), "--");
    setLabel(objects.labelaqivalue, buf);
    setLabel(objects.labelaqistatus, aqiStatusText(aqi));

    setLabelFarbe(objects.labelwsaqi, aqiColor(aqi), buf);
    snprintf(buf, sizeof(buf), pollen.o3 >= 0 ? "%.0f" : "--", pollen.o3);     setLabelFarbe(objects.labelwso3,   o3Color(pollen.o3),     buf);
    snprintf(buf, sizeof(buf), pollen.pm25 >= 0 ? "%.0f" : "--", pollen.pm25); setLabelFarbe(objects.labelwspm25, pm25Color(pollen.pm25), buf);
    snprintf(buf, sizeof(buf), "%.0f", wetter.uv_next3h);                      setLabelFarbe(objects.labelwsuv,   uvColor(wetter.uv_next3h), buf);
  }

  // Biowetter-Kurzübersicht (4 wählbare Kategorien): vor 18 Uhr heute Nachmittag, danach morgen Vormittag
  {
    static const char* BIO_NAMEN[] = {"Allg. Befinden","Hypotonie","Bluthochdruck","Rheuma entz.","Rheuma deg.","Asthma","Wärmebelast."};
    int cats[4] = {cfg.ws_cat0, cfg.ws_cat1, cfg.ws_cat2, cfg.ws_cat3};
    lv_obj_t* nameL[4] = {objects.labelwscat1name, objects.labelwscat2name, objects.labelwscat3name, objects.labelwscat4name};
    lv_obj_t* valL[4]  = {objects.labelwscat1val,  objects.labelwscat2val,  objects.labelwscat3val,  objects.labelwscat4val};
    int periode = (hasTime && ti.tm_hour >= 18) ? 2 : 1;
    setLabel(objects.biowetter, periode == 1 ? "Heute Nachmittag" : "Morgen Vormittag");
    bool leer = !bio.geladen || !bio.vorhanden[periode];
    for (int i = 0; i < 4; i++) {
      int cat = (cats[i] < 0 || cats[i] > 6) ? 0 : cats[i];
      setLabel(nameL[i], BIO_NAMEN[cat]);
      if (leer) setLabelFarbe(valL[i], lv_color_hex(0x888888), "-");
      else setLabelFarbe(valL[i], bioWertColor(bio.wert[periode][cat]), bioWertText(bio.wert[periode][cat]));
    }
  }

  // ── Biowetter & Pollen ─────────────────────────────────────────────────────
  {
    // [Block][Spalte][Kategorie]: Block 0 = Heute, 1 = Morgen; Spalte 0 = Vormittag, 1 = Nachmittag
    lv_obj_t* bl[2][2][7] = {
      { { objects.labelbiobevindenvm,  objects.labelbiobpniedrigvm,  objects.labelbiobphochvm,  objects.labelbioreumentzvm,  objects.labelbioreumdegenvm,  objects.labelbioapsthmvm,  objects.labelbiow_rmevm },
        { objects.labelbiobewindennm,  objects.labelbiobpniedrignm,  objects.labelbiobphochnm,  objects.labelbioreumentznm,  objects.labelbioreumdegenm,   objects.labelbioapsthmam,  objects.labelbiow_rmenm } },
      { { objects.labelbiobevindenvm2, objects.labelbiobpniedrigvm2, objects.labelbiobphochvm2, objects.labelbioreumentzvm2, objects.labelbioreumdegenvm2, objects.labelbioapsthmvm2, objects.labelbiow_rmevm2 },
        { objects.labelbiobewindennm2, objects.labelbiobpniedrignm2, objects.labelbiobphochnm2, objects.labelbioreumentznm2, objects.labelbioreumdegenm2,  objects.labelbioapsthmam2, objects.labelbiow_rmenm2 } }
    };
    const int periode[2][2] = { {0, 1}, {2, 3} };   // today_morning/afternoon, tomorrow_morning/afternoon
    for (int b = 0; b < 2; b++)
      for (int s = 0; s < 2; s++) {
        int p = periode[b][s];
        bool leer = !bio.geladen || !bio.vorhanden[p];
        for (int k = 0; k < 7; k++) {
          setBioChip(bl[b][s][k], leer ? 0 : bio.wert[p][k], leer);
        }
      }

    // Pollenvorhersage (DWD, 3 Tage): Spaltenköpfe sind die Daten
    if (hasTime) {
      time_t jetzt = mktime(&ti);
      lv_obj_t* dl[3] = {objects.labelday1, objects.labelday2, objects.labelday3};
      for (int d = 0; d < 3; d++) {
        time_t t = jetzt + d * 86400;
        struct tm tt; localtime_r(&t, &tt);
        strftime(buf, sizeof(buf), "%d.%m.", &tt);
        setLabel(dl[d], buf);
      }
    }
    setPollenChip(objects.labelbirkeday1, pollen.dwd_birke);       setPollenChip(objects.labelbirkeday2, pollen.dwd_birke_tmr);       setPollenChip(objects.labelbirkeday3, pollen.dwd_birke_da);
    setPollenChip(objects.labelgraeserday1, pollen.dwd_graeser);   setPollenChip(objects.labelgraeserday2, pollen.dwd_graeser_tmr);   setPollenChip(objects.labelgraeserday3, pollen.dwd_graeser_da);
    setPollenChip(objects.labelerleday1, pollen.dwd_erle);         setPollenChip(objects.labelerleday2, pollen.dwd_erle_tmr);         setPollenChip(objects.labelerleday3, pollen.dwd_erle_da);
    setPollenChip(objects.labelhaselday1, pollen.dwd_hasel);       setPollenChip(objects.labelhaselday2, pollen.dwd_hasel_tmr);       setPollenChip(objects.labelhaselday3, pollen.dwd_hasel_da);
    setPollenChip(objects.labelescheday1, pollen.dwd_esche);       setPollenChip(objects.labelescheday2, pollen.dwd_esche_tmr);       setPollenChip(objects.labelescheday3, pollen.dwd_esche_da);
    setPollenChip(objects.labelroggenday1, pollen.dwd_roggen);     setPollenChip(objects.labelroggenday2, pollen.dwd_roggen_tmr);     setPollenChip(objects.labelroggenday3, pollen.dwd_roggen_da);
    setPollenChip(objects.labelbeifussday1, pollen.dwd_beifuss);   setPollenChip(objects.labelbeifussday2, pollen.dwd_beifuss_tmr);   setPollenChip(objects.labelbeifussday3, pollen.dwd_beifuss_da);
    setPollenChip(objects.labelambrosiaday1, pollen.dwd_ambrosia); setPollenChip(objects.labelambrosiaday2, pollen.dwd_ambrosia_tmr); setPollenChip(objects.labelambrosiaday3, pollen.dwd_ambrosia_da);

    // Pollen stündlich (Open-Meteo, nächste 3 Stunden)
    lv_obj_t* tl[3] = {objects.labelhour1, objects.labelhour2, objects.labelhour3};
    lv_obj_t* birke[3]    = {objects.labelbirkehour1,    objects.labelbirkehour2,    objects.labelbirkehour3};
    lv_obj_t* graeser[3]  = {objects.labelgraeserhour1,  objects.labelgraeserhour2,  objects.labelgraeserhour3};
    lv_obj_t* erle[3]     = {objects.labelerlehour1,     objects.labelerlehour2,     objects.labelerlehour3};
    lv_obj_t* hasel[3]    = {objects.labelhaselhour1,    objects.labelhaselhour2,    objects.labelhaselhour3};
    lv_obj_t* beifuss[3]  = {objects.labelbeifusshour1,  objects.labelbeifusshour2,  objects.labelbeifusshour3};
    lv_obj_t* ambrosia[3] = {objects.labelambrosiahour1, objects.labelambrosiahour2, objects.labelambrosiahour3};
    lv_obj_t* roggen[3]   = {objects.labelroggenhour1,   objects.labelroggenhour2,   objects.labelroggenhour3};
    lv_obj_t* esche[3]    = {objects.labeleschehour1,    objects.labeleschehour2,    objects.labeleschehour3};
    for (int i = 0; i < 3; i++) {
      snprintf(buf, sizeof(buf), "%02d:00", pollen.h_slot[i]);
      setLabel(tl[i], buf);
      setPollenChip(birke[i],    openMeteoToDwd(pollen.h_birke[i]));
      setPollenChip(graeser[i],  openMeteoToDwd(pollen.h_graeser[i]));
      setPollenChip(erle[i],     openMeteoToDwd(pollen.h_erle[i]));
      setPollenChip(hasel[i],    pollen.h_hasel[i]);
      setPollenChip(beifuss[i],  openMeteoToDwd(pollen.h_beifuss[i]));
      setPollenChip(ambrosia[i], openMeteoToDwd(pollen.h_ambrosia[i]));
      setPollenChip(roggen[i],   pollen.h_roggen[i]);
      setPollenChip(esche[i],    pollen.h_esche[i]);
    }
  }

  // ── Luftqualität ───────────────────────────────────────────────────────────
  {
    auto bar = [](lv_obj_t* b, float val, int maxWert, lv_color_t col) {
      if (!b) return;
      if (lv_bar_get_max_value(b) != maxWert) lv_bar_set_range(b, 0, maxWert);
      int wert = (val >= 0) ? (int)val : 0;
      if (lv_bar_get_value(b) != wert) lv_bar_set_value(b, wert, LV_ANIM_OFF);
      if (!farbeGleich(lv_obj_get_style_bg_color(b, LV_PART_INDICATOR), col)) lv_obj_set_style_bg_color(b, col, LV_PART_INDICATOR);
    };
    snprintf(buf, sizeof(buf), pollen.pm25 >= 0 ? "%.1f" : "--", pollen.pm25); setLabelFarbe(objects.labelpm25value, pm25Color(pollen.pm25), buf);
    snprintf(buf, sizeof(buf), pollen.pm10 >= 0 ? "%.1f" : "--", pollen.pm10); setLabelFarbe(objects.labelpm10value, pm10Color(pollen.pm10), buf);
    snprintf(buf, sizeof(buf), pollen.no2  >= 0 ? "%.0f" : "--", pollen.no2);  setLabelFarbe(objects.labelno2value,  no2Color(pollen.no2),   buf);
    snprintf(buf, sizeof(buf), pollen.o3   >= 0 ? "%.0f" : "--", pollen.o3);   setLabelFarbe(objects.labelo3value,   o3Color(pollen.o3),     buf);
    bar(objects.barlabelpm25value, pollen.pm25, 75,  pm25Color(pollen.pm25));
    bar(objects.barpm10value,      pollen.pm10, 150, pm10Color(pollen.pm10));
    bar(objects.barlabelno2value,  pollen.no2,  200, no2Color(pollen.no2));
    bar(objects.barlabelo3value,   pollen.o3,   240, o3Color(pollen.o3));
  }

  // ── Pollenwarnung (der Warnscreen wird von uiWarnenPruefen angezeigt) ──────
  PollenEintrag pe[8];
  pollenSortiert(pe);
  pollenWarnAktiv = cfg.pollen_warn && (pe[0].v > (float)cfg.pollen_schwelle);
  if (pollenWarnAktiv) {
    setLabel(objects.labelpollenwarntitel, "Pollenwarnung !");
    setLabel(objects.labelpollenwarnart,   pe[0].name);
    setLabel(objects.labelpollenwarnhint,  "Tippen zum Bestätigen");
  }
}

// ── Bootscreen und Einrichtungs-Hinweis ──────────────────────────────────────
void uiBoot(const char* text, int pct) {
  LvLock lock;
  setLabel(objects.labelstatus, text);
  if (objects.barwifi) lv_bar_set_value(objects.barwifi, pct, LV_ANIM_OFF);
}

static lv_obj_t* hinweisScreen = nullptr;
static lv_obj_t* hinweisTitel  = nullptr;
static lv_obj_t* hinweisText   = nullptr;

void uiHinweis(const char* titel, const char* text) {
  LvLock lock;
  if (!hinweisScreen) {
    hinweisScreen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(hinweisScreen, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(hinweisScreen, LV_OPA_COVER, 0);

    hinweisTitel = lv_label_create(hinweisScreen);
    lv_obj_set_style_text_font(hinweisTitel, &font_montserrat_32, 0);
    lv_obj_set_style_text_color(hinweisTitel, lv_color_hex(0xf5a623), 0);
    lv_obj_align(hinweisTitel, LV_ALIGN_TOP_MID, 0, 50);

    hinweisText = lv_label_create(hinweisScreen);
    lv_label_set_long_mode(hinweisText, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hinweisText, 860);
    lv_obj_set_style_text_font(hinweisText, &font_montserrat_22, 0);
    lv_obj_set_style_text_color(hinweisText, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_align(hinweisText, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hinweisText, LV_ALIGN_TOP_MID, 0, 130);
  }
  lv_label_set_text(hinweisTitel, titel);
  lv_label_set_text(hinweisText, text);
  lv_scr_load(hinweisScreen);
}
