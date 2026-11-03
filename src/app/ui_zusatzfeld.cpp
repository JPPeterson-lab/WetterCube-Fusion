// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Zusatzfeld: Lueften-Ampel und Muellabfuhr ohne eigene Seite. Beide Inhalte lassen sich je nach Einstellung (WebUI, Karte "Zusatzfeld")
// in einem der beiden Felder des Hauptscreens zeigen: im Container der Pollenbelastung oder im Container des Biowetters.
// Mehrere Inhalte im selben Feld wechseln automatisch (alle 8 s) oder per Antippen des Feldes, je nach Einstellung.
// Die Objekte kommen aus PicoPixel (Screen 1, je Feld im Container: LabelZusatzStartTitel, LabelZusatzStartZeile1 bis 4 im Pollen-Container,
// LabelZusatzBioTitel, LabelZusatzBioZeile1 bis 4 im Bio-Container). Der Code setzt Lage, Schriften, Farben und Texte.
#include "app_state.h"
#include "lueften.h"
#include "../ui/screens.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
enum Inhalt { Z_ORIGINAL = 0, Z_LUEFTEN = 1, Z_MUELL = 2 };   // Z_ORIGINAL: was das Feld sonst zeigt (Pollen bzw. Biowetter)
const unsigned long WECHSEL_MS = 8000;

struct Feld {
  int ort;                       // Wert in cfg.zf_*: 1 = Pollen-Container, 2 = Bio-Container
  lv_obj_t* box = nullptr;       // Container
  lv_obj_t* titel = nullptr;
  lv_obj_t* z[4] = {nullptr, nullptr, nullptr, nullptr};
  lv_obj_t* marker[3] = {nullptr, nullptr, nullptr};     // farbige Tonnen-Kaestchen
  int w = 150;                   // Breite des Containers
  bool breit = false;            // Bio-Container: mehr Platz, groessere Schrift
  int idx = 0;                   // gezeigter Inhalt innerhalb der Liste
  unsigned long t0 = 0;
  int layout = -1;
};
Feld s_f[2] = {{1}, {2}};
bool s_bereit = false;

uint32_t textFarbe() { return theme_colors[active_theme_index][THEME_COLOR_TEXT] & 0xFFFFFF; }
void setzeText(lv_obj_t* o, const char* t) { if (o && strcmp(lv_label_get_text(o), t) != 0) lv_label_set_text(o, t); }
void setzeFarbe(lv_obj_t* o, uint32_t c) {
  if (!o) return;
  const lv_color_t col = lv_color_hex(c);
  if (lv_color_to32(lv_obj_get_style_text_color(o, LV_PART_MAIN)) != lv_color_to32(col)) lv_obj_set_style_text_color(o, col, 0);
}
void zeigen(lv_obj_t* o, bool an) {
  if (!o) return;
  if (an) { if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); }
  else    { if (!lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }
}
void kommaDezimal(char* s) { for (; *s; s++) if (*s == '.') *s = ','; }

// Farbe der Tonne nach dem Namen (Gelbe Tonne gelb, Bio braun, Rest grau, Papier blau, Glas gruen)
uint32_t tonnenFarbe(const char* name) {
  char k[64]; size_t i = 0;
  for (; name[i] && i < sizeof(k) - 1; i++) k[i] = (char)tolower((unsigned char)name[i]);
  k[i] = 0;
  auto hat = [&](const char* w) { return strstr(k, w) != nullptr; };
  if (hat("gelb") || hat("wertstoff") || hat("leichtverp") || hat("verpack")) return 0xF5C400;
  if (hat("bio") || hat("gruenabf") || hat("grünabf") || hat("garten") || hat("laub")) return 0x7A5230;
  if (hat("papier") || hat("karton") || hat("blau")) return 0x2563EB;
  if (hat("glas")) return 0x16A34A;
  if (hat("rest") || hat("schwarz") || hat("hausm") || hat("grau")) return 0x6B7280;
  return 0x9CA3AF;
}

bool lueftenDa(const LueftenStatus& s) { return s.zustand != LZ_KEINE_DATEN && s.nRaeume > 0; }
bool muellDa() { return cfg.muell_aktiv && muell.geladen && muell.n > 0; }

