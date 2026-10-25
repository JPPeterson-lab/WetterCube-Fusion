// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Firmware-Update: automatische Prüfung, Update-Symbol auf dem Hauptscreen, Popup "Jetzt installieren / Später",
// Update-Fenster mit Fortschrittsbalken. Die Installation läuft im Netzwerk-Task (nie im LVGL-Callback).
#include "app_state.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include <ArduinoJson.h>
#include "navigation.h"
#include "../sensors/sensors.h"
#include "esp_heap_caps.h"
#include "../ui/screens.h"
#include "../ui/fonts/fonts.h"

// Schreibt eine Firmware in den freien Update-Slot (esp_ota_*): begin, write (nacheinander), end prüft das Abbild und setzt den Startslot
namespace {
struct OtaSchreiber {
  const esp_partition_t* ziel = nullptr;
  esp_ota_handle_t h = 0;
  bool offen = false;
  size_t geschrieben = 0;
  String fehler;
  bool begin() {
    ziel = esp_ota_get_next_update_partition(nullptr);
    if (!ziel) { fehler = "Kein Platz für das Update vorhanden"; return false; }
    esp_err_t e = esp_ota_begin(ziel, OTA_WITH_SEQUENTIAL_WRITES, &h);
    if (e != ESP_OK) { fehler = String("Der Update-Platz lässt sich nicht beschreiben (") + esp_err_to_name(e) + ")"; return false; }
    offen = true; geschrieben = 0; fehler = "";
    return true;
  }
  bool write(const uint8_t* d, size_t n) {
    esp_err_t e = esp_ota_write(h, d, n);
    if (e != ESP_OK) { fehler = String("Schreibfehler im Flash (") + esp_err_to_name(e) + ")"; return false; }
    geschrieben += n;
    return true;
  }
  bool end() {
    offen = false;
    esp_err_t e = esp_ota_end(h);
    if (e != ESP_OK) { fehler = e == ESP_ERR_OTA_VALIDATE_FAILED ? String("Die Firmware-Datei ist beschädigt") : String("Abschluss fehlgeschlagen (") + esp_err_to_name(e) + ")"; return false; }
    e = esp_ota_set_boot_partition(ziel);
    if (e != ESP_OK) { fehler = String("Startpartition lässt sich nicht setzen (") + esp_err_to_name(e) + ")"; return false; }
    return true;
  }
  void abbrechen() { if (offen) { esp_ota_abort(h); offen = false; } }
} g_ota;
}

bool          updateVerfuegbar     = false;
String        updateNeueVersion;
volatile bool otaStartAngefordert  = false;
unsigned long letzterUpdateCheck   = 0;
unsigned long updateCheckIntervall = UPDATE_CHECK_ERST;
int           updateLetztesErgebnis = 0;

// Pruefabstand aus der Einstellung (1, 6 oder 24 Stunden)
unsigned long updateIntervallMs() { return (unsigned long)cfg.update_intervall_h * 3600000UL; }

// true, wenn Version a neuer ist als b. Vorabversionen ("0.9.9-alpha2", "1.0.0-beta1", "1.0.0-rc1") sind alter als das Release
// ohne Zusatz und werden untereinander nach Name (alpha < beta < rc) und dann nach Nummer verglichen
bool versionNeuer(const String& a, const String& b) {
  int pa[3] = {0, 0, 0}, pb[3] = {0, 0, 0};
  sscanf(a.c_str(), "%d.%d.%d", &pa[0], &pa[1], &pa[2]);
  sscanf(b.c_str(), "%d.%d.%d", &pb[0], &pb[1], &pb[2]);
  for (int i = 0; i < 3; i++) if (pa[i] != pb[i]) return pa[i] > pb[i];
  const char* sa = strchr(a.c_str(), '-');
  const char* sb = strchr(b.c_str(), '-');
  if (!sa || !sb) return sb && !sa;                   // Release ist neuer als jede Vorabversion
  sa++; sb++;
  size_t za = 0, zb = 0;                              // Name (Buchstaben) und Nummer trennen
  while (sa[za] && !isdigit((unsigned char)sa[za])) za++;
  while (sb[zb] && !isdigit((unsigned char)sb[zb])) zb++;
  int c = strncmp(sa, sb, za < zb ? za : zb);
  if (c == 0 && za != zb) c = za > zb ? 1 : -1;
  if (c != 0) return c > 0;
  return atoi(sa + za) > atoi(sb + zb);
}

