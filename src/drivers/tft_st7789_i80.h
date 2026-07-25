#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_i80_bus_handle_t bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    u8g2_t u8g2;
    uint8_t *buffer;
    uint8_t *shadow;
    uint8_t *line_buffer;
    size_t buffer_size;
    size_t shadow_size;
    size_t line_buffer_size;
    uint64_t shadow_valid_rows;
    esp_err_t last_error;
    uint8_t backlight_percent;
    bool bus_ready;
    bool backlight_power;
} tft_st7789_i80_t;

esp_err_t tft_st7789_i80_init(tft_st7789_i80_t *display);
esp_err_t tft_st7789_i80_resume(tft_st7789_i80_t *display);
void tft_st7789_i80_deinit(tft_st7789_i80_t *display);
u8g2_t *tft_st7789_i80_get_u8g2(tft_st7789_i80_t *display);
bool tft_st7789_i80_backlight_supported(void);
esp_err_t tft_st7789_i80_get_backlight(const tft_st7789_i80_t *display, uint8_t *percent);
esp_err_t tft_st7789_i80_set_backlight(tft_st7789_i80_t *display, uint8_t percent);

#ifdef __cplusplus
}
#endif
