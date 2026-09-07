#include "solar_os_board_display.h"

#include <string.h>

#include "esp_log.h"
#include "io_expander_aw9523b.h"
#include "pmic_axp2101.h"
#include "solar_os_board.h"
#include "solar_os_buses.h"
#include "tft_ili9341.h"

/*
 * CoreS3's panel cannot be declared as an ordinary manifest device.
 * The shared tft display service fixes MADCTL at 0x88 and never issues
 * INVON, while this glass is an ILI9342C that needs 0xa8 and inverted
 * colours or everything renders in negative. Its reset line also sits
 * behind the AW9523B expander and its backlight behind the AXP2101,
 * neither of which is a GPIO the device bindings can name. So the board
 * drives the panel itself and answers the display contract directly.
 */

static const char *TAG = "cores3_display";

static tft_ili9341_t panel;
static uint8_t backlight_percent = 100;

static void display_bind(solar_os_board_display_t *display)
{
    display->driver = &panel;
    display->driver_name = "ili9342c";
    display->u8g2 = tft_ili9341_get_u8g2(&panel);
    display->controller = SOLAR_OS_BOARD_DISPLAY_CONTROLLER;
    display->width = SOLAR_OS_BOARD_DISPLAY_WIDTH;
    display->height = SOLAR_OS_BOARD_DISPLAY_HEIGHT;
    /* The panel driver carries the indexed surface and raster paths, so
     * graphics runs in colour rather than as a one-bit expansion. */
    display->surface_formats = SOLAR_OS_DISPLAY_FORMAT_INDEX8_BIT;
    display->frame_formats = SOLAR_OS_DISPLAY_FORMAT_INDEX2_BIT;
    display->preferred_stream_fps = 30;
    display->max_stream_pixels_per_second = 1600000U;
    display->ready = true;
}

/*
 * Power before reset, in that order: BLDO1 feeds the panel's digital
 * VDD and DLDO1 the backlight boost, and releasing reset into an
 * unpowered panel leaves it in an undefined state.
 */
static esp_err_t display_bringup(void)
{
    const esp_err_t pmic_err = pmic_axp2101_cores3_bringup();
    if (pmic_err != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 bring-up failed: %s", esp_err_to_name(pmic_err));
        return pmic_err;
    }

    const esp_err_t expander_err = io_expander_aw9523b_init();
    if (expander_err != ESP_OK) {
        ESP_LOGE(TAG, "AW9523B bring-up failed: %s", esp_err_to_name(expander_err));
        return expander_err;
    }

    return ESP_OK;
}

static esp_err_t display_start(void)
{
    /* Board-driven panels are outside the expansion device framework,
     * which is what normally leases a bus, so take the lease here.
     * Without it the bus reports no holder and adding the device fails
     * with ESP_ERR_INVALID_STATE. */
    const esp_err_t lease = solar_os_bus_acquire("spi0",
                                                 SOLAR_OS_BUS_PROTOCOL_SPI,
                                                 "board:display0");
    if (lease != ESP_OK && lease != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi0 lease failed: %s", esp_err_to_name(lease));
        return lease;
    }

    const tft_ili9341_config_t config = {
        .spi_bus = "spi0", /* the manifest bus, not the panel label */
        .cs_pin = SOLAR_OS_BOARD_PIN_LCD_CS,
        .dc_pin = SOLAR_OS_BOARD_PIN_LCD_DC,
        .reset_pin = -1,     /* behind the AW9523B, released above */
        .backlight_pin = -1, /* behind the AXP2101, driven below */
        .spi_clock_hz = SOLAR_OS_BOARD_DISPLAY_SPI_CLOCK_HZ,
        .backlight_pwm_hz = 0,
        .width = SOLAR_OS_BOARD_DISPLAY_NATIVE_WIDTH,
        .height = SOLAR_OS_BOARD_DISPLAY_NATIVE_HEIGHT,
        .madctl = SOLAR_OS_BOARD_DISPLAY_MADCTL,
        .st7796 = false,
        .invert = SOLAR_OS_BOARD_DISPLAY_INVERT,
        .backlight_active_high = false,
        .backlight_pwm = false,
        .rotation = SOLAR_OS_BOARD_DISPLAY_U8G2_ROTATION,
    };

    return tft_ili9341_init(&panel, &config);
}

esp_err_t solar_os_board_display_init(solar_os_board_display_t *display)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(display, 0, sizeof(*display));

    const esp_err_t bringup_err = display_bringup();
    if (bringup_err != ESP_OK) {
        return bringup_err;
    }

    const esp_err_t err = display_start();
    if (err != ESP_OK) {
        return err;
    }

    (void)pmic_axp2101_set_backlight(backlight_percent);
    display_bind(display);
    return ESP_OK;
}

