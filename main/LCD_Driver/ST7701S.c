#include "ST7701S.h"

/* This panel uses a hybrid interface: a 3-wire SPI link is used once at boot
 * to push the ST7701S's internal register configuration (the sequence below
 * is vendor-specific and must not be changed), then the panel switches to
 * streaming pixels over the 16-bit RGB parallel bus for normal operation. */

#define SPI_WriteComm(cmd)  ST7701S_WriteCommand(handle, cmd)
#define SPI_WriteData(data) ST7701S_WriteData(handle, data)
#define Delay(ms)            vTaskDelay(pdMS_TO_TICKS(ms))

static const char *TAG = "LCD";

ST7701S_handle ST7701S_newObject(int mosi, int sclk, int cs, int spi_host)
{
    ST7701S_handle handle = heap_caps_calloc(1, sizeof(ST7701S), MALLOC_CAP_DEFAULT);

    handle->spi_io_config_t.miso_io_num = -1;
    handle->spi_io_config_t.mosi_io_num = mosi;
    handle->spi_io_config_t.sclk_io_num = sclk;
    handle->spi_io_config_t.quadwp_io_num = -1;
    handle->spi_io_config_t.quadhd_io_num = -1;
    handle->spi_io_config_t.max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE;
    ESP_ERROR_CHECK(spi_bus_initialize(spi_host, &handle->spi_io_config_t, SPI_DMA_CH_AUTO));

    handle->st7701s_protocol_config_t.command_bits = 1;
    handle->st7701s_protocol_config_t.address_bits = 8;
    handle->st7701s_protocol_config_t.clock_speed_hz = 4000000;
    handle->st7701s_protocol_config_t.mode = 0;
    handle->st7701s_protocol_config_t.spics_io_num = cs;
    handle->st7701s_protocol_config_t.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(spi_host, &handle->st7701s_protocol_config_t, &handle->spi_device));

    return handle;
}

void ST7701S_WriteCommand(ST7701S_handle handle, uint8_t cmd)
{
    spi_transaction_t t = { .rxlength = 0, .length = 0, .cmd = 0, .addr = cmd };
    spi_device_transmit(handle->spi_device, &t);
}

void ST7701S_WriteData(ST7701S_handle handle, uint8_t data)
{
    spi_transaction_t t = { .rxlength = 0, .length = 0, .cmd = 1, .addr = data };
    spi_device_transmit(handle->spi_device, &t);
}

