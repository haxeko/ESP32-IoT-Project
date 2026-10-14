// Host stub of espressif/led_strip: records the pixels sent to the strip and counts the refreshes.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct sim_strip *led_strip_handle_t;
typedef enum { LED_MODEL_WS2812 } led_model_t;
typedef enum { RMT_CLK_SRC_DEFAULT } rmt_clock_source_t;
typedef struct {
    int format;
} led_color_component_format_t;
#define LED_STRIP_COLOR_COMPONENT_FMT_GRB ((led_color_component_format_t){0})

typedef struct {
    int strip_gpio_num;
    uint32_t max_leds;
    led_model_t led_model;
    led_color_component_format_t color_component_format;
    struct {
        uint32_t invert_out : 1;
    } flags;
} led_strip_config_t;

typedef struct {
    rmt_clock_source_t clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    struct {
        uint32_t with_dma : 1;
    } flags;
} led_strip_rmt_config_t;

#define SIM_MAX_LEDS 64
struct sim_strip {
    uint32_t leds;
    uint8_t px[SIM_MAX_LEDS][3]; /* what the strip shows after the last refresh */
    uint8_t buf[SIM_MAX_LEDS][3]; /* pixel buffer, sent on refresh */
};
extern struct sim_strip g_sim_strip;
extern int g_sim_refreshes;
extern esp_err_t g_sim_strip_create_result;

static inline esp_err_t led_strip_new_rmt_device(const led_strip_config_t *led_config,
                                                 const led_strip_rmt_config_t *rmt_config, led_strip_handle_t *ret)
{
    (void)rmt_config;
    if (g_sim_strip_create_result != ESP_OK) {
        return g_sim_strip_create_result;
    }
    g_sim_strip.leds = led_config->max_leds;
    *ret = &g_sim_strip;
    return ESP_OK;
}

static inline esp_err_t led_strip_set_pixel(led_strip_handle_t s, uint32_t i, uint32_t r, uint32_t g, uint32_t b)
{
    if (i >= s->leds) {
        return ESP_ERR_INVALID_ARG;
    }
    s->buf[i][0] = (uint8_t)r;
    s->buf[i][1] = (uint8_t)g;
    s->buf[i][2] = (uint8_t)b;
    return ESP_OK;
}

static inline esp_err_t led_strip_refresh(led_strip_handle_t s)
{
    for (uint32_t i = 0; i < s->leds; i++) {
        s->px[i][0] = s->buf[i][0];
        s->px[i][1] = s->buf[i][1];
        s->px[i][2] = s->buf[i][2];
    }
    g_sim_refreshes++;
    return ESP_OK;
}

static inline esp_err_t led_strip_clear(led_strip_handle_t s)
{
    for (uint32_t i = 0; i < s->leds; i++) {
        s->buf[i][0] = s->buf[i][1] = s->buf[i][2] = 0;
    }
    return led_strip_refresh(s);
}
