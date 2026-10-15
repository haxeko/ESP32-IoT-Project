#include "lift.h"

#include <sdkconfig.h>
#include <stdbool.h>
#include <stdint.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "led_bar.h"
#include "lift_map.h"
#include "stepper.h"

static const char *TAG = "lift";

#if CONFIG_MOTOR_ENABLED

#define POLL_MOVING_MS 20  /* LED follow-up while moving: < one LED height at full speed */
#define POLL_IDLE_MS   250 /* housekeeping at rest (driver off delay) */
#define MAX_NOT_CLOSED 3   /* closings that missed the end stop (none reached it in between) before giving up */

static TaskHandle_t s_task;
/* Wanted state, written by the LED driver's callback, read by the task: (on << 8) | level, one word */
static volatile uint32_t s_want;
/* Lift task state */
static int32_t s_last_target = -1; /* last target the stepper accepted; -1: go to the wanted one */
static int s_not_closed;           /* closings that missed the end stop since one reached it */
static bool s_stopped;             /* given up: the lamp does not move until restarted */

/* led_bar lock held: only store and wake the task */
static void on_target(bool on, uint8_t level)
{
    s_want = ((uint32_t)on << 8) | level;
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

/* The lamp cannot be moved safely: motor stopped (holding if Driver off at rest is 0), the LEDs follow the level,
 * until a restart */
static void give_up(const char *why)
{
    ESP_LOGE(TAG, "%s: the lamp does not move until restarted, the LEDs follow the brightness", why);
    stepper_stop();
    led_bar_follow_level();
    s_stopped = true;
}

static void home_or_give_up(void)
{
    led_bar_set_exposed(0); /* position unknown while homing: nothing lit */
    esp_err_t err = stepper_home();
    if (err != ESP_OK) {
        give_up(esp_err_to_name(err));
    }
}

/* Off closes the lamp; level L opens it to expose the LEDs a plain bar would light for L */
static int32_t wanted_target(uint32_t want, int32_t travel)
{
    if (!((want >> 8) & 1)) {
        return 0;
    }
    return lift_steps_for_leds(led_bar_count_for_level((uint8_t)want), CONFIG_LED_COUNT, travel);
}

/* One pass of the lift task towards `target` (steps; `want` only for the log): move, light the exposed LEDs, react
 * to the stepper. Returns the time until the next pass (ms). */
static uint32_t lift_pass(int32_t target, uint32_t want)
{
    if (s_stopped) {
        return POLL_IDLE_MS; /* given up: no move until a restart */
    }
    const int32_t travel = stepper_travel_steps();
    if (target != s_last_target) {
        if (!stepper_ready() && !stepper_busy()) {
            /* After a closing that did not reach the end stop (and backed up): find the closed position again
             * first. While it still backs up, wait (the move below is refused). */
            ESP_LOGI(TAG, "Homing again before moving");
            home_or_give_up();
            if (s_stopped) {
                return POLL_IDLE_MS;
            }
        }
        /* Refused while a closing checks the end stop or backs up: tried again at the next pass */
        if (stepper_move_to(target)) {
            ESP_LOGI(TAG, "%s, level %u: to %ld/%ld steps", (want >> 8) & 1 ? "on" : "off", (unsigned)(want & 0xff),
                     (long)target, (long)travel);
            s_last_target = target;
        }
    }

    led_bar_set_exposed(lift_exposed_leds(stepper_position(), CONFIG_LED_COUNT, travel));

    switch (stepper_poll()) {
    case STEPPER_EVENT_REZEROED:
        ESP_LOGW(TAG, "Closed on the end stop more than 1 mm off: steps were lost, position corrected");
        break;
    case STEPPER_EVENT_FAULT:
        ESP_LOGE(TAG, "End stop pressed where the lamp cannot be closed (broken wire, noise, lost steps): homing "
                      "again");
        home_or_give_up();
        s_last_target = -1; /* go back to the wanted height */
        break;
    case STEPPER_EVENT_NOT_CLOSED:
        ESP_LOGE(TAG, "Closing did not reach the end stop (something in the way?): backed up 10 mm");
        if (++s_not_closed >= MAX_NOT_CLOSED) {
            give_up("The lamp did not close several times in a row");
        }
        /* Home again at the next command, not now: whatever blocked it may still be there */
        break;
    case STEPPER_EVENT_NONE:
        break;
    }
    if (s_stopped) {
        return POLL_IDLE_MS;
    }
    /* Closed on the end stop: checked right after the poll that ends the closing, before a waiting opening is
     * applied at the next pass */
    if (s_not_closed && s_last_target == 0 && stepper_ready() && !stepper_busy() && stepper_position() == 0) {
        s_not_closed = 0;
    }
    /* Moving, or a refused move to try again: soon */
    return stepper_busy() || target != s_last_target ? POLL_MOVING_MS : POLL_IDLE_MS;
}

static void lift_task(void *arg)
{
    led_bar_set_exposed(0); /* position unknown until homed: nothing lit */
    if (stepper_init() != ESP_OK) {
        give_up("Motor driver init failed");
    } else {
        home_or_give_up();
    }
    while (!s_stopped) {
        uint32_t want = s_want;
        uint32_t wait_ms = lift_pass(wanted_target(want, stepper_travel_steps()), want);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); /* keep the callback's task handle valid */
    }
}

esp_err_t lift_init(void)
{
    if (xTaskCreate(lift_task, "lift", 3072, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the lift task");
        return ESP_ERR_NO_MEM;
    }
    led_bar_set_target_callback(on_target);
    return ESP_OK;
}

#else

esp_err_t lift_init(void)
{
    ESP_LOGI(TAG, "No motor (CONFIG_MOTOR_ENABLED off): the LEDs follow the brightness");
    return ESP_OK;
}

#endif /* CONFIG_MOTOR_ENABLED */
