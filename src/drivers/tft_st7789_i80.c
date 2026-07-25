#include "tft_st7789_i80.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "solar_os_board.h"
#include "solar_os_vector.h"

#ifndef SOLAR_OS_BOARD_DISPLAY_PCLK_HZ
#error "SOLAR_OS_BOARD_DISPLAY_PCLK_HZ must be defined by the board profile"
#endif

#ifndef SOLAR_OS_BOARD_DISPLAY_NATIVE_WIDTH
#define SOLAR_OS_BOARD_DISPLAY_NATIVE_WIDTH SOLAR_OS_BOARD_DISPLAY_WIDTH
#endif

#ifndef SOLAR_OS_BOARD_DISPLAY_NATIVE_HEIGHT
#define SOLAR_OS_BOARD_DISPLAY_NATIVE_HEIGHT SOLAR_OS_BOARD_DISPLAY_HEIGHT
#endif

#ifndef SOLAR_OS_BOARD_DISPLAY_INVERT
#define SOLAR_OS_BOARD_DISPLAY_INVERT 1
#endif

#ifndef SOLAR_OS_BOARD_DISPLAY_U8G2_ROTATION
#define SOLAR_OS_BOARD_DISPLAY_U8G2_ROTATION U8G2_R0
#endif

/* Gap between the panel's visible glass and the start of its GRAM,
 * in the panel's native (unrotated) coordinate frame -- common on
 * ST7789 modules narrower than the controller's full GRAM width
 * (e.g. a 170px-wide panel centered in a 240px GRAM needs gap_x=35).
 * u8g2's rotation is a pure software transform applied before tiles
 * reach this driver, so the gap is always native-frame regardless of
 * the active u8g2 rotation. */
#ifndef SOLAR_OS_BOARD_DISPLAY_GAP_X
#define SOLAR_OS_BOARD_DISPLAY_GAP_X 0
#endif
#ifndef SOLAR_OS_BOARD_DISPLAY_GAP_Y
#define SOLAR_OS_BOARD_DISPLAY_GAP_Y 0
#endif

#ifndef SOLAR_OS_BOARD_LCD_BACKLIGHT_ACTIVE_LEVEL
#define SOLAR_OS_BOARD_LCD_BACKLIGHT_ACTIVE_LEVEL 1
#endif
#ifndef SOLAR_OS_BOARD_LCD_BACKLIGHT_PWM
#define SOLAR_OS_BOARD_LCD_BACKLIGHT_PWM 0
#endif
#ifndef SOLAR_OS_BOARD_LCD_BACKLIGHT_PWM_FREQ_HZ
#define SOLAR_OS_BOARD_LCD_BACKLIGHT_PWM_FREQ_HZ 20000U
#endif

#if SOLAR_OS_BOARD_LCD_BACKLIGHT_PWM
#include "pwm_port.h"
#endif

#define ST7789_WIDTH SOLAR_OS_BOARD_DISPLAY_NATIVE_WIDTH
#define ST7789_HEIGHT SOLAR_OS_BOARD_DISPLAY_NATIVE_HEIGHT
#define ST7789_TILE_WIDTH ((ST7789_WIDTH + 7) / 8)
#define ST7789_TILE_HEIGHT ((ST7789_HEIGHT + 7) / 8)
#define ST7789_BUFFER_ROW_BYTES (ST7789_TILE_WIDTH * 8)
#define ST7789_BUFFER_BYTES (ST7789_BUFFER_ROW_BYTES * ST7789_TILE_HEIGHT)
#define ST7789_LINE_BYTES (ST7789_WIDTH * 2)
#define ST7789_RGB565_BLACK 0x0000
#define ST7789_RGB565_WHITE 0xffff

static const char *TAG = "tft_st7789_i80";
static tft_st7789_i80_t *active_display;

