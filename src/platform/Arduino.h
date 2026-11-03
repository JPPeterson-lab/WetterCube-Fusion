// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#pragma once
// Kleine eigene Ersatzschicht für die wenigen Hilfsfunktionen, die der Code aus der Arduino-Welt kennt (millis, delay, ESP.*, String).
// Reiner Eigencode auf ESP-IDF; die Arduino-ESP32-Komponente ist nicht mehr Teil des Projekts. ArduinoJson findet über diese Datei
// "String" (ARDUINOJSON_ENABLE_ARDUINO_STRING).
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <algorithm>
#include <type_traits>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "fusion_string.h"

#define PROGMEM
#define FPSTR(p) (p)

static inline unsigned long millis() { return (unsigned long)(esp_timer_get_time() / 1000); }
static inline void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

template <class A, class B> static inline typename std::common_type<A, B>::type min(A a, B b) { return a < b ? (typename std::common_type<A, B>::type)a : (typename std::common_type<A, B>::type)b; }
template <class A, class B> static inline typename std::common_type<A, B>::type max(A a, B b) { return a > b ? (typename std::common_type<A, B>::type)a : (typename std::common_type<A, B>::type)b; }
template <class T, class L, class H> static inline T constrain(T x, L lo, H hi) { return x < (T)lo ? (T)lo : (x > (T)hi ? (T)hi : x); }

// Speicher- und Neustart-Abfragen (interner RAM bzw. PSRAM)
struct EspKlasse {
  void restart() { esp_restart(); }
  uint32_t getFreeHeap() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
  uint32_t getMinFreeHeap() { return (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL); }
  uint32_t getMaxAllocHeap() { return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }
  uint32_t getPsramSize() { return (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM); }
  uint32_t getFreePsram() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
  uint32_t getMinFreePsram() { return (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM); }
  uint32_t getMaxAllocPsram() { return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM); }
};
inline EspKlasse ESP;

// Lokale Zeit; wartet bis zu ms Millisekunden auf eine gültige Uhrzeit (nach SNTP-Abgleich)
bool getLocalTime(struct tm* info, uint32_t ms = 5000);
