#pragma once

#include "lvgl.h"
#include <stdbool.h>

// Builds the Circle of Fifths screen as a child of parent (a tileview tile).
// Call once after LVGL_Init().
void CircleOfFifthsUI_Create(lv_obj_t *parent);

// Toggles between the selected key's major and minor scale/chords. Either
// swipe direction just flips it -- there's no separate "major direction"
// vs. "minor direction".
void CircleOfFifthsUI_HandleSwipe(bool swipe_left);
