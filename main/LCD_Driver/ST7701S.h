#pragma once

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_err.h"
#include "esp_log.h"

#include "TCA9554PWR.h"

/* ---- Panel geometry ---- */
#define LCD_H_RES              480
#define LCD_V_RES              480
#define LCD_PIXEL_CLOCK_HZ     (18 * 1000 * 1000)
// One panel-internal frame buffer + a bounce buffer (see LCD_Init) is the
// combination Espressif's own RGB-panel examples actually pair together;
// pairing the bounce buffer with 2 full frame buffers instead (what this
// was previously set to) measured ~300ms per lv_timer_handler() call on
// this hardware -- badly serialized, not just slow.
#define LCD_NUM_FB             1

/* ---- Init-time SPI (3-wire, CS via IO-expander) ---- */
#define LCD_MOSI                1
#define LCD_SCLK                2
#define LCD_SPI_CS              -1   // manual CS via TCA9554 EXIO3, not a real GPIO

/* ---- RGB parallel interface (streams pixel data after init) ---- */
#define LCD_PIN_NUM_BK_LIGHT     6
#define LCD_BK_LIGHT_ON_LEVEL    1
#define LCD_BK_LIGHT_OFF_LEVEL   !LCD_BK_LIGHT_ON_LEVEL
#define LCD_PIN_NUM_HSYNC        38
#define LCD_PIN_NUM_VSYNC        39
#define LCD_PIN_NUM_DE           40
#define LCD_PIN_NUM_PCLK         41
#define LCD_PIN_NUM_DATA0        5   // B0
#define LCD_PIN_NUM_DATA1        45  // B1
#define LCD_PIN_NUM_DATA2        48  // B2
#define LCD_PIN_NUM_DATA3        47  // B3
#define LCD_PIN_NUM_DATA4        21  // B4
#define LCD_PIN_NUM_DATA5        14  // G0
#define LCD_PIN_NUM_DATA6        13  // G1
#define LCD_PIN_NUM_DATA7        12  // G2
#define LCD_PIN_NUM_DATA8        11  // G3
#define LCD_PIN_NUM_DATA9        10  // G4
#define LCD_PIN_NUM_DATA10       9   // G5
#define LCD_PIN_NUM_DATA11       46  // R0
#define LCD_PIN_NUM_DATA12       3   // R1
#define LCD_PIN_NUM_DATA13       8   // R2
#define LCD_PIN_NUM_DATA14       18  // R3
#define LCD_PIN_NUM_DATA15       17  // R4
#define LCD_PIN_NUM_DISP_EN      -1

#define LEDC_HS_TIMER            LEDC_TIMER_0
#define LEDC_LS_MODE             LEDC_LOW_SPEED_MODE
#define LEDC_HS_CH0_GPIO         LCD_PIN_NUM_BK_LIGHT
#define LEDC_HS_CH0_CHANNEL      LEDC_CHANNEL_0
#define LEDC_ResolutionRatio     LEDC_TIMER_13_BIT
#define LEDC_MAX_Duty            ((1 << LEDC_ResolutionRatio) - 1)
#define Backlight_MAX            100

extern esp_lcd_panel_handle_t panel_handle;
extern uint8_t LCD_Backlight;

typedef struct {
    spi_device_handle_t spi_device;
    spi_bus_config_t spi_io_config_t;
    spi_device_interface_config_t st7701s_protocol_config_t;
} ST7701S;
typedef ST7701S *ST7701S_handle;

ST7701S_handle ST7701S_newObject(int mosi, int sclk, int cs, int spi_host);
void ST7701S_screen_init(ST7701S_handle handle);
void ST7701S_WriteCommand(ST7701S_handle handle, uint8_t cmd);
void ST7701S_WriteData(ST7701S_handle handle, uint8_t data);
esp_err_t ST7701S_reset(void);
esp_err_t ST7701S_CS_EN(void);
esp_err_t ST7701S_CS_Dis(void);

void LCD_Init(void);
void Backlight_Init(void);
void Set_Backlight(uint8_t Light);
