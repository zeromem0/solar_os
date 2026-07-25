#include "solar_os_encoder.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "solar_os_board_caps.h"
#include "solar_os_keys.h"
#include "solar_os_log.h"

#if SOLAR_OS_BOARD_HAS_ENCODER
#include "solar_os_board.h"

#ifndef SOLAR_OS_BOARD_PIN_ENCODER_A
#error "Board enables ENCODER but does not define SOLAR_OS_BOARD_PIN_ENCODER_A."
#endif
#ifndef SOLAR_OS_BOARD_PIN_ENCODER_B
#error "Board enables ENCODER but does not define SOLAR_OS_BOARD_PIN_ENCODER_B."
#endif
#ifndef SOLAR_OS_BOARD_PIN_ENCODER_BUTTON
#error "Board enables ENCODER but does not define SOLAR_OS_BOARD_PIN_ENCODER_BUTTON."
#endif

/* Standard EC11-style encoders emit 4 quadrature transitions per
 * physical detent (click). */
#ifndef SOLAR_OS_ENCODER_STEPS_PER_DETENT
#define SOLAR_OS_ENCODER_STEPS_PER_DETENT 4
#endif
#ifndef SOLAR_OS_ENCODER_BUTTON_DEBOUNCE_MS
#define SOLAR_OS_ENCODER_BUTTON_DEBOUNCE_MS 30U
#endif
#ifndef SOLAR_OS_ENCODER_BUTTON_LONG_PRESS_MS
#define SOLAR_OS_ENCODER_BUTTON_LONG_PRESS_MS 500U
#endif

/*
 * The encoder is one rotational axis; SolarOS apps generally expect
 * four-direction navigation (Casio-style zone/clock editors move
 * between fields on LEFT/RIGHT and adjust the selected field's value
 * on UP/DOWN). A short click toggles which pair rotation currently
 * drives; a long press sends Enter (open/commit/confirm).
 */
typedef enum {
    ENCODER_MODE_NAVIGATE, /* rotation -> LEFT/RIGHT */
    ENCODER_MODE_ADJUST,   /* rotation -> UP/DOWN */
} encoder_mode_t;

static const char *TAG = "solar_os_encoder";
static bool encoder_initialized;
static uint8_t quad_state;
static int8_t quad_accum;
static encoder_mode_t encoder_mode = ENCODER_MODE_NAVIGATE;
static bool button_level_pressed;
static bool button_debounced_pressed;
static uint32_t button_change_ms;
static bool button_long_fired;

/* Quadrature delta table, indexed by (previous 2-bit AB << 2) | (current
 * 2-bit AB). Invalid/bounce transitions map to 0 and are ignored. */
static const int8_t quad_delta_table[16] = {
    0, -1, +1, 0,
    +1, 0, 0, -1,
    -1, 0, 0, +1,
    0, +1, -1, 0,
};

static uint32_t encoder_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint8_t encoder_read_ab(void)
{
    const int a = gpio_get_level(SOLAR_OS_BOARD_PIN_ENCODER_A);
    const int b = gpio_get_level(SOLAR_OS_BOARD_PIN_ENCODER_B);
    return (uint8_t)(((a ? 1U : 0U) << 1) | (b ? 1U : 0U));
}

static bool encoder_button_is_pressed(void)
{
    /* Active low, internal pull-up. */
    return gpio_get_level(SOLAR_OS_BOARD_PIN_ENCODER_BUTTON) == 0;
}
#endif

esp_err_t solar_os_encoder_init(void)
{
#if !SOLAR_OS_BOARD_HAS_ENCODER
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (encoder_initialized) {
        return ESP_OK;
    }

    const uint64_t pin_mask =
        (1ULL << (unsigned)SOLAR_OS_BOARD_PIN_ENCODER_A) |
        (1ULL << (unsigned)SOLAR_OS_BOARD_PIN_ENCODER_B) |
        (1ULL << (unsigned)SOLAR_OS_BOARD_PIN_ENCODER_BUTTON);
    const gpio_config_t io_config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&io_config);
    if (err != ESP_OK) {
        return err;
    }

    quad_state = encoder_read_ab();
    quad_accum = 0;
    encoder_mode = ENCODER_MODE_NAVIGATE;
    button_level_pressed = encoder_button_is_pressed();
    button_debounced_pressed = button_level_pressed;
    button_change_ms = encoder_millis();
    button_long_fired = false;

    encoder_initialized = true;
    SOLAR_OS_LOGI(TAG, "encoder ready");
    return ESP_OK;
#endif
}

size_t solar_os_encoder_read_chars(char *buffer, size_t buffer_len)
{
#if !SOLAR_OS_BOARD_HAS_ENCODER
    (void)buffer;
    (void)buffer_len;
    return 0;
#else
    if (buffer == NULL || buffer_len == 0 || !encoder_initialized) {
        return 0;
    }

    size_t count = 0;
    const uint32_t now_ms = encoder_millis();

    const uint8_t ab = encoder_read_ab();
    const uint8_t index = (uint8_t)((quad_state << 2) | ab);
    quad_state = ab;
    quad_accum = (int8_t)(quad_accum + quad_delta_table[index & 0x0fU]);

    while (quad_accum >= SOLAR_OS_ENCODER_STEPS_PER_DETENT && count < buffer_len) {
        quad_accum -= SOLAR_OS_ENCODER_STEPS_PER_DETENT;
        buffer[count++] = (char)(encoder_mode == ENCODER_MODE_ADJUST ?
                                 SOLAR_OS_KEY_UP : SOLAR_OS_KEY_RIGHT);
    }
    while (quad_accum <= -SOLAR_OS_ENCODER_STEPS_PER_DETENT && count < buffer_len) {
        quad_accum += SOLAR_OS_ENCODER_STEPS_PER_DETENT;
        buffer[count++] = (char)(encoder_mode == ENCODER_MODE_ADJUST ?
                                 SOLAR_OS_KEY_DOWN : SOLAR_OS_KEY_LEFT);
    }

    const bool level_pressed = encoder_button_is_pressed();
    if (level_pressed != button_level_pressed) {
        button_level_pressed = level_pressed;
        button_change_ms = now_ms;
    } else if ((now_ms - button_change_ms) >= SOLAR_OS_ENCODER_BUTTON_DEBOUNCE_MS &&
               button_debounced_pressed != button_level_pressed) {
        button_debounced_pressed = button_level_pressed;
        if (button_debounced_pressed) {
            button_long_fired = false;
        } else if (!button_long_fired && count < buffer_len) {
            /* Released before the long-press threshold: short click,
             * toggle which pair of keys rotation sends. */
            encoder_mode = encoder_mode == ENCODER_MODE_NAVIGATE ?
                ENCODER_MODE_ADJUST : ENCODER_MODE_NAVIGATE;
        }
    }

    if (button_debounced_pressed && !button_long_fired &&
        (now_ms - button_change_ms) >= SOLAR_OS_ENCODER_BUTTON_LONG_PRESS_MS &&
        count < buffer_len) {
        button_long_fired = true;
        buffer[count++] = '\r';
    }

    return count;
#endif
}
