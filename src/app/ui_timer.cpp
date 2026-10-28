// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Timer und Stoppuhr (Taste "buttontimer" auf dem Menue-Screen), nach dem Vorbild des JPDeck.
// Der Zustand von Timer und Stoppuhr lebt ausserhalb des Screens: beide laufen weiter, wenn der Screen geschlossen wird.
// Laeuft der Timer ab, blinkt das ganze Display (auf jedem Screen) bis zum Antippen; Boards mit Lautsprecher (P4) spielen dazu den
// in der Web-Oberflaeche gewaehlten Alarmton in der eingestellten Lautstaerke.
#include "app_state.h"
#include "navigation.h"
#include "../board/board.h"
#include "../ui/screens.h"
#include "../ui/colors.h"
#include "../ui/fonts/fonts.h"

// ── Zustand (unabhaengig vom Screen) ───────────────────────────────────────────
enum TmState { TM_IDLE, TM_RUN, TM_PAUSE, TM_DONE };
static TmState  tmState    = TM_IDLE;
static uint32_t tmSetMs    = 0;        // eingestellte Dauer (Ziel fuer "Zuruecksetzen")
static uint32_t tmRemainMs = 0;        // Rest, solange nicht TM_RUN
static uint32_t tmEndMs    = 0;        // millis()-Zeitpunkt des Ablaufs, solange TM_RUN
#define TM_MAX_MS 359999000UL          // 99:59:59

static bool     swRun   = false;
static uint32_t swStart = 0, swAccum = 0;
#define SW_LAPS 5
static uint32_t swLap[SW_LAPS];        // neueste Runde zuerst (Gesamtzeit)
static int      swLapN = 0;
static int      swLapTotal = 0;

static uint32_t tmRemaining() {
  if (tmState != TM_RUN) return tmRemainMs;
  int32_t d = (int32_t)(tmEndMs - millis());
  return d > 0 ? (uint32_t)d : 0;
}
static uint32_t swElapsed() { return swAccum + (swRun ? millis() - swStart : 0); }

// ── Alarm: ganzes Display blinkt, Ton laeuft ──────────────────────────────────
static lv_obj_t*   alObj   = nullptr;
static lv_obj_t*   alLabel = nullptr;
static lv_timer_t* alTimer = nullptr;
static bool        alPhase = false;
static uint32_t    alT0    = 0;
#define ALARM_MAX_MS 60000UL           // nach einer Minute von selbst aufhoeren

static void alarmStop() {
  boardAudioStopp();
  if (alTimer) { lv_timer_del(alTimer); alTimer = nullptr; }
  if (alObj)   { lv_obj_del(alObj); alObj = alLabel = nullptr; }
  boardHelligkeit(cfg.brightness);
  tmState = TM_IDLE;
  tmRemainMs = tmSetMs;
}

static void alarmTick(lv_timer_t*) {
  alPhase = !alPhase;
  lv_obj_set_style_bg_color(alObj, alPhase ? lv_color_white() : lv_color_hex(0xcc0000), 0);
  lv_obj_set_style_text_color(alLabel, alPhase ? lv_color_hex(0xcc0000) : lv_color_white(), 0);
  boardHelligkeit(alPhase ? 100 : 20);                           // auch die Hintergrundbeleuchtung pulsiert
  if (millis() - alT0 > ALARM_MAX_MS) alarmStop();
}

static void cbAlarmTap(lv_event_t*) { alarmStop(); }