// Fragt die neueste veröffentlichte Version ab
bool holeNeuesteVersion(String& latest) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure sc;
  HTTPClient http;
  http.begin(sc, OTA_VERSION_URL);
  http.setTimeout(10000);
  bool ok = false;
  if (http.GET() == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
      const char* v = doc["version"] | "";
      if (v[0]) { latest = String(v); ok = true; }
    }
  }
  http.end();
  return ok;
}

void pruefeUpdate() {
  letzterUpdateCheck = millis();
  if (cfg.update_modus == 0) {                    // Updates ganz aus: keine Anfrage an GitHub, kein Hinweis
    updateCheckIntervall = updateIntervallMs();
    updateVerfuegbar = false;
    aktualisiereUpdateBadge();
    return;
  }
  String latest;
  if (!holeNeuesteVersion(latest)) {
    updateLetztesErgebnis = 2;
    updateCheckIntervall = UPDATE_CHECK_RETRY;
    logDiag("[Update] Pruefung fehlgeschlagen, neuer Versuch in 1 Std.");
    return;
  }
  updateLetztesErgebnis = 1;
  updateCheckIntervall = updateIntervallMs();
  bool neu = versionNeuer(latest, FIRMWARE_VERSION);
  logDiag("[Update] Pruefung: installiert %s, verfuegbar %s", FIRMWARE_VERSION, latest.c_str());
  if (neu && (!updateVerfuegbar || updateNeueVersion != latest))
    logDiag("[Update] Neue Version verfuegbar: %s (installiert: %s)", latest.c_str(), FIRMWARE_VERSION);
  updateVerfuegbar  = neu;
  updateNeueVersion = latest;
  aktualisiereUpdateBadge();
}

// ── Symbol und Popup auf dem Hauptscreen ─────────────────────────────────────
static lv_obj_t* updateBadge = nullptr;
static lv_obj_t* updatePopup = nullptr;

static void cbPopupSchliessen(lv_event_t*) {
  if (updatePopup) { lv_obj_del(updatePopup); updatePopup = nullptr; }
}
static void cbPopupInstallieren(lv_event_t*) {
  if (updatePopup) { lv_obj_del(updatePopup); updatePopup = nullptr; }
  otaStartAngefordert = true;     // der Netzwerk-Task startet die Installation
}

