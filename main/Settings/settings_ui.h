#pragma once

#include "lvgl.h"
#include <stdbool.h>

// Builds the Settings tile's 3 sub-pages (landing/gear, Wi-Fi Configuration,
// Guitar Vault Configuration) as children of parent. Call once, after
// WiFiProvisioning_Init() and Pairing_Init() (it creates their UIs on its
// own sub-pages).
void SettingsUI_Create(lv_obj_t *parent);

// true = swipe right-to-left (next page), false = left-to-right (prev page).
// Cycles landing -> Wi-Fi -> Guitar Vault, wrapping in both directions.
void SettingsUI_HandleSwipe(bool swipe_left);

// Jumps back to the landing/gear page. Call whenever the Settings tile
// becomes the active top-level tile, so it always starts there.
void SettingsUI_ResetToLanding(void);
