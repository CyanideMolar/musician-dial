#pragma once
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

#include "TCA9554PWR.h"
#include "I2C_Driver.h"
#include "ST7701S.h"

esp_err_t esp_lcd_touch_new_i2c_cst820(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *tp);

#define CST820_I2C_ADDRESS   (0x15)

#define CST820_IO_CONFIG()                        \
    {                                              \
        .dev_addr = CST820_I2C_ADDRESS,            \
        .control_phase_bytes = 1,                  \
        .dc_bit_offset = 0,                        \
        .lcd_cmd_bits = 8,                          \
        .flags = { .disable_control_phase = 1 },    \
    }

#define CST820_INT_IO   16
#define CST820_RST_IO   -1   // reset is done via TCA9554 EXIO2, not a direct GPIO

extern esp_lcd_touch_handle_t tp;

void Touch_Init(void);
