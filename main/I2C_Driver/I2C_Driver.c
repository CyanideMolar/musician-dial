#include "I2C_Driver.h"

static const char *TAG = "I2C";

static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

void I2C_Init(void)
{
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized");
}

esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
    uint8_t buf[Length + 1];
    buf[0] = Reg_addr;
    memcpy(&buf[1], Reg_data, Length);
    return i2c_master_write_to_device(I2C_MASTER_NUM, Driver_addr, buf, Length + 1,
                                       I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, Driver_addr, &Reg_addr, 1, Reg_data, Length,
                                         I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}