// Inhalte, die in diesem Feld laufen: zuerst das Original, dann Lueften und Muell, wenn sie hier gewaehlt sind und Daten da sind
int inhalte(const Feld& f, int* l) {
  int n = 0; l[n++] = Z_ORIGINAL;
  if (cfg.zf_lueften == f.ort && lueftenDa(lueften)) l[n++] = Z_LUEFTEN;
  if (cfg.zf_muell == f.ort && muellDa()) l[n++] = Z_MUELL;
  return n;
}

const LueftenRaum* feuchtester(const LueftenStatus& s) {
  const LueftenRaum* r = nullptr;
  for (int i = 0; i < s.nRaeume && i < 8; i++) if (!r || s.raum[i].feuchte > r->feuchte) r = &s.raum[i];
  return r;
}

// Namen des ersten Abholtags als Liste (eine Tonne je Zeile)
int tonnen(char names[4][40]) {
  int n = 0;
  const char* p = muell.t[0].namen;
  while (*p && n < 4) {
    const char* e = strchr(p, '\n'); size_t l = e ? (size_t)(e - p) : strlen(p);
    if (l >= 40) l = 39;
    memcpy(names[n], p, l); names[n][l] = 0; n++;
    if (!e) break;
    p = e + 1;
  }
  return n;
}

void tonneFaerben(lv_obj_t* t, uint32_t farbe) {
  if (!t || lv_obj_get_child_cnt(t) < 3) return;
  const uint32_t dunkel = (farbe >> 1) & 0x7F7F7F;   // Deckel und Griff etwas dunkler
  lv_obj_set_style_bg_color(lv_obj_get_child(t, 0), lv_color_hex(farbe), 0);
  lv_obj_set_style_bg_color(lv_obj_get_child(t, 1), lv_color_hex(dunkel), 0);
  lv_obj_set_style_bg_color(lv_obj_get_child(t, 2), lv_color_hex(dunkel), 0);
}

void stil(lv_obj_t* o, const lv_font_t* fnt, int x, int y, int w, int h, lv_text_align_t a, lv_label_long_mode_t m) {
  if (!o) return;
  lv_label_set_long_mode(o, m);
  lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
  lv_obj_set_style_text_font(o, fnt, 0); lv_obj_set_style_text_align(o, a, 0);
}

