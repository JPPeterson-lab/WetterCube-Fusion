// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// Absturz-Aufzeichnung: Bei einer Panic (Ausnahme, abort(), Watchdog) merkt sich ein eingehaengter Handler Ursache, Aufgabe,
// Adresse und die Rueckspruenge im RTC-Speicher, der den Neustart uebersteht. Beim naechsten Start landet alles im Log
// (und damit auf der Speicherkarte). Die Zahlen lassen sich mit addr2line gegen die passende .elf-Datei in Zeilen uebersetzen:
//   xtensa-esp32s3-elf-addr2line -pfiaC -e releases-elf/wettercube_fusion-<Version>.elf <Adressen>    (ESP32-S3)
//   riscv32-esp-elf-addr2line    -pfiaC -e releases-elf/wettercube_fusion-<Version>.elf <Adressen>    (ESP32-P4)
// Eingehaengt wird per Linker-Option --wrap=esp_panic_handler (main/CMakeLists.txt).
#include "app_state.h"
#include "esp_attr.h"
#include "esp_debug_helpers.h"
#include "esp_private/panic_internal.h"
#if CONFIG_IDF_TARGET_ARCH_XTENSA
#include "xtensa_context.h"
#else
#include "riscv/rvruntime-frames.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct AbsturzInfo { uint32_t magic; uint32_t pc; uint32_t exccause; uint32_t n; uint32_t pcs[16]; char grund[40]; char aufgabe[20]; char abort[64]; };
static RTC_NOINIT_ATTR AbsturzInfo s_info;
static const uint32_t MAGIC = 0xC0DE5A17;

extern "C" void __real_esp_panic_handler(panic_info_t* info);

// Kleine eigene Kopierfunktion: im Panic-Zustand lieber nichts aufrufen, was im Flash liegen koennte
static IRAM_ATTR void kopieren(char* ziel, size_t n, const char* quelle) {
  size_t i = 0;
  if (quelle) for (; quelle[i] && i < n - 1; i++) ziel[i] = quelle[i];
  ziel[i] = 0;
}

extern "C" IRAM_ATTR void __wrap_esp_panic_handler(panic_info_t* info) {
  AbsturzInfo& a = s_info;
  a.magic = MAGIC; a.pc = (uint32_t)(uintptr_t)info->addr; a.exccause = 0; a.n = 0; a.abort[0] = 0;
  kopieren(a.grund, sizeof(a.grund), info->reason);
  TaskHandle_t t = xTaskGetCurrentTaskHandle();
  kopieren(a.aufgabe, sizeof(a.aufgabe), t ? pcTaskGetName(t) : "?");
  if (g_panic_abort) kopieren(a.abort, sizeof(a.abort), g_panic_abort_details);
  if (info->frame) {
#if CONFIG_IDF_TARGET_ARCH_XTENSA
    XtExcFrame* xf = (XtExcFrame*)info->frame;
    a.exccause = xf->exccause;
    esp_backtrace_frame_t f = {.pc = (uint32_t)xf->pc, .sp = (uint32_t)xf->a1, .next_pc = (uint32_t)xf->a0, .exc_frame = xf};
    do { a.pcs[a.n++] = f.pc; } while (a.n < 16 && f.next_pc != 0 && esp_backtrace_get_next_frame(&f));
#else
    // RISC-V: Programmzaehler und Ruecksprungadresse aus dem Ausnahme-Rahmen (eine volle Rueckverfolgung ohne Rahmenzeiger ist unsicher)
    RvExcFrame* rf = (RvExcFrame*)info->frame;
    a.exccause = rf->mcause;
    a.pcs[a.n++] = (uint32_t)rf->mepc;
    if (rf->ra) a.pcs[a.n++] = (uint32_t)rf->ra;
#endif
  }
  __real_esp_panic_handler(info);       // druckt wie gewohnt und startet neu
}

// Beim Start: Aufzeichnung des letzten Absturzes ins Log schreiben und loeschen
void absturzInfoMelden() {
  AbsturzInfo& a = s_info;
  if (a.magic != MAGIC) return;
  a.magic = 0;
  a.grund[sizeof(a.grund) - 1] = 0; a.aufgabe[sizeof(a.aufgabe) - 1] = 0; a.abort[sizeof(a.abort) - 1] = 0;
  logDiag("[Absturz] %s%s%s, Aufgabe '%s', Adresse 0x%08x, Ausnahme %u", a.grund, a.abort[0] ? ": " : "", a.abort, a.aufgabe, (unsigned)a.pc, (unsigned)a.exccause);
  char z[200]; z[0] = 0; size_t l = 0;
  for (uint32_t i = 0; i < a.n && i < 16 && l < sizeof(z) - 14; i++) l += snprintf(z + l, sizeof(z) - l, "0x%08x ", (unsigned)a.pcs[i]);
  if (a.n) logDiag("[Absturz] Rücksprünge: %s", z);
}
