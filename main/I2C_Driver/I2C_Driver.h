#pragma once

#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

/* Shared I2C bus: TCA9554 IO-expander (0x20) + CST820 touch (0x15) */
#define I2C_MASTER_SCL_IO            7
#define I2C_MASTER_SDA_IO            15
#define I2C_MASTER_NUM               0
#define I2C_MASTER_FREQ_HZ           400000
#define I2C_MASTER_TIMEOUT_MS        1000

void I2C_Init(void);
esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length);
esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length);
