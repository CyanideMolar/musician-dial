#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "ST7701S.h"
#include "CST820.h"

extern lv_display_t *disp;

void LVGL_Init(void);

// Called with true for an upward swipe, false for downward. Tracked from
// raw touch coordinates in the indev read callback rather than LVGL's
// built-in gesture system, which turned out to have several undocumented
// preconditions (touch must not be claimed by scroll handling anywhere in
// the object's ancestor chain, a minimum distance/velocity, etc.) that
// made it unreliable on this hardware.
typedef void (*lvgl_swipe_cb_t)(bool swipe_up);
void LVGL_Driver_SetSwipeCallback(lvgl_swipe_cb_t cb);

// Same heuristic, horizontal axis. Whichever axis has the larger release-time
// delta (and clears the threshold) wins for a given gesture -- a vertical
// drag never also fires this, and vice versa. Called with true for a
// right-to-left swipe, false for left-to-right.
typedef void (*lvgl_horizontal_swipe_cb_t)(bool swipe_left);
void LVGL_Driver_SetHorizontalSwipeCallback(lvgl_horizontal_swipe_cb_t cb);
