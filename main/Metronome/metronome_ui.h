#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

// Builds the metronome screen as a child of parent (a tileview tile). Call
// once after LVGL_Init().
void MetronomeUI_Create(lv_obj_t *parent);

// Call from the beat-event consumer (see main.c) whenever a beat fires.
void MetronomeUI_OnBeat(uint8_t beat_index, bool is_downbeat, uint16_t beat_period_ms);

// True while the BPM numeric-entry dialog is open, so main.c can ignore a
// swipe that started as a drag across the dialog's keypad.
bool MetronomeUI_IsModalOpen(void);
