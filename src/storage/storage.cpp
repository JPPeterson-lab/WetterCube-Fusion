// Speicherkarte: Dauer-Log und Sensor-Verlauf als Tagesdateien. Das Einhängen steckt in der Board-Schicht (src/board):
// S3-Board über SPI, P4-Board über SDMMC.
#include "storage.h"
#include "../app/app_state.h"
#include "../sensors/sensors.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "../board/board.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_partition.h"

static const char* TAG = "speicher";
#define MOUNT   "/sdcard"
#define LOG_TAGE      30          // Log-Dateien so lange behalten
#define VERLAUF_TAGE  400         // Verlaufsdateien so lange behalten
#define MIN_FREI_MB   20          // darunter werden die ältesten Dateien gelöscht

static sdmmc_card_t* s_card = nullptr;
static volatile bool s_bereit = false;
static bool s_kopieErledigt = false;     // Notfall-Kopie der Firmware für diese Karte erledigt
static unsigned long s_tMount = 0, s_tLog = 0;
static uint32_t s_logSeq = 0;
static int s_verlaufMinute = -1;   // zuletzt geschriebene 10-Minuten-Marke (Tag*144 + Slot)
static int s_aufraeumenTag = -1;
static char s_status[40] = "nicht erkannt";

bool speicherBereit() { return s_bereit; }

static void statusAktualisieren() {
  uint64_t g, f;
  if (s_bereit && speicherInfo(&g, &f)) snprintf(s_status, sizeof(s_status), "bereit, %.1f GB frei von %.1f GB", f / 1e9, g / 1e9);
  else if (!s_bereit) strlcpy(s_status, "nicht erkannt", sizeof(s_status));
}
const char* speicherStatusText() { return s_status; }

bool speicherInfo(uint64_t* gesamt, uint64_t* frei) {
  if (!s_bereit) return false;
  uint64_t g = 0, f = 0;
  if (esp_vfs_fat_info(MOUNT, &g, &f) != ESP_OK) return false;
  if (gesamt) *gesamt = g;
  if (frei) *frei = f;
  return true;
}

// ── Einhängen ────────────────────────────────────────────────────────────────
static bool einhaengen() {
  char diag[128] = "";
  if (boardSdEinhaengen(MOUNT, &s_card, diag, sizeof(diag))) return true;
  s_card = nullptr;
  static unsigned long letzteDiag = 0;
  if (diag[0] && (millis() - letzteDiag > 300000UL || letzteDiag == 0)) { letzteDiag = millis(); logDiag("[Karte] %s", diag); }
  return false;
}

static void aushaengen() {
  s_kopieErledigt = false;
  if (s_card) { esp_vfs_fat_sdcard_unmount(MOUNT, s_card); s_card = nullptr; }
  s_bereit = false;
  statusAktualisieren();
}

static bool ordnerAnlegen(const char* rel) {
  char p[48]; snprintf(p, sizeof(p), MOUNT "/%s", rel);
  struct stat st;
  if (stat(p, &st) == 0) return true;
  return mkdir(p, 0775) == 0;
}

// ── Aufräumen: alte Dateien löschen ──────────────────────────────────────────
static void alteDateienLoeschen(const char* ordner, int tage) {
  char dir[48]; snprintf(dir, sizeof(dir), MOUNT "/%s", ordner);
  DIR* d = opendir(dir);
  if (!d) return;
  time_t jetzt = time(nullptr);
  struct dirent* de;
  while ((de = readdir(d)) != nullptr) {
    int y, mo, da;
    if (sscanf(de->d_name, "%d-%d-%d", &y, &mo, &da) != 3) continue;
    struct tm t = {}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = da; t.tm_hour = 12; t.tm_isdst = -1;
    time_t zeit = mktime(&t);
    if (zeit > 0 && (jetzt - zeit) > (time_t)tage * 86400) {
      char f[96]; snprintf(f, sizeof(f), "%s/%s", dir, de->d_name);
      unlink(f);
    }
  }
  closedir(d);
}