// Lage und Schrift der Zeilen je nach Inhalt (nur bei Wechsel)
void ordnen(Feld& f, int inhalt) {
  if (f.layout == inhalt) return;
  f.layout = inhalt;
  const lv_font_t* gross = &font_montserrat_20;
  const lv_font_t* klein = f.breit ? &font_montserrat_20 : &font_montserrat_latin_ext_16;
  const int iw = f.w - 12;
  stil(f.titel, gross, 6, f.breit ? 4 : 6, iw, 28, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
  if (inhalt == Z_MUELL) {
    const int y0 = f.breit ? 34 : 44, r0 = f.breit ? 66 : 84, st = f.breit ? 34 : 34;
    stil(f.z[0], gross, 6, y0, iw, 28, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
    for (int i = 1; i < 4; i++) stil(f.z[i], klein, 26, r0 + (i - 1) * st, f.w - 30, 26, LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_DOT);
    for (int i = 0; i < 3; i++) if (f.marker[i]) lv_obj_set_pos(f.marker[i], 8, r0 + i * st + 3);
  } else {
    const int y[4] = {f.breit ? 32 : 40, f.breit ? 84 : 110, f.breit ? 112 : 146, f.breit ? 138 : 174};
    // schmales Feld: Rat in kleinerer Schrift mit Platz fuer drei Zeilen ("Lüften nötig, aber draußen ungünstig")
    stil(f.z[0], f.breit ? gross : &font_montserrat_18, 6, y[0], iw, f.breit ? 50 : 66, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_WRAP);
    stil(f.z[1], gross, 6, y[1], iw, 28, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
    stil(f.z[2], &font_montserrat_latin_ext_16, 6, y[2], iw, 24, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
    stil(f.z[3], &font_montserrat_latin_ext_16, 6, y[3], iw, 24, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_DOT);
  }
}

// Zeile "Raum 83 %" in die Feldbreite bringen: erst normale Schrift, dann kleinere, zuletzt den Raumnamen kuerzen (Prozentwert bleibt sichtbar)
void raumZeile(lv_obj_t* o, const char* raum, float feuchte, int breite, const lv_font_t* gross, const lv_font_t* klein, char* b, size_t n) {
  char name[24]; strlcpy(name, raum, sizeof(name));
  snprintf(b, n, "%s %.0f %%", name, feuchte);
  const lv_font_t* fnt = gross;
  if (lv_txt_get_width(b, strlen(b), fnt, 0, LV_TEXT_FLAG_NONE) > breite) fnt = klein;
  bool gekuerzt = false;
  while (strlen(name) > 1 && lv_txt_get_width(b, strlen(b), fnt, 0, LV_TEXT_FLAG_NONE) > breite) {
    size_t l = strlen(name) - 1;
    while (l > 0 && (name[l] & 0xC0) == 0x80) l--;   // UTF-8-Fortsetzungsbytes (ä, ö, ü) nicht zerreissen
    name[l] = 0; gekuerzt = true;
    snprintf(b, n, "%s%s %.0f %%", name, gekuerzt ? "..." : "", feuchte);
  }
  lv_obj_set_style_text_font(o, fnt, 0);
}

void zeichnen(Feld& f) {
  if (!f.box || !f.titel) return;
  int l[3]; const int n = inhalte(f, l);
  if (f.idx >= n) f.idx = 0;
  const int inhalt = l[f.idx];
  const bool zusatz = inhalt != Z_ORIGINAL;

  // Original-Beschriftungen aus, wenn ein Zusatzinhalt laeuft (und umgekehrt)
  lv_obj_t* eigene[] = {f.titel, f.z[0], f.z[1], f.z[2], f.z[3], f.marker[0], f.marker[1], f.marker[2]};
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(f.box); i++) {
    lv_obj_t* c = lv_obj_get_child(f.box, i);
    bool istEigen = false;
    for (lv_obj_t* e : eigene) if (e == c) { istEigen = true; break; }
    if (!istEigen) zeigen(c, !zusatz);
  }
  if (!zusatz) { for (lv_obj_t* e : eigene) zeigen(e, false); return; }

  ordnen(f, inhalt);
  const uint32_t text = textFarbe();
  zeigen(f.titel, true);
  setzeFarbe(f.titel, text);
  for (int i = 0; i < 3; i++) zeigen(f.marker[i], false);

  char b[64];
  if (inhalt == Z_LUEFTEN) {
    const LueftenStatus st = lueftenKopie();
    const LueftenRaum* r = feuchtester(st);
    setzeText(f.titel, "Lüften");
    const char* kopf = st.kopf; if (strncmp(kopf, "Lüften: ", 9) == 0) kopf += 9;   // der Titel sagt es schon: mehr Platz fuer den Rat
    setzeText(f.z[0], kopf[0] ? kopf : "-");  setzeFarbe(f.z[0], st.farbe);
    if (r) raumZeile(f.z[1], r->raum, r->feuchte, f.w - 12, &font_montserrat_20, &font_montserrat_latin_ext_16, b, sizeof(b)); else b[0] = 0;
    setzeText(f.z[1], b); setzeFarbe(f.z[1], text);
    if (r) { snprintf(b, sizeof(b), "Taupunkt %.0f °C", fabsf(r->taupunkt) < 0.5f ? 0.0f : r->taupunkt); kommaDezimal(b); } else b[0] = 0;
    setzeText(f.z[2], b); setzeFarbe(f.z[2], text);
    if (st.aussenBekannt) snprintf(b, sizeof(b), "Außen %.0f %%", st.aussenFeuchte); else b[0] = 0;
    setzeText(f.z[3], b); setzeFarbe(f.z[3], text);
    for (int i = 0; i < 4; i++) zeigen(f.z[i], true);
  } else {   // Muellabfuhr
    char tag[24]; muellTagText(muell.t[0].tag, tag, sizeof(tag));
    char nm[4][40]; const int nt = tonnen(nm);
    setzeText(f.titel, "Müllabfuhr");
    setzeText(f.z[0], tag); setzeFarbe(f.z[0], text);
    zeigen(f.z[0], true);
    for (int i = 0; i < 3; i++) {
      if (i < nt) {
        setzeText(f.z[i + 1], nm[i]); setzeFarbe(f.z[i + 1], text); zeigen(f.z[i + 1], true);
        if (f.marker[i]) { tonneFaerben(f.marker[i], tonnenFarbe(nm[i])); zeigen(f.marker[i], true); }
      } else zeigen(f.z[i + 1], false);
    }
  }
}

void weiter(Feld& f) { int l[3]; const int n = inhalte(f, l); f.idx = (f.idx + 1) % n; f.t0 = millis(); zeichnen(f); }
void klick(lv_event_t* e) { Feld* f = (Feld*)lv_event_get_user_data(e); if (f) weiter(*f); }

void takt(lv_timer_t*) {
  if (!cfg.zf_auto) return;
  const unsigned long jetzt = millis();
  for (Feld& f : s_f) { int l[3]; if (inhalte(f, l) > 1 && jetzt - f.t0 >= WECHSEL_MS) { weiter(f); return; } }
}

void feldEinrichten(Feld& f) {
  if (!f.box) return;
  for (int i = 0; i < 3; i++) {   // kleine Tonne: Koerper, Deckel, Griff (Farbe nach Muellart, siehe tonnenFarbeSetzen)
    lv_obj_t* t = lv_obj_create(f.box);
    lv_obj_remove_style_all(t);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(t, 18, 19);
    auto kasten = [&](int px, int py, int w, int h, int r) {
      lv_obj_t* o = lv_obj_create(t);
      lv_obj_remove_style_all(o);
      lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_pos(o, px, py); lv_obj_set_size(o, w, h);
      lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(o, r, 0);
    };
    kasten(2, 5, 14, 13, 2);   // Koerper
    kasten(0, 1, 18, 5, 2);    // Deckel
    kasten(6, 0, 6, 2, 1);     // Griff
    lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
    f.marker[i] = t;
  }
  lv_obj_add_flag(f.box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(f.box, klick, LV_EVENT_CLICKED, &f);
  f.t0 = millis();
}

void einrichten() {
  if (s_bereit) return;
  s_bereit = true;
  Feld& p = s_f[0];
  p.box = objects.container_29; p.titel = objects.labelzusatzstarttitel;
  p.z[0] = objects.labelzusatzstartzeile1; p.z[1] = objects.labelzusatzstartzeile2; p.z[2] = objects.labelzusatzstartzeile3; p.z[3] = objects.labelzusatzstartzeile4;
  p.w = 150; p.breit = false;
  Feld& b = s_f[1];
  b.box = objects.container_6; b.titel = objects.labelzusatzbiotitel;
  b.z[0] = objects.labelzusatzbiozeile1; b.z[1] = objects.labelzusatzbiozeile2; b.z[2] = objects.labelzusatzbiozeile3; b.z[3] = objects.labelzusatzbiozeile4;
  b.w = 247; b.breit = true;
  feldEinrichten(p); feldEinrichten(b);
  lv_timer_create(takt, 500, nullptr);
}
}  // namespace

// Aufrufer haelt den LVGL-Lock (aktualisiereUI)
void aktualisiereZusatzfeld() {
  einrichten();
  for (Feld& f : s_f) zeichnen(f);
}

// Nur fuer Bildschirmfotos (screenshot.cpp): zeigt in beiden Feldern den Zusatzinhalt (Aufrufer haelt den LVGL-Lock). an = false stellt zurueck.
void zusatzfeldFuerFoto(bool an) {
  einrichten();
  for (Feld& f : s_f) {
    int l[3]; const int n = inhalte(f, l);
    f.idx = (an && n > 1) ? 1 : 0;
    f.t0 = millis();
    zeichnen(f);
  }
}
