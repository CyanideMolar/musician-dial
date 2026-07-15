set(SRC_FILES
    "main.c"
    "I2C_Driver/I2C_Driver.c"
    "EXIO/TCA9554PWR.c"
    "LCD_Driver/ST7701S.c"
    "Touch_Driver/CST820.c"
    "LVGL_Driver/LVGL_Driver.c"
    "Buzzer/Buzzer.c"
    "Metronome/metronome_engine.c"
    "Metronome/metronome_ui.c"
    "CircleOfFifths/circle_of_fifths_ui.c"
    "WiFiProvisioning/wifi_provisioning.c"
    "WiFiProvisioning/wifi_provisioning_ui.c"
    "Pairing/pairing.c"
    "Pairing/pairing_ui.c"
    "GuitarCollection/guitar_collection.c"
    "GuitarCollection/guitar_collection_ui.c"
    "Settings/settings_ui.c"
    "Debug/screenshot.c"
    "fonts/lv_font_montserrat_18_bold.c"
    "fonts/lv_font_dejavu_18.c"
    "fonts/lv_font_dejavu_28.c"
    "fonts/lv_font_dejavu_32.c"
    "fonts/lv_font_dejavu_48.c"
)

set(INC_DIRS
    "."
    "I2C_Driver"
    "EXIO"
    "LCD_Driver"
    "Touch_Driver"
    "LVGL_Driver"
    "Buzzer"
    "Metronome"
    "CircleOfFifths"
    "WiFiProvisioning"
    "Pairing"
    "GuitarCollection"
    "Settings"
    "Debug"
)

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS ${INC_DIRS}
    REQUIRES driver esp_lcd esp_timer esp_lcd_touch esp_driver_usb_serial_jtag
             nvs_flash esp_wifi esp_netif esp_event esp_http_server
             esp_http_client json
    PRIV_REQUIRES mbedtls
)
