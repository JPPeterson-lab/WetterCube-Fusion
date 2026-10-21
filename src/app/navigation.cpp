// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jan Althaus (JPPeterson-lab)
#include "navigation.h"
#include "app_state.h"

// ── Navigationsplan ───────────────────────────────────────────────────────────
//  Hauptkette (Kreis):
//    screen_1 ─> verlauf ─> biowetter ─> allergen ─> warnkarte1 ─> airquality ─> sensoren ─> screen_1
//  Warnkarte1 <-> Warnkarte2 über den Hub-Button
//  Home-Icons führen überall auf screen_1, fc_settings auf screenmenu
// ─────────────────────────────────────────────────────────────────────────────

void nav_goto(enum ScreensEnum id)
{
    lv_obj_t *scr = ((lv_obj_t **)&objects)[id - 1];
    if (scr) lv_scr_load(scr);
}

// Ein Callback für alle Ziele: das Ziel steckt im user_data
static void on_nav(lv_event_t *e)
{
    nav_goto((enum ScreensEnum)(intptr_t)lv_event_get_user_data(e));
}

static void link(lv_obj_t *btn, enum ScreensEnum target)
{
    if (!btn) return;
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, on_nav, LV_EVENT_CLICKED, (void *)(intptr_t)target);
}

// Absturz-Spur: welcher Screen war zuletzt aktiv (steht nach einem Absturz im /log)
static void screenSpur(lv_timer_t*)
{
    static lv_obj_t* letzter = nullptr;
    lv_obj_t* a = lv_scr_act();
    if (a == letzter) return;
    letzter = a;
    struct { lv_obj_t* o; const char* n; } t[] = {
        {objects.screen_1, "Hauptscreen"}, {objects.screenverlauf, "Temperaturverlauf"}, {objects.screenbiowetter, "Biowetter"},
        {objects.screenallergen, "Allergene"}, {objects.screenwarnkarte1, "Warnkarte"}, {objects.screenwarnkarte2, "Warnliste"},
        {objects.screenairquality, "Luftqualitaet"}, {objects.screensensoren, "Sensoren"}, {objects.screenmenu, "Menue"},
        {objects.screenwarnungpollen, "Pollenwarnung"}, {objects.screenwarnunglueften, "Lueftungswarnung"}, {objects.screennachtuhr, "Nacht-Uhr"},
    };
    char buf[48] = "Screen: anderer";
    for (auto &e : t) if (e.o && e.o == a) { snprintf(buf, sizeof(buf), "Screen: %s", e.n); break; }
    spur(buf);
}

void nav_init()
{
    lv_timer_create(screenSpur, 250, nullptr);
    // screen_1 (Startseite)
    link(objects.labelbuttonforward,       SCREEN_ID_SCREENVERLAUF);
    link(objects.labelbuttonbackward,      SCREEN_ID_SCREENSENSOREN);
    link(objects.fc_settings,              SCREEN_ID_SCREENMENU);

    // Temperaturverlauf
    link(objects.buttonverlaufzurueck,     SCREEN_ID_SCREEN_1);
    link(objects.buttonverlaufweiter,      SCREEN_ID_SCREENBIOWETTER);
    link(objects.buttonverlaufhome,        SCREEN_ID_SCREEN_1);

    // Biowetter
    link(objects.labelbuttonbackward_4,    SCREEN_ID_SCREENVERLAUF);
    link(objects.labelbuttonforward_6,     SCREEN_ID_SCREENALLERGEN);
    link(objects.labelbuttonhome_7,        SCREEN_ID_SCREEN_1);

    // Allergene (Pollen stündlich und 3 Tage; die Buttons heißen im PicoPixel-Entwurf noch button_1*)
    link(objects.button_1_1,               SCREEN_ID_SCREENBIOWETTER);
    link(objects.button_1,                 SCREEN_ID_SCREENWARNKARTE1);
    link(objects.button_1_2,               SCREEN_ID_SCREEN_1);

    // Warnkarte 1 und 2
    link(objects.labelbuttonbackward_1,    SCREEN_ID_SCREENALLERGEN);
    link(objects.labelbuttonforward_1,     SCREEN_ID_SCREENAIRQUALITY);
    link(objects.labelbuttonhome,          SCREEN_ID_SCREEN_1);
    link(objects.labelbuttonscreenhub,     SCREEN_ID_SCREENWARNKARTE2);
    link(objects.labelbuttonscreenhubback, SCREEN_ID_SCREENWARNKARTE1);
    link(objects.labelbuttonhome_1,        SCREEN_ID_SCREEN_1);

    // Luftqualität
    link(objects.labelbuttonbackward_2,    SCREEN_ID_SCREENWARNKARTE1);
    link(objects.labelbuttonforward_2,     SCREEN_ID_SCREENSENSOREN);
    link(objects.labelbuttonhome_2,        SCREEN_ID_SCREEN_1);

    // Sensoren
    link(objects.buttonsensorenzurueck,    SCREEN_ID_SCREENAIRQUALITY);
    link(objects.buttonsensorenweiter,     SCREEN_ID_SCREEN_1);
    link(objects.buttonsensorenhome,       SCREEN_ID_SCREEN_1);

    // Einstellungen
    link(objects.labelbuttonmenu_2,        SCREEN_ID_SCREEN_1);
}
