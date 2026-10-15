// Host test of the WS2812 bar driver (main/led_bar.c): LED count per level, fixed intensity, refreshes only on
// visible changes, the fade-in hold on Matter "On", Identify blinking, fill direction and init failure.
// Built three times by run.sh: default, -DCONFIG_LED_FILL_REVERSE=1 and -DSIM_STRIP_INIT_FAILS.
#include "check.h"

/* sdkconfig.h leaves a disabled bool option undefined; 0 behaves the same in led_bar.c */
#ifndef CONFIG_LED_FILL_REVERSE
#define CONFIG_LED_FILL_REVERSE 0
#endif

#include "led_bar.c"

struct sim_timer g_sim_timers[SIM_MAX_TIMERS];
int g_sim_timer_count;
int64_t g_sim_now_us = 1000 * 1000;
struct sim_strip g_sim_strip;
int g_sim_refreshes;
#ifdef SIM_STRIP_INIT_FAILS
esp_err_t g_sim_strip_create_result = ESP_FAIL;
#else
esp_err_t g_sim_strip_create_result = ESP_OK;
#endif

/* led_bar_init() creates the Identify timer first, then the fade-in hold timer */
#define IDENTIFY_TIMER (&g_sim_timers[0])
#define HOLD_TIMER (&g_sim_timers[1])

static void fire(struct sim_timer *t)
{
    t->callback(t->arg);
}

#if !defined(SIM_STRIP_INIT_FAILS)
static int lit(void)
{
    int n = 0;
    for (int i = 0; i < CONFIG_LED_COUNT; i++) {
        n += g_sim_strip.px[i][0] || g_sim_strip.px[i][1] || g_sim_strip.px[i][2];
    }
    return n;
}

static int px_is(int i, uint8_t r, uint8_t g, uint8_t b)
{
    return g_sim_strip.px[i][0] == r && g_sim_strip.px[i][1] == g && g_sim_strip.px[i][2] == b;
}

static void test_count_for_level(void)
{
    /* README table: app brightness -> Matter level (% * 254 / 100) -> LEDs */
    CHECK(led_bar_count_for_level(0) == 0);
    CHECK(led_bar_count_for_level(1) == 1); /* MinLevel: at least one LED while on */
    CHECK(led_bar_count_for_level(3) == 1);    /* 1 % */
    CHECK(led_bar_count_for_level(25) == 3);   /* 10 % */
    CHECK(led_bar_count_for_level(64) == 8);   /* 25 % */
    CHECK(led_bar_count_for_level(127) == 15); /* 50 % */
    CHECK(led_bar_count_for_level(191) == 23); /* 75 % */
    CHECK(led_bar_count_for_level(254) == 30); /* 100 % */
    CHECK(led_bar_count_for_level(255) == 30); /* above MaxLevel: clamped */

    /* Monotonic and every count from 1 to 30 is reachable */
    int prev = 0, seen = 0;
    for (int level = 1; level <= 254; level++) {
        int n = led_bar_count_for_level((uint8_t)level);
        CHECK(n >= prev && n <= prev + 1);
        seen += n != prev;
        prev = n;
    }
    CHECK(seen == 30);
}

static void test_intensity_color_and_refreshes(void)
{
    led_bar_set_state(true, 254, 255, 255, 255);
    CHECK(lit() == 30);
    CHECK(px_is(0, 77, 77, 77) && px_is(29, 77, 77, 77)); /* 30 % of 255, rounded */

    led_bar_set_rgb(255, 0, 0);
    CHECK(px_is(0, 77, 0, 0));

    led_bar_set_level(127);
    CHECK(lit() == 15);
#if CONFIG_LED_FILL_REVERSE
    CHECK(px_is(29, 77, 0, 0) && px_is(15, 77, 0, 0) && px_is(14, 0, 0, 0)); /* filled from the end */
#else
    CHECK(px_is(0, 77, 0, 0) && px_is(14, 77, 0, 0) && px_is(15, 0, 0, 0)); /* filled from DIN */
#endif

    /* Nothing visible changes: no refresh */
    int refreshes = g_sim_refreshes;
    led_bar_set_level(127);
    led_bar_set_level(128); /* still 15 LEDs */
    led_bar_set_rgb(255, 0, 0);
    led_bar_set_power(true);
    CHECK(g_sim_refreshes == refreshes);
    led_bar_set_rgb(0, 0, 255);
    CHECK(g_sim_refreshes == refreshes + 1 && px_is(0, 0, 0, 77) == !CONFIG_LED_FILL_REVERSE);

    led_bar_set_power(false);
    CHECK(lit() == 0 && !led_bar_get_power());
    led_bar_set_power(true);
    CHECK(lit() == 15 && led_bar_get_power());
}

