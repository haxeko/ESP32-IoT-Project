/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include "quiet_learn.h"

static const char *TAG = "quiet_learn";

static constexpr uint32_t kPollMs = 100;
// The radar sends about 10 engineering frames per second; fewer than 2 per
// second means the reports did not come through.
static constexpr uint32_t kMinFramesPerSec = 2;

// Factory defaults (HLK-LD2410 serial protocol, table 7). The still thresholds
// of gates 0 and 1 cannot be set.
static const uint8_t kFactoryMoving[LD2410_GATE_COUNT] = { 50, 50, 40, 30, 20, 15, 15, 15, 15 };
static const uint8_t kFactoryStill[LD2410_GATE_COUNT] = { 0, 0, 40, 40, 30, 30, 20, 20, 20 };
static constexpr int kFirstStillGate = 2;

static uint8_t learned_threshold(uint8_t noise, uint8_t margin, uint8_t factory)
{
    int value = noise + margin;
    if (value < factory) {
        value = factory;
    }
    return (uint8_t)(value > 100 ? 100 : value);
}

void quiet_learn_compute_thresholds(const ld2410_capture_t *noise, const ld2410_thresholds_t *current,
                                    uint8_t margin, ld2410_thresholds_t *out)
{
    for (int g = 0; g < LD2410_GATE_COUNT; g++) {
        out->moving[g] = learned_threshold(noise->moving[g], margin, kFactoryMoving[g]);
        out->still[g] = g < kFirstStillGate ? current->still[g]
                                            : learned_threshold(noise->still[g], margin, kFactoryStill[g]);
    }
}

// Sleeps ms in kPollMs steps; false when cancelled.
static bool wait_unless_cancelled(uint32_t ms, quiet_learn_cancelled_cb_t cancelled)
{
    for (uint32_t t = 0; t < ms; t += kPollMs) {
        if (cancelled && cancelled()) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
    return !(cancelled && cancelled());
}

static void log_thresholds(const ld2410_capture_t *noise, const ld2410_thresholds_t *before,
                           const ld2410_thresholds_t *after)
{
    ESP_LOGI(TAG, "Gate (0.75 m each): moving noise -> threshold (was) | still noise -> threshold (was)");
    for (int g = 0; g < LD2410_GATE_COUNT; g++) {
        if (g < kFirstStillGate) {
            ESP_LOGI(TAG, "Gate %d: %3u -> %3u (%3u) | not settable", g, noise->moving[g], after->moving[g],
                     before->moving[g]);
        } else {
            ESP_LOGI(TAG, "Gate %d: %3u -> %3u (%3u) | %3u -> %3u (%3u)", g, noise->moving[g], after->moving[g],
                     before->moving[g], noise->still[g], after->still[g], before->still[g]);
        }
    }
}

// Inside configuration mode: computes and writes the new thresholds, then reads
// them back. Returns true when the radar holds the new values.
static bool apply_thresholds(const ld2410_capture_t *noise)
{
    ld2410_thresholds_t before;
    if (ld2410_read_thresholds(&before) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read the radar thresholds, nothing changed");
        return false;
    }
    ld2410_thresholds_t after;
    quiet_learn_compute_thresholds(noise, &before, CONFIG_LD2410_LEARN_MARGIN, &after);
    log_thresholds(noise, &before, &after);

    for (uint8_t g = 0; g < LD2410_GATE_COUNT; g++) {
        if (ld2410_set_gate_thresholds(g, after.moving[g], after.still[g]) != ESP_OK) {
            ESP_LOGE(TAG, "Writing gate %u failed, the radar may hold a mix of old and new thresholds", g);
            return false;
        }
    }

    ld2410_thresholds_t check;
    if (ld2410_read_thresholds(&check) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read the thresholds back");
        return false;
    }
    for (int g = 0; g < LD2410_GATE_COUNT; g++) {
        if (check.moving[g] != after.moving[g] || (g >= kFirstStillGate && check.still[g] != after.still[g])) {
            ESP_LOGE(TAG, "Gate %d reads back %u/%u instead of %u/%u", g, check.moving[g], check.still[g],
                     after.moving[g], after.still[g]);
            return false;
        }
    }
    return true;
}

quiet_learn_result_t quiet_learn_run(quiet_learn_cancelled_cb_t cancelled, quiet_learn_phase_cb_t on_phase)
{
    const uint32_t delay_s = CONFIG_LD2410_LEARN_DELAY_SEC;
    const uint32_t duration_s = CONFIG_LD2410_LEARN_DURATION_SEC;
    ESP_LOGI(TAG, "Empty room learning: leave the room, recording starts in %" PRIu32 " s and lasts %" PRIu32 " s",
             delay_s, duration_s);
    if (on_phase) {
        on_phase(QUIET_LEARN_WAITING);
    }
    if (!wait_unless_cancelled(delay_s * 1000, cancelled)) {
        ESP_LOGI(TAG, "Learning cancelled, nothing changed");
        return QUIET_LEARN_CANCELLED;
    }

    // Engineering mode adds the energy of every gate to the reports.
    esp_err_t err = ld2410_config_begin();
    if (err == ESP_OK) {
        err = ld2410_set_engineering_mode(true);
        if (ld2410_config_end() != ESP_OK && err == ESP_OK) {
            err = ESP_FAIL;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "The radar does not answer commands (check the wire from GPIO%d to the LD2410 RX pin), "
                      "nothing changed", CONFIG_LD2410_UART_TX_PIN);
        return QUIET_LEARN_FAILED;
    }

    ESP_LOGI(TAG, "Recording the empty room for %" PRIu32 " s", duration_s);
    if (on_phase) {
        on_phase(QUIET_LEARN_RECORDING);
    }
    ld2410_capture_start();
    const bool completed = wait_unless_cancelled(duration_s * 1000, cancelled);
    ld2410_capture_t noise;
    ld2410_capture_stop(&noise);

    if (ld2410_config_begin() != ESP_OK) {
        ESP_LOGE(TAG, "The radar stopped answering commands, nothing changed (engineering mode stays on "
                      "until the radar restarts)");
        return QUIET_LEARN_FAILED;
    }
    quiet_learn_result_t result = QUIET_LEARN_FAILED;
    if (!completed) {
        ESP_LOGI(TAG, "Learning cancelled, nothing changed");
        result = QUIET_LEARN_CANCELLED;
    } else if (noise.frames < duration_s * kMinFramesPerSec) {
        ESP_LOGE(TAG, "Only %" PRIu32 " engineering reports in %" PRIu32 " s, nothing changed", noise.frames,
                 duration_s);
    } else if (apply_thresholds(&noise)) {
        result = QUIET_LEARN_SAVED;
    }
    if (ld2410_set_engineering_mode(false) != ESP_OK) {
        ESP_LOGW(TAG, "Could not leave engineering mode (it ends when the radar restarts)");
    }
    if (ld2410_config_end() != ESP_OK) {
        ESP_LOGW(TAG, "Could not end the configuration mode");
    }
    if (result == QUIET_LEARN_SAVED) {
        ESP_LOGI(TAG, "Learning done: new thresholds saved in the LD2410 (%" PRIu32 " reports)", noise.frames);
    }
    return result;
}
