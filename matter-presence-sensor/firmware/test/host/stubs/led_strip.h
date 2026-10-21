// Host test stand-in for the led_strip component (espressif/led_strip 3.1): one RGB LED whose last colour sent is
// recorded in g_strip.
#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct led_strip_t *led_strip_handle_t;
typedef enum { LED_MODEL_WS2812 } led_model_t;
typedef int led_color_component_format_t;
#define LED_STRIP_COLOR_COMPONENT_FMT_GRB 0
typedef enum { RMT_CLK_SRC_DEFAULT } rmt_clock_source_t;

typedef struct {
    int strip_gpio_num;
    uint32_t max_leds;
    led_model_t led_model;
    led_color_component_format_t color_component_format;
} led_strip_config_t;

typedef struct {
    rmt_clock_source_t clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    struct {
        uint32_t with_dma : 1;
    } flags;
} led_strip_rmt_config_t;

struct strip_sim_t {
    int gpio = -1;            // pin given at creation, -1: not created
    uint32_t r = 0, g = 0, b = 0; // pixel set, not yet shown
    uint32_t shown_r = 0, shown_g = 0, shown_b = 0;
    int refreshes = 0;
};
inline strip_sim_t g_strip;

inline esp_err_t led_strip_new_rmt_device(const led_strip_config_t *config, const led_strip_rmt_config_t *rmt,
                                          led_strip_handle_t *ret)
{
    g_strip = strip_sim_t();
    g_strip.gpio = config->strip_gpio_num;
    *ret = reinterpret_cast<led_strip_handle_t>(&g_strip);
    return ESP_OK;
}

inline esp_err_t led_strip_set_pixel(led_strip_handle_t strip, uint32_t index, uint32_t r, uint32_t g, uint32_t b)
{
    g_strip.r = r;
    g_strip.g = g;
    g_strip.b = b;
    return ESP_OK;
}

inline esp_err_t led_strip_refresh(led_strip_handle_t strip)
{
    g_strip.shown_r = g_strip.r;
    g_strip.shown_g = g_strip.g;
    g_strip.shown_b = g_strip.b;
    g_strip.refreshes++;
    return ESP_OK;
}

inline esp_err_t led_strip_clear(led_strip_handle_t strip)
{
    g_strip.r = g_strip.g = g_strip.b = 0;
    return led_strip_refresh(strip);
}
