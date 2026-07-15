#pragma once

#include "lvgl.h"

// Builds the Guitar Vault pairing widgets as children of parent (the same
// tile WiFiProvisioningUI_Create builds on). Call once, after Pairing_Init().
void PairingUI_Create(lv_obj_t *parent);

// Call every iteration of the main loop. Hides itself entirely unless WiFi
// is currently connected (see WiFiProvisioning_GetState()).
void PairingUI_Tick(void);