static const u8x8_display_info_t st7789_display_info = {
    .chip_enable_level = 0,
    .chip_disable_level = 1,
    .post_chip_enable_wait_ns = 0,
    .pre_chip_disable_wait_ns = 0,
    .reset_pulse_width_ms = 20,
    .post_reset_wait_ms = 120,
    .sda_setup_time_ns = 0,
    .sck_pulse_width_ns = 0,
    .sck_clock_hz = SOLAR_OS_BOARD_DISPLAY_PCLK_HZ,
    .spi_mode = 0,
    .i2c_bus_clock_100kHz = 0,
    .data_setup_time_ns = 0,
    .write_pulse_width_ns = 0,
    .tile_width = ST7789_TILE_WIDTH,
    .tile_height = ST7789_TILE_HEIGHT,
    .default_x_offset = 0,
    .flipmode_x_offset = 0,
    .pixel_width = ST7789_WIDTH,
    .pixel_height = ST7789_HEIGHT,
};

static bool gpio_valid(gpio_num_t pin)
{
    return pin >= 0 && pin < GPIO_NUM_MAX;
}

static bool st7789_backlight_supported(void)
{
#ifdef SOLAR_OS_BOARD_PIN_LCD_BL
    return gpio_valid(SOLAR_OS_BOARD_PIN_LCD_BL);
#else
    return false;
#endif
}

static uint8_t st7789_backlight_duty(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    return SOLAR_OS_BOARD_LCD_BACKLIGHT_ACTIVE_LEVEL ? percent : (uint8_t)(100U - percent);
}

static esp_err_t st7789_apply_backlight(uint8_t percent)
{
    if (!st7789_backlight_supported()) {
        return ESP_ERR_NOT_SUPPORTED;
    }

#ifdef SOLAR_OS_BOARD_PIN_LCD_BL
#if SOLAR_OS_BOARD_LCD_BACKLIGHT_PWM
    return pwm_port_set(SOLAR_OS_BOARD_PIN_LCD_BL,
                        SOLAR_OS_BOARD_LCD_BACKLIGHT_PWM_FREQ_HZ,
                        st7789_backlight_duty(percent));
#else
    const int active = SOLAR_OS_BOARD_LCD_BACKLIGHT_ACTIVE_LEVEL ? 1 : 0;
    return gpio_set_level(SOLAR_OS_BOARD_PIN_LCD_BL, percent > 0 ? active : !active);
#endif
#else
    (void)percent;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void st7789_set_backlight_power(tft_st7789_i80_t *display, bool on)
{
    if (display == NULL) {
        return;
    }

    display->backlight_power = on;
    const uint8_t percent = on ? display->backlight_percent : 0;
    const esp_err_t err = st7789_apply_backlight(percent);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        display->last_error = err;
        ESP_LOGW(TAG, "backlight set failed: %s", esp_err_to_name(err));
    }
}

/* RD is never driven by the ESP-IDF i80 bus (it only writes); the
 * panel treats it as an active-low read-strobe, so hold it high. */
static esp_err_t st7789_configure_static_pins(void)
{
    uint64_t pin_mask = 0;
#ifdef SOLAR_OS_BOARD_PIN_LCD_RD
    if (gpio_valid(SOLAR_OS_BOARD_PIN_LCD_RD)) {
        pin_mask |= (1ULL << (unsigned)SOLAR_OS_BOARD_PIN_LCD_RD);
    }
#endif
#ifdef SOLAR_OS_BOARD_PIN_LCD_BL
    if (gpio_valid(SOLAR_OS_BOARD_PIN_LCD_BL)) {
        pin_mask |= (1ULL << (unsigned)SOLAR_OS_BOARD_PIN_LCD_BL);
    }
#endif
    if (pin_mask == 0) {
        return ESP_OK;
    }

    const gpio_config_t io_config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "gpio config failed");
#ifdef SOLAR_OS_BOARD_PIN_LCD_RD
    if (gpio_valid(SOLAR_OS_BOARD_PIN_LCD_RD)) {
        ESP_RETURN_ON_ERROR(gpio_set_level(SOLAR_OS_BOARD_PIN_LCD_RD, 1), TAG, "rd high failed");
    }
#endif
    return ESP_OK;
}

static void st7789_fill_line(tft_st7789_i80_t *display, uint16_t rgb565, size_t pixels)
{
    solar_os_vector_fill_rgb565_be(display->line_buffer, rgb565, pixels);
}