static void alarmStart() {
  if (alObj) return;
  alObj = lv_obj_create(lv_layer_top());                         // liegt ueber jedem Screen und faengt alle Beruehrungen ab
  lv_obj_set_pos(alObj, 0, 0);
  lv_obj_set_size(alObj, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
  lv_obj_set_style_radius(alObj, 0, 0);
  lv_obj_set_style_border_width(alObj, 0, 0);
  lv_obj_set_style_bg_color(alObj, lv_color_hex(0xcc0000), 0);
  lv_obj_set_style_bg_opa(alObj, LV_OPA_COVER, 0);
  lv_obj_clear_flag(alObj, LV_OBJ_FLAG_SCROLLABLE);
  alLabel = lv_label_create(alObj);
  lv_label_set_text(alLabel, "Zeit abgelaufen!\nZum Beenden tippen");
  lv_obj_set_style_text_font(alLabel, &font_montserrat_48, 0);
  lv_obj_set_style_text_align(alLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(alLabel, lv_color_white(), 0);
  lv_obj_center(alLabel);
  lv_obj_add_event_cb(alObj, cbAlarmTap, LV_EVENT_CLICKED, nullptr);
  alPhase = false;
  alT0 = millis();
  alTimer = lv_timer_create(alarmTick, 450, nullptr);
  boardHelligkeit(100);
  boardAudioTon(cfg.timer_ton, cfg.timer_laut);
}

// ── Screen-Objekte (nur solange der Screen offen ist) ─────────────────────────
static lv_obj_t *scr, *lblTime, *lblSub, *lblLaps;
static lv_obj_t *btnTimerMode, *btnSwMode, *btnStart, *lblStart, *btnReset, *btnLap;
static lv_obj_t *adjust[8];
static int      mode = 0;              // 0 = Timer, 1 = Stoppuhr

static lv_style_t stBtn, stBtnPressed;
static bool       stylesReady = false;

static void fmtTimer(char* b, size_t n, uint32_t ms) {
  uint32_t s = (ms + 999) / 1000, h = s / 3600, m = (s / 60) % 60;
  if (h) snprintf(b, n, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)(s % 60));
  else   snprintf(b, n, "%02u:%02u", (unsigned)m, (unsigned)(s % 60));
}
static void fmtSw(char* b, size_t n, uint32_t ms) {
  uint32_t t = ms / 100, tenth = t % 10, s = t / 10, h = s / 3600, m = (s / 60) % 60;
  if (h) snprintf(b, n, "%u:%02u:%02u.%u", (unsigned)h, (unsigned)m, (unsigned)(s % 60), (unsigned)tenth);
  else   snprintf(b, n, "%02u:%02u.%u", (unsigned)m, (unsigned)(s % 60), (unsigned)tenth);
}

static void refresh() {
  if (!scr) return;
  char b[24], c[24], txt[200];
  if (mode == 0) {
    fmtTimer(b, sizeof(b), tmRemaining());
    lv_label_set_text(lblTime, b);
    if (tmSetMs) { fmtTimer(c, sizeof(c), tmSetMs); snprintf(txt, sizeof(txt), "Eingestellt: %s", c); }
    else snprintf(txt, sizeof(txt), "Zeit mit den Tasten einstellen");
    lv_label_set_text(lblSub, txt);
    lv_label_set_text(lblStart, tmState == TM_RUN ? "Pause" : "Start");
  } else {
    fmtSw(b, sizeof(b), swElapsed());
    lv_label_set_text(lblTime, b);
    lv_label_set_text(lblSub, swLapTotal ? "Rundenzeiten" : "Start druecken, mit Runde Zwischenzeiten nehmen");
    txt[0] = 0;
    for (int i = 0; i < swLapN; i++) {                           // Runde n: Gesamtzeit (+Differenz zur vorigen)
      uint32_t diff = swLap[i] - (i + 1 < swLapN ? swLap[i + 1] : 0);
      char a[24], d[24]; fmtSw(a, sizeof(a), swLap[i]); fmtSw(d, sizeof(d), diff);
      snprintf(txt + strlen(txt), sizeof(txt) - strlen(txt), "Runde %d   %s   (+%s)\n", swLapTotal - i, a, d);
    }
    lv_label_set_text(lblLaps, txt);
    lv_label_set_text(lblStart, swRun ? "Stopp" : "Start");
  }
}

// ── Bedienung ─────────────────────────────────────────────────────────────────
static void tmAdd(int32_t deltaMs) {
  int64_t rem = (int64_t)tmRemaining() + deltaMs;
  if (rem < 0) rem = 0;
  if (rem > (int64_t)TM_MAX_MS) rem = TM_MAX_MS;
  if (tmState == TM_RUN) { tmEndMs = millis() + (uint32_t)rem; if (rem == 0) { tmRemainMs = 0; tmState = TM_IDLE; } }
  else {
    tmRemainMs = (uint32_t)rem;
    tmSetMs = (uint32_t)rem;                                     // im Stillstand ist die Einstellung zugleich das Ziel
    tmState = TM_IDLE;
  }
}

static void tmStartPause() {
  if (tmState == TM_RUN) { tmRemainMs = tmRemaining(); tmState = TM_PAUSE; return; }
  if (tmRemainMs == 0) return;
  tmEndMs = millis() + tmRemainMs;
  tmState = TM_RUN;
}

static void tmReset(bool clear) {
  tmState = TM_IDLE;
  if (clear) tmSetMs = 0;
  tmRemainMs = tmSetMs;
}

static void swStartStop() {
  if (swRun) { swAccum += millis() - swStart; swRun = false; }
  else { swStart = millis(); swRun = true; }
}
static void swReset() { swRun = false; swAccum = 0; swLapN = 0; swLapTotal = 0; }
static void swDoLap() {
  if (!swRun) return;
  for (int i = SW_LAPS - 1; i > 0; i--) swLap[i] = swLap[i - 1];
  swLap[0] = swElapsed();
  if (swLapN < SW_LAPS) swLapN++;
  swLapTotal++;
}

static void closeScreen() {
  nav_goto(SCREEN_ID_SCREENMENU);
  if (scr) lv_obj_del_async(scr);                                // erst nach dem Callback loeschen (die Taste ist Kind des Screens)
  scr = lblTime = lblSub = lblLaps = btnTimerMode = btnSwMode = btnStart = lblStart = btnReset = btnLap = nullptr;
}

static void layoutMode() {
  const bool tm = (mode == 0);
  for (int i = 0; i < 8; i++) { if (tm) lv_obj_clear_flag(adjust[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(adjust[i], LV_OBJ_FLAG_HIDDEN); }
  if (tm) lv_obj_add_flag(lblLaps, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(lblLaps, LV_OBJ_FLAG_HIDDEN);
  if (tm) lv_obj_add_flag(btnLap, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(btnLap, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(btnStart, 20, 440);
  lv_obj_set_size(btnStart, tm ? 640 : 420, 100);
  lv_obj_set_pos(btnReset, tm ? 676 : 740, 440);
  lv_obj_set_size(btnReset, tm ? 328 : 264, 100);
  lv_obj_set_style_bg_color(btnTimerMode, lv_color_hex(tm ? 0x3498db : 0x7f8c8d), 0);
  lv_obj_set_style_bg_color(btnSwMode,    lv_color_hex(tm ? 0x7f8c8d : 0x3498db), 0);
  refresh();
}

enum { B_MODE_TM, B_MODE_SW, B_CLOSE, B_START, B_RESET, B_LAP, B_ADJ0 };
struct Adj { const char* text; int32_t ms; uint32_t color; };
static const Adj ADJ[8] = {
  { "+1 Min",  60000, 0x3498db }, { "+5 Min",  300000, 0x3498db }, { "+10 Min", 600000, 0x3498db }, { "+30 Sek", 30000, 0x3498db },
  { "-1 Min", -60000, 0xe67e22 }, { "-30 Sek", -30000, 0xe67e22 }, { "+1 Std", 3600000, 0x9b59b6 }, { "L\xC3\xB6schen", 0, 0xe74c3c },
};

static void cbBtn(lv_event_t* e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  switch (id) {
    case B_MODE_TM: mode = 0; layoutMode(); return;
    case B_MODE_SW: mode = 1; layoutMode(); return;
    case B_CLOSE:   closeScreen(); return;
    case B_START:   if (mode == 0) tmStartPause(); else swStartStop(); break;
    case B_RESET:   if (mode == 0) tmReset(false); else swReset(); break;
    case B_LAP:     swDoLap(); break;
    default: {
      const Adj& a = ADJ[id - B_ADJ0];
      if (a.ms == 0) tmReset(true); else tmAdd(a.ms);
    }
  }
  refresh();
}

static lv_obj_t* makeBtn(lv_obj_t* parent, int x, int y, int w, int h, const char* text, uint32_t color, int id, const lv_font_t* font) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_add_style(b, &stBtn, 0);
  lv_obj_add_style(b, &stBtnPressed, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cbBtn, LV_EVENT_CLICKED, (void*)(intptr_t)id);
  return b;
}

static void buildScreen() {
  if (!stylesReady) {
    lv_style_init(&stBtn);
    lv_style_set_radius(&stBtn, 14);
    lv_style_set_border_width(&stBtn, 0);
    lv_style_set_shadow_width(&stBtn, 0);
    lv_style_set_bg_opa(&stBtn, LV_OPA_COVER);
    lv_style_set_text_color(&stBtn, lv_color_white());
    lv_style_init(&stBtnPressed);
    lv_style_set_bg_opa(&stBtnPressed, LV_OPA_60);
    stylesReady = true;
  }
  scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(theme_colors[active_theme_index][THEME_COLOR_SCREEN] & 0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  btnTimerMode = makeBtn(scr, 20, 14, 220, 56, "Timer", 0x3498db, B_MODE_TM, &font_montserrat_32);
  btnSwMode    = makeBtn(scr, 252, 14, 220, 56, "Stoppuhr", 0x7f8c8d, B_MODE_SW, &font_montserrat_32);
  makeBtn(scr, 764, 14, 240, 56, "Zur\xC3\xBC" "ck", 0xcc0000, B_CLOSE, &font_montserrat_32);

  lv_obj_t* panel = lv_obj_create(scr);
  lv_obj_set_pos(panel, 20, 84);
  lv_obj_set_size(panel, 984, 170);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x1b2631), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(panel, 14, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lblTime = lv_label_create(panel);
  lv_obj_set_width(lblTime, 944);
  lv_obj_set_style_text_align(lblTime, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(lblTime, &font_montserrat_62, 0);
  lv_obj_set_style_text_color(lblTime, lv_color_white(), 0);
  lv_obj_align(lblTime, LV_ALIGN_TOP_MID, 0, 28);
  lblSub = lv_label_create(panel);
  lv_obj_set_width(lblSub, 944);
  lv_obj_set_style_text_align(lblSub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(lblSub, &font_montserrat_22, 0);
  lv_obj_set_style_text_color(lblSub, lv_color_hex(0x85c1e9), 0);
  lv_obj_align(lblSub, LV_ALIGN_BOTTOM_MID, 0, -14);

  // Zeit-Tasten (Timer): 4 x 2
  for (int i = 0; i < 8; i++) {
    adjust[i] = makeBtn(scr, 20 + (i % 4) * 249, 270 + (i / 4) * 80, 237, 68, ADJ[i].text, ADJ[i].color, B_ADJ0 + i, &font_montserrat_32);
  }
  // Rundenliste (Stoppuhr)
  lblLaps = lv_label_create(scr);
  lv_obj_set_pos(lblLaps, 30, 276);
  lv_obj_set_width(lblLaps, 964);
  lv_obj_set_style_text_font(lblLaps, &font_montserrat_22, 0);
  lv_obj_set_style_text_color(lblLaps, lv_color_hex(theme_colors[active_theme_index][THEME_COLOR_TEXT] & 0xFFFFFF), 0);

  btnStart = makeBtn(scr, 20, 440, 640, 100, "Start", 0x2ecc71, B_START, &font_montserrat_48);
  lblStart = lv_obj_get_child(btnStart, 0);
  btnReset = makeBtn(scr, 676, 440, 328, 100, "Zur\xC3\xBC" "cksetzen", 0x34495e, B_RESET, &font_montserrat_32);
  btnLap   = makeBtn(scr, 452, 440, 276, 100, "Runde", 0x16a085, B_LAP, &font_montserrat_32);
  layoutMode();
}

static void cbOpen(lv_event_t*) {
  if (scr) { lv_obj_del(scr); scr = nullptr; }                   // Rest eines nie geschlossenen Screens
  buildScreen();
  lv_scr_load(scr);
}

// ── Takt: laeuft immer, auch bei geschlossenem Screen ──────────────────────────
static void tickCb(lv_timer_t*) {
  if (tmState == TM_RUN && tmRemaining() == 0) {
    tmRemainMs = 0;
    tmState = TM_DONE;
    alarmStart();
  }
  refresh();
}

// Fuer Bildschirmfotos (/screenshot.png?s=timer): Screen aufbauen, ohne ihn zu laden; zeigt 5 Minuten, wenn noch nichts eingestellt ist
lv_obj_t* uiTimerFuerFoto() {
  if (scr) { lv_obj_del(scr); scr = nullptr; }
  if (tmState == TM_IDLE && tmSetMs == 0) { tmSetMs = tmRemainMs = 300000; }
  mode = 0;
  buildScreen();
  return scr;
}

void uiTimerInit() {
  LvLock lock;
  if (objects.buttontimer) { lv_obj_add_flag(objects.buttontimer, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(objects.buttontimer, cbOpen, LV_EVENT_CLICKED, nullptr); }
  lv_timer_create(tickCb, 100, nullptr);
}
