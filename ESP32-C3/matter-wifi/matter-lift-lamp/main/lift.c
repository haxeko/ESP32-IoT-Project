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

static TaskHandle_t s_task;
/* Wanted state, written by the LED driver's callback, read by the task: (on << 8) | level, one word */
static volatile uint32_t s_want;

/* led_bar lock held: only store and wake the task */
static void on_target(bool on, uint8_t level)
{
    s_want = ((uint32_t)on << 8) | level;
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

#define MAX_NOT_CLOSED 3 /* closings that did not reach the end stop before giving up until a restart */

/* The lamp cannot be moved safely: driver off, the LEDs follow the level, until a restart */
static void give_up(const char *why)
{
    ESP_LOGE(TAG, "%s: motor off, the LEDs follow the brightness, the lamp does not move until restarted", why);
    stepper_disable();
    led_bar_follow_level();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); /* keep the callback's task handle valid */
    }
}

static void home_or_give_up(void)
{
    led_bar_set_exposed(0); /* position unknown while homing: nothing lit */
    esp_err_t err = stepper_home();
    if (err != ESP_OK) {
        give_up(esp_err_to_name(err));
    }
}

static void lift_task(void *arg)
{
    led_bar_set_exposed(0); /* position unknown until homed: nothing lit */
    if (stepper_init() != ESP_OK) {
        give_up("Motor driver init failed");
    }
    home_or_give_up();

    const int32_t travel = stepper_travel_steps();
    int32_t last_target = -1;
    int not_closed = 0;
    for (;;) {
        uint32_t want = s_want;
        bool on = (want >> 8) & 1;
        /* Off closes the lamp; level L opens it to expose the LEDs a plain bar would light for L */
        int32_t target = 0;
        if (on) {
            target = lift_steps_for_leds(led_bar_count_for_level((uint8_t)want), CONFIG_LED_COUNT, travel);
        }
        if (target != last_target) {
            if (!stepper_ready()) {
                /* After a closing that did not reach the end stop: find the closed position again first */
                ESP_LOGI(TAG, "Homing again before moving");
                home_or_give_up();
            }
            ESP_LOGI(TAG, "%s, level %u: to %ld/%ld steps", on ? "on" : "off", (unsigned)(want & 0xff), (long)target,
                     (long)travel);
            stepper_move_to(target);
            last_target = target;
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
            last_target = -1; /* go back to the wanted height */
            break;
        case STEPPER_EVENT_NOT_CLOSED:
            ESP_LOGE(TAG, "Closing did not reach the end stop (something in the way?): backed up 10 mm and let go");
            if (++not_closed >= MAX_NOT_CLOSED) {
                give_up("The lamp did not close several times");
            }
            /* Home again at the next command, not now: whatever blocked it may still be there */
            break;
        case STEPPER_EVENT_NONE:
            break;
        }

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(stepper_busy() ? POLL_MOVING_MS : POLL_IDLE_MS));
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
