#pragma once

#include "TCA9554PWR.h"

/* Onboard piezo buzzer is driven through the TCA9554 IO-expander (EXIO8) —
 * it is a simple on/off driver, not a PWM tone generator. */
void Buzzer_On(void);
void Buzzer_Off(void);
