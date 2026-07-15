#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "ST7701S.h"
#include "CST820.h"
#include "LVGL_Driver.h"
#include "metronome_engine.h"
#include "metronome_ui.h"
#include "circle_of_fifths_ui.h"
#include "wifi_provisioning.h"
#include "wifi_provisioning_ui.h"
#include "pairing.h"
#include "pairing_ui.h"
#include "guitar_collection.h"
#include "guitar_collection_ui.h"
#include "settings_ui.h"
#include "loading_ui.h"
#include "screenshot.h"

#include "lvgl.h"

#define NUM_TILES 4

static lv_obj_t *s_tiles[NUM_TILES]; // metronome, circle of fifths, guitar vault, settings
static int s_active_tile;

// Full-screen "apps," swapped by a swipe -- detected from raw touch
// coordinates (see LVGL_Driver's swipe callback), not LVGL's built-in
// gesture system, and not an lv_tileview. A sliding pan has to render
// pieces of both screens on every frame while dragging, and measured on
// this hardware (no 2D acceleration) at 150-220ms per frame, mostly LVGL's
// own software compositing rather than the panel write itself. An instant
// cut only ever renders one full screen at a time (~40-60ms).
static void on_swipe(bool swipe_up)
{
    if (LoadingUI_IsActive() || MetronomeUI_IsModalOpen()) {
        // Don't let a drag across the boot loading screen (or the
        // BPM-entry keypad) switch screens. The raw-coordinate swipe
        // heuristic in LVGL_Driver.c doesn't know or care what's drawn on
        // top, so this has to be checked explicitly.
        return;
    }
    lv_obj_add_flag(s_tiles[s_active_tile], LV_OBJ_FLAG_HIDDEN);
    // Up = forward a tile, down = back a tile.
    s_active_tile = swipe_up ? (s_active_tile + 1) % NUM_TILES
                              : (s_active_tile - 1 + NUM_TILES) % NUM_TILES;
    lv_obj_remove_flag(s_tiles[s_active_tile], LV_OBJ_FLAG_HIDDEN);

    if (s_active_tile == 3) {
        // Always land on the gear/landing sub-page on entry, never wherever
        // it was last left mid-browse.
        SettingsUI_ResetToLanding();
    }
}

// Horizontal swipes mean something on the Guitar Vault tile (browse cards,
// once there's a carousel to browse) and the Settings tile (cycle its
// sub-pages) -- everywhere else this is a no-op, same guard-clause shape as
// MetronomeUI_IsModalOpen() above.
static void on_horizontal_swipe(bool swipe_left)
{
    if (LoadingUI_IsActive()) {
        return;
    }
    if (s_active_tile == 2 && GuitarCollectionUI_IsActive()) {
        GuitarCollectionUI_HandleSwipe(swipe_left);
    } else if (s_active_tile == 3) {
        SettingsUI_HandleSwipe(swipe_left);
    }
}

static lv_obj_t *make_screen_container(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

void app_main(void)
{
    I2C_Init();
    ESP_ERROR_CHECK(EXIO_Init());

    LCD_Init();
    Touch_Init();
    LVGL_Init();

    LVGL_Driver_SetSwipeCallback(on_swipe);
    LVGL_Driver_SetHorizontalSwipeCallback(on_horizontal_swipe);

    lv_obj_t *root_screen = lv_screen_active();
    lv_obj_set_style_bg_color(root_screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(root_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_tiles[0] = make_screen_container(root_screen);
    s_tiles[1] = make_screen_container(root_screen);
    s_tiles[2] = make_screen_container(root_screen);
    s_tiles[3] = make_screen_container(root_screen);
    lv_obj_add_flag(s_tiles[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_tiles[2], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_tiles[3], LV_OBJ_FLAG_HIDDEN);
    s_active_tile = 0;

    Metronome_Init();
    MetronomeUI_Create(s_tiles[0]);
    CircleOfFifthsUI_Create(s_tiles[1]);
    GuitarCollectionUI_Create(s_tiles[2]);
    SettingsUI_Create(s_tiles[3]);
    // Created last so it renders on top of every tile (LVGL draws siblings
    // in creation order) -- masks the boot flicker below for its first 5s.
    LoadingUI_Create(root_screen);
    Screenshot_Init();

    // Render the first real frame before starting WiFi. The panel is already
    // active (LCD_Init, above) but lv_timer_handler() has never run yet, so
    // it's been scanning out whatever was in the framebuffer before LVGL drew
    // anything. WiFiProvisioning_Init() then does real synchronous work on
    // this same task (esp_wifi_init, PHY calibration) with no further
    // lv_timer_handler() call until the main loop starts below -- confirmed
    // on real hardware as visible flicker during the first several seconds
    // of boot. Drawing the metronome screen first, before any of that runs,
    // fixes the ordering bug regardless of how much of the flicker was this
    // vs. WiFi's own RF activity (outside software's control either way).
    lv_timer_handler();

    WiFiProvisioning_Init();
    Pairing_Init();
    GuitarCollection_Init();

    QueueHandle_t beat_queue = Metronome_GetEventQueue();
    metronome_beat_event_t evt;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5));

        // Drain beat events (LVGL calls aren't safe from the metronome's own
        // timer callback, so this is the only place that applies them)
        // BEFORE calling lv_timer_handler(), so a beat's visual change gets
        // drawn this pass instead of sitting invalidated until the next one.
        while (xQueueReceive(beat_queue, &evt, 0) == pdTRUE) {
            MetronomeUI_OnBeat(evt.beat_index, evt.is_downbeat, evt.beat_period_ms);
        }

        // Handled here, not inside Screenshot_Init's own task, since
        // lv_snapshot_take() touches LVGL rendering state that only this
        // loop is allowed to call into.
        Screenshot_CheckAndCapture(root_screen);
        WiFiProvisioningUI_Tick();
        PairingUI_Tick();
        GuitarCollectionUI_Tick();
        LoadingUI_Tick();

        lv_timer_handler();
    }
}
