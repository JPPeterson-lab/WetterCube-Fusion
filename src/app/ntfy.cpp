// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// ntfy: Nachrichten ueber die JSON-Schnittstelle (POST auf die Server-Adresse), damit Umlaute ohne Kopfzeilen-Tricks ankommen
#include "app_state.h"
#include "ntfy.h"
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

struct NtfyNachricht { char titel[72]; char text[256]; char tags[48]; int prio; };
static const int WARTE_MAX = 4;
static NtfyNachricht s_warte[WARTE_MAX];
static int s_n = 0;
static bool s_laeuft = false;
static SemaphoreHandle_t s_mx = nullptr;
static char s_status[96] = "Noch nichts gesendet";

bool ntfyBereit() { return cfg.ntfy_aktiv && cfg.ntfy_server.length() > 8 && cfg.ntfy_topic.length() > 0; }
bool ntfyQuelleAktiv(NtfyQuelle q) { return ntfyBereit() && (cfg.ntfy_quellen & q); }
const char* ntfyStatusText() { return s_status; }

static void statusSetzen(const char* t) { strlcpy(s_status, t, sizeof(s_status)); }

// Eine Nachricht abschicken; Rueckgabe: HTTP-Code oder -1
static int senden(const NtfyNachricht& m) {
  JsonDocument doc;
  doc["topic"]    = std::string(cfg.ntfy_topic.c_str());
  doc["title"]    = std::string(m.titel);
  doc["message"]  = std::string(m.text);
  doc["priority"] = m.prio < 1 ? 3 : m.prio > 5 ? 5 : m.prio;
  if (m.tags[0]) { JsonArray a = doc["tags"].to<JsonArray>(); a.add(std::string(m.tags)); }
  std::string body; serializeJson(doc, body);
  WiFiClientSecure client;
  HTTPClient http;
  http.begin(client, cfg.ntfy_server);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");
  if (cfg.ntfy_token.length()) http.addHeader("Authorization", String("Bearer ") + cfg.ntfy_token);
  int code = http.POST(String(body.c_str()));
  if (code < 0) { char e[100]; client.lastError(e, sizeof(e)); logDiag("[ntfy] Senden fehlgeschlagen: %s", e); snprintf(s_status, sizeof(s_status), "Fehler: Server nicht erreicht"); }
  else if (code >= 200 && code < 300) { logDiag("[ntfy] gesendet: %s", m.titel); snprintf(s_status, sizeof(s_status), "Gesendet (HTTP %d)", code); }
  else { logDiag("[ntfy] Server antwortet mit HTTP %d", code); snprintf(s_status, sizeof(s_status), "Fehler: Server antwortet mit HTTP %d%s", code, code == 401 || code == 403 ? " (Zugang prüfen)" : ""); }
  http.end();
  return code;
}

static void ntfyTask(void*) {
  for (;;) {
    NtfyNachricht m;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (s_n == 0) { s_laeuft = false; xSemaphoreGive(s_mx); break; }
    m = s_warte[0];
    for (int i = 1; i < s_n; i++) s_warte[i - 1] = s_warte[i];
    s_n--;
    xSemaphoreGive(s_mx);
    if (WiFi.status() == WL_CONNECTED) senden(m); else statusSetzen("Fehler: kein WLAN");
  }
  vTaskDelete(NULL);
}

void ntfySenden(const char* titel, const char* text, int prio, const char* tags) {
  if (!ntfyBereit()) return;
  if (!s_mx) s_mx = xSemaphoreCreateMutex();
  xSemaphoreTake(s_mx, portMAX_DELAY);
  if (s_n >= WARTE_MAX) { for (int i = 1; i < s_n; i++) s_warte[i - 1] = s_warte[i]; s_n--; }   // aelteste verwerfen
  NtfyNachricht& m = s_warte[s_n++];
  strlcpy(m.titel, titel ? titel : "", sizeof(m.titel));
  strlcpy(m.text, text ? text : "", sizeof(m.text));
  strlcpy(m.tags, tags ? tags : "", sizeof(m.tags));
  m.prio = prio;
  bool starten = !s_laeuft;
  if (starten) s_laeuft = true;
  xSemaphoreGive(s_mx);
  if (starten && xTaskCreate(ntfyTask, "ntfy", 8192, nullptr, 3, nullptr) != pdPASS) {
    xSemaphoreTake(s_mx, portMAX_DELAY); s_laeuft = false; xSemaphoreGive(s_mx);
    statusSetzen("Fehler: zu wenig Speicher");
  }
}

void ntfyTest() {
  if (!ntfyBereit()) { statusSetzen("Bitte erst Server und Thema eintragen und speichern"); return; }
  statusSetzen("Wird gesendet ...");
  ntfySenden("WetterCube Fusion", "Testnachricht: Die Benachrichtigung funktioniert.", 3, "white_check_mark");
}
