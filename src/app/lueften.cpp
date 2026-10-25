// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "app_state.h"
#include "lueften.h"
#include "ntfy.h"
#include "../sensors/sensors.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

LueftenStatus lueften;
static SemaphoreHandle_t s_mx = nullptr;

float taupunktC(float t, float rh) {
  if (rh < 1.0f) rh = 1.0f;
  const float a = 17.62f, b = 243.12f;
  float g = logf(rh / 100.0f) + a * t / (b + t);
  return b * g / (a - g);
}
float absFeuchteGm3(float t, float rh) {
  float es = 6.112f * expf(17.62f * t / (243.12f + t));          // Saettigungsdampfdruck in hPa
  return 216.7f * (rh / 100.0f * es) / (273.15f + t);
}

// Messwerte, die keine Raumklima-Werte sind (WLAN-Signal in %, Akku in % ...), nicht als Luftfeuchte nehmen
// Relative Feuchte, die Aussenluft (absolute Feuchte g/m3) bei Raumtemperatur haette
static float nachLueften(float raumTemp, float aussenAbs) {
  const float satt = absFeuchteGm3(raumTemp, 100.0f);
  return satt > 0.1f ? 100.0f * aussenAbs / satt : 100.0f;
}

static bool technisch(const SensorCfg& c) {
  static const char* W[] = {"signal", "wifi", "wlan", "rssi", "batter", "akku", "uptime", "laufzeit", "lade"};
  char n[24]; size_t i = 0; for (; c.name[i] && i < sizeof(n) - 1; i++) n[i] = tolower((unsigned char)c.name[i]); n[i] = 0;
  for (const char* w : W) if (strstr(n, w)) return true;
  return false;
}
static bool istTemp(const SensorCfg& c)    { return !technisch(c) && (strstr(c.einheit, "C") || strstr(c.einheit, "°")); }
static bool istFeuchte(const SensorCfg& c) {
  if (technisch(c) || c.einheit[0] != '%') return false;
  return true;
}

// Haltezeit je Raum: kurze Spitzen (Duschen, Kochen) loesen keine Warnung aus
struct Merker { uint32_t hash; uint32_t seitMs; };
static Merker s_merker[8];
static uint32_t hashName(const char* s) { uint32_t h = 2166136261u; for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; } return h ? h : 1; }
static Merker* merkerFuer(const char* raum) {
  uint32_t h = hashName(raum); Merker* frei = nullptr;
  for (auto& m : s_merker) { if (m.hash == h) return &m; if (!m.hash && !frei) frei = &m; }
  if (!frei) frei = &s_merker[0];
  *frei = {h, 0};
  return frei;
}

// Episode fuer ntfy: nach dem ersten Hinweis hoechstens alle 6 Stunden erneut, Entwarnung nur nach gesendetem Hinweis
static uint32_t s_ntfyLetzter = 0; static bool s_ntfyGesendet = false;

// Beispielzustand fuer Bildschirmfotos (/demo und ?s=lueftenwarnung); der naechste echte Durchlauf ueberschreibt ihn
void lueftenDemoSetzen() {
  if (!s_mx) s_mx = xSemaphoreCreateMutex();
  LueftenStatus st;
  st.zustand = LZ_LUEFTEN; st.farbe = 0x00A838;
  strlcpy(st.kopf, "Jetzt lüften", sizeof(st.kopf));
  strlcpy(st.detail, "Küche 72 % feucht. Wasser in der Luft: draußen 6.1, drinnen 14.8 g/m3", sizeof(st.detail));
  st.nRaeume = 1; strlcpy(st.raum[0].raum, "Küche", sizeof(st.raum[0].raum)); st.raum[0].feuchte = 72; st.raum[0].temp = 21; st.raum[0].stufe = 2;
  st.warnAktiv = true; st.warnSchimmel = false;
  strlcpy(st.warnTitel, "Zu feucht !", sizeof(st.warnTitel));
  strlcpy(st.warnRaeume, "Küche: 72 % (Taupunkt 16 °C)\nBad: 69 % (Taupunkt 15 °C)", sizeof(st.warnRaeume));
  strlcpy(st.warnHinweis, "Jetzt lüften: draußen ist es trockener", sizeof(st.warnHinweis));
  xSemaphoreTake(s_mx, portMAX_DELAY); lueften = st; xSemaphoreGive(s_mx);
}