static void zeigeUpdatePopup() {
  if (updatePopup || !objects.screen_1) return;

  // Abgedunkeltes Overlay über den ganzen Screen: fängt Berührungen ab, damit darunter nichts ausgelöst wird
  updatePopup = lv_obj_create(objects.screen_1);
  lv_obj_set_size(updatePopup, 1024, 600);
  lv_obj_set_pos(updatePopup, 0, 0);
  lv_obj_set_style_bg_color(updatePopup, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(updatePopup, LV_OPA_60, 0);
  lv_obj_set_style_border_width(updatePopup, 0, 0);
  lv_obj_set_style_radius(updatePopup, 0, 0);
  lv_obj_clear_flag(updatePopup, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* karte = lv_obj_create(updatePopup);
  lv_obj_set_size(karte, 700, 380);
  lv_obj_align(karte, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(karte, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(karte, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(karte, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_border_width(karte, 2, 0);
  lv_obj_set_style_radius(karte, 12, 0);
  lv_obj_clear_flag(karte, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* titel = lv_label_create(karte);
  lv_label_set_text(titel, "Update verfügbar");
  lv_obj_set_style_text_font(titel, &font_montserrat_32, 0);
  lv_obj_set_style_text_color(titel, lv_color_hex(0xf5a623), 0);
  lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 10);

  String text = "Neue Version: " + updateNeueVersion + "\nInstalliert: " + String(FIRMWARE_VERSION) +
                "\n\nDas Update dauert ca. 1 Minute, danach startet das Gerät selbst neu.";
  lv_obj_t* lbl = lv_label_create(karte);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, 640);
  lv_label_set_text(lbl, text.c_str());
  lv_obj_set_style_text_font(lbl, &font_montserrat_22, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xeeeeee), 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 62);

  // Warnhinweis: kein Strom trennen (beim Arduino-Build zusätzlich: damit niemand das Flackern für einen Defekt hält)
  lv_obj_t* warn = lv_label_create(karte);
  lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(warn, 640);
#if UPDATE_DISPLAY_FLACKERT
  lv_label_set_text(warn, "Achtung: Das Display flackert dabei wild!\nDas ist normal. Gerät NICHT vom Strom trennen!");
#else
  lv_label_set_text(warn, "Das Update dauert ca. 1 Minute.\nGerät dabei NICHT vom Strom trennen!");
#endif
  lv_obj_set_style_text_font(warn, &font_montserrat_22, 0);
  lv_obj_set_style_text_color(warn, lv_color_hex(0xffa500), 0);
  lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(warn, LV_ALIGN_TOP_MID, 0, 196);

  lv_obj_t* btnJa = lv_btn_create(karte);
  lv_obj_set_size(btnJa, 300, 62);
  lv_obj_align(btnJa, LV_ALIGN_BOTTOM_LEFT, 14, -12);
  lv_obj_set_style_bg_color(btnJa, lv_color_hex(0x2e8b57), 0);
  lv_obj_t* lblJa = lv_label_create(btnJa);
  lv_label_set_text(lblJa, "Jetzt installieren");
  lv_obj_set_style_text_font(lblJa, &font_montserrat_22, 0);
  lv_obj_set_style_text_color(lblJa, lv_color_hex(0xffffff), 0);
  lv_obj_center(lblJa);
  lv_obj_add_event_cb(btnJa, cbPopupInstallieren, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* btnNein = lv_btn_create(karte);
  lv_obj_set_size(btnNein, 240, 62);
  lv_obj_align(btnNein, LV_ALIGN_BOTTOM_RIGHT, -14, -12);
  lv_obj_set_style_bg_color(btnNein, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_color(btnNein, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_border_width(btnNein, 1, 0);
  lv_obj_t* lblNein = lv_label_create(btnNein);
  lv_label_set_text(lblNein, "Später");
  lv_obj_set_style_text_font(lblNein, &font_montserrat_22, 0);
  lv_obj_set_style_text_color(lblNein, lv_color_hex(0xffffff), 0);
  lv_obj_center(lblNein);
  lv_obj_add_event_cb(btnNein, cbPopupSchliessen, LV_EVENT_CLICKED, nullptr);
}

static void cbUpdateBadgeTap(lv_event_t*) { zeigeUpdatePopup(); }

void erstelleUpdateBadge() {
  LvLock lock;
  if (updateBadge || !objects.screen_1) return;
  // Freie Fläche links oben (über dem Pollenfeld: x 0–190, y 0–86); der Regen-/Frost-/UV-Hinweis liegt unter der Uhr
  updateBadge = lv_btn_create(objects.screen_1);
  lv_obj_set_size(updateBadge, 160, 56);
  lv_obj_set_pos(updateBadge, 14, 14);
  lv_obj_set_style_bg_color(updateBadge, lv_color_hex(0xf5a623), 0);
  lv_obj_set_style_bg_opa(updateBadge, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(updateBadge, 17, 0);
  lv_obj_set_style_border_width(updateBadge, 0, 0);
  lv_obj_set_style_shadow_width(updateBadge, 0, 0);
  lv_obj_t* lbl = lv_label_create(updateBadge);
  lv_label_set_text(lbl, LV_SYMBOL_DOWNLOAD " Update");   // Symbol und ASCII: eingebaute Schrift
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x1a1a1a), 0);
  lv_obj_center(lbl);
  lv_obj_add_flag(updateBadge, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(updateBadge, cbUpdateBadgeTap, LV_EVENT_CLICKED, nullptr);
}

void aktualisiereUpdateBadge() {
  LvLock lock;
  if (!updateBadge) return;
  // Der Hinweis am Display erscheint nur im Modus 2 (Display und Web-Oberflaeche)
  if (updateVerfuegbar && cfg.update_modus == 2) lv_obj_clear_flag(updateBadge, LV_OBJ_FLAG_HIDDEN);
  else                                           lv_obj_add_flag(updateBadge, LV_OBJ_FLAG_HIDDEN);
}

// Nach dem Umstellen des Update-Modus: Modus 0 loescht den Stand, sonst wird bald (nach 20 s) neu geprueft
void updateModusGeaendert() {
  if (cfg.update_modus == 0) {
    updateVerfuegbar = false;
  } else {
    letzterUpdateCheck   = millis();
    updateCheckIntervall = 20000UL;
  }
  aktualisiereUpdateBadge();
}

// ── Update-Fenster mit Fortschrittsbalken ────────────────────────────────────
static lv_obj_t* updScreen  = nullptr;
static lv_obj_t* updTitel   = nullptr;
static lv_obj_t* updText    = nullptr;
static lv_obj_t* updWarn    = nullptr;
static lv_obj_t* updBalken  = nullptr;
static lv_obj_t* updProzent = nullptr;
static int       updLetztesProzent = -1;

static void zeigeUpdateScreen(const char* titel, uint32_t titelFarbe, const char* warnung, const char* text, bool mitBalken) {
  LvLock lock;
  if (!updScreen) {
    updScreen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(updScreen, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(updScreen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(updScreen, LV_OBJ_FLAG_SCROLLABLE);

    updTitel = lv_label_create(updScreen);
    lv_obj_set_style_text_font(updTitel, &font_montserrat_48, 0);

    updText = lv_label_create(updScreen);
    lv_label_set_long_mode(updText, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(updText, 860);
    lv_obj_set_style_text_font(updText, &font_montserrat_22, 0);
    lv_obj_set_style_text_color(updText, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_align(updText, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(updText, LV_ALIGN_TOP_MID, 0, 300);

    // Große Warnzeile: das Flackern ist normal, das Gerät darf nicht vom Strom getrennt werden
    updWarn = lv_label_create(updScreen);
    lv_label_set_long_mode(updWarn, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(updWarn, 900);
    lv_obj_set_style_text_font(updWarn, &font_montserrat_32, 0);
    lv_obj_set_style_text_color(updWarn, lv_color_hex(0xffa500), 0);
    lv_obj_set_style_text_align(updWarn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(updWarn, LV_ALIGN_TOP_MID, 0, 160);

    updBalken = lv_bar_create(updScreen);
    lv_obj_set_size(updBalken, 700, 28);
    lv_obj_align(updBalken, LV_ALIGN_BOTTOM_MID, 0, -120);
    lv_bar_set_range(updBalken, 0, 100);
    lv_obj_set_style_bg_color(updBalken, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_bg_color(updBalken, lv_color_hex(0x4cd964), LV_PART_INDICATOR);

    updProzent = lv_label_create(updScreen);
    lv_obj_set_style_text_font(updProzent, &font_montserrat_32, 0);
    lv_obj_set_style_text_color(updProzent, lv_color_hex(0xffffff), 0);
    lv_obj_align(updProzent, LV_ALIGN_BOTTOM_MID, 0, -60);
  }
  lv_label_set_text(updTitel, titel);
  lv_obj_set_style_text_color(updTitel, lv_color_hex(titelFarbe), 0);
  lv_obj_align(updTitel, LV_ALIGN_TOP_MID, 0, 50);
  lv_label_set_text(updWarn, warnung);
  lv_label_set_text(updText, text);
  if (mitBalken) { lv_obj_clear_flag(updBalken, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(updProzent, LV_OBJ_FLAG_HIDDEN); }
  else           { lv_obj_add_flag(updBalken, LV_OBJ_FLAG_HIDDEN);   lv_obj_add_flag(updProzent, LV_OBJ_FLAG_HIDDEN); }
  updLetztesProzent = -1;
  lv_bar_set_value(updBalken, 0, LV_ANIM_OFF);
  lv_label_set_text(updProzent, "0 %");
  lv_scr_load(updScreen);
}

// Fortschrittsanzeige (Netzwerk-Task), nach jedem geschriebenen Block
static void updateFortschritt(size_t done, size_t total) {
  if (!updBalken || total == 0) return;
  int p = (int)((uint64_t)done * 100 / total);
  if (p == updLetztesProzent) return;
  updLetztesProzent = p;
  LvLock lock;
  lv_bar_set_value(updBalken, p, LV_ANIM_OFF);
  lv_label_set_text_fmt(updProzent, "%d %%", p);
}

String installiereUpdateIntern() {
  WiFi.setSleep(false);   // schneller Datenstrom waehrend des Updates (danach Neustart oder unten wieder an)
  logDiag("[OTA] Lade %s", OTA_BIN_URL);
  zeigeUpdateScreen("Update wird installiert", 0xf5a623,
#if UPDATE_DISPLAY_FLACKERT
                    "Achtung: Das Display flackert gleich wild!\nDas ist normal. Gerät NICHT vom Strom trennen!",
#else
                    "Gerät NICHT vom Strom trennen!",
#endif
                    "Das Update dauert ca. 1 Minute, danach startet das Gerät selbst neu.", true);

  WiFiClientSecure client;
  HTTPClient http;
  http.begin(client, OTA_BIN_URL);
  http.setTimeout(30000);
  int code = http.GETStream();
  if (code != 200) {
    char e[100] = ""; if (code < 0) client.lastError(e, sizeof(e));
    http.end();
    logDiag("[OTA] Fehler: HTTP %d %s", code, e);
    return "download error " + String(code);
  }
  int contentLen = http.getSize();
  if (!g_ota.begin()) { http.end(); logDiag("[OTA] Fehler: %s", g_ota.fehler.c_str()); return g_ota.fehler; }
  uint8_t* buf = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) { g_ota.abbrechen(); http.end(); return "Zu wenig Speicher für das Update"; }
  unsigned long letzteDaten = millis(), startMs = millis(), letzterBericht = millis();
  bool ok = true;
  for (;;) {
    int n = http.read(buf, 4096);
    if (millis() - letzterBericht > 5000) { letzterBericht = millis(); printf("[OTA] %u von %d Bytes nach %lu s\n", (unsigned)g_ota.geschrieben, contentLen, (millis() - startMs) / 1000); }
    if (n > 0) {
      letzteDaten = millis();
      if (!g_ota.write(buf, (size_t)n)) { ok = false; break; }
      updateFortschritt(g_ota.geschrieben, contentLen > 0 ? (size_t)contentLen : g_ota.geschrieben + 1);
    } else if (n == 0) break;                                     // Ende
    else if (millis() - letzteDaten > 20000) { g_ota.fehler = "Die Verbindung ist abgebrochen"; logDiag("[OTA] Abbruch nach %u von %d Bytes, %lu s, read=%d", (unsigned)g_ota.geschrieben, contentLen, (millis() - startMs) / 1000, n); ok = false; break; }
    else delay(20);                                               // gerade nichts angekommen: kurz warten
  }
  heap_caps_free(buf);
  http.end();
  if (ok && contentLen > 0 && g_ota.geschrieben != (size_t)contentLen) { g_ota.fehler = "Die Datei ist unvollständig angekommen"; ok = false; }
  if (ok) ok = g_ota.end();
  if (!ok) {
    g_ota.abbrechen();
    logDiag("[OTA] Fehler: %s", g_ota.fehler.c_str());
    return g_ota.fehler.length() ? g_ota.fehler : String("unknown error");
  }
  logDiag("[OTA] %u Bytes geschrieben, Neustart", (unsigned)g_ota.geschrieben);
  return "";
}

// Update mit freiem Speicher: der MQTT-Client wird solange beendet (er haelt Puffer im knappen internen RAM); schlaegt das Update fehl, verbindet er wieder
String installiereUpdate() {
  logDiag("[OTA] Interner Speicher vor dem Update: frei %u, groesster Block %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  sensorenMqttPause(true);
  logDiag("[OTA] MQTT pausiert: frei %u, groesster Block %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  String f = installiereUpdateIntern();
  if (f.length()) sensorenMqttPause(false);
  return f;
}

void updateErfolgNeustart() {
  zeigeUpdateScreen("Update installiert", 0x4cd964, "", "Das Gerät startet jetzt neu ...", false);
  delay(1500);
  ESP.restart();
}

// Zeigt den Fehler kurz und springt zurück zum Hauptscreen
void updateFehlerAnzeigen(const String& fehler) {
  WiFi.setSleep(true);
  String text = "Das Update ist fehlgeschlagen:\n" + bereinigeText(fehler) + "\n\nDas Gerät läuft mit der alten Version weiter.";
  zeigeUpdateScreen("Update fehlgeschlagen", 0xff5555, "", text.c_str(), false);
  delay(6000);
  LvLock lock;
  nav_goto(SCREEN_ID_SCREEN_1);
}

// ── Rollback-Schutz ──────────────────────────────────────────────────────────
// Mit aktiviertem Rollback gilt eine frisch installierte Firmware erst als gültig, wenn sie gesund gestartet ist. Wird sie nie als
// gültig gemeldet (Absturz, Hänger, Strom gezogen), startet nach dem nächsten Neustart die vorherige Version.
#include "esp_ota_ops.h"
#include "esp_app_format.h"

static bool   s_wartetAufGueltig = false;
static String s_rollbackVersion;     // Version der Firmware, die nicht gestartet ist (leer = kein Rollback)

void otaStartPruefen() {
  const esp_partition_t* lauf = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (lauf && esp_ota_get_state_partition(lauf, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
    s_wartetAufGueltig = true;
    logDiag("[OTA] Neue Firmware, wird nach gesundem Start als gueltig markiert");
  }
  const esp_partition_t* ungueltig = esp_ota_get_last_invalid_partition();
  if (ungueltig) {
    esp_app_desc_t d;
    s_rollbackVersion = (esp_ota_get_partition_description(ungueltig, &d) == ESP_OK) ? String(d.version) : String("?");
    logDiag("[OTA] Rollback: Version %s ist nicht gestartet, die vorherige laeuft", s_rollbackVersion.c_str());
  }
}

// Regelmäßig aufrufen: 60 s nach dem Start ohne Absturz gilt die Firmware als gesund
void otaGesundPruefen(unsigned long jetzt) {
  static bool erledigt = false;
  if (erledigt || jetzt < 60000UL) return;
  erledigt = true;
  notfallGesund();                       // 60 s ohne Absturz: Absturzzähler zurücksetzen
  if (!s_wartetAufGueltig) return;
  s_wartetAufGueltig = false;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) logDiag("[OTA] Start gesund: Firmware v" FIRMWARE_VERSION " als gueltig markiert");
}

bool otaWartetAufGueltig() { return s_wartetAufGueltig; }

bool otaAktuellGueltig() {
  const esp_partition_t* lauf = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (!lauf || esp_ota_get_state_partition(lauf, &st) != ESP_OK) return true;   // Zustand nicht gesetzt: per USB geflasht, gilt als gültig
  return st == ESP_OTA_IMG_VALID || st == ESP_OTA_IMG_UNDEFINED;
}

// Einmalige Meldung auf dem Display, wenn das letzte Update nicht gestartet ist (und die vorherige Version läuft)
void otaRollbackAnzeigen() {
  if (s_rollbackVersion.isEmpty()) return;
  Preferences p; p.begin("wcfota", false);
  String gemerkt = p.getString("rb_ver", "");
  if (gemerkt == s_rollbackVersion) { p.end(); return; }
  p.putString("rb_ver", s_rollbackVersion); p.end();
  String text = "Die neue Version " + s_rollbackVersion + " hat nicht gestartet.\nDie vorherige Version (" FIRMWARE_VERSION ") läuft wieder.\n\n"
                "Das Gerät funktioniert normal. Du kannst das Update später erneut versuchen.";
  zeigeUpdateScreen("Update zurückgenommen", 0xf5a623, "", text.c_str(), false);
  delay(9000);
  LvLock lock;
  nav_goto(SCREEN_ID_SCREEN_1);
}

// Kurzbeschreibung der Slots für das /log
String otaSlotInfo() {
  const esp_partition_t* lauf = esp_ota_get_running_partition();
  String s = String("Firmware-Slot: ") + (lauf ? lauf->label : "?");
  esp_ota_img_states_t st;
  if (lauf && esp_ota_get_state_partition(lauf, &st) == ESP_OK) {
    s += st == ESP_OTA_IMG_VALID ? " (gueltig)" : st == ESP_OTA_IMG_PENDING_VERIFY ? " (neu, wartet auf gesunden Start)" : st == ESP_OTA_IMG_NEW ? " (neu)" : " (Zustand ungesetzt)";
  }
  const esp_partition_t* andere = esp_ota_get_next_update_partition(lauf);
  esp_app_desc_t d;
  if (andere && esp_ota_get_partition_description(andere, &d) == ESP_OK) s += String(", andere Version im Slot ") + andere->label + ": " + d.version;
  else if (andere) s += String(", Slot ") + andere->label + " ohne gueltige Firmware";
  return s;
}

// ── Firmware aus einer Datei (Upload über die Web-Oberfläche) ─────────────────
static size_t s_dateiGesamt = 0, s_dateiGeschrieben = 0;

String otaDateiStart(size_t gesamt) {
  s_dateiGesamt = gesamt; s_dateiGeschrieben = 0;
  WiFi.setSleep(false);   // schneller Upload; bei einem Fehler schaltet otaDateiAbbrechen() / updateFehlerAnzeigen() den Sparmodus wieder ein
  zeigeUpdateScreen("Update wird installiert", 0xf5a623, "Gerät NICHT vom Strom trennen!",
                    "Die Firmware wird aus der hochgeladenen Datei installiert. Danach startet das Gerät selbst neu.", true);
  updLetztesProzent = -1;
  if (!g_ota.begin()) return g_ota.fehler;
  return "";
}

String otaDateiSchreiben(uint8_t* buf, size_t len) {
  if (s_dateiGeschrieben == 0) {
    // Kopf prüfen: Firmware-Abbild, richtiger Chip, richtiges Projekt (schützt vor Dateien aus anderen Projekten)
    if (len < 112 || buf[0] != ESP_IMAGE_HEADER_MAGIC) { g_ota.abbrechen(); return "Das ist keine Firmware-Datei (.bin)."; }
    uint16_t chip = buf[12] | (buf[13] << 8);
    if (chip != CONFIG_IDF_FIRMWARE_CHIP_ID) { g_ota.abbrechen(); return "Diese Firmware ist für einen anderen Chip gebaut."; }
    if (strncmp((const char*)buf + 80, "wettercube_fusion", 32) != 0) { g_ota.abbrechen(); return "Das ist keine WetterCube-Fusion-Firmware."; }
  }
  if (!g_ota.write(buf, len)) { String e = g_ota.fehler; g_ota.abbrechen(); return e; }
  s_dateiGeschrieben += len;
  if (s_dateiGesamt) updateFortschritt(s_dateiGeschrieben, s_dateiGesamt);
  return "";
}

void otaDateiAbbrechen() { g_ota.abbrechen(); WiFi.setSleep(true); }

String otaDateiEnde() {
  if (!g_ota.end()) return g_ota.fehler;
  const esp_partition_t* neu = esp_ota_get_boot_partition();
  esp_app_desc_t d;
  if (neu && esp_ota_get_partition_description(neu, &d) == ESP_OK) logDiag("[OTA] Aus Datei installiert: Version %s (Slot %s)", d.version, neu->label);
  return "";
}
