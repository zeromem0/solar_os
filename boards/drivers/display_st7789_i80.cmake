set(SOLAR_OS_BOARD_DISPLAY_DRIVER "st7789_i80")
list(APPEND SOLAR_OS_BOARD_SRCS
    "board/solar_os_board_display_st7789_i80.c"
    "drivers/tft_st7789_i80.c"
)
list(APPEND SOLAR_OS_BOARD_REQUIRES
    esp_lcd
    u8g2
)
