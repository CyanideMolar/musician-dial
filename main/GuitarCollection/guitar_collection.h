#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define GUITAR_ID_MAX_LEN   32
#define GUITAR_NAME_MAX_LEN 64

void GuitarCollection_Init(void);

// (Re)fetches the guitar list from Guitar Vault using the API key stored in
// NVS (same "guitar_vault"/"api_key" convention as Pairing). No-op if a
// refresh is already in flight.
void GuitarCollection_Refresh(void);

bool GuitarCollection_IsLoaded(void);
int GuitarCollection_Count(void);
bool GuitarCollection_GetName(int index, char *out, size_t out_len);