static esp_err_t st7789_fill_screen(tft_st7789_i80_t *display, uint16_t rgb565)
{
    st7789_fill_line(display, rgb565, ST7789_WIDTH);
    for (uint16_t row = 0; row < ST7789_HEIGHT; row++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(display->panel,
                                                       0, row, ST7789_WIDTH, row + 1,
                                                       display->line_buffer),
                            TAG,
                            "fill draw failed");
    }
    return ESP_OK;
}

static void st7789_invalidate_shadow(tft_st7789_i80_t *display)
{
    if (display != NULL) {
        display->shadow_valid_rows = 0;
    }
}

static bool st7789_shadow_matches(tft_st7789_i80_t *display,
                                  const uint8_t *tile_data,
                                  uint8_t x_pos,
                                  uint8_t y_pos,
                                  uint8_t count)
{
    if (display == NULL ||
        display->shadow == NULL ||
        display->shadow_size != ST7789_BUFFER_BYTES ||
        tile_data == NULL ||
        y_pos >= ST7789_TILE_HEIGHT ||
        x_pos >= ST7789_TILE_WIDTH ||
        count == 0 ||
        x_pos + count > ST7789_TILE_WIDTH ||
        (display->shadow_valid_rows & (1ULL << y_pos)) == 0) {
        return false;
    }

    const size_t offset = ((size_t)y_pos * ST7789_BUFFER_ROW_BYTES) + ((size_t)x_pos * 8U);
    return memcmp(&display->shadow[offset], tile_data, (size_t)count * 8U) == 0;
}

static void st7789_shadow_update(tft_st7789_i80_t *display,
                                 const uint8_t *tile_data,
                                 uint8_t x_pos,
                                 uint8_t y_pos,
                                 uint8_t count)
{
    if (display == NULL ||
        display->shadow == NULL ||
        display->shadow_size != ST7789_BUFFER_BYTES ||
        tile_data == NULL ||
        y_pos >= ST7789_TILE_HEIGHT ||
        x_pos >= ST7789_TILE_WIDTH ||
        count == 0 ||
        x_pos + count > ST7789_TILE_WIDTH) {
        return;
    }

    const size_t offset = ((size_t)y_pos * ST7789_BUFFER_ROW_BYTES) + ((size_t)x_pos * 8U);
    memcpy(&display->shadow[offset], tile_data, (size_t)count * 8U);
    display->shadow_valid_rows |= (1ULL << y_pos);
}

static void st7789_line_from_tile(tft_st7789_i80_t *display,
                                  const uint8_t *tile_data,
                                  int row,
                                  int width)
{
    solar_os_vector_expand_1bpp_to_rgb565_be(display->line_buffer,
                                             tile_data,
                                             (unsigned)row,
                                             ST7789_RGB565_BLACK,
                                             ST7789_RGB565_WHITE,
                                             (size_t)width);
}

static esp_err_t st7789_draw_tile(tft_st7789_i80_t *display, const u8x8_tile_t *tile)
{
    if (display == NULL || tile == NULL || tile->tile_ptr == NULL || tile->cnt == 0) {
        return ESP_OK;
    }
    if (tile->x_pos >= ST7789_TILE_WIDTH || tile->y_pos >= ST7789_TILE_HEIGHT) {
        return ESP_OK;
    }

    uint8_t count = tile->cnt;
    if (tile->x_pos + count > ST7789_TILE_WIDTH) {
        count = ST7789_TILE_WIDTH - tile->x_pos;
    }
    if (count == 0) {
        return ESP_OK;
    }

    if (st7789_shadow_matches(display, tile->tile_ptr, tile->x_pos, tile->y_pos, count)) {
        return ESP_OK;
    }

    const uint16_t x = (uint16_t)tile->x_pos * 8U;
    const uint16_t y = (uint16_t)tile->y_pos * 8U;
    uint16_t width = (uint16_t)count * 8U;
    uint16_t height = 8;
    if (x + width > ST7789_WIDTH) {
        width = ST7789_WIDTH - x;
    }
    if (y + height > ST7789_HEIGHT) {
        height = ST7789_HEIGHT - y;
    }
    if (width == 0 || height == 0) {
        return ESP_OK;
    }

    for (uint16_t row = 0; row < height; row++) {
        st7789_line_from_tile(display, tile->tile_ptr, row, width);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(display->panel,
                                                       x, y + row, x + width, y + row + 1,
                                                       display->line_buffer),
                            TAG,
                            "tile draw failed");
    }

    st7789_shadow_update(display, tile->tile_ptr, tile->x_pos, tile->y_pos, count);
    return ESP_OK;
}

