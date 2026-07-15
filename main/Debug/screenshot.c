#include "screenshot.h"
#include "ST7701S.h" // LCD_H_RES / LCD_V_RES

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "Screenshot";
static volatile bool s_trigger_pending = false;

// LVGL's own allocator (lv_malloc, used by lv_snapshot_take()'s automatic
// draw-buf path) is a fixed 64KB internal pool (CONFIG_LV_MEM_SIZE_KILOBYTES
// in sdkconfig.defaults) completely separate from the system heap -- a
// 450KB RGB565 snapshot can never fit in it no matter how much PSRAM is
// free. Same problem the main LVGL framebuffers already solved in
// LVGL_Driver.c: allocate our own buffer from PSRAM directly and hand it to
// LVGL instead of asking LVGL to allocate one itself. Allocated once at
// init and kept forever rather than per-capture, since this is an
// occasional debug feature, not something worth the alloc/free churn of a
// 450KB PSRAM chunk on every use.
static uint8_t *s_snap_buf = NULL;
static size_t s_snap_buf_size = 0;

static const char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Encodes and prints in 48-input-byte (64-output-char) lines rather than
// building one giant string, since the whole image is ~450KB of base64
// text -- streaming it line by line avoids a second full-size allocation
// on top of the snapshot buffer itself. 48 is divisible by 3, so only the
// final line (the actual end of the whole buffer) can end up needing '='
// padding; every earlier line is a clean 4-char-aligned base64 chunk, so
// concatenating them on the host side and decoding as one stream is safe.
static void base64_print_lines(const uint8_t *data, size_t len)
{
    char line[65];
    size_t i = 0;
    uint32_t line_count = 0;
    while (i < len) {
        // ~9600 lines total for a full 480x480 frame with zero yielding
        // starves this core's IDLE task long enough to trip the watchdog
        // mid-transfer -- confirmed on real hardware, and worse, the
        // resulting watchdog error log gets interleaved into this same
        // stdout stream, corrupting the base64 data. Yielding periodically
        // avoids both problems.
        if ((line_count++ & 0x3F) == 0) {
            vTaskDelay(1);
        }
        size_t chunk = (len - i < 48) ? (len - i) : 48;
        size_t out = 0;
        size_t j = 0;
        while (j + 3 <= chunk) {
            uint32_t v = ((uint32_t)data[i + j] << 16) | ((uint32_t)data[i + j + 1] << 8) | data[i + j + 2];
            line[out++] = B64_CHARS[(v >> 18) & 0x3F];
            line[out++] = B64_CHARS[(v >> 12) & 0x3F];
            line[out++] = B64_CHARS[(v >> 6) & 0x3F];
            line[out++] = B64_CHARS[v & 0x3F];
            j += 3;
        }
        size_t rem = chunk - j;
        if (rem == 1) {
            uint32_t v = (uint32_t)data[i + j] << 16;
            line[out++] = B64_CHARS[(v >> 18) & 0x3F];
            line[out++] = B64_CHARS[(v >> 12) & 0x3F];
            line[out++] = '=';
            line[out++] = '=';
        } else if (rem == 2) {
            uint32_t v = ((uint32_t)data[i + j] << 16) | ((uint32_t)data[i + j + 1] << 8);
            line[out++] = B64_CHARS[(v >> 18) & 0x3F];
            line[out++] = B64_CHARS[(v >> 12) & 0x3F];
            line[out++] = B64_CHARS[(v >> 6) & 0x3F];
            line[out++] = '=';
        }
        line[out] = '\0';
        printf("%s\n", line);
        i += chunk;
    }
}

static void screenshot_reader_task(void *arg)
{
    char line[32];
    while (1) {
        if (fgets(line, sizeof(line), stdin) != NULL) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
            if (strcmp(line, "SCREENSHOT") == 0) {
                s_trigger_pending = true;
            }
        }
    }
}

void Screenshot_Init(void)
{
    // The console's default stdio path over USB Serial/JTAG is
    // non-blocking reads with busy-polling left to the caller -- fgets()
    // in a plain while(1) against that never yields when idle, which
    // starves the IDLE task on that core and trips the watchdog (seen on
    // real hardware). Switching to the driver-backed VFS mode makes reads
    // genuinely blocking and interrupt-driven instead.
    usb_serial_jtag_driver_config_t usj_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usj_cfg));
    usb_serial_jtag_vfs_use_driver();

    s_snap_buf_size = (size_t)LCD_H_RES * LCD_V_RES * 2; // RGB565 = 2 bytes/px
    s_snap_buf = heap_caps_malloc(s_snap_buf_size, MALLOC_CAP_SPIRAM);
    if (!s_snap_buf) {
        ESP_LOGE(TAG, "failed to allocate %u-byte PSRAM snapshot buffer", (unsigned)s_snap_buf_size);
    }

    xTaskCreate(screenshot_reader_task, "screenshot_rd", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
}

void Screenshot_CheckAndCapture(lv_obj_t *screen)
{
    if (!s_trigger_pending) {
        return;
    }
    s_trigger_pending = false;

    if (!s_snap_buf) {
        ESP_LOGE(TAG, "no snapshot buffer");
        printf("---SCREENSHOT-FAIL---\n");
        return;
    }

    lv_image_dsc_t dsc;
    if (lv_snapshot_take_to_buf(screen, LV_COLOR_FORMAT_RGB565, &dsc, s_snap_buf, s_snap_buf_size) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "snapshot failed");
        printf("---SCREENSHOT-FAIL---\n");
        return;
    }

    printf("---SCREENSHOT-BEGIN w=%u h=%u stride=%u size=%lu---\n", (unsigned)dsc.header.w, (unsigned)dsc.header.h,
           (unsigned)dsc.header.stride, (unsigned long)dsc.data_size);
    base64_print_lines(dsc.data, dsc.data_size);
    printf("---SCREENSHOT-END---\n");
}