static void aeltesteLoeschen(const char* ordner) {   // bei knappem Platz die älteste Datei des Ordners entfernen
  char dir[48]; snprintf(dir, sizeof(dir), MOUNT "/%s", ordner);
  DIR* d = opendir(dir);
  if (!d) return;
  char aelteste[64] = ""; struct dirent* de;
  while ((de = readdir(d)) != nullptr) {
    if (de->d_name[0] == '.') continue;
    if (!aelteste[0] || strcmp(de->d_name, aelteste) < 0) strlcpy(aelteste, de->d_name, sizeof(aelteste));
  }
  closedir(d);
  if (aelteste[0]) { char f[96]; snprintf(f, sizeof(f), "%s/%s", dir, aelteste); unlink(f); }
}

static void aufraeumen() {
  alteDateienLoeschen("log", LOG_TAGE);
  alteDateienLoeschen("verlauf", VERLAUF_TAGE);
  uint64_t g, f;
  for (int i = 0; i < 10 && speicherInfo(&g, &f) && f < (uint64_t)MIN_FREI_MB * 1024 * 1024; i++) { aeltesteLoeschen("log"); aeltesteLoeschen("verlauf"); }
}

// ── Dauer-Log ────────────────────────────────────────────────────────────────
static void logSchreiben() {
  const size_t pufferGr = LOG_RING_SIZE * LOG_LINE_LEN;
  char* puffer = (char*)heap_caps_malloc(pufferGr, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   // nicht im internen RAM halten
  if (!puffer) return;
  int n = logNeuHolen(&s_logSeq, puffer, pufferGr);
  if (n <= 0) { heap_caps_free(puffer); return; }
  struct tm ti; char datei[64];
  if (getLocalTime(&ti, 0)) snprintf(datei, sizeof(datei), MOUNT "/log/%04d-%02d-%02d.log", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  else strlcpy(datei, MOUNT "/log/ohne-zeit.log", sizeof(datei));
  FILE* f = fopen(datei, "a");
  if (!f) { heap_caps_free(puffer); ESP_LOGW(TAG, "Log nicht schreibbar, Karte wird neu gesucht"); aushaengen(); return; }
  fwrite(puffer, 1, n, f);
  fclose(f);
  heap_caps_free(puffer);
}

// ── Verlauf der Sensoren (alle 10 Minuten) ───────────────────────────────────
static void verlaufSchreiben(const struct tm& ti) {
  char datei[64];
  snprintf(datei, sizeof(datei), MOUNT "/verlauf/%04d-%02d-%02d.csv", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  struct stat st; bool neu = stat(datei, &st) != 0;
  FILE* f = fopen(datei, "a");
  if (!f) { ESP_LOGW(TAG, "Verlauf nicht schreibbar, Karte wird neu gesucht"); aushaengen(); return; }
  if (neu) fputs("zeit,id,raum,name,wert,einheit\n", f);
  char zeit[24]; snprintf(zeit, sizeof(zeit), "%04d-%02d-%02d %02d:%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min);
  auto zeile = [&](const char* id, const char* raum, const char* name, float wert, int dez, const char* einheit) {
    fprintf(f, "%s,%s,%s,%s,%.*f,%s\n", zeit, id, raum, name, dez, wert, einheit);
  };
  int64_t jetztMs = esp_timer_get_time() / 1000;
  for (int i = 0; i < sensorenAnzahl(); i++) {
    SensorCfg c; SensorWert w;
    if (!sensorenHole(i, &c, &w) || !w.gueltig) continue;
    if (jetztMs - w.zuletzt_ms > 15 * 60 * 1000) continue;     // veraltete Werte nicht als aktuell eintragen
    zeile(c.id, c.raum, c.name, w.wert, c.dezimal, c.einheit);
  }
  if (wetter.geladen) {                                         // Außenwerte von Open-Meteo
    zeile("aussen_temp", "Aussen", "Temperatur", wetter.temp, 1, "°C");
    zeile("aussen_feuchte", "Aussen", "Luftfeuchte", wetter.humidity, 0, "%");
    zeile("aussen_druck", "Aussen", "Luftdruck", wetter.pressure, 0, "hPa");
    zeile("aussen_wind", "Aussen", "Wind", wetter.wind_speed, 0, "km/h");
  }
  fclose(f);
}


// ── Einstellungen auf die Karte sichern (ohne Passwörter), damit nach einem Werksreset nichts verloren ist ──
static volatile bool s_sichAnf = false;
static unsigned long s_sichZeit = 0;
void speicherSicherungAnfordern() { s_sichAnf = true; s_sichZeit = millis(); }

static void sicherungSchreiben() {
  ordnerAnlegen("notfall");
  String json = webuiSicherungJson();
  FILE* f = fopen(MOUNT "/notfall/einstellungen.tmp", "w");
  if (!f) return;
  bool ok = fwrite(json.c_str(), 1, json.length(), f) == json.length();
  fclose(f);
  if (!ok) { unlink(MOUNT "/notfall/einstellungen.tmp"); return; }
  unlink(MOUNT "/notfall/einstellungen.json");
  rename(MOUNT "/notfall/einstellungen.tmp", MOUNT "/notfall/einstellungen.json");
}

String speicherSicherungLesen() {
  if (!s_bereit) return "";
  FILE* f = fopen(MOUNT "/notfall/einstellungen.json", "r");
  if (!f) return "";
  String out; char* buf = (char*)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { fclose(f); return ""; }
  size_t n;
  while ((n = fread(buf, 1, 2048, f)) > 0 && out.length() < 65536) out.concat(buf, n);
  heap_caps_free(buf); fclose(f);
  return out;
}

// ── „Letzte gute Version“: die laufende Firmware als notfall/firmware.bin auf die Karte kopieren ──
// Die Notfall-App installiert sie bei Bedarf (und prüft sie vorher mit der SHA-256-Summe am Ende des Abbilds).
static String s_kopieStatus = "noch nicht gespeichert";
static FILE*  s_kopieDatei = nullptr;
static char*  s_kopieBuf = nullptr;
static size_t s_kopieLaenge = 0, s_kopiePos = 0;

static size_t imageLaenge(const esp_partition_t* p) {   // Länge des Abbilds laut Segmentköpfen (wie im Bootloader)
  uint8_t kopf[24];
  if (esp_partition_read(p, 0, kopf, sizeof(kopf)) != ESP_OK || kopf[0] != ESP_IMAGE_HEADER_MAGIC) return 0;
  size_t off = 24;
  for (int s = 0; s < kopf[1]; s++) {
    uint32_t seg[2];
    if (esp_partition_read(p, off, seg, 8) != ESP_OK) return 0;
    off += 8 + seg[1];
    if (off > p->size) return 0;
  }
  size_t laenge = (off + 1 + 15) & ~(size_t)15;
  if (kopf[23] == 1) laenge += 32;
  return laenge <= p->size ? laenge : 0;
}

String speicherNotfallStatus() { return s_kopieStatus; }

static void kopieAbbrechen(const char* grund) {
  if (s_kopieDatei) { fclose(s_kopieDatei); s_kopieDatei = nullptr; unlink(MOUNT "/notfall/firmware.tmp"); }
  if (s_kopieBuf) { heap_caps_free(s_kopieBuf); s_kopieBuf = nullptr; }
  s_kopieErledigt = true;
  s_kopieStatus = String("Fehler: ") + grund;
  logDiag("[Karte] Notfall-Kopie der Firmware fehlgeschlagen: %s", grund);
}

// Pro Aufruf ein Stück (32 KB), damit die Web-Oberfläche währenddessen erreichbar bleibt
static void kopieTick() {
  if (s_kopieErledigt || !s_bereit || millis() < 90000UL || !otaAktuellGueltig()) return;
  const esp_partition_t* lauf = esp_ota_get_running_partition();
  if (!s_kopieDatei) {
    esp_app_desc_t d;
    if (!lauf || esp_ota_get_partition_description(lauf, &d) != ESP_OK) { s_kopieErledigt = true; return; }
    // schon aktuell? (Versionsdatei neben der Kopie)
    char gespeichert[40] = "";
    FILE* v = fopen(MOUNT "/notfall/firmware.version", "r");
    if (v) { size_t n = fread(gespeichert, 1, sizeof(gespeichert) - 1, v); gespeichert[n] = 0; fclose(v); }
    struct stat st;
    if (strcmp(gespeichert, d.version) == 0 && stat(MOUNT "/notfall/firmware.bin", &st) == 0) {
      s_kopieErledigt = true; s_kopieStatus = String("Version ") + d.version + " gespeichert"; return;
    }
    s_kopieLaenge = imageLaenge(lauf);
    if (!s_kopieLaenge) { kopieAbbrechen("Abbild nicht lesbar"); return; }
    uint64_t g, fr;
    if (speicherInfo(&g, &fr) && fr < (uint64_t)s_kopieLaenge + 4 * 1024 * 1024) { kopieAbbrechen("zu wenig Platz auf der Karte"); return; }
    ordnerAnlegen("notfall");
    s_kopieDatei = fopen(MOUNT "/notfall/firmware.tmp", "wb");
    s_kopieBuf = (char*)heap_caps_malloc(32768, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_kopieDatei || !s_kopieBuf) { kopieAbbrechen("Datei lässt sich nicht anlegen"); return; }
    s_kopiePos = 0;
    s_kopieStatus = "wird gespeichert ...";
  }
  size_t n = s_kopieLaenge - s_kopiePos; if (n > 32768) n = 32768;
  esp_err_t le = esp_partition_read(lauf, s_kopiePos, s_kopieBuf, n);
  size_t gs = le == ESP_OK ? fwrite(s_kopieBuf, 1, n, s_kopieDatei) : 0;
  if (le != ESP_OK || gs != n) {
    logDiag("[Karte] Kopie bei %u von %u Bytes: Lesen %s, geschrieben %u von %u, errno %d", (unsigned)s_kopiePos, (unsigned)s_kopieLaenge, esp_err_to_name(le), (unsigned)gs, (unsigned)n, errno);
    kopieAbbrechen("Schreibfehler"); return;
  }
  s_kopiePos += n;
  if (s_kopiePos < s_kopieLaenge) return;
  fclose(s_kopieDatei); s_kopieDatei = nullptr; heap_caps_free(s_kopieBuf); s_kopieBuf = nullptr;
  esp_app_desc_t d; esp_ota_get_partition_description(lauf, &d);
  unlink(MOUNT "/notfall/firmware.bin");
  if (rename(MOUNT "/notfall/firmware.tmp", MOUNT "/notfall/firmware.bin") != 0) { kopieAbbrechen("Umbenennen fehlgeschlagen"); return; }
  FILE* v = fopen(MOUNT "/notfall/firmware.version", "w"); if (v) { fputs(d.version, v); fclose(v); }
  s_kopieErledigt = true; s_kopieStatus = String("Version ") + d.version + " gespeichert";
  logDiag("[Karte] Notfall-Kopie der Firmware %s gespeichert (%u KB)", d.version, (unsigned)(s_kopieLaenge / 1024));
}

// ── Takt ─────────────────────────────────────────────────────────────────────
void speicherInit() {
  if (einhaengen()) {
    s_bereit = true;
    ordnerAnlegen("log"); ordnerAnlegen("verlauf");
    statusAktualisieren();
    ESP_LOGI(TAG, "Karte eingehängt: %s", s_status);
    logDiag("[Karte] %s", s_status);
    aufraeumen();
  } else {
    logDiag("[Karte] nicht erkannt (optional, alles läuft auch ohne)");
  }
  s_tMount = millis(); s_tLog = millis();
}

void speicherTick() {
  unsigned long jetzt = millis();
  if (!s_bereit) {
    // Suche nach einer Karte: erst alle 30 s, bei Misserfolg seltener (bis alle 5 Minuten): jeder Versuch belegt SPI und I2C und stoert das Display
    static int fehlversuche = 0;
    unsigned long warte = fehlversuche < 3 ? 30000UL : fehlversuche < 6 ? 120000UL : 300000UL;
    if (jetzt - s_tMount < warte) return;
    s_tMount = jetzt;
    if (!einhaengen()) { fehlversuche++; return; }
    fehlversuche = 0;
    {
      s_bereit = true; ordnerAnlegen("log"); ordnerAnlegen("verlauf"); statusAktualisieren();
      logDiag("[Karte] erkannt: %s", s_status);
    }
    return;
  }
  if (jetzt - s_tLog >= 15000) { s_tLog = jetzt; logSchreiben(); }
  if (s_sichAnf && jetzt - s_sichZeit > 5000) { s_sichAnf = false; sicherungSchreiben(); }
  kopieTick();
  struct tm ti;
  if (!getLocalTime(&ti, 0) || ti.tm_year + 1900 < 2024) return;   // ohne gültige Uhrzeit keine Verlaufsdaten
  int marke = ti.tm_yday * 144 + ti.tm_hour * 6 + ti.tm_min / 10;
  if (marke != s_verlaufMinute) {
    s_verlaufMinute = marke;
    verlaufSchreiben(ti);
    statusAktualisieren();
  }
  if (ti.tm_yday != s_aufraeumenTag) { s_aufraeumenTag = ti.tm_yday; aufraeumen(); }
}

// ── Zugriff für die WebUI ────────────────────────────────────────────────────
static bool ordnerErlaubt(const char* o) { return strcmp(o, "verlauf") == 0 || strcmp(o, "log") == 0; }

int speicherDateien(const char* ordner, char* out, size_t n) {
  if (!s_bereit || !ordnerErlaubt(ordner) || n < 2) return 0;
  char dir[48]; snprintf(dir, sizeof(dir), MOUNT "/%s", ordner);
  DIR* d = opendir(dir);
  if (!d) return 0;
  int cnt = 0; size_t len = 0; out[0] = 0;
  struct dirent* de;
  while ((de = readdir(d)) != nullptr) {
    if (de->d_name[0] == '.') continue;
    char f[96]; snprintf(f, sizeof(f), "%s/%s", dir, de->d_name);
    struct stat st; if (stat(f, &st) != 0) continue;
    int w = snprintf(out + len, n - len, "%s;%ld\n", de->d_name, (long)st.st_size);
    if (w < 0 || (size_t)w >= n - len) break;
    len += w; cnt++;
  }
  closedir(d);
  return cnt;
}

bool speicherDateiSenden(const char* rel, void (*chunk)(const char*, size_t, void*), void* ctx, size_t* groesse) {
  if (!s_bereit) return false;
  // nur „verlauf/NAME“ oder „log/NAME“ mit harmlosen Zeichen
  const char* slash = strchr(rel, '/');
  if (!slash || slash == rel || strlen(rel) > 60) return false;
  char ord[12]; size_t ol = slash - rel; if (ol >= sizeof(ord)) return false;
  memcpy(ord, rel, ol); ord[ol] = 0;
  if (!ordnerErlaubt(ord)) return false;
  for (const char* p = slash + 1; *p; p++) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '-' || *p == '_' || *p == '.')) return false;
  if (strstr(slash + 1, "..")) return false;
  char pfad[96]; snprintf(pfad, sizeof(pfad), MOUNT "/%s", rel);
  struct stat st; if (stat(pfad, &st) != 0) return false;
  if (groesse) *groesse = st.st_size;
  if (!chunk) return true;
  FILE* f = fopen(pfad, "r");
  if (!f) return false;
  char* buf = (char*)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); size_t r;
  if (!buf) { fclose(f); return false; }
  while ((r = fread(buf, 1, 2048, f)) > 0) chunk(buf, r, ctx);
  heap_caps_free(buf);
  fclose(f);
  return true;
}
