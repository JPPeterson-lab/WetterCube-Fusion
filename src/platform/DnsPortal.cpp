// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "DnsPortal.h"
#include <string.h>
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static volatile bool s_laeuft = false;
static TaskHandle_t s_task = nullptr;
static uint32_t s_ip = 0;

static void dnsTask(void*) {
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (s < 0) { s_laeuft = false; s_task = nullptr; vTaskDelete(nullptr); return; }
  struct sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(53); a.sin_addr.s_addr = htonl(INADDR_ANY);
  bind(s, (struct sockaddr*)&a, sizeof(a));
  struct timeval tv = {1, 0}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  uint8_t buf[512];
  while (s_laeuft) {
    struct sockaddr_in von; socklen_t vl = sizeof(von);
    int n = recvfrom(s, buf, sizeof(buf) - 16, 0, (struct sockaddr*)&von, &vl);
    if (n < 12) continue;
    // Frage bis zum Ende des Namens + 4 Byte (Typ, Klasse) übernehmen
    int i = 12; while (i < n && buf[i] != 0) i += buf[i] + 1;
    int ende = i + 5;
    if (ende > n) continue;
    buf[2] = 0x81; buf[3] = 0x80;                    // Antwort, keine Fehler
    buf[6] = 0; buf[7] = 1; buf[8] = buf[9] = buf[10] = buf[11] = 0;   // eine Antwort
    int p = ende;
    buf[p++] = 0xC0; buf[p++] = 0x0C;                // Name: Zeiger auf die Frage
    buf[p++] = 0; buf[p++] = 1; buf[p++] = 0; buf[p++] = 1;   // Typ A, Klasse IN
    buf[p++] = 0; buf[p++] = 0; buf[p++] = 0; buf[p++] = 30;  // TTL 30 s
    buf[p++] = 0; buf[p++] = 4;
    memcpy(buf + p, &s_ip, 4); p += 4;
    sendto(s, buf, p, 0, (struct sockaddr*)&von, vl);
  }
  close(s);
  s_task = nullptr;
  vTaskDelete(nullptr);
}

void dnsPortalStart(uint32_t ipLwip) {
  if (s_laeuft) return;
  s_ip = ipLwip; s_laeuft = true;
  xTaskCreate(dnsTask, "dns", 3072, nullptr, 3, &s_task);
}
void dnsPortalStop() { s_laeuft = false; }
