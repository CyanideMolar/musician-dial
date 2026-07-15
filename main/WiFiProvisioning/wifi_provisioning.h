#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    WIFI_PROV_STATE_CONNECTING, // trying stored credentials
    WIFI_PROV_STATE_AP_MODE,    // broadcasting the setup AP, waiting for a submission
    WIFI_PROV_STATE_CONNECTED,
    WIFI_PROV_STATE_FAILED,     // no stored creds (or they didn't work) and setup not yet (re)started
} wifi_prov_state_t;

// nvs_flash_init + netif/event loop init. If credentials are already stored,
// kicks off a background STA connection attempt (~10s timeout). Never starts
// the setup AP on its own -- call WiFiProvisioning_StartSetup() for that, so
// the device never radiates an open setup network the user didn't ask for.
// Call once after LVGL_Init().
void WiFiProvisioning_Init(void);

// Starts the SoftAP + local HTTP server so a phone can submit credentials.
// Safe to call again while already in AP_MODE (no-op).
void WiFiProvisioning_StartSetup(void);

wifi_prov_state_t WiFiProvisioning_GetState(void);

// Valid while GetState() == WIFI_PROV_STATE_AP_MODE. Returns false otherwise.
bool WiFiProvisioning_GetApCredentials(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len);

// Valid while GetState() == WIFI_PROV_STATE_CONNECTED. Empty string otherwise.
const char *WiFiProvisioning_GetConnectedSsid(void);

// Clears the stored credentials and disconnects immediately. Back to
// WIFI_PROV_STATE_FAILED (mirrors Pairing_Forget()'s pattern).
void WiFiProvisioning_Forget(void);