esp_err_t solar_os_board_display_resume(solar_os_board_display_t *display)
{
    if (display == NULL || display->driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = tft_ili9341_resume((tft_ili9341_t *)display->driver);
    if (err != ESP_OK) {
        display->ready = false;
        return err;
    }

    (void)pmic_axp2101_set_backlight(backlight_percent);
    display_bind(display);
    return ESP_OK;
}

void solar_os_board_display_deinit(solar_os_board_display_t *display)
{
    if (display != NULL && display->driver != NULL) {
        tft_ili9341_deinit((tft_ili9341_t *)display->driver);
        memset(display, 0, sizeof(*display));
    }
}

u8g2_t *solar_os_board_display_u8g2(solar_os_board_display_t *display)
{
    return display != NULL ? display->u8g2 : NULL;
}

const char *solar_os_board_display_driver_name(const solar_os_board_display_t *display)
{
    return display != NULL && display->driver_name != NULL ? display->driver_name : "unknown";
}

const char *solar_os_board_display_controller(const solar_os_board_display_t *display)
{
    return display != NULL && display->controller != NULL ? display->controller : "unknown";
}

uint16_t solar_os_board_display_width(const solar_os_board_display_t *display)
{
    return display != NULL ? display->width : 0;
}

uint16_t solar_os_board_display_height(const solar_os_board_display_t *display)
{
    return display != NULL ? display->height : 0;
}

bool solar_os_board_display_ready(const solar_os_board_display_t *display)
{
    return display != NULL && display->ready;
}

/* Brightness is the PMIC's boost converter, not a panel pin, so it is
 * answered here rather than by the panel driver's own backlight path. */
bool solar_os_board_display_brightness_supported(const solar_os_board_display_t *display)
{
    (void)display;
    return true;
}

esp_err_t solar_os_board_display_get_brightness(const solar_os_board_display_t *display,
                                                uint8_t *percent)
{
    if (display == NULL || percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *percent = backlight_percent;
    return ESP_OK;
}

esp_err_t solar_os_board_display_set_brightness(solar_os_board_display_t *display,
                                                uint8_t percent)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (percent > 100U) {
        percent = 100U;
    }
    const esp_err_t err = pmic_axp2101_set_backlight(percent);
    if (err == ESP_OK) {
        backlight_percent = percent;
    }
    return err;
}

const char *solar_os_board_display_controller_mode(const solar_os_board_display_t *display)
{
    (void)display;
    return NULL;
}

const char *solar_os_board_display_controller_mode_values(const solar_os_board_display_t *display)
{
    (void)display;
    return NULL;
}

esp_err_t solar_os_board_display_set_controller_mode(solar_os_board_display_t *display,
                                                     const char *mode)
{
    (void)display;
    (void)mode;
    return ESP_ERR_NOT_SUPPORTED;
}

uint32_t solar_os_board_display_surface_formats(const solar_os_board_display_t *display)
{
    return display != NULL ? display->surface_formats : 0U;
}

uint32_t solar_os_board_display_frame_formats(const solar_os_board_display_t *display)
{
    return display != NULL ? display->frame_formats : 0U;
}

uint16_t solar_os_board_display_preferred_stream_fps(const solar_os_board_display_t *display)
{
    return display != NULL ? display->preferred_stream_fps : 0U;
}

uint32_t solar_os_board_display_max_stream_pixels_per_second(
    const solar_os_board_display_t *display)
{
    return display != NULL ? display->max_stream_pixels_per_second : 0U;
}

esp_err_t solar_os_board_display_set_colors(solar_os_board_display_t *display,
                                            uint32_t foreground_rgb888,
                                            uint32_t background_rgb888)
{
    if (display == NULL || display->driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return tft_ili9341_set_colors((tft_ili9341_t *)display->driver,
                                  foreground_rgb888,
                                  background_rgb888);
}

esp_err_t solar_os_board_display_set_high_refresh_override(solar_os_board_display_t *display,
                                                           bool enabled,
                                                           uint16_t hz_tenths)
{
    (void)display;
    (void)enabled;
    (void)hz_tenths;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t solar_os_board_display_present_surface(solar_os_board_display_t *display,
                                                 const solar_os_display_surface_t *surface)
{
    if (display == NULL || display->driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return tft_ili9341_present_surface((tft_ili9341_t *)display->driver, surface);
}

esp_err_t solar_os_board_display_present_frame(solar_os_board_display_t *display,
                                               const solar_os_display_raster_t *frame)
{
    if (display == NULL || display->driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return tft_ili9341_present_frame((tft_ili9341_t *)display->driver, frame);
}

esp_err_t solar_os_board_display_runtime_ready(solar_os_board_display_t *display)
{
    return display != NULL && display->ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}
