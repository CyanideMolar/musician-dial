#pragma once

#include "lvgl.h"
#include <stdbool.h>

// Builds a full-screen loading overlay covering every tile. Call once, as
// the very last *_Create() in app_main -- LVGL draws siblings in creation
// order, and this needs to render on top of every tile underneath.
void LoadingUI_Create(lv_obj_t *parent);

// True until the loading window has elapsed (see LoadingUI_Tick). Main.c
// gates tile-swipe routing on this the same way it does on
// MetronomeUI_IsModalOpen().
bool LoadingUI_IsActive(void);

// Call every main-loop iteration. Hides the overlay for good once the
// loading window elapses; a no-op afterwards.
void LoadingUI_Tick(void);
