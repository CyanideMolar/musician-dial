#pragma once

#include "lvgl.h"
#include <stdbool.h>

// Builds the guitar-carousel widgets (and an inactive-state hint pointing at
// the Settings tile). Call once, after GuitarCollection_Init().
void GuitarCollectionUI_Create(lv_obj_t *parent);

// Call every main-loop iteration. Triggers a list refresh on the transition
// into "paired," and updates the displayed card's name text.
void GuitarCollectionUI_Tick(void);

// True once the carousel is the active, visible content (paired + at least
// attempted a load) -- main.c gates horizontal-swipe routing on this.
bool GuitarCollectionUI_IsActive(void);

// True while the end-of-practice-session review dialog is open. main.c gates
// both swipe directions on this, same as MetronomeUI_IsModalOpen().
bool GuitarCollectionUI_IsModalOpen(void);

// true = swipe right-to-left (next card), false = left-to-right (prev card).
void GuitarCollectionUI_HandleSwipe(bool swipe_left);