static esp_err_t st7789_full_init(tft_st7789_i80_t *display)
{
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(display->panel), TAG, "reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(display->panel), TAG, "init failed");
#if SOLAR_OS_BOARD_DISPLAY_GAP_X != 0 || SOLAR_OS_BOARD_DISPLAY_GAP_Y != 0
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(display->panel,
                                              SOLAR_OS_BOARD_DISPLAY_GAP_X,
                                              SOLAR_OS_BOARD_DISPLAY_GAP_Y),
                        TAG,
                        "set gap failed");
#endif
#if SOLAR_OS_BOARD_DISPLAY_INVERT
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(display->panel, true), TAG, "invert failed");
#endif
    ESP_RETURN_ON_ERROR(st7789_fill_screen(display, ST7789_RGB565_WHITE), TAG, "screen clear failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(display->panel, true), TAG, "disp on failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    st7789_set_backlight_power(display, true);

    st7789_invalidate_shadow(display);
    display->last_error = ESP_OK;
    return ESP_OK;
}

static uint8_t st7789_u8x8_byte_cb(u8x8_t *u8x8, uint8_t message, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)message;
    (void)arg_int;
    (void)arg_ptr;
    return 1;
}

static uint8_t st7789_u8x8_display_cb(u8x8_t *u8x8, uint8_t message, uint8_t arg_int, void *arg_ptr)
{
    if (message == U8X8_MSG_DISPLAY_SETUP_MEMORY) {
        u8x8_d_helper_display_setup_memory(u8x8, &st7789_display_info);
        return 1;
    }

    tft_st7789_i80_t *display = active_display;
    if (display == NULL) {
        return 0;
    }

    switch (message) {
    case U8X8_MSG_DISPLAY_INIT:
        return st7789_full_init(display) == ESP_OK ? 1 : 0;

    case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
        st7789_invalidate_shadow(display);
        if (arg_int == 0) {
            st7789_set_backlight_power(display, true);
            return esp_lcd_panel_disp_on_off(display->panel, true) == ESP_OK ? 1 : 0;
        }
        st7789_set_backlight_power(display, false);
        return esp_lcd_panel_disp_on_off(display->panel, false) == ESP_OK ? 1 : 0;

    case U8X8_MSG_DISPLAY_DRAW_TILE:
        return st7789_draw_tile(display, (const u8x8_tile_t *)arg_ptr) == ESP_OK ? 1 : 0;

    default:
        return 0;
    }
}

