#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    PAIRING_STATE_NOT_PAIRED,
    PAIRING_STATE_REQUESTING,        // POST /api/pair/init in flight
    PAIRING_STATE_AWAITING_APPROVAL, // showing code/QR, polling /api/pair/status
    PAIRING_STATE_PAIRED,
    PAIRING_STATE_ERROR,
} pairing_state_t;

// Reads a stored API key from NVS (namespace "guitar_vault") if present ->
// PAIRED, else NOT_PAIRED. Call once, after WiFiProvisioning_Init().
void Pairing_Init(void);

// User tapped "Connect to Guitar Vault". Spawns a background task that calls
// POST /api/pair/init, then polls GET /api/pair/status until approved.
// No-op if already PAIRED, REQUESTING, or AWAITING_APPROVAL.
void Pairing_Start(void);

// Clears the stored API key. Back to NOT_PAIRED.
void Pairing_Forget(void);

pairing_state_t Pairing_GetState(void);

// Valid while GetState() == PAIRING_STATE_AWAITING_APPROVAL.
bool Pairing_GetPendingInfo(char *code_out, size_t code_len, char *url_out, size_t url_len);