static void test_fade_in_hold(void)
{
    led_bar_set_state(true, 127, 255, 0, 0);
    CHECK(lit() == 15);

    /* Off command: level ramps down to MinLevel, OnOff off, level restored */
    for (int l = 126; l >= 1; l--) {
        led_bar_set_level((uint8_t)l);
    }
    led_bar_set_power(false);
    led_bar_set_level(127);
    CHECK(lit() == 0);

    /* On command: OnOff on, CurrentLevel drops to MinLevel, then steps back up: the bar must not shrink */
    led_bar_set_power(true);
    CHECK(lit() == 15);
    int refreshes = g_sim_refreshes, min_lit = lit();
    led_bar_set_level(1);
    CHECK(HOLD_TIMER->running);
    for (int l = 1; l <= 127; l++) {
        led_bar_set_level((uint8_t)l);
        min_lit = lit() < min_lit ? lit() : min_lit;
    }
    CHECK(min_lit == 15 && g_sim_refreshes == refreshes && !HOLD_TIMER->running);
    led_bar_set_level(254);
    CHECK(lit() == 30);
    led_bar_set_level(127);

    /* Dimming down during the fade-in: follow it at once */
    led_bar_set_power(false);
    led_bar_set_power(true);
    led_bar_set_level(1);
    led_bar_set_level(2);
    led_bar_set_level(3);
    CHECK(lit() == 15);
    led_bar_set_level(2);
    CHECK(lit() == 1 && !HOLD_TIMER->running);
    led_bar_set_level(127);

    /* Fade-in stops below the held level: the safety timer shows the real level */
    led_bar_set_power(false);
    led_bar_set_power(true);
    for (int l = 1; l <= 60; l++) {
        led_bar_set_level((uint8_t)l);
    }
    CHECK(lit() == 15 && HOLD_TIMER->running);
    fire(HOLD_TIMER);
    CHECK(lit() == 7); /* level 60 -> 7 LEDs */

    /* MoveToLevelWithOnOff from off, going up from the stored level (no drop to MinLevel): no hold */
    led_bar_set_power(false);
    led_bar_set_power(true);
    led_bar_set_level(61);
    CHECK(!HOLD_TIMER->running);
    for (int l = 62; l <= 200; l++) {
        led_bar_set_level((uint8_t)l);
    }
    CHECK(lit() == 24);

    /* Switched on without a level change (button), later dimmed to MinLevel: a normal change, no hold */
    led_bar_set_power(false);
    led_bar_set_power(true);
    g_sim_now_us += 5 * 1000 * 1000;
    led_bar_set_level(1);
    CHECK(lit() == 1 && !HOLD_TIMER->running);
}

static void test_identify(void)
{
    led_bar_set_state(true, 127, 0, 255, 0);
    led_bar_identify_start(1); /* Blink effect: one on/off cycle */
    CHECK(lit() == 30 && IDENTIFY_TIMER->running && IDENTIFY_TIMER->periodic);
    fire(IDENTIFY_TIMER);
    CHECK(lit() == 0);
    fire(IDENTIFY_TIMER);
    CHECK(lit() == 15 && !IDENTIFY_TIMER->running); /* back to the normal state */

    /* Identify (until stopped) works while the light is off, then shows "off" again */
    led_bar_set_power(false);
    led_bar_identify_start(0);
    CHECK(lit() == 30);
    for (int i = 0; i < 9; i++) {
        fire(IDENTIFY_TIMER);
    }
    CHECK(lit() == 0 && IDENTIFY_TIMER->running);
    led_bar_identify_stop();
    CHECK(lit() == 0 && !IDENTIFY_TIMER->running);
}

#else
static void test_init_failure(void)
{
    /* The strip could not be created: init reports it, the API keeps working without a strip */
    CHECK(led_bar_init() == ESP_FAIL);
    led_bar_set_state(true, 254, 255, 255, 255);
    led_bar_set_power(false);
    CHECK(!led_bar_get_power());
    led_bar_set_power(true);
    led_bar_set_level(10);
    led_bar_set_rgb(1, 2, 3);
    led_bar_identify_start(1);
    fire(IDENTIFY_TIMER);
    led_bar_identify_stop();
    CHECK(led_bar_get_power() && g_sim_refreshes == 0);
}
#endif

int main(void)
{
#if defined(SIM_STRIP_INIT_FAILS)
    printf("led_bar (strip init fails):\n");
    test_init_failure();
#else
    printf("led_bar%s:\n", CONFIG_LED_FILL_REVERSE ? " (reverse fill)" : "");
    CHECK(led_bar_init() == ESP_OK);
    CHECK(lit() == 0 && g_sim_refreshes == 1); /* cleared once at init */
    test_count_for_level();
    test_intensity_color_and_refreshes();
    test_fade_in_hold();
    test_identify();
#endif
    return check_report();
}
