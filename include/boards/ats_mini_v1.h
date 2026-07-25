#pragma once

#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "solar_os_bus_types.h"
#include "solar_os_pin_types.h"

#define SOLAR_OS_BOARD_ID "ats_mini_v1"
#define SOLAR_OS_BOARD_NAME "ATS-Mini v1"
#define SOLAR_OS_BOARD_VENDOR "esp32-si4732"
#define SOLAR_OS_BOARD_MODULE_NAME "ESP32-S3-WROOM-1-N16R8"

/*
 * Real ATS-Mini pinout (the pocket-receiver board, not the LilyGo
 * T-Embed shield variant), taken from the upstream project's
 * Common.h/tft_setup.h and https://esp32-si4732.github.io/ats-mini/hardware.html.
 * UART0/USB-CDC/PSRAM pins are unchanged from the generic DevKitC-1
 * bring-up (same module, same fixed pins); everything else below is
 * ATS-Mini-specific.
 */

#define SOLAR_OS_BOARD_UART_PORT UART_NUM_0
#define SOLAR_OS_BOARD_PIN_UART_TX GPIO_NUM_43
#define SOLAR_OS_BOARD_PIN_UART_RX GPIO_NUM_44

/* BOOT button, doubles as the SolarOS key input. */
#define SOLAR_OS_BOARD_PIN_KEY GPIO_NUM_0
#define SOLAR_OS_BOARD_KEY_ACTIVE_LEVEL 0
#define SOLAR_OS_BOARD_KEY_PULL_UP 1
#define SOLAR_OS_BOARD_KEY_PULL_DOWN 0

/* Rotary encoder (quadrature A/B) plus its push button. Not yet wired
 * to a SolarOS input driver -- board.h only records the pins so far. */
#define SOLAR_OS_BOARD_PIN_ENCODER_A GPIO_NUM_2
#define SOLAR_OS_BOARD_PIN_ENCODER_B GPIO_NUM_1
#define SOLAR_OS_BOARD_PIN_ENCODER_BUTTON GPIO_NUM_21

/* Battery voltage divider, ADC1 channel. */
#define SOLAR_OS_BOARD_PIN_BATTERY_ADC GPIO_NUM_4

/* SI4732/5 radio: I2C bus, reset, LDO power enable. Not yet wired to
 * a SolarOS driver/app -- board.h only records the pins so far. */
#define SOLAR_OS_BOARD_I2C_PORT I2C_NUM_0
#define SOLAR_OS_BOARD_PIN_I2C_SDA GPIO_NUM_18
#define SOLAR_OS_BOARD_PIN_I2C_SCL GPIO_NUM_17
#define SOLAR_OS_BOARD_PIN_RADIO_RESET GPIO_NUM_16
#define SOLAR_OS_BOARD_PIN_RADIO_LDO_ENABLE GPIO_NUM_15

/* Audio path: hardware L/R mute (active high) and amp enable (active
 * high), both driven by application code around the radio, not by a
 * SolarOS audio driver. */
#define SOLAR_OS_BOARD_PIN_AUDIO_MUTE GPIO_NUM_3
#define SOLAR_OS_BOARD_PIN_AUDIO_AMP_ENABLE GPIO_NUM_10

/*
 * ST7789, 170x320, 8-bit parallel (Intel 8080 / i80) bus -- see
 * drivers/tft_st7789_i80.c. RD is tied to a GPIO output held high
 * (the panel is never read from); WR/D0-D7 are driven by the ESP-IDF
 * i80 LCD peripheral, not bit-banged.
 */
#define SOLAR_OS_BOARD_DISPLAY_CONTROLLER "ST7789"
#define SOLAR_OS_BOARD_DISPLAY_WIDTH 170
#define SOLAR_OS_BOARD_DISPLAY_HEIGHT 320
#define SOLAR_OS_BOARD_DISPLAY_PCLK_HZ 20000000
/* First hardware bring-up showed the image upside down (prompt at the
 * top rendered flipped, scale-like garbage at the bottom) -- 180
 * degree rotation fixes that without needing swapped native dims. */
#define SOLAR_OS_BOARD_DISPLAY_U8G2_ROTATION U8G2_R2
#define SOLAR_OS_BOARD_PIN_LCD_RST GPIO_NUM_5
#define SOLAR_OS_BOARD_PIN_LCD_CS GPIO_NUM_6
#define SOLAR_OS_BOARD_PIN_LCD_DC GPIO_NUM_7
#define SOLAR_OS_BOARD_PIN_LCD_WR GPIO_NUM_8
#define SOLAR_OS_BOARD_PIN_LCD_RD GPIO_NUM_9
#define SOLAR_OS_BOARD_PIN_LCD_BL GPIO_NUM_38
#define SOLAR_OS_BOARD_LCD_DATA_PINS { \
    GPIO_NUM_39, GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42, \
    GPIO_NUM_45, GPIO_NUM_46, GPIO_NUM_47, GPIO_NUM_48, \
}

