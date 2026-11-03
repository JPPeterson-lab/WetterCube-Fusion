// WebUI der Speicherkarte: Seite /karte (Status, Dateiliste) und Download unter /karte/datei?d=verlauf/2026-10-16.csv
#include "storage.h"
#include "../app/webui_theme.h"
#include "../platform/WebServer.h"
#include "esp_heap_caps.h"

static const char KOPF[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>WetterCube Fusion – Speicherkarte</title>
)HTML" WEBUI_THEME_HEAD WEBUI_THEME_CSS R"HTML(
<style>
body{font-family:Arial,sans-serif;background:var(--bg);color:var(--txt);margin:0;padding:14px;max-width:760px;margin:auto;line-height:1.45}
h1{font-size:1.4em} h2{font-size:1.1em;margin:0 0 6px} .card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;margin-bottom:14px}
a{color:var(--acc)} table{width:100%;border-collapse:collapse;font-size:.92em} td{padding:6px 4px;border-bottom:1px solid var(--bd2)} .klein{font-size:.85em;color:var(--mut)}
.ok{color:#3fb950}.aus{color:#f85149}
</style></head><body>
)HTML" WEBUI_NAVBAR R"HTML(
<h1>Speicherkarte</h1>
)HTML";

static void tabelle(WebServer& s, const char* ordner, const char* titel, const char* hinweis) {
  char* liste = (char*)heap_caps_malloc(3072, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!liste) return;
  int n = speicherDateien(ordner, liste, 3072);
  String h = String("<div class=\"card\"><h2>") + titel + "</h2><p class=\"klein\">" + hinweis + "</p>";
  if (n == 0) h += "<p class=\"klein\">Noch keine Dateien.</p></div>";
  else {
    h += "<table>";
    // neueste zuerst: die Liste kommt in Verzeichnisreihenfolge, hier umgekehrt ausgeben
    String zeilen[64]; int z = 0;
    for (char* p = strtok(liste, "\n"); p && z < 64; p = strtok(nullptr, "\n")) zeilen[z++] = p;
    for (int i = z - 1; i >= 0; i--) {
      int sep = zeilen[i].indexOf(';');
      String name = zeilen[i].substring(0, sep); long gr = zeilen[i].substring(sep + 1).toInt();
      h += "<tr><td><a href=\"/karte/datei?d=" + String(ordner) + "/" + name + "\">" + name + "</a></td><td>" +
           (gr >= 1024 ? String(gr / 1024) + " KB" : String(gr) + " Bytes") + "</td></tr>";
    }
    h += "</table></div>";
  }
  heap_caps_free(liste);
  s.sendContent(h);
}

void speicherWebuiRegistrieren(WebServer& server) {
  server.on("/karte", HTTP_GET, [&server]() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html; charset=utf-8", "");
    server.sendContent_P(KOPF);
    String st = String("<div class=\"card\"><h2>Status</h2><p class=\"") + (speicherBereit() ? "ok" : "aus") + "\">" + speicherStatusText() + "</p>";
    if (!speicherBereit()) st += "<p class=\"klein\">Die Karte ist optional. Stecke eine microSD-Karte (FAT32) in den Einschub, das Ger&auml;t sucht alle 30 Sekunden danach.</p>";
    st += "<p class=\"klein\">Verlaufsdaten und Log liegen als Tagesdateien auf der Karte. Es werden keine Zugangsdaten (WLAN, MQTT) gespeichert. "
          "&Auml;ltere Dateien werden automatisch gel&ouml;scht (Log nach 30 Tagen, Verlauf nach 400 Tagen, bei knappem Platz die &auml;ltesten).</p></div>";
    server.sendContent(st);
    if (speicherBereit()) {
      tabelle(server, "verlauf", "Verlauf der Sensoren", "Eine Datei je Tag (CSV, alle 10 Minuten ein Wert je Sensor, dazu die Au&szlig;enwerte von Open-Meteo). Mit Excel oder Numbers &ouml;ffnen.");
      tabelle(server, "log", "Log", "Eine Datei je Tag mit allen Diagnose-Meldungen, auch &uuml;ber Neustarts hinweg.");
    }
    server.sendContent("</body></html>");
    server.sendContent("");
  });
  server.on("/karte/datei", HTTP_GET, [&server]() {
    String d = server.arg("d");
    size_t gr = 0;
    if (!speicherDateiSenden(d.c_str(), nullptr, nullptr, &gr)) { server.send(404, "text/plain", "Datei nicht gefunden"); return; }
    String name = d.substring(d.lastIndexOf('/') + 1);
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    server.setContentLength(gr);
    server.send(200, name.endsWith(".csv") ? "text/csv; charset=utf-8" : "text/plain; charset=utf-8", "");
    speicherDateiSenden(d.c_str(), [](const char* b, size_t n, void* ctx) { ((WebServer*)ctx)->sendContent(b, n); }, &server, nullptr);
  });
}
