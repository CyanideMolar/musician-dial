#include "CST820.h"

#define DATA_START_REG   (0x15)
#define CHIP_ID_REG      (0x01)
#define TOUCH_NUM_REG    (0x02)
#define TOUCH_POS_REG    (0x03)

static const char *TAG = "CST820";

static esp_err_t read_data(esp_lcd_touch_handle_t tp);
static bool get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                    uint8_t *point_num, uint8_t max_point_num);
static esp_err_t del(esp_lcd_touch_handle_t tp);
static esp_err_t i2c_read_bytes(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len);
static esp_err_t i2c_write_bytes(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len);
static esp_err_t reset(esp_lcd_touch_handle_t tp);
static esp_err_t read_id(esp_lcd_touch_handle_t tp);

esp_err_t esp_lcd_touch_new_i2c_cst820(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *tp)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG, "invalid io");
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "invalid config");
    ESP_RETURN_ON_FALSE(tp, ESP_ERR_INVALID_ARG, TAG, "invalid handle");

    esp_err_t ret = ESP_OK;
    esp_lcd_touch_handle_t cst820 = calloc(1, sizeof(esp_lcd_touch_t));
    ESP_GOTO_ON_FALSE(cst820, ESP_ERR_NO_MEM, err, TAG, "malloc failed");

    cst820->io = io;
    cst820->read_data = read_data;
    cst820->get_xy = get_xy;
    cst820->del = del;
    cst820->data.lock.owner = portMUX_FREE_VAL;
    memcpy(&cst820->config, config, sizeof(esp_lcd_touch_config_t));

    if (cst820->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_cfg = {
            .mode = GPIO_MODE_INPUT,
            .intr_type = GPIO_INTR_NEGEDGE,
            .pin_bit_mask = BIT64(cst820->config.int_gpio_num),
        };
        ESP_GOTO_ON_ERROR(gpio_config(&int_cfg), err, TAG, "int gpio config failed");
        if (cst820->config.interrupt_callback) {
            esp_lcd_touch_register_interrupt_callback(cst820, cst820->config.interrupt_callback);
        }
    }

    ESP_GOTO_ON_ERROR(reset(cst820), err, TAG, "reset failed");
    ESP_GOTO_ON_ERROR(read_id(cst820), err, TAG, "read id failed");
    *tp = cst820;
    return ESP_OK;

err:
    if (cst820) {
        del(cst820);
    }
    ESP_LOGE(TAG, "initialization failed");
    return ret;
}

static esp_err_t read_data(esp_lcd_touch_handle_t tp)
{
    esp_err_t err;
    uint8_t buf[12];
    uint8_t clear = 0;
    uint8_t sleep_ack = 0xAB;
    uint8_t touch_cnt;
    uint8_t wake = 0x01;
    uint8_t disable_auto_sleep = 0x01;

    assert(tp != NULL);

    /* CST820 auto-sleeps between touches; wake it before every poll or
     * reads fail. Confirmed on hardware: dropping this caused every
     * read_data() call to fail with an I2C transaction error. */
    i2c_master_write_to_device(I2C_MASTER_NUM, CST820_I2C_ADDRESS, &wake, 1,
                                I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    i2c_write_bytes(tp, 0xFE, &disable_auto_sleep, 1);

    err = i2c_read_bytes(tp, TOUCH_NUM_REG, buf, 1);
    ESP_RETURN_ON_ERROR(err, TAG, "i2c read failed");
    i2c_write_bytes(tp, TOUCH_POS_REG, &sleep_ack, 1);

    touch_cnt = buf[0] & 0x0F;
    if (touch_cnt == 0) {
        i2c_write_bytes(tp, TOUCH_NUM_REG, &clear, 1);
        return ESP_OK;
    }
    if (touch_cnt > 2) {
        i2c_write_bytes(tp, TOUCH_NUM_REG, &clear, 1);
        return ESP_OK;
    }

    err = i2c_read_bytes(tp, TOUCH_POS_REG, buf, touch_cnt * 6);
    ESP_RETURN_ON_ERROR(err, TAG, "i2c read failed");
    i2c_write_bytes(tp, TOUCH_NUM_REG, &clear, 1);

    taskENTER_CRITICAL(&tp->data.lock);
    if (touch_cnt > CONFIG_ESP_LCD_TOUCH_MAX_POINTS) {
        touch_cnt = CONFIG_ESP_LCD_TOUCH_MAX_POINTS;
    }
    tp->data.points = touch_cnt;
    for (int i = 0; i < touch_cnt; i++) {
        tp->data.coords[i].x = ((uint16_t)(buf[i * 6] & 0x0F) << 8) + buf[i * 6 + 1];
        tp->data.coords[i].y = ((uint16_t)(buf[i * 6 + 2] & 0x0F) << 8) + buf[i * 6 + 3];
        tp->data.coords[i].strength = 50;
    }
    taskEXIT_CRITICAL(&tp->data.lock);

    return ESP_OK;
}

static bool get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                    uint8_t *point_num, uint8_t max_point_num)
{
    portENTER_CRITICAL(&tp->data.lock);
    *point_num = (tp->data.points > max_point_num) ? max_point_num : tp->data.points;
    for (int i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;
        if (strength) {
            strength[i] = tp->data.coords[i].strength;
        }
    }
    tp->data.points = 0;
    portEXIT_CRITICAL(&tp->data.lock);
    return (*point_num > 0);
}

static esp_err_t del(esp_lcd_touch_handle_t tp)
{
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
    }
    free(tp);
    return ESP_OK;
}

static esp_err_t reset(esp_lcd_touch_handle_t tp)
{
    Set_EXIO(TCA9554_EXIO2, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    Set_EXIO(TCA9554_EXIO2, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static esp_err_t read_id(esp_lcd_touch_handle_t tp)
{
    uint8_t id = 0;
    uint8_t wake = 0x01;
    i2c_write_bytes(tp, DATA_START_REG, &wake, 1);
    ESP_RETURN_ON_ERROR(i2c_read_bytes(tp, CHIP_ID_REG, &id, 1), TAG, "i2c read failed");
    ESP_LOGI(TAG, "chip id: %d", id);
    return ESP_OK;
}

static esp_err_t i2c_read_bytes(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len)
{
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}

static esp_err_t i2c_write_bytes(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len)
{
    return esp_lcd_panel_io_tx_param(tp->io, reg, data, len);
}

esp_lcd_touch_handle_t tp = NULL;

void Touch_Init(void)
{
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = CST820_IO_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_MASTER_NUM, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_V_RES,
        .y_max = LCD_H_RES,
        .rst_gpio_num = CST820_RST_IO,
        .int_gpio_num = CST820_INT_IO,
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    ESP_LOGI(TAG, "initializing CST820 touch controller");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst820(tp_io_handle, &tp_cfg, &tp));
}