esp_err_t tft_st7789_i80_init(tft_st7789_i80_t *display)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(display, 0, sizeof(*display));
    display->last_error = ESP_OK;
    display->backlight_percent = 100;

    ESP_RETURN_ON_ERROR(st7789_configure_static_pins(), TAG, "static pin config failed");
    st7789_set_backlight_power(display, false);

    const gpio_num_t data_pins[] = SOLAR_OS_BOARD_LCD_DATA_PINS;
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = SOLAR_OS_BOARD_PIN_LCD_DC,
        .wr_gpio_num = SOLAR_OS_BOARD_PIN_LCD_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .bus_width = 8,
        .max_transfer_bytes = ST7789_LINE_BYTES,
        .dma_burst_size = 64,
    };
    for (size_t i = 0; i < 8; i++) {
        bus_config.data_gpio_nums[i] = data_pins[i];
    }
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_config, &display->bus), TAG, "i80 bus create failed");
    display->bus_ready = true;

    const esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = SOLAR_OS_BOARD_PIN_LCD_CS,
        .pclk_hz = SOLAR_OS_BOARD_DISPLAY_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(display->bus, &io_config, &display->io),
                        TAG,
                        "panel io create failed");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = SOLAR_OS_BOARD_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(display->io, &panel_config, &display->panel),
                        TAG,
                        "panel create failed");

    display->line_buffer_size = ST7789_LINE_BYTES;
    display->line_buffer = heap_caps_malloc(display->line_buffer_size,
                                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (display->line_buffer == NULL) {
        tft_st7789_i80_deinit(display);
        return ESP_ERR_NO_MEM;
    }

    display->buffer_size = ST7789_BUFFER_BYTES;
    display->buffer = heap_caps_malloc(display->buffer_size, MALLOC_CAP_8BIT);
    if (display->buffer == NULL) {
        tft_st7789_i80_deinit(display);
        return ESP_ERR_NO_MEM;
    }
    memset(display->buffer, 0, display->buffer_size);

    display->shadow_size = ST7789_BUFFER_BYTES;
    display->shadow = heap_caps_malloc(display->shadow_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (display->shadow == NULL) {
        ESP_LOGW(TAG, "display shadow allocation failed, partial update skipping disabled");
        display->shadow_size = 0;
    } else {
        memset(display->shadow, 0, display->shadow_size);
        st7789_invalidate_shadow(display);
    }

    u8g2_SetupDisplay(&display->u8g2,
                      st7789_u8x8_display_cb,
                      u8x8_dummy_cb,
                      st7789_u8x8_byte_cb,
                      u8x8_dummy_cb);
    u8g2_SetupBuffer(&display->u8g2,
                     display->buffer,
                     ST7789_TILE_HEIGHT,
                     u8g2_ll_hvline_vertical_top_lsb,
                     SOLAR_OS_BOARD_DISPLAY_U8G2_ROTATION);
    active_display = display;
    u8g2_InitDisplay(&display->u8g2);
    u8g2_SetPowerSave(&display->u8g2, 0);

    return display->last_error;
}

esp_err_t tft_st7789_i80_resume(tft_st7789_i80_t *display)
{
    if (display == NULL ||
        display->panel == NULL ||
        display->buffer == NULL ||
        display->line_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(st7789_configure_static_pins(), TAG, "resume pin config failed");
    active_display = display;
    display->last_error = ESP_OK;
    st7789_invalidate_shadow(display);
    u8g2_InitDisplay(&display->u8g2);
    u8g2_SetPowerSave(&display->u8g2, 0);
    return display->last_error;
}

void tft_st7789_i80_deinit(tft_st7789_i80_t *display)
{
    if (display == NULL) {
        return;
    }

    st7789_set_backlight_power(display, false);

    if (display->panel != NULL) {
        esp_lcd_panel_del(display->panel);
        display->panel = NULL;
    }
    if (display->io != NULL) {
        esp_lcd_panel_io_del(display->io);
        display->io = NULL;
    }
    if (display->bus_ready) {
        esp_lcd_del_i80_bus(display->bus);
        display->bus = NULL;
        display->bus_ready = false;
    }

    if (display->line_buffer != NULL) {
        heap_caps_free(display->line_buffer);
        display->line_buffer = NULL;
    }
    if (display->buffer != NULL) {
        heap_caps_free(display->buffer);
        display->buffer = NULL;
    }
    if (display->shadow != NULL) {
        heap_caps_free(display->shadow);
        display->shadow = NULL;
    }

    if (active_display == display) {
        active_display = NULL;
    }

    display->buffer_size = 0;
    display->shadow_size = 0;
    display->line_buffer_size = 0;
    display->shadow_valid_rows = 0;
}

u8g2_t *tft_st7789_i80_get_u8g2(tft_st7789_i80_t *display)
{
    return display == NULL ? NULL : &display->u8g2;
}

bool tft_st7789_i80_backlight_supported(void)
{
    return st7789_backlight_supported();
}

esp_err_t tft_st7789_i80_get_backlight(const tft_st7789_i80_t *display, uint8_t *percent)
{
    if (percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (display == NULL) {
        *percent = 0;
        return ESP_ERR_INVALID_STATE;
    }
    if (!st7789_backlight_supported()) {
        *percent = 100;
        return ESP_ERR_NOT_SUPPORTED;
    }

    *percent = display->backlight_percent;
    return ESP_OK;
}

esp_err_t tft_st7789_i80_set_backlight(tft_st7789_i80_t *display, uint8_t percent)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!st7789_backlight_supported()) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    display->backlight_percent = percent;
    if (display->backlight_power) {
        return st7789_apply_backlight(percent);
    }
    return ESP_OK;
}
