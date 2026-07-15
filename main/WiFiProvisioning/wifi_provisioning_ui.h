#pragma once

#include "lvgl.h"

// Builds the Guitar Vault / WiFi-status screen as a child of parent (a
// tileview tile). Call once after LVGL_Init() and WiFiProvisioning_Init().
void WiFiProvisioningUI_Create(lv_obj_t *parent);

// Call every iteration of the main loop (see main.c). Only touches widgets
// when the underlying state actually changed since the last tick.
void WiFiProvisioningUI_Tick(void);
