#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define METRONOME_BPM_MIN   30
#define METRONOME_BPM_MAX   300

typedef struct {
    uint8_t beat_index;      // 0-based position within the measure
    bool is_downbeat;        // true when beat_index == 0
    uint16_t beat_period_ms; // current ms-per-beat, for the UI to time its animation
} metronome_beat_event_t;

// Sets up timers and the beat-event queue. Call once at boot.
void Metronome_Init(void);

// Beat events are posted here from the timer callback; the UI task drains it
// so all LVGL calls stay on the one task that owns the display.
QueueHandle_t Metronome_GetEventQueue(void);

void Metronome_Start(void);
void Metronome_Stop(void);
bool Metronome_IsRunning(void);

void Metronome_SetBPM(uint16_t bpm);
uint16_t Metronome_GetBPM(void);

void Metronome_SetBeatsPerMeasure(uint8_t beats);
uint8_t Metronome_GetBeatsPerMeasure(void);

// Call once per physical tap; after two or more taps within 2s of each
// other it derives a BPM from the average interval and applies it.
void Metronome_TapTempo(void);
