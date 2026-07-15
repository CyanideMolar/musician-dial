#include "LVGL_Driver.h"
#include "esp_heap_caps.h"
#include <assert.h>

static const char *TAG = "LVGL";

lv_display_t *disp = NULL;
static lv_indev_t *indev = NULL;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static uint32_t lvgl_tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static lvgl_swipe_cb_t s_swipe_cb = NULL;
static bool s_touch_was_down = false;
static int32_t s_touch_start_y = 0;
static int32_t s_touch_last_y = 0;

#define SWIPE_MIN_DISTANCE_PX 60

void LVGL_Driver_SetSwipeCallback(lvgl_swipe_cb_t cb)
{
    s_swipe_cb = cb;
}

static void touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x[1] = { 0 };
    uint16_t y[1] = { 0 };
    uint8_t count = 0;

    esp_lcd_touch_handle_t touch = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(touch);
    bool pressed = esp_lcd_touch_get_coordinates(touch, x, y, NULL, &count, 1);

    if (pressed && count > 0) {
        data->point.x = x[0];
        data->point.y = y[0];
        data->state = LV_INDEV_STATE_PRESSED;

        if (!s_touch_was_down) {
            s_touch_was_down = true;
            s_touch_start_y = y[0];
        }
        s_touch_last_y = y[0];
    } else {
        if (s_touch_was_down) {
            s_touch_was_down = false;
            int32_t dy = s_touch_last_y - s_touch_start_y;
            if (s_swipe_cb && (dy >= SWIPE_MIN_DISTANCE_PX || dy <= -SWIPE_MIN_DISTANCE_PX)) {
                s_swipe_cb(dy < 0);
            }
        }
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void LVGL_Init(void)
{
    ESP_LOGI(TAG, "initializing LVGL");
    lv_init();

    disp = lv_display_create(LCD_H_RES, LCD_V_RES);

    // Separate draw buffers in PSRAM + PARTIAL mode: LVGL composes only the
    // actual dirty rectangle, not the whole 480x480 screen every frame, and
    // the panel's own single frame buffer + bounce buffer (see LCD_Init)
    // handles getting that region onto the display without tearing. This is
    // the pairing Espressif's RGB-panel examples use; asking the panel for
    // its own internal frame buffer here (the previous approach) only makes
    // sense with 2 full frame buffers and no bounce buffer, a different,
    // non-PARTIAL configuration.
    size_t buffer_size_bytes = LCD_H_RES * LCD_V_RES * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(buffer_size_bytes, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(buffer_size_bytes, MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);
    lv_display_set_buffers(disp, buf1, buf2, buffer_size_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_user_data(disp, panel_handle);

    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read_cb);
    lv_indev_set_user_data(indev, tp);
    // LVGL ties the indev read timer to LV_DEF_REFR_PERIOD by default (5ms
    // here, for smooth animation); that was slowed to 20ms because each
    // touch read's I2C transactions were delaying beat animations behind
    // the audio click. That turned out to actually be a render-pipeline
    // buffer misconfiguration (since fixed -- see LCD_Init/this file's
    // history), not fundamentally the touch polling rate, and 20ms is too
    // coarse for swipe gestures: too few position samples for the tileview
    // to reliably recognize a drag, and a visibly choppy pan even when it
    // does. 10ms is a middle ground -- twice the sampling of the value that
    // caused the beat-lag regression, half of the 5ms default.
    lv_timer_set_period(lv_indev_get_read_timer(indev), 10);

    lv_tick_set_cb(lvgl_tick_cb);
}