esp_err_t ST7701S_reset(void)
{
    Set_EXIO(TCA9554_EXIO1, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    Set_EXIO(TCA9554_EXIO1, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

esp_err_t ST7701S_CS_EN(void)
{
    Set_EXIO(TCA9554_EXIO3, true);
    vTaskDelay(pdMS_TO_TICKS(10));
    Set_EXIO(TCA9554_EXIO3, false);
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

esp_err_t ST7701S_CS_Dis(void)
{
    Set_EXIO(TCA9554_EXIO3, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    Set_EXIO(TCA9554_EXIO3, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

void ST7701S_screen_init(ST7701S_handle handle)
{
    SPI_WriteComm(0xFF);
    SPI_WriteData(0x77); SPI_WriteData(0x01); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x10);

    SPI_WriteComm(0xC0);
    SPI_WriteData(0x3B); SPI_WriteData(0x00);

    SPI_WriteComm(0xC1);
    SPI_WriteData(0x0B); SPI_WriteData(0x02);

    SPI_WriteComm(0xC2);
    SPI_WriteData(0x07); SPI_WriteData(0x02);

    SPI_WriteComm(0xCC);
    SPI_WriteData(0x10);

    SPI_WriteComm(0xCD);
    SPI_WriteData(0x08);

    SPI_WriteComm(0xB0);
    SPI_WriteData(0x00); SPI_WriteData(0x11); SPI_WriteData(0x16); SPI_WriteData(0x0e);
    SPI_WriteData(0x11); SPI_WriteData(0x06); SPI_WriteData(0x05); SPI_WriteData(0x09);
    SPI_WriteData(0x08); SPI_WriteData(0x21); SPI_WriteData(0x06); SPI_WriteData(0x13);
    SPI_WriteData(0x10); SPI_WriteData(0x29); SPI_WriteData(0x31); SPI_WriteData(0x18);

    SPI_WriteComm(0xB1);
    SPI_WriteData(0x00); SPI_WriteData(0x11); SPI_WriteData(0x16); SPI_WriteData(0x0e);
    SPI_WriteData(0x11); SPI_WriteData(0x07); SPI_WriteData(0x05); SPI_WriteData(0x09);
    SPI_WriteData(0x09); SPI_WriteData(0x21); SPI_WriteData(0x05); SPI_WriteData(0x13);
    SPI_WriteData(0x11); SPI_WriteData(0x2a); SPI_WriteData(0x31); SPI_WriteData(0x18);

    SPI_WriteComm(0xFF);
    SPI_WriteData(0x77); SPI_WriteData(0x01); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x11);

    SPI_WriteComm(0xB0);
    SPI_WriteData(0x6d);

    SPI_WriteComm(0xB1);
    SPI_WriteData(0x37);

    SPI_WriteComm(0xB2);
    SPI_WriteData(0x81);

    SPI_WriteComm(0xB3);
    SPI_WriteData(0x80);

    SPI_WriteComm(0xB5);
    SPI_WriteData(0x43);

    SPI_WriteComm(0xB7);
    SPI_WriteData(0x85);

    SPI_WriteComm(0xB8);
    SPI_WriteData(0x20);

    SPI_WriteComm(0xC1);
    SPI_WriteData(0x78);

    SPI_WriteComm(0xC2);
    SPI_WriteData(0x78);

    SPI_WriteComm(0xD0);
    SPI_WriteData(0x88);

    SPI_WriteComm(0xE0);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x02);

    SPI_WriteComm(0xE1);
    SPI_WriteData(0x03); SPI_WriteData(0xA0); SPI_WriteData(0x00); SPI_WriteData(0x00);
    SPI_WriteData(0x04); SPI_WriteData(0xA0); SPI_WriteData(0x00); SPI_WriteData(0x00);
    SPI_WriteData(0x00); SPI_WriteData(0x20); SPI_WriteData(0x20);

    SPI_WriteComm(0xE2);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00);
    SPI_WriteData(0x00);

    SPI_WriteComm(0xE3);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x11); SPI_WriteData(0x00);

    SPI_WriteComm(0xE4);
    SPI_WriteData(0x22); SPI_WriteData(0x00);

    SPI_WriteComm(0xE5);
    SPI_WriteData(0x05); SPI_WriteData(0xEC); SPI_WriteData(0xA0); SPI_WriteData(0xA0);
    SPI_WriteData(0x07); SPI_WriteData(0xEE); SPI_WriteData(0xA0); SPI_WriteData(0xA0);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00);

    SPI_WriteComm(0xE6);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x11); SPI_WriteData(0x00);

    SPI_WriteComm(0xE7);
    SPI_WriteData(0x22); SPI_WriteData(0x00);

    SPI_WriteComm(0xE8);
    SPI_WriteData(0x06); SPI_WriteData(0xED); SPI_WriteData(0xA0); SPI_WriteData(0xA0);
    SPI_WriteData(0x08); SPI_WriteData(0xEF); SPI_WriteData(0xA0); SPI_WriteData(0xA0);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00);

    SPI_WriteComm(0xEB);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x40); SPI_WriteData(0x40);
    SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00);

    SPI_WriteComm(0xED);
    SPI_WriteData(0xFF); SPI_WriteData(0xFF); SPI_WriteData(0xFF); SPI_WriteData(0xBA);
    SPI_WriteData(0x0A); SPI_WriteData(0xBF); SPI_WriteData(0x45); SPI_WriteData(0xFF);
    SPI_WriteData(0xFF); SPI_WriteData(0x54); SPI_WriteData(0xFB); SPI_WriteData(0xA0);
    SPI_WriteData(0xAB); SPI_WriteData(0xFF); SPI_WriteData(0xFF); SPI_WriteData(0xFF);

    SPI_WriteComm(0xEF);
    SPI_WriteData(0x10); SPI_WriteData(0x0D); SPI_WriteData(0x04); SPI_WriteData(0x08);
    SPI_WriteData(0x3F); SPI_WriteData(0x1F);

    SPI_WriteComm(0xFF);
    SPI_WriteData(0x77); SPI_WriteData(0x01); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x13);

    SPI_WriteComm(0xEF);
    SPI_WriteData(0x08);

    SPI_WriteComm(0xFF);
    SPI_WriteData(0x77); SPI_WriteData(0x01); SPI_WriteData(0x00); SPI_WriteData(0x00); SPI_WriteData(0x00);

    SPI_WriteComm(0x36);
    SPI_WriteData(0x00);

    SPI_WriteComm(0x3A);
    SPI_WriteData(0x66);

    SPI_WriteComm(0x11); // sleep out
    Delay(480);

    SPI_WriteComm(0x20); // display inversion off
    Delay(120);
    SPI_WriteComm(0x29); // display on
}

