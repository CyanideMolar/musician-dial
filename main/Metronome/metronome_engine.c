#include "metronome_engine.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "Buzzer.h"

static const char *TAG = "Metronome";

#define BUZZER_CLICK_US       30000  // regular beat: how long the buzzer stays on
#define BUZZER_DBL_CLICK_US   20000  // downbeat: on-time for each of the two clicks
#define BUZZER_DBL_GAP_US     40000  // downbeat: silent gap between the two clicks
#define TAP_MAX_GAP_US    2000000 // taps further apart than this start a new tap sequence
#define TAP_HISTORY       12      // average over the last N button presses

/* The buzzer only has on/off, no pitch control (it's driven through the
 * TCA9554 IO-expander over I2C, not a PWM-capable GPIO), so the downbeat is
 * made to stand out with a double-click instead of a different tone. Steps
 * run one at a time via a single one-shot timer -- can't just vTaskDelay
 * between clicks since this all happens inside an esp_timer callback,
 * which would block the shared esp_timer service task. */
typedef struct {
    bool buzzer_on;
    uint32_t hold_us; // 0 marks the last step
} click_step_t;

static const click_step_t CLICK_SINGLE[] = {
    { true, BUZZER_CLICK_US },
    { false, 0 },
};
static const click_step_t CLICK_DOUBLE[] = {
    { true, BUZZER_DBL_CLICK_US },
    { false, BUZZER_DBL_GAP_US },
    { true, BUZZER_DBL_CLICK_US },
    { false, 0 },
};

static esp_timer_handle_t s_beat_timer = NULL;
static esp_timer_handle_t s_click_timer = NULL;
static const click_step_t *s_click_seq = NULL;
static int s_click_idx = 0;
static QueueHandle_t s_beat_queue = NULL;

static uint16_t s_bpm = 120;
static uint8_t s_beats_per_measure = 4;
static uint8_t s_current_beat = 0;
static bool s_running = false;

static int64_t s_last_tap_us = 0;
static int64_t s_tap_times[TAP_HISTORY]; // ring buffer of press timestamps, not intervals
static int s_tap_count = 0;              // total presses in the current sequence (uncapped)

static uint64_t period_us(void)
{
    return 60000000ULL / s_bpm;
}

static void click_step_cb(void *arg)
{
    const click_step_t *step = &s_click_seq[s_click_idx];
    if (step->buzzer_on) {
        Buzzer_On();
    } else {
        Buzzer_Off();
    }
    if (step->hold_us == 0) {
        return; // sequence finished
    }
    s_click_idx++;
    esp_timer_start_once(s_click_timer, step->hold_us);
}

static void start_click_sequence(const click_step_t *seq)
{
    esp_timer_stop(s_click_timer); // ignore ESP_ERR_INVALID_STATE if not running
    s_click_seq = seq;
    s_click_idx = 0;
    click_step_cb(NULL); // apply the first step now; it schedules the rest
}

static void beat_timer_cb(void *arg)
{
    metronome_beat_event_t evt = {
        .beat_index = s_current_beat,
        .is_downbeat = (s_current_beat == 0),
        .beat_period_ms = (uint16_t)(period_us() / 1000),
    };
    xQueueSend(s_beat_queue, &evt, 0);

    start_click_sequence(evt.is_downbeat ? CLICK_DOUBLE : CLICK_SINGLE);

    s_current_beat = (s_current_beat + 1) % s_beats_per_measure;
}

void Metronome_Init(void)
{
    s_beat_queue = xQueueCreate(4, sizeof(metronome_beat_event_t));

    const esp_timer_create_args_t beat_args = {
        .callback = &beat_timer_cb,
        .name = "metronome_beat",
    };
    ESP_ERROR_CHECK(esp_timer_create(&beat_args, &s_beat_timer));

    const esp_timer_create_args_t click_args = {
        .callback = &click_step_cb,
        .name = "metronome_click",
    };
    ESP_ERROR_CHECK(esp_timer_create(&click_args, &s_click_timer));
}

QueueHandle_t Metronome_GetEventQueue(void)
{
    return s_beat_queue;
}

void Metronome_Start(void)
{
    if (s_running) {
        return;
    }
    s_current_beat = 0;
    s_running = true;
    // esp_timer_start_periodic's first callback only fires after one full
    // period, which at slow BPMs is a very audible delay before the first
    // click -- exactly wrong for cueing off a beat 1 you're already
    // hearing elsewhere. Fire beat 1 synchronously right now, then let the
    // periodic timer pick up beat 2 onward at the normal spacing.
    beat_timer_cb(NULL);
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_beat_timer, period_us()));
    ESP_LOGI(TAG, "started at %u BPM", s_bpm);
}

void Metronome_Stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;
    esp_timer_stop(s_beat_timer);
    esp_timer_stop(s_click_timer);
    Buzzer_Off();
    ESP_LOGI(TAG, "stopped");
}

bool Metronome_IsRunning(void)
{
    return s_running;
}

void Metronome_SetBPM(uint16_t bpm)
{
    if (bpm < METRONOME_BPM_MIN) bpm = METRONOME_BPM_MIN;
    if (bpm > METRONOME_BPM_MAX) bpm = METRONOME_BPM_MAX;
    s_bpm = bpm;
    if (s_running) {
        esp_timer_stop(s_beat_timer);
        ESP_ERROR_CHECK(esp_timer_start_periodic(s_beat_timer, period_us()));
    }
}

uint16_t Metronome_GetBPM(void)
{
    return s_bpm;
}

void Metronome_SetBeatsPerMeasure(uint8_t beats)
{
    if (beats < 1) beats = 1;
    if (beats > 12) beats = 12;
    s_beats_per_measure = beats;
    s_current_beat = 0;
}

uint8_t Metronome_GetBeatsPerMeasure(void)
{
    return s_beats_per_measure;
}

void Metronome_TapTempo(void)
{
    int64_t now = esp_timer_get_time();

    if (s_last_tap_us == 0 || (now - s_last_tap_us) >= TAP_MAX_GAP_US) {
        s_tap_count = 0; // gap too long (or first tap ever) -- start a new sequence
    }
    s_last_tap_us = now;

    s_tap_times[s_tap_count % TAP_HISTORY] = now;
    s_tap_count++;

    // Average across however many of the last TAP_HISTORY presses are
    // available. The interval sum telescopes to just (newest - oldest) /
    // (n - 1), so there's no need to track individual intervals.
    int n = s_tap_count < TAP_HISTORY ? s_tap_count : TAP_HISTORY;
    if (n >= 2) {
        int oldest_slot = (s_tap_count - n) % TAP_HISTORY;
        int64_t oldest_us = s_tap_times[oldest_slot];
        int64_t avg_us = (now - oldest_us) / (n - 1);
        Metronome_SetBPM((uint16_t)(60000000LL / avg_us));
    }
}