#define SOLAR_OS_BOARD_SPI_HOST SPI2_HOST
#define SOLAR_OS_BOARD_SPI_NAME "FSPI"
#define SOLAR_OS_BOARD_PIN_SPI_SCLK GPIO_NUM_12
#define SOLAR_OS_BOARD_PIN_SPI_MISO GPIO_NUM_13
#define SOLAR_OS_BOARD_PIN_SPI_MOSI GPIO_NUM_11
#define SOLAR_OS_BOARD_SPI_MAX_TRANSFER_SZ 4096
#define SOLAR_OS_BOARD_RUNTIME_SPI_HOST_MASK (1U << SPI3_HOST)
#define SOLAR_OS_BOARD_RUNTIME_UART_PORT_MASK ((1U << UART_NUM_1) | (1U << UART_NUM_2))
#define SOLAR_OS_BOARD_SPI_CS_SLOTS { \
    {.pin = GPIO_NUM_14, .name = "gpio14"}, \
}
#define SOLAR_OS_BOARD_BUSES { \
    { \
        .name = "i2c0", \
        .protocol = SOLAR_OS_BUS_PROTOCOL_I2C, \
        .origin = SOLAR_OS_BUS_ORIGIN_BOARD, \
        .sharing = SOLAR_OS_BUS_SHARED, \
        .config.i2c = { \
            .port = SOLAR_OS_BOARD_I2C_PORT, \
            .sda_pin = SOLAR_OS_BOARD_PIN_I2C_SDA, \
            .scl_pin = SOLAR_OS_BOARD_PIN_I2C_SCL, \
            .speed_hz = SOLAR_OS_BUS_I2C_DEFAULT_SPEED_HZ, \
        }, \
    }, \
    { \
        .name = "spi0", \
        .protocol = SOLAR_OS_BUS_PROTOCOL_SPI, \
        .origin = SOLAR_OS_BUS_ORIGIN_BOARD, \
        .sharing = SOLAR_OS_BUS_SHARED, \
        .config.spi = { \
            .host = SOLAR_OS_BOARD_SPI_HOST, \
            .sclk_pin = SOLAR_OS_BOARD_PIN_SPI_SCLK, \
            .miso_pin = SOLAR_OS_BOARD_PIN_SPI_MISO, \
            .mosi_pin = SOLAR_OS_BOARD_PIN_SPI_MOSI, \
            .max_transfer_size = SOLAR_OS_BOARD_SPI_MAX_TRANSFER_SZ, \
            .cs_count = 1, \
            .cs = { \
                {.name = "gpio14", .pin = GPIO_NUM_14}, \
            }, \
        }, \
    }, \
    { \
        .name = "uart0", \
        .protocol = SOLAR_OS_BUS_PROTOCOL_UART, \
        .origin = SOLAR_OS_BUS_ORIGIN_BOARD, \
        .sharing = SOLAR_OS_BUS_EXCLUSIVE, \
        .config.uart = { \
            .port = SOLAR_OS_BOARD_UART_PORT, \
            .tx_pin = SOLAR_OS_BOARD_PIN_UART_TX, \
            .rx_pin = SOLAR_OS_BOARD_PIN_UART_RX, \
            .baud_rate = SOLAR_OS_BUS_UART_DEFAULT_BAUD_RATE, \
        }, \
    }, \
}

#define SOLAR_OS_BOARD_EXPANSION_GPIO_MASK 0ULL
/*
 * Every GPIO on this board is claimed by a fixed function (encoder,
 * radio, battery ADC, display, audio, or the board-defined spi0
 * bus) -- unlike the generic DevKitC-1 bring-up this is copied from,
 * there is no free expansion GPIO/ADC/PWM left to expose.
 */
#define SOLAR_OS_BOARD_USER_GPIO_MASK 0ULL
#define SOLAR_OS_BOARD_EXPANSION_GPIO_LIST ""
#define SOLAR_OS_BOARD_USER_GPIO_LIST ""
#define SOLAR_OS_BOARD_EXPANSION_ADC_MASK 0ULL
#define SOLAR_OS_BOARD_EXPANSION_PWM_MASK 0ULL
#define SOLAR_OS_BOARD_GPIO_SLOTS { \
    {.pin = 0, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "BOOT/download"}, \
    {.pin = 1, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "rotary encoder B"}, \
    {.pin = 2, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "rotary encoder A"}, \
    {.pin = 3, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "audio mute / strapping"}, \
    {.pin = 4, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "battery ADC"}, \
    {.pin = 5, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD RST"}, \
    {.pin = 6, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD CS"}, \
    {.pin = 7, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD DC"}, \
    {.pin = 8, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD WR"}, \
    {.pin = 9, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD RD"}, \
    {.pin = 10, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "audio amp enable"}, \
    {.pin = 11, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "SPI MOSI"}, \
    {.pin = 12, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "SPI SCK"}, \
    {.pin = 13, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "SPI MISO"}, \
    {.pin = 14, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "SPI CS"}, \
    {.pin = 15, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "radio LDO enable"}, \
    {.pin = 16, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "radio reset"}, \
    {.pin = 17, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "radio I2C SCL"}, \
    {.pin = 18, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "radio I2C SDA"}, \
    {.pin = 19, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "USB D-/CDC"}, \
    {.pin = 20, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "USB D+/CDC"}, \
    {.pin = 21, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "rotary encoder button"}, \
    {.pin = 35, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "Octal PSRAM"}, \
    {.pin = 36, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "Octal PSRAM"}, \
    {.pin = 37, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "Octal PSRAM"}, \
    {.pin = 38, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD backlight"}, \
    {.pin = 39, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD D0 / JTAG MTCK"}, \
    {.pin = 40, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD D1 / JTAG MTDO"}, \
    {.pin = 41, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD D2 / JTAG MTDI"}, \
    {.pin = 42, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD D3 / JTAG MTMS"}, \
    {.pin = 43, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "UART TX"}, \
    {.pin = 44, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "UART RX"}, \
    {.pin = 45, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD D4 / strapping"}, \
    {.pin = 46, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD D5 / strapping"}, \
    {.pin = 47, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD D6"}, \
    {.pin = 48, .policy = SOLAR_OS_PIN_POLICY_FIXED, .role = "LCD D7"}, \
}
