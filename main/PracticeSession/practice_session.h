#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PRACTICE_STATE_IDLE,   // no session running
    PRACTICE_STATE_ACTIVE, // timing a session, one guitar "current" at a time
    PRACTICE_STATE_REVIEW, // ended, awaiting a rating -> Log or Discard
} practice_state_t;

void PracticeSession_Init(void);

practice_state_t PracticeSession_GetState(void);

// Starts a new session with this guitar as the current one. No-op unless IDLE.
void PracticeSession_Start(const char *guitar_id, const char *guitar_name);

// True if guitar_id is the session's current guitar. Only meaningful while
// ACTIVE -- drives the End Practice vs. Switch Guitar button choice in the UI.
bool PracticeSession_IsCurrentGuitar(const char *guitar_id);

// Finalizes the current guitar's elapsed time into its running total, then
// makes guitar_id/guitar_name the new current guitar. No-op if guitar_id is
// already current, or the session isn't ACTIVE.
void PracticeSession_SwitchGuitar(const char *guitar_id, const char *guitar_name);

// Live total across every segment (completed + the currently-running one).
// Valid while ACTIVE or REVIEW.
uint32_t PracticeSession_GetTotalElapsedSeconds(void);

// Finalizes the current segment and freezes all totals. ACTIVE -> REVIEW.
void PracticeSession_End(void);

int PracticeSession_GetSegmentCount(void);
bool PracticeSession_GetSegment(int index, char *name_out, size_t name_len, uint32_t *seconds_out);

// "Forget Session" -- clears everything. REVIEW -> IDLE.
void PracticeSession_Discard(void);

// POSTs the frozen segments + rating to Guitar Vault via the stored API key,
// in a background task. REVIEW -> IDLE once the request completes, whether
// it succeeded or not (no retry flow here).
void PracticeSession_Log(uint8_t rating);