LueftenStatus lueftenKopie() {
  LueftenStatus k;
  if (!s_mx) return k;
  xSemaphoreTake(s_mx, portMAX_DELAY); k = lueften; xSemaphoreGive(s_mx);
  return k;
}

void lueftenBerechnen() {
  if (!s_mx) s_mx = xSemaphoreCreateMutex();
  LueftenStatus st;
  const uint32_t jetztMs = (uint32_t)(esp_timer_get_time() / 1000);
  const int64_t jetztS = esp_timer_get_time() / 1000;

  // Aussenluft
  if (wetter.geladen) {
    st.aussenTemp = wetter.temp; st.aussenFeuchte = wetter.humidity; st.aussenAbs = absFeuchteGm3(wetter.temp, wetter.humidity); st.aussenBekannt = true;
  }

  // Raeume: je Raumname ein Temperatur- und ein Feuchtesensor (nur MQTT-Sensoren, frisch)
  struct Tmp { char raum[20]; bool hatT = false, hatF = false; float t = 0, f = 0; char fname[24] = ""; };
  static Tmp tmp[8];
  int n = 0;
  for (int i = 0; i < sensorenAnzahl(); i++) {
    SensorCfg c; SensorWert w;
    if (!sensorenHole(i, &c, &w)) continue;
    if (c.quelle != SQ_MQTT || !c.raum[0] || !w.gueltig || jetztS - w.zuletzt_ms > 15 * 60 * 1000) continue;
    int k = -1;
    for (int q = 0; q < n; q++) if (strcmp(tmp[q].raum, c.raum) == 0) { k = q; break; }
    if (k < 0) { if (n >= 8) continue; k = n++; tmp[k] = Tmp(); strlcpy(tmp[k].raum, c.raum, sizeof(tmp[k].raum)); }
    if (!tmp[k].hatT && istTemp(c))    { tmp[k].t = w.wert; tmp[k].hatT = true; }
    if (!tmp[k].hatF && istFeuchte(c)) { tmp[k].f = w.wert; tmp[k].hatF = true; strlcpy(tmp[k].fname, c.name, sizeof(tmp[k].fname)); }
  }
  const int thr = cfg.lueft_feuchte;
  bool warnRaum = false, schimmel = false; int zeilen = 0;
  bool bedarf = false, hilft = false, hoch = false;
  float heissMax = -100; char heissRaum[20] = "";   // waermster Raum (Kuehlluften)
  for (int q = 0; q < n; q++) {
    if (!tmp[q].hatT || !tmp[q].hatF) continue;
    LueftenRaum& r = st.raum[st.nRaeume++];
    strlcpy(r.raum, tmp[q].raum, sizeof(r.raum)); strlcpy(r.feuchteName, tmp[q].fname, sizeof(r.feuchteName));
    r.temp = tmp[q].t; r.feuchte = tmp[q].f;
    r.taupunkt = taupunktC(r.temp, r.feuchte); r.absFeuchte = absFeuchteGm3(r.temp, r.feuchte);
    if (r.temp > heissMax) { heissMax = r.temp; strlcpy(heissRaum, r.raum, sizeof(heissRaum)); }
    r.stufe = r.feuchte >= thr + 10 ? 3 : r.feuchte >= thr ? 2 : r.feuchte >= thr - 5 ? 1 : 0;
    // Lueften hilft, wenn die Aussenluft nach dem Aufheizen auf Raumtemperatur die Raumfeuchte um mindestens 5 Prozentpunkte senkt
    // (Regen draussen ist kein Gegenargument: kalte Regenluft enthaelt meist weniger Wasser als warme Raumluft)
    if (r.stufe >= 1) { bedarf = true; if (st.aussenBekannt && nachLueften(r.temp, st.aussenAbs) <= r.feuchte - 5.0f) hilft = true; }
    if (r.stufe >= 2) hoch = true;
    // Haltezeit: ab Grenze 15 Minuten, ab Grenze + 10 schon 5 Minuten. Die Warnung bleibt, bis die Feuchte 3 Punkte unter die Grenze faellt
    // (sonst wuerde sie an der Grenze flackern)
    Merker* m = merkerFuer(r.raum);
    const bool drueber = r.stufe >= 2, nahe = m->seitMs && r.feuchte >= thr - 3;
    if (drueber || nahe) {
      if (!m->seitMs) m->seitMs = jetztMs ? jetztMs : 1;
      uint32_t halte = (r.stufe == 3) ? 5UL * 60000UL : 15UL * 60000UL;
      if (jetztMs - m->seitMs >= halte) {
        warnRaum = true; if (r.stufe == 3) schimmel = true; hoch = true;
        if (zeilen < 3) {
          char z[64]; snprintf(z, sizeof(z), "%s: %.0f %% (Taupunkt %.0f °C)", r.raum, r.feuchte, r.taupunkt);
          if (zeilen) strlcat(st.warnRaeume, "\n", sizeof(st.warnRaeume));
          strlcat(st.warnRaeume, z, sizeof(st.warnRaeume)); zeilen++;
        }
      }
    } else m->seitMs = 0;
  }

  // Aussenlage: Luftqualitaet und Pollen sperren das Lueften, Hitze nur als Hinweis
  char grund[80] = ""; bool gesperrt = false;
  if (pollen.aqi > 60) { snprintf(grund, sizeof(grund), "Luft draußen schlecht (AQI %d)", pollen.aqi); gesperrt = true; }
  else if (pollenWarnAktiv) { snprintf(grund, sizeof(grund), "Pollenbelastung draußen hoch"); gesperrt = true; }

  // Zustand
  char hoechst[48] = ""; float hoechstAbs = 0, hoechstTemp = 20;     // feuchtester Raum: "Kueche 61 %" und sein Wassergehalt der Luft (g/m3)
  { float fm = -1; for (int i = 0; i < st.nRaeume; i++) if (st.raum[i].feuchte > fm) { fm = st.raum[i].feuchte; hoechstAbs = st.raum[i].absFeuchte; hoechstTemp = st.raum[i].temp; snprintf(hoechst, sizeof(hoechst), "%s %.0f %%", st.raum[i].raum, fm); } }
  if (st.nRaeume == 0) { st.zustand = LZ_KEINE_DATEN; st.farbe = 0x888888; strlcpy(st.kopf, "Lüften: keine Raumdaten", sizeof(st.kopf)); strlcpy(st.detail, "Pro Raum Temperatur- und Feuchtesensor mit gleichem Raumnamen", sizeof(st.detail)); }
  else if (gesperrt && bedarf) { st.zustand = LZ_NICHT_MOEGLICH; st.farbe = 0xE01E00; strlcpy(st.kopf, "Lüften nötig, aber draußen ungünstig", sizeof(st.kopf)); snprintf(st.detail, sizeof(st.detail), "%s, feucht: %s", grund, hoechst); }
  else if (gesperrt) { st.zustand = LZ_NICHT_NOETIG; st.farbe = 0xE8A000; strlcpy(st.kopf, "Fenster besser geschlossen", sizeof(st.kopf)); snprintf(st.detail, sizeof(st.detail), "%s", grund); }
  else if (bedarf && hilft) { st.zustand = LZ_LUEFTEN; st.farbe = 0x00A838;
    // Regen: Fenster nur kippen, damit es nicht hereinregnet (am Ergebnis aendert das nichts, die Luft ist trotzdem trockener)
    const int w = wetter.wmo_code;
    const bool regen = wetter.geladen && ((w >= 51 && w <= 67) || (w >= 80 && w <= 82) || w >= 95);
    strlcpy(st.kopf, regen ? "Lüften: Fenster kippen" : "Jetzt lüften", sizeof(st.kopf));
    snprintf(st.detail, sizeof(st.detail), "%s feucht, danach ca. %.0f %%. Wasser: draußen %.1f, innen %.1f g/m3", hoechst,
             nachLueften(hoechstTemp, st.aussenAbs), st.aussenAbs, hoechstAbs); }
  else if (bedarf) { st.zustand = LZ_WENIG; st.farbe = 0xE8A000; strlcpy(st.kopf, "Lüften bringt jetzt wenig", sizeof(st.kopf));
    snprintf(st.detail, sizeof(st.detail), "%s feucht. Wasser in der Luft: draußen %.1f, drinnen %.1f g/m3", hoechst, st.aussenAbs, hoechstAbs); }
  else if (cfg.lueft_kuehlen && st.aussenBekannt && heissMax >= (float)cfg.lueft_hitze) {
    // Sommer: warmer Raum. Draussen mindestens 2 Grad kuehler -> lueften, sonst Fenster und Rolllaeden zu
    if (st.aussenTemp <= heissMax - 2.0f) { st.zustand = LZ_KUEHLEN; st.farbe = 0x00A838; strlcpy(st.kopf, "Fenster auf: draußen kühler", sizeof(st.kopf)); snprintf(st.detail, sizeof(st.detail), "%s %.1f °C, draußen %.1f °C", heissRaum, heissMax, st.aussenTemp); }
    else { st.zustand = LZ_HITZE_ZU; st.farbe = 0xE8A000; strlcpy(st.kopf, "Fenster und Rollläden zu", sizeof(st.kopf)); snprintf(st.detail, sizeof(st.detail), "draußen nicht kühler: %s %.1f °C, draußen %.1f °C", heissRaum, heissMax, st.aussenTemp); }
  }
  else { st.zustand = LZ_OK; st.farbe = 0x00A838; strlcpy(st.kopf, "Raumluft in Ordnung", sizeof(st.kopf)); snprintf(st.detail, sizeof(st.detail), "höchste Feuchte: %s", hoechst); }

  // Warnung
  st.warnAktiv = warnRaum && hoch;
  st.warnSchimmel = schimmel;
  if (st.warnAktiv) {
    strlcpy(st.warnTitel, schimmel ? "Schimmelgefahr !" : "Zu feucht !", sizeof(st.warnTitel));
    if (st.zustand == LZ_LUEFTEN)             strlcpy(st.warnHinweis, "Jetzt lüften: draußen ist es trockener", sizeof(st.warnHinweis));
    else if (st.zustand == LZ_WENIG)          strlcpy(st.warnHinweis, "Lüften bringt jetzt wenig. Besser heizen oder entfeuchten", sizeof(st.warnHinweis));
    else if (st.zustand == LZ_NICHT_MOEGLICH) snprintf(st.warnHinweis, sizeof(st.warnHinweis), "%s. Später stoßlüften", grund);
    else                                      strlcpy(st.warnHinweis, "Bitte lüften", sizeof(st.warnHinweis));
  }

  xSemaphoreTake(s_mx, portMAX_DELAY);
  bool vorher = lueften.warnAktiv;
  lueften = st;
  xSemaphoreGive(s_mx);

  // ntfy-Meldung zu Beginn der Warnung (danach hoechstens alle 6 Stunden) und Entwarnung
  if (ntfyQuelleAktiv(NTFY_Q_LUEFTEN) && cfg.lueft_warnung) {
    if (st.warnAktiv && (!s_ntfyGesendet || jetztMs - s_ntfyLetzter >= 6UL * 3600UL * 1000UL)) {
      char titel[72], text[220];
      snprintf(titel, sizeof(titel), "%s", st.warnTitel);
      snprintf(text, sizeof(text), "%s\n%s", st.warnRaeume, st.warnHinweis);
      ntfySenden(titel, text, schimmel ? 4 : 3, schimmel ? "warning,droplet" : "droplet");
      s_ntfyGesendet = true; s_ntfyLetzter = jetztMs ? jetztMs : 1;
    } else if (!st.warnAktiv && vorher && s_ntfyGesendet && cfg.ntfy_entwarnung) {
      ntfySenden("Raumluft wieder in Ordnung", st.detail, 2, "white_check_mark");
      s_ntfyGesendet = false;
    }
  }
  if (!st.warnAktiv && !(ntfyQuelleAktiv(NTFY_Q_LUEFTEN) && cfg.lueft_warnung)) s_ntfyGesendet = false;
}
