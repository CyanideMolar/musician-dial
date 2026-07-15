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

#include "lvgl.h"

static lv_obj_t *s_tile_metronome;
static lv_obj_t *s_tile_cof;

// Two full-screen "apps," swapped by a swipe -- detected from raw touch
// coordinates (see LVGL_Driver's swipe callback), not LVGL's built-in
// gesture system, and not an lv_tileview. A sliding pan has to render
// pieces of both screens on every frame while dragging, and measured on
// this hardware (no 2D acceleration) at 150-220ms per frame, mostly LVGL's
// own software compositing rather than the panel write itself. An instant
// cut only ever renders one full screen at a time (~40-60ms).
static void on_swipe(bool swipe_up)
{
    (void)swipe_up; // only 2 screens, so either direction just toggles
    if (MetronomeUI_IsModalOpen()) {
        // Don't let a drag across the BPM-entry keypad switch screens.
        return;
    }
    bool showing_metronome = !lv_obj_has_flag(s_tile_metronome, LV_OBJ_FLAG_HIDDEN);
    if (showing_metronome) {
        lv_obj_add_flag(s_tile_metronome, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_tile_cof, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_tile_cof, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_tile_metronome, LV_OBJ_FLAG_HIDDEN);
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

    lv_obj_t *root_screen = lv_screen_active();
    lv_obj_set_style_bg_color(root_screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(root_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_tile_metronome = make_screen_container(root_screen);
    s_tile_cof = make_screen_container(root_screen);
    lv_obj_add_flag(s_tile_cof, LV_OBJ_FLAG_HIDDEN);

    Metronome_Init();
    MetronomeUI_Create(s_tile_metronome);
    CircleOfFifthsUI_Create(s_tile_cof);

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

        lv_timer_handler();
    }
}
