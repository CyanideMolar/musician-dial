#pragma once

#include "lvgl.h"

// Spawns a low-priority background task that blocks on stdin (the USB
// Serial/JTAG console -- see CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG in
// sdkconfig.defaults) waiting for a "SCREENSHOT\n" line from the host. Call
// once during startup.
void Screenshot_Init(void);

// Call once per app_main loop iteration, passing the currently active
// screen. If a trigger arrived since the last call, this takes an LVGL
// snapshot of it and prints it as base64-encoded RGB565 framed with
// ---SCREENSHOT-BEGIN/END--- markers. Must run on the same task that owns
// LVGL (app_main's loop) -- lv_snapshot_take() touches rendering internals
// that aren't safe to call from the stdin-reader task itself.
void Screenshot_CheckAndCapture(lv_obj_t *screen);
