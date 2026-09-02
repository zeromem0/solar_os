#include "solar_os_board_display.h"

#include <string.h>

#include "solar_os_board.h"
#include "tft_st7789_i80.h"

static tft_st7789_i80_t st7789_display;

static void display_bind_st7789(solar_os_board_display_t *display)
{
    display->driver = &st7789_display;
    display->driver_name = "st7789_i80";
    display->u8g2 = tft_st7789_i80_get_u8g2(&st7789_display);
    display->controller = SOLAR_OS_BOARD_DISPLAY_CONTROLLER;
    display->width = SOLAR_OS_BOARD_DISPLAY_WIDTH;
    display->height = SOLAR_OS_BOARD_DISPLAY_HEIGHT;
    display->ready = true;
}

esp_err_t solar_os_board_display_init(solar_os_board_display_t *display)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(display, 0, sizeof(*display));

    const esp_err_t err = tft_st7789_i80_init(&st7789_display);
    if (err != ESP_OK) {
        return err;
    }

    display_bind_st7789(display);
    return ESP_OK;
}

esp_err_t solar_os_board_display_resume(solar_os_board_display_t *display)
{
    if (display == NULL || display->driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = tft_st7789_i80_resume((tft_st7789_i80_t *)display->driver);
    if (err != ESP_OK) {
        display->ready = false;
        return err;
    }

    display_bind_st7789(display);
    return ESP_OK;
}

void solar_os_board_display_deinit(solar_os_board_display_t *display)
{
    if (display != NULL && display->driver != NULL) {
        tft_st7789_i80_deinit((tft_st7789_i80_t *)display->driver);
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

bool solar_os_board_display_brightness_supported(const solar_os_board_display_t *display)
{
    (void)display;
    return tft_st7789_i80_backlight_supported();
}

esp_err_t solar_os_board_display_get_brightness(const solar_os_board_display_t *display,
                                                uint8_t *percent)
{
    if (display == NULL || display->driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return tft_st7789_i80_get_backlight((const tft_st7789_i80_t *)display->driver, percent);
}

esp_err_t solar_os_board_display_set_brightness(solar_os_board_display_t *display,
                                                uint8_t percent)
{
    if (display == NULL || display->driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return tft_st7789_i80_set_backlight((tft_st7789_i80_t *)display->driver, percent);
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

/*
 * The rest of the 4.10 display contract.
 *
 * This panel is presented through u8g2, not through the streaming
 * surface/frame path the newer boards use, so the four descriptors read
 * back whatever the driver set -- zero -- and the streaming entry points
 * say so rather than pretending. With no formats advertised the display
 * service does not call them.
 */
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
    (void)display;
    (void)foreground_rgb888;
    (void)background_rgb888;
    return ESP_ERR_NOT_SUPPORTED;
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
    (void)display;
    (void)surface;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t solar_os_board_display_present_frame(solar_os_board_display_t *display,
                                               const solar_os_display_raster_t *frame)
{
    (void)display;
    (void)frame;
    return ESP_ERR_NOT_SUPPORTED;
}

/* Nothing starts later on this panel: the i80 bus and the u8g2 buffer are
 * both up when init returns, so the runtime has nothing left to wait for. */
esp_err_t solar_os_board_display_runtime_ready(solar_os_board_display_t *display)
{
    return display != NULL ? ESP_OK : ESP_ERR_INVALID_ARG;
}
