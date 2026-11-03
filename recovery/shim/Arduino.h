#pragma once
// Kleine Ersatz-Kopfdatei: die gemeinsam genutzten Hardware-Dateien (src/hardware, src/display) brauchen nur delay(), millis() und min()
#include <stdint.h>
#include <algorithm>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
static inline void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static inline unsigned long millis() { return (unsigned long)(esp_timer_get_time() / 1000); }
using std::min;
using std::max;

#include "driver/gpio.h"
#define INPUT 0
#define OUTPUT 1
#define HIGH 1
#define LOW 0
static inline void pinMode(int pin, int mode) { gpio_reset_pin((gpio_num_t)pin); gpio_set_direction((gpio_num_t)pin, mode == OUTPUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT); }
static inline void digitalWrite(int pin, int v) { gpio_set_level((gpio_num_t)pin, v); }
