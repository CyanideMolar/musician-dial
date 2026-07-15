#pragma once

#include "lvgl.h"

// Builds the Circle of Fifths screen as a child of parent (a tileview tile).
// Call once after LVGL_Init().
void CircleOfFifthsUI_Create(lv_obj_t *parent);
