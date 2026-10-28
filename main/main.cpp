// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
// WetterCube Fusion — Einstieg (reines ESP-IDF, ohne Arduino-ESP32): app_main() initialisiert den Einstellungsspeicher und startet die Tasks
#include "src/platform/Arduino.h"
#include "src/display/lvgl_port.h"
#include "src/board/board.h"
#include "src/app/navigation.h"
#include "src/app/app_state.h"
#include "src/app/next_geraete.h"
#include "src/app/homeassistant.h"
#include "src/ui/ui.h"
#include "src/sensors/sensors.h"
#include "src/storage/storage.h"
#include "nvs_flash.h"

static void memLog(const char* wo)
{
    printf("[Mem] %-14s SRAM frei %6u (groesster Block %6u) | PSRAM frei %u\n", wo,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void starten()
{
    printf("\n[WetterCube Fusion] Start v" FIRMWARE_VERSION "\n");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);   // früh setzen, damit die Zeitstempel im Log lokal sind
    tzset();
    spurBeimStart();
    notfallZaehlen();
    resetGrundText = resetGrundName(esp_reset_reason());
    logDiag("[Boot] v%s, Neustart-Grund: %s", FIRMWARE_VERSION, resetGrundText.c_str());
    if (spurVorAbsturz.length()) logDiag("[Boot] Zuletzt aktiv vor dem Neustart: %s", spurVorAbsturz.c_str());
    absturzInfoMelden();   // Ursache und Rücksprünge des letzten Absturzes ins Log
    ladeCfg();
    haInit();         // Home Assistant: "Letzter Wille" des MQTT-Clients setzen (vor dem Start des Clients)
    sensorenInit();   // Sensorliste laden, interner Sensor, MQTT
    sensorAlarmInit();
    memLog("Start");

    esp_lcd_touch_handle_t tp    = boardTouchInit();               // initialisiert auch I2C (und die IO-Erweiterung des S3-Boards)
    nextGeraeteInit();                                             // Liste der Next-Displays laden
    speicherInit();                                                // Speicherkarte (optional): Dauer-Log und Verlauf
    memLog("nach Touch");
    esp_lcd_panel_handle_t panel = boardPanelInit();
    memLog("nach Panel");

    // Framebuffer schwarz vorbelegen (kein Artefakt-Flash beim Start)
    void *fb = nullptr;
    boardFramebuffer(&fb);
    if (fb) memset(fb, 0, BOARD_LCD_H_RES * BOARD_LCD_V_RES * BOARD_LCD_BYTES_PRO_PIXEL);

    boardBacklightAn();
    ESP_ERROR_CHECK(lvgl_port_init(panel, tp));
    memLog("nach LVGL-Port");

    if (lvgl_port_lock(-1)) {
#if LV_USE_PNG
        lv_png_init();                       // für spätere Warnkarte (PNG vom DWD-WMS)
#endif
        ui_init();                           // erzeugt alle Screens (lädt kurz Biowetter)
        nav_init();
        uiWarnInit();
        uiMenuInit();
        uiTimerInit();
        uiSensorenInit();
        uiLuftInit();
        uiLueftenInit();
        uiNachtInit();
        uiSystemInit();
        uiVerlaufInit();
        uiHistorieInit();
        notfallInit();
        erstelleUpdateBadge();
        nav_goto(SCREEN_ID_SCREENBOOT);      // Start immer über den Bootscreen
        lvgl_port_unlock();
    }

    memLog("nach UI");
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("[Mem] LVGL-Speicher: %u von %u Bytes belegt\n", (unsigned)(mon.total_size - mon.free_size), (unsigned)mon.total_size);
    logDiag("[Boot] LVGL-Speicher nach dem Start: frei %u von %u Bytes (%u %% belegt), SRAM frei %u",
            (unsigned)mon.free_size, (unsigned)mon.total_size, (unsigned)mon.used_pct, (unsigned)ESP.getFreeHeap());

    appStart();   // Netzwerk-Task auf Core 0: WLAN, Daten, WebUI
}

// Einstieg: Einstellungsspeicher bereitmachen, danach alles Weitere (alles läuft in Tasks: LVGL auf Core 1, Netzwerk/Daten auf Core 0)
extern "C" void app_main(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    starten();
}
