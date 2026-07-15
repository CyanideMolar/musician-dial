#include "TCA9554PWR.h"

uint8_t Read_REG(uint8_t REG)
{
    uint8_t bitsStatus = 0;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, REG, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDRESS << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &bitsStatus, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return bitsStatus;
}

void Write_REG(uint8_t REG, uint8_t Data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, REG, true);
    i2c_master_write_byte(cmd, Data, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
}

void Mode_EXIO(uint8_t Pin, uint8_t State)
{
    uint8_t bitsStatus = Read_REG(TCA9554_CONFIG_REG);
    uint8_t Data = (0x01 << (Pin - 1)) | bitsStatus;
    Write_REG(TCA9554_CONFIG_REG, Data);
}

void Mode_EXIOS(uint8_t PinState)
{
    Write_REG(TCA9554_CONFIG_REG, PinState);
}

uint8_t Read_EXIO(uint8_t Pin)
{
    uint8_t inputBits = Read_REG(TCA9554_INPUT_REG);
    return (inputBits >> (Pin - 1)) & 0x01;
}

uint8_t Read_EXIOS(void)
{
    return Read_REG(TCA9554_INPUT_REG);
}

void Set_EXIO(uint8_t Pin, uint8_t State)
{
    if (State >= 2 || Pin < 1 || Pin > 8) {
        ESP_LOGE("EXIO", "Set_EXIO: invalid pin/state");
        return;
    }
    uint8_t bitsStatus = Read_REG(TCA9554_OUTPUT_REG);
    uint8_t Data = State ? ((0x01 << (Pin - 1)) | bitsStatus)
                          : (~(0x01 << (Pin - 1)) & bitsStatus);
    Write_REG(TCA9554_OUTPUT_REG, Data);
}

void Set_EXIOS(uint8_t PinState)
{
    Write_REG(TCA9554_OUTPUT_REG, PinState);
}

void Set_Toggle(uint8_t Pin)
{
    Set_EXIO(Pin, !Read_EXIO(Pin));
}

void TCA9554PWR_Init(uint8_t PinState)
{
    Mode_EXIOS(PinState);
}

esp_err_t EXIO_Init(void)
{
    TCA9554PWR_Init(0x00); // all 8 pins as outputs
    Buzzer_Off();
    return ESP_OK;
}
