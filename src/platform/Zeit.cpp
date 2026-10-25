// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Uhrzeit: SNTP-Abgleich und lokale Zeit
#include "Arduino.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

bool getLocalTime(struct tm* info, uint32_t ms) {
  uint32_t t0 = millis();
  for (;;) {
    time_t jetzt; time(&jetzt);
    localtime_r(&jetzt, info);
    if (info->tm_year > (2016 - 1900)) return true;
    if (millis() - t0 >= ms) return false;
    delay(10);
  }
}

// Startet den Abgleich mit mehreren Zeitservern (die Zeitzone setzt der Aufrufer per setenv("TZ") und tzset())
void zeitServerStarten() {
  static bool laeuft = false;
  if (laeuft) return;
  laeuft = true;
  esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.cloudflare.com", "time.google.com"));
  esp_netif_sntp_init(&c);
}
