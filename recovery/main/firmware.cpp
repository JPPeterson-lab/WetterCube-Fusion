// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Prüfen und Installieren von Firmware-Abbildern (Aufbau des ESP-Abbilds: Kopf 24 Byte, Segmente, Prüfsummenbyte, SHA-256 am Ende)
#include "firmware.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mbedtls/sha256.h"
#include "sdkconfig.h"

static const char* TAG = "firmware";
static const char* PROJEKT = "wettercube_fusion";   // nur Abbilder der Hauptfirmware gelten (nicht die Notfall-App, nicht fremde Projekte)

// Lesezugriff auf ein Abbild: Datei oder Partition
struct Leser {
  FILE* f = nullptr;
  const esp_partition_t* p = nullptr;
  size_t gesamt = 0;
  bool lesen(size_t off, void* buf, size_t n) {
    if (off + n > gesamt) return false;
    if (f) return fseek(f, (long)off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
    return esp_partition_read(p, off, buf, n) == ESP_OK;
  }
};

static void setzeFehler(FwInfo& i, const char* t) { strlcpy(i.fehler, t, sizeof(i.fehler)); }

static FwInfo pruefeLeser(Leser& L, size_t maxLaenge) {
  FwInfo info;
  uint8_t kopf[24 + 8 + sizeof(esp_app_desc_t)];
  if (L.gesamt < sizeof(kopf) || !L.lesen(0, kopf, sizeof(kopf))) { setzeFehler(info, "Datei zu kurz oder nicht lesbar"); return info; }
  if (kopf[0] != ESP_IMAGE_HEADER_MAGIC) { setzeFehler(info, "Das ist kein Firmware-Abbild"); return info; }
  uint16_t chip = kopf[12] | (kopf[13] << 8);
  if (chip != CONFIG_IDF_FIRMWARE_CHIP_ID) { setzeFehler(info, "Firmware ist für einen anderen Chip gebaut"); return info; }
  const esp_app_desc_t* d = (const esp_app_desc_t*)(kopf + 24 + 8);
  if (d->magic_word != ESP_APP_DESC_MAGIC_WORD) { setzeFehler(info, "Kein gültiger Firmware-Kopf"); return info; }
  if (strcmp(d->project_name, PROJEKT) != 0) { setzeFehler(info, "Das ist keine WetterCube-Fusion-Firmware"); return info; }
  strlcpy(info.version, d->version, sizeof(info.version));
  // Länge des Abbilds aus den Segmentköpfen (wie im Bootloader)
  size_t off = 24;
  for (int s = 0; s < kopf[1]; s++) {
    uint32_t seg[2];
    if (!L.lesen(off, seg, 8)) { setzeFehler(info, "Abbild beschädigt (Segmentkopf)"); return info; }
    off += 8 + seg[1];
    if (off > L.gesamt) { setzeFehler(info, "Datei ist unvollständig"); return info; }
  }
  size_t laenge = (off + 1 + 15) & ~(size_t)15;
  bool hash = kopf[23] == 1;
  if (hash) laenge += 32;
  if (laenge > L.gesamt) { setzeFehler(info, "Datei ist unvollständig"); return info; }
  if (laenge > maxLaenge) { setzeFehler(info, "Firmware ist zu groß für den Slot"); return info; }
  if (hash) {   // SHA-256 über alles vor dem angehängten Hash
    mbedtls_sha256_context c; mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
    static uint8_t buf[4096];
    size_t pos = 0, bis = laenge - 32;
    while (pos < bis) {
      size_t n = bis - pos < sizeof(buf) ? bis - pos : sizeof(buf);
      if (!L.lesen(pos, buf, n)) { mbedtls_sha256_free(&c); setzeFehler(info, "Lesefehler beim Prüfen"); return info; }
      mbedtls_sha256_update(&c, buf, n); pos += n;
    }
    uint8_t soll[32], ist[32];
    mbedtls_sha256_finish(&c, ist); mbedtls_sha256_free(&c);
    if (!L.lesen(bis, soll, 32) || memcmp(soll, ist, 32) != 0) { setzeFehler(info, "Prüfsumme stimmt nicht (Datei beschädigt)"); return info; }
  }
  info.groesse = laenge; info.ok = true;
  return info;
}

FwInfo pruefeDatei(const char* pfad) {
  FwInfo info;
  struct stat st;
  if (stat(pfad, &st) != 0) { setzeFehler(info, "Datei nicht gefunden"); return info; }
  Leser L; L.f = fopen(pfad, "rb"); L.gesamt = st.st_size;
  if (!L.f) { setzeFehler(info, "Datei nicht lesbar"); return info; }
  // Hoechstgroesse = Groesse eines Firmware-Slots (S3: 0x6C0000, P4: 0xA00000), sonst passt die Datei nicht
  const esp_partition_t* slot = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
  info = pruefeLeser(L, slot ? slot->size : 0x6C0000);
  fclose(L.f);
  return info;
}

FwInfo pruefeSlot(const esp_partition_t* p) {
  FwInfo info;
  if (!p) { setzeFehler(info, "Slot nicht vorhanden"); return info; }
  Leser L; L.p = p; L.gesamt = p->size;
  return pruefeLeser(L, p->size);
}

const esp_partition_t* slotHolen(int index) {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + index), NULL);
}