esp_lcd_panel_handle_t panel_handle = NULL;

void LCD_Init(void)
{
    ST7701S_reset();
    ST7701S_CS_EN();
    vTaskDelay(pdMS_TO_TICKS(100));

    ST7701S_handle st7701s = ST7701S_newObject(LCD_MOSI, LCD_SCLK, LCD_SPI_CS, SPI2_HOST);
    ST7701S_screen_init(st7701s);

    ESP_LOGI(TAG, "Installing RGB LCD panel driver");
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16, // RGB565
        .psram_trans_align = 64,
        .num_fbs = LCD_NUM_FB,
        // Without this, the RGB peripheral DMAs pixels straight out of PSRAM
        // and any bus contention (confirmed on hardware: touch I2C reads)
        // stalls it long enough to desync HSYNC/VSYNC, seen as the whole
        // picture shifting/tearing. This stages scanlines through internal
        // SRAM instead, which isn't sensitive to PSRAM latency spikes.
        .bounce_buffer_size_px = 10 * LCD_H_RES,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = LCD_PIN_NUM_DISP_EN,
        .pclk_gpio_num = LCD_PIN_NUM_PCLK,
        .vsync_gpio_num = LCD_PIN_NUM_VSYNC,
        .hsync_gpio_num = LCD_PIN_NUM_HSYNC,
        .de_gpio_num = LCD_PIN_NUM_DE,
        .data_gpio_nums = {
            LCD_PIN_NUM_DATA0, LCD_PIN_NUM_DATA1, LCD_PIN_NUM_DATA2, LCD_PIN_NUM_DATA3,
            LCD_PIN_NUM_DATA4, LCD_PIN_NUM_DATA5, LCD_PIN_NUM_DATA6, LCD_PIN_NUM_DATA7,
            LCD_PIN_NUM_DATA8, LCD_PIN_NUM_DATA9, LCD_PIN_NUM_DATA10, LCD_PIN_NUM_DATA11,
            LCD_PIN_NUM_DATA12, LCD_PIN_NUM_DATA13, LCD_PIN_NUM_DATA14, LCD_PIN_NUM_DATA15,
        },
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_back_porch = 10,
            .hsync_front_porch = 50,
            .hsync_pulse_width = 8,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .vsync_pulse_width = 3,
            .flags.pclk_active_neg = false,
        },
        .flags.fb_in_psram = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    ST7701S_CS_Dis();
    Backlight_Init();
}

/* ---- Backlight (LEDC PWM on GPIO6, direct pin — not via IO-expander) ---- */

uint8_t LCD_Backlight = 80;
static ledc_channel_config_t ledc_channel;

void Backlight_Init(void)
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_ResolutionRatio,
        .freq_hz = 5000,
        .speed_mode = LEDC_LS_MODE,
        .timer_num = LEDC_HS_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel.channel = LEDC_HS_CH0_CHANNEL;
    ledc_channel.duty = 0;
    ledc_channel.gpio_num = LCD_PIN_NUM_BK_LIGHT;
    ledc_channel.speed_mode = LEDC_LS_MODE;
    ledc_channel.timer_sel = LEDC_HS_TIMER;
    ledc_channel_config(&ledc_channel);
    ledc_fade_func_install(0);

    Set_Backlight(LCD_Backlight);
}

void Set_Backlight(uint8_t Light)
{
    if (Light > Backlight_MAX) Light = Backlight_MAX;
    uint32_t duty = Light == 0 ? 0 : (LEDC_MAX_Duty - (81 * (Backlight_MAX - Light)));
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}