int slotIndex(const esp_partition_t* p) {
  if (!p) return -1;
  if (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) return 0;
  if (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) return 1;
  return -1;
}

static void err(char* f, size_t n, const char* t) { if (f && n) strlcpy(f, t, n); }

bool installiereDatei(const char* pfad, const esp_partition_t* ziel, void (*fortschritt)(int), char* fehler, size_t fehlerLen) {
  FwInfo info = pruefeDatei(pfad);
  if (!info.ok) { err(fehler, fehlerLen, info.fehler); return false; }
  FILE* f = fopen(pfad, "rb");
  if (!f) { err(fehler, fehlerLen, "Datei nicht lesbar"); return false; }
  esp_ota_handle_t h = 0;
  if (esp_ota_begin(ziel, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) { fclose(f); err(fehler, fehlerLen, "Der Slot lässt sich nicht beschreiben"); return false; }
  static uint8_t buf[4096];
  size_t pos = 0; int letzte = -1;
  while (pos < info.groesse) {
    size_t n = info.groesse - pos < sizeof(buf) ? info.groesse - pos : sizeof(buf);
    if (fread(buf, 1, n, f) != n) { fclose(f); esp_ota_abort(h); err(fehler, fehlerLen, "Lesefehler von der Karte"); return false; }
    if (esp_ota_write(h, buf, n) != ESP_OK) { fclose(f); esp_ota_abort(h); err(fehler, fehlerLen, "Schreibfehler im Flash"); return false; }
    pos += n;
    int p = (int)((uint64_t)pos * 100 / info.groesse);
    if (p != letzte && fortschritt) { letzte = p; fortschritt(p); }
  }
  fclose(f);
  if (esp_ota_end(h) != ESP_OK) { err(fehler, fehlerLen, "Die geschriebene Firmware ist fehlerhaft"); return false; }
  if (esp_ota_set_boot_partition(ziel) != ESP_OK) { err(fehler, fehlerLen, "Startpartition lässt sich nicht setzen"); return false; }
  ESP_LOGI(TAG, "Firmware %s in Slot %s installiert", info.version, ziel->label);
  return true;
}

bool setzeStartSlot(const esp_partition_t* p, char* fehler, size_t fehlerLen) {
  if (!p || esp_ota_set_boot_partition(p) != ESP_OK) { err(fehler, fehlerLen, "Startpartition lässt sich nicht setzen"); return false; }
  return true;
}

bool webPasswortEntfernen(char* fehler, size_t fehlerLen) {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { err(fehler, fehlerLen, "Einstellungsspeicher ist nicht lesbar"); return false; }
  nvs_handle_t h;
  if (nvs_open("wcfusion", NVS_READWRITE, &h) != ESP_OK) { err(fehler, fehlerLen, "Einstellungsspeicher lässt sich nicht öffnen"); return false; }
  e = nvs_erase_key(h, "web_pw");
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) { err(fehler, fehlerLen, "Das Passwort lässt sich nicht löschen"); return false; }   // nicht vorhanden = auch gut
  return true;
}

bool werksreset(char* fehler, size_t fehlerLen) {
  nvs_flash_deinit();
  if (nvs_flash_erase() != ESP_OK) { err(fehler, fehlerLen, "Einstellungsspeicher lässt sich nicht löschen"); return false; }
  return true;
}
