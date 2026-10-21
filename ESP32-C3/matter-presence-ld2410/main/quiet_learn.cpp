/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quiet_learn.h"
#include "radar_settings.h"

static const char *TAG = "quiet_learn";

static constexpr uint32_t kPollMs = 100;
// The radar sends about 10 engineering reports per second; fewer than 2 per second means the reports did not
// come through.
static constexpr uint32_t kMinFramesPerSec = 2;

// Factory defaults (HLK-LD2410 serial protocol, table 7). The still thresholds of gates 0 and 1 cannot be set.
static const uint8_t kFactoryMoving[LD2410_GATE_COUNT] = { 50, 50, 40, 30, 20, 15, 15, 15, 15 };
static const uint8_t kFactoryStill[LD2410_GATE_COUNT] = { 0, 0, 40, 40, 30, 30, 20, 20, 20 };
static constexpr int kFirstStillGate = 2;

// ---------------------------------------------------------------------------------------------------------------------
// Thresholds from a recording

// Energy of the frame at rank ceil(frames * percentile / 100) when the frames of the gate are sorted by energy
static uint8_t gate_noise(const uint16_t counts[LD2410_MAX_ENERGY + 1], uint8_t percentile)
{
    uint32_t frames = 0;
    for (int e = 0; e <= LD2410_MAX_ENERGY; e++) {
        frames += counts[e];
    }
    const uint32_t rank = (frames * percentile + 99) / 100;
    uint32_t seen = 0;
    for (int e = 0; e <= LD2410_MAX_ENERGY; e++) {
        seen += counts[e];
        if (seen > 0 && seen >= rank) {
            return (uint8_t)e;
        }
    }
    return 0; // no frames
}

void quiet_learn_noise(const ld2410_capture_t *capture, uint8_t percentile, quiet_learn_levels_t *out)
{
    for (int g = 0; g < LD2410_GATE_COUNT; g++) {
        out->moving[g] = gate_noise(capture->moving[g], percentile);
        out->still[g] = gate_noise(capture->still[g], percentile);
    }
}

static uint8_t learned_threshold(uint8_t noise, uint8_t margin, uint8_t max_raise, uint8_t factory)
{
    const int limit = factory + max_raise < 100 ? factory + max_raise : 100;
    int value = noise + margin;
    if (value > limit) {
        value = limit;
    }
    if (value < factory) {
        value = factory;
    }
    return (uint8_t)value;
}

void quiet_learn_compute_thresholds(const quiet_learn_levels_t *noise, const ld2410_thresholds_t *current,
                                    uint8_t margin, uint8_t max_raise, ld2410_thresholds_t *out)
{
    for (int g = 0; g < LD2410_GATE_COUNT; g++) {
        out->moving[g] = learned_threshold(noise->moving[g], margin, max_raise, kFactoryMoving[g]);
        out->still[g] = g < kFirstStillGate ? current->still[g]
                                            : learned_threshold(noise->still[g], margin, max_raise, kFactoryStill[g]);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Helpers

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

static bool same_thresholds(const ld2410_thresholds_t *a, const ld2410_thresholds_t *b)
{
    for (int g = 0; g < LD2410_GATE_COUNT; g++) {
        if (a->moving[g] != b->moving[g] || (g >= kFirstStillGate && a->still[g] != b->still[g])) {
            return false;
        }
    }
    return true;
}

static void log_no_answer()
{
    ESP_LOGE(TAG, "The radar does not answer commands (check the wire from GPIO%d to the LD2410 RX pin), nothing "
                  "changed", CONFIG_LD2410_UART_TX_PIN);
}

static void end_config()
{
    if (ld2410_config_end() != ESP_OK) {
        ESP_LOGW(TAG, "Could not end the configuration mode");
    }
}

// " (3.75 m to 4.5 m)" for gate 5 at 75 cm per gate, "" when the gate length is unknown
static void format_gate_span(int gate, uint16_t gate_cm, char *buf, size_t len)
{
    if (gate_cm == 0) {
        buf[0] = '\0';
        return;
    }
    char from[12];
    char to[12];
    radar_format_distance((uint32_t)gate * gate_cm, from, sizeof(from));
    radar_format_distance((uint32_t)(gate + 1) * gate_cm, to, sizeof(to));
    snprintf(buf, len, " (%s to %s)", from, to);
}

// "50,50,40,30,20,15,15,15,15", "-" for the still thresholds of gates 0 and 1
static void format_thresholds(const uint8_t *values, bool still, char *buf, size_t len)
{
    size_t used = 0;
    buf[0] = '\0';
    for (int g = 0; g < LD2410_GATE_COUNT && used < len; g++) {
        const char *sep = g == 0 ? "" : ",";
        const int n = still && g < kFirstStillGate ? snprintf(buf + used, len - used, "%s-", sep)
                                                   : snprintf(buf + used, len - used, "%s%u", sep, values[g]);
        if (n < 0) {
            break;
        }
        used += (size_t)n;
    }
}

static void log_threshold_list(const char *what, const ld2410_thresholds_t *thresholds)
{
    char moving[40];
    char still[40];
    format_thresholds(thresholds->moving, false, moving, sizeof(moving));
    format_thresholds(thresholds->still, true, still, sizeof(still));
    ESP_LOGI(TAG, "%s: moving %s | still %s", what, moving, still);
}

// Inside configuration mode: writes the thresholds of every gate, then reads them back. True when the radar holds
// them.
static bool write_thresholds(const ld2410_thresholds_t *want)
{
    for (uint8_t g = 0; g < LD2410_GATE_COUNT; g++) {
        if (ld2410_set_gate_thresholds(g, want->moving[g], want->still[g]) != ESP_OK) {
            ESP_LOGE(TAG, "Writing gate %u failed, the radar may hold a mix of old and new thresholds", g);
            return false;
        }
    }
    ld2410_thresholds_t check;
    if (ld2410_read_thresholds(&check) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read the thresholds back, the radar may hold a mix of old and new thresholds");
        return false;
    }
    for (int g = 0; g < LD2410_GATE_COUNT; g++) {
        if (check.moving[g] != want->moving[g] || (g >= kFirstStillGate && check.still[g] != want->still[g])) {
            ESP_LOGE(TAG, "Gate %d reads back %u/%u instead of %u/%u, the radar may hold a mix of old and new "
                          "thresholds", g, check.moving[g], check.still[g], want->moving[g], want->still[g]);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Learning on the ESP32

// A threshold that could not follow the noise of its gate: limited to the factory default +
// CONFIG_LD2410_LEARN_MAX_RAISE, or raised to 100.
static void warn_gate(int gate, const char *kind, uint8_t noise, uint8_t threshold, uint8_t factory, uint16_t gate_cm)
{
    char span[32];
    format_gate_span(gate, gate_cm, span, sizeof(span));
    if (threshold >= 100) {
        ESP_LOGW(TAG, "Gate %d%s: %s noise %u, threshold 100: no %s target is seen there any more", gate, span, kind,
                 noise, kind);
    } else if (noise + CONFIG_LD2410_LEARN_MARGIN > threshold) {
        ESP_LOGW(TAG, "Gate %d%s: %s noise %u, threshold limited to %u (factory %u + %d). Something kept moving "
                      "there while recording (person, fan, curtain) and may be reported as presence: learn again "
                      "without it, or raise \"Highest raise above the factory threshold\" in menuconfig",
                 gate, span, kind, noise, threshold, factory, CONFIG_LD2410_LEARN_MAX_RAISE);
    }
}

static void log_thresholds(uint32_t frames, uint16_t gate_cm, const quiet_learn_levels_t *noise,
                           const quiet_learn_levels_t *peak, const ld2410_thresholds_t *before,
                           const ld2410_thresholds_t *after)
{
    ESP_LOGI(TAG, "%" PRIu32 " reports. Noise = energy of %d %% of them, threshold = noise + %d, from the factory "
                  "default to factory + %d", frames, CONFIG_LD2410_LEARN_PERCENTILE, CONFIG_LD2410_LEARN_MARGIN,
             CONFIG_LD2410_LEARN_MAX_RAISE);
    char each[24] = "";
    if (gate_cm != 0) {
        char length[12];
        radar_format_distance(gate_cm, length, sizeof(length));
        snprintf(each, sizeof(each), " (%s each)", length);
    }
    ESP_LOGI(TAG, "Gate%s: moving noise (highest) -> threshold (was) | still noise (highest) -> threshold (was)",
             each);
    for (int g = 0; g < LD2410_GATE_COUNT; g++) {
        if (g < kFirstStillGate) {
            ESP_LOGI(TAG, "Gate %d: %3u (%3u) -> %3u (%3u) | not settable", g, noise->moving[g], peak->moving[g],
                     after->moving[g], before->moving[g]);
        } else {
            ESP_LOGI(TAG, "Gate %d: %3u (%3u) -> %3u (%3u) | %3u (%3u) -> %3u (%3u)", g, noise->moving[g],
                     peak->moving[g], after->moving[g], before->moving[g], noise->still[g], peak->still[g],
                     after->still[g], before->still[g]);
        }
    }
    for (int g = 0; g < LD2410_GATE_COUNT; g++) {
        warn_gate(g, "moving", noise->moving[g], after->moving[g], kFactoryMoving[g], gate_cm);
        if (g >= kFirstStillGate) {
            warn_gate(g, "still", noise->still[g], after->still[g], kFactoryStill[g], gate_cm);
        }
    }
}

// Inside configuration mode: computes and writes the new thresholds, then reads them back. True when the radar
// holds the new values.
static bool apply_thresholds(const ld2410_capture_t *capture, uint16_t gate_cm)
{
    ld2410_thresholds_t before;
    if (ld2410_read_thresholds(&before) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read the radar thresholds, nothing changed");
        return false;
    }
    quiet_learn_levels_t noise;
    quiet_learn_levels_t peak;
    quiet_learn_noise(capture, CONFIG_LD2410_LEARN_PERCENTILE, &noise);
    quiet_learn_noise(capture, 100, &peak);
    ld2410_thresholds_t after;
    quiet_learn_compute_thresholds(&noise, &before, CONFIG_LD2410_LEARN_MARGIN, CONFIG_LD2410_LEARN_MAX_RAISE, &after);
    log_thresholds(capture->frames, gate_cm, &noise, &peak, &before, &after);
    return write_thresholds(&after);
}

// Records the engineering reports after wait_ms more of the delay before recording, then writes the thresholds.
static quiet_learn_result_t run_with_capture(ld2410_capture_t *capture, quiet_learn_cancelled_cb_t cancelled,
                                             quiet_learn_phase_cb_t on_phase, uint16_t gate_cm, uint32_t wait_ms)
{
    const uint32_t duration_s = CONFIG_LD2410_LEARN_DURATION_SEC;
    if (!wait_unless_cancelled(wait_ms, cancelled)) {
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
        log_no_answer();
        return QUIET_LEARN_FAILED;
    }

    ESP_LOGI(TAG, "Recording the empty room for %" PRIu32 " s", duration_s);
    if (on_phase) {
        on_phase(QUIET_LEARN_RECORDING);
    }
    ld2410_capture_start(capture);
    const bool completed = wait_unless_cancelled(duration_s * 1000, cancelled);
    ld2410_capture_stop();

    if (ld2410_config_begin() != ESP_OK) {
        ESP_LOGE(TAG, "The radar stopped answering commands, nothing changed (engineering mode stays on until the "
                      "radar restarts)");
        return QUIET_LEARN_FAILED;
    }
    quiet_learn_result_t result = QUIET_LEARN_FAILED;
    if (!completed) {
        ESP_LOGI(TAG, "Learning cancelled, nothing changed");
        result = QUIET_LEARN_CANCELLED;
    } else if (capture->frames < duration_s * kMinFramesPerSec) {
        ESP_LOGE(TAG, "Only %" PRIu32 " engineering reports in %" PRIu32 " s, nothing changed", capture->frames,
                 duration_s);
    } else if (apply_thresholds(capture, gate_cm)) {
        result = QUIET_LEARN_SAVED;
    }
    if (ld2410_set_engineering_mode(false) != ESP_OK) {
        ESP_LOGW(TAG, "Could not leave engineering mode (it ends when the radar restarts)");
    }
    end_config();
    if (result == QUIET_LEARN_SAVED) {
        ESP_LOGI(TAG, "Learning done: new thresholds saved in the LD2410 (%" PRIu32 " reports)", capture->frames);
    }
    return result;
}

static quiet_learn_result_t run_on_esp32(quiet_learn_cancelled_cb_t cancelled, quiet_learn_phase_cb_t on_phase,
                                         uint16_t gate_cm, uint32_t wait_ms, quiet_learn_noise_cb_t on_noise)
{
    // Energy counts of every gate (about 3.6 KB), only while learning
    ld2410_capture_t *capture = static_cast<ld2410_capture_t *>(malloc(sizeof(ld2410_capture_t)));
    if (capture == nullptr) {
        ESP_LOGE(TAG, "Not enough memory for the recording (%u bytes), nothing changed",
                 (unsigned)sizeof(ld2410_capture_t));
        return QUIET_LEARN_FAILED;
    }
    const quiet_learn_result_t result = run_with_capture(capture, cancelled, on_phase, gate_cm, wait_ms);
    if (result == QUIET_LEARN_SAVED && on_noise) {
        on_noise(capture, gate_cm);
    }
    free(capture);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The radar's own background noise detection (command 0x000B)

#if CONFIG_LD2410_LEARN_RADAR_AUTO
// The radar waits this long before it measures
static constexpr uint32_t kRadarLeaveMs = 10 * 1000;
// It reports LD2410_STATE_NOISE_RUNNING within this time once it accepted the command
static constexpr uint32_t kRadarStartMs = 15 * 1000;
// Margin after its wait and measuring time before giving up
static constexpr uint32_t kRadarMarginMs = 30 * 1000;
// The radar answers commands again about a second after a restart
static constexpr uint32_t kRadarRestartMs = 1000;
// The noise of the empty room for the presence engine needs more engineering reports than the thresholds
static constexpr uint32_t kMinNoiseFramesPerSec = 5;

enum RadarAutoStart { kRadarAutoStarted, kRadarAutoUnsupported, kRadarAutoNoAnswer };

// Reads the radar version and thresholds, then asks the radar for its background noise detection. Unsupported: the
// radar refused the command or did not answer it, but answers the others. engineering_off: the engineering mode
// is turned off first, so that the radar measures as without it (the presence engine keeps it on otherwise).
static RadarAutoStart start_radar_auto(uint32_t duration_s, ld2410_thresholds_t *before, bool *before_ok,
                                       bool engineering_off)
{
    if (ld2410_config_begin() != ESP_OK) {
        return kRadarAutoNoAnswer;
    }
    if (engineering_off && ld2410_set_engineering_mode(false) != ESP_OK) {
        ESP_LOGW(TAG, "Could not leave engineering mode before the radar measures");
    }
    ld2410_version_t version;
    if (ld2410_read_firmware_version(&version) == ESP_OK) {
        char v[24];
        ld2410_format_version(&version, v, sizeof(v));
        ESP_LOGI(TAG, "LD2410 firmware %s", v);
    }
    *before_ok = ld2410_read_thresholds(before) == ESP_OK;
    const esp_err_t err = ld2410_start_noise_detection((uint16_t)duration_s);
    end_config();
    return err == ESP_OK ? kRadarAutoStarted : kRadarAutoUnsupported;
}

// Stops a running detection by restarting the radar; its thresholds stay as they were [not documented by Hi-Link].
// Returns once the radar answers commands again, so a command sent next (e.g. the factory thresholds chosen while
// the radar measured) does not time out.
static void abort_radar_auto()
{
    if (ld2410_config_begin() != ESP_OK) {
        ESP_LOGW(TAG, "Could not restart the radar to stop its background noise detection");
        return;
    }
    if (ld2410_restart() != ESP_OK) {
        ESP_LOGW(TAG, "The radar refused to restart, its background noise detection may go on");
        end_config();
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(kRadarRestartMs));
}

static void log_radar_auto_result(const ld2410_thresholds_t *before, bool before_ok, const ld2410_thresholds_t *after)
{
    if (before_ok) {
        log_threshold_list("Thresholds before", before);
    }
    log_threshold_list("Thresholds set by the radar", after);
    for (int g = 0; g < LD2410_GATE_COUNT; g++) {
        if (after->moving[g] > kFactoryMoving[g] + CONFIG_LD2410_LEARN_MAX_RAISE) {
            ESP_LOGW(TAG, "Gate %d: moving threshold %u, more than factory %u + %d: something kept moving there "
                          "while measuring?", g, after->moving[g], kFactoryMoving[g], CONFIG_LD2410_LEARN_MAX_RAISE);
        }
        if (g >= kFirstStillGate && after->still[g] > kFactoryStill[g] + CONFIG_LD2410_LEARN_MAX_RAISE) {
            ESP_LOGW(TAG, "Gate %d: still threshold %u, more than factory %u + %d: something kept moving there "
                          "while measuring?", g, after->still[g], kFactoryStill[g], CONFIG_LD2410_LEARN_MAX_RAISE);
        }
    }
}

// Engineering reports of the empty room for the presence engine, before the radar measures itself. False: nothing
// usable, *result says why (logged).
static bool record_noise(ld2410_capture_t *capture, uint32_t seconds, quiet_learn_cancelled_cb_t cancelled,
                         quiet_learn_phase_cb_t on_phase, quiet_learn_result_t *result)
{
    *result = QUIET_LEARN_FAILED;
    esp_err_t err = ld2410_config_begin();
    if (err == ESP_OK) {
        err = ld2410_set_engineering_mode(true);
        if (ld2410_config_end() != ESP_OK && err == ESP_OK) {
            err = ESP_FAIL;
        }
    }
    if (err != ESP_OK) {
        log_no_answer();
        return false;
    }
    ESP_LOGI(TAG, "Recording the noise of the empty room on the ESP32 for %" PRIu32 " s, then the LD2410 measures",
             seconds);
    if (on_phase) {
        on_phase(QUIET_LEARN_RECORDING);
    }
    ld2410_capture_start(capture);
    const bool completed = wait_unless_cancelled(seconds * 1000, cancelled);
    ld2410_capture_stop();
    if (!completed) {
        ESP_LOGI(TAG, "Learning cancelled, nothing changed");
        *result = QUIET_LEARN_CANCELLED;
        return false;
    }
    if (capture->frames < seconds * kMinNoiseFramesPerSec) {
        ESP_LOGE(TAG, "Only %" PRIu32 " engineering reports in %" PRIu32 " s, nothing changed", capture->frames,
                 seconds);
        return false;
    }
    return true;
}

// Waits for the radar's background noise detection to end, then reads the thresholds it stored.
static quiet_learn_result_t run_radar_auto(quiet_learn_cancelled_cb_t cancelled, quiet_learn_phase_cb_t on_phase,
                                           uint32_t duration_s, const ld2410_thresholds_t *before, bool before_ok)
{
    ESP_LOGI(TAG, "The LD2410 measures the empty room itself: %" PRIu32 " s more to leave, then %" PRIu32 " s",
             kRadarLeaveMs / 1000, duration_s);
    const uint32_t limit_ms = kRadarLeaveMs + duration_s * 1000 + kRadarMarginMs;
    bool recording = false;
    bool running = false;
    uint8_t end_state = 0;
    for (uint32_t t = 0;; t += kPollMs) {
        if (cancelled && cancelled()) {
            abort_radar_auto();
            ESP_LOGI(TAG, "Learning cancelled, radar restarted to stop its detection");
            return QUIET_LEARN_CANCELLED;
        }
        if (!recording && t >= kRadarLeaveMs) {
            recording = true;
            if (on_phase) {
                on_phase(QUIET_LEARN_RECORDING);
            }
        }
        ld2410_target_t target;
        if (ld2410_get_target(&target)) {
            if (target.state == LD2410_STATE_NOISE_RUNNING) {
                running = true;
            } else if (target.state == LD2410_STATE_NOISE_DONE || target.state == LD2410_STATE_NOISE_FAILED) {
                end_state = target.state;
                break;
            } else if (running) {
                break; // back to normal reports without a result report
            }
        }
        if (!running && t >= kRadarStartMs) {
            ESP_LOGE(TAG, "The radar accepted the background noise detection but does not run it, nothing changed");
            return QUIET_LEARN_FAILED;
        }
        if (t >= limit_ms) {
            abort_radar_auto();
            ESP_LOGE(TAG, "The radar's background noise detection did not end in %" PRIu32 " s, radar restarted",
                     limit_ms / 1000);
            return QUIET_LEARN_FAILED;
        }
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }

    if (ld2410_config_begin() != ESP_OK) {
        ESP_LOGE(TAG, "The radar stopped answering commands after its background noise detection");
        return QUIET_LEARN_FAILED;
    }
    uint16_t status = 0xFFFF;
    const bool status_ok = ld2410_read_noise_detection_status(&status) == ESP_OK;
    ld2410_thresholds_t after;
    const bool after_ok = ld2410_read_thresholds(&after) == ESP_OK;
    end_config();
    const bool done = end_state == LD2410_STATE_NOISE_DONE || (end_state == 0 && status_ok && status == 2);
    if (!done) {
        ESP_LOGE(TAG, "The radar's background noise detection failed (report state %u, status %d)", end_state,
                 status_ok ? status : -1);
        return QUIET_LEARN_FAILED;
    }
    if (!after_ok) {
        ESP_LOGE(TAG, "The radar finished its background noise detection, but its thresholds could not be read");
        return QUIET_LEARN_FAILED;
    }
    log_radar_auto_result(before, before_ok, &after);
    ESP_LOGI(TAG, "Learning done: thresholds set by the LD2410 background noise detection");
    return QUIET_LEARN_SAVED;
}
#endif // CONFIG_LD2410_LEARN_RADAR_AUTO

// ---------------------------------------------------------------------------------------------------------------------

quiet_learn_result_t quiet_learn_run(quiet_learn_cancelled_cb_t cancelled, quiet_learn_phase_cb_t on_phase,
                                     uint16_t gate_cm, quiet_learn_noise_cb_t on_noise, uint32_t noise_seconds)
{
    const uint32_t delay_s = CONFIG_LD2410_LEARN_DELAY_SEC;
    const uint32_t duration_s = CONFIG_LD2410_LEARN_DURATION_SEC;
#if CONFIG_LD2410_LEARN_RADAR_AUTO
    if (on_noise) {
        ESP_LOGI(TAG, "Empty room learning: leave the room. In %" PRIu32 " s the ESP32 records the noise for %" PRIu32
                      " s, then the LD2410 measures the room itself (%" PRIu32 " s): stay out until the LED stops "
                      "blinking", delay_s, noise_seconds, kRadarLeaveMs / 1000 + duration_s);
    } else
#endif
    {
        ESP_LOGI(TAG, "Empty room learning: leave the room, recording starts in %" PRIu32 " s and lasts %" PRIu32
                      " s", delay_s, duration_s);
    }
    if (on_phase) {
        on_phase(QUIET_LEARN_WAITING);
    }
    uint32_t wait_ms = delay_s * 1000;
#if CONFIG_LD2410_LEARN_RADAR_AUTO
    // Noise of the empty room for the presence engine, recorded before the radar measures (allocated meanwhile)
    ld2410_capture_t *noise = nullptr;
    if (on_noise) {
        if (!wait_unless_cancelled(wait_ms, cancelled)) {
            ESP_LOGI(TAG, "Learning cancelled, nothing changed");
            return QUIET_LEARN_CANCELLED;
        }
        wait_ms = 0;
        noise = static_cast<ld2410_capture_t *>(malloc(sizeof(ld2410_capture_t)));
        if (noise == nullptr) {
            ESP_LOGE(TAG, "Not enough memory for the recording (%u bytes), nothing changed",
                     (unsigned)sizeof(ld2410_capture_t));
            return QUIET_LEARN_FAILED;
        }
        quiet_learn_result_t result;
        if (!record_noise(noise, noise_seconds, cancelled, on_phase, &result)) {
            free(noise);
            return result;
        }
    } else {
        // The radar waits kRadarLeaveMs itself: ask it that much before the end of the delay.
        const uint32_t lead_ms = wait_ms > kRadarLeaveMs ? wait_ms - kRadarLeaveMs : 0;
        if (!wait_unless_cancelled(lead_ms, cancelled)) {
            ESP_LOGI(TAG, "Learning cancelled, nothing changed");
            return QUIET_LEARN_CANCELLED;
        }
        wait_ms -= lead_ms;
    }
    ld2410_thresholds_t before;
    bool before_ok = false;
    switch (start_radar_auto(duration_s, &before, &before_ok, noise != nullptr)) {
    case kRadarAutoStarted: {
        const quiet_learn_result_t result = run_radar_auto(cancelled, on_phase, duration_s, &before, before_ok);
        if (result == QUIET_LEARN_SAVED && noise != nullptr) {
            on_noise(noise, gate_cm);
        }
        free(noise);
        return result;
    }
    case kRadarAutoNoAnswer:
        free(noise);
        log_no_answer();
        return QUIET_LEARN_FAILED;
    case kRadarAutoUnsupported:
        // The ESP32 records the room again for the thresholds (and the noise from that recording)
        free(noise);
        ESP_LOGI(TAG, "This LD2410 firmware has no background noise detection: learning on the ESP32");
        break;
    }
#endif
    return run_on_esp32(cancelled, on_phase, gate_cm, wait_ms, on_noise);
}

bool quiet_learn_restore_factory(void)
{
    ESP_LOGI(TAG, "Restoring the LD2410 factory thresholds");
    if (ld2410_config_begin() != ESP_OK) {
        log_no_answer();
        return false;
    }
    bool restored = false;
    ld2410_thresholds_t before;
    if (ld2410_read_thresholds(&before) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read the radar thresholds, nothing changed");
    } else {
        ld2410_thresholds_t factory;
        memcpy(factory.moving, kFactoryMoving, sizeof(factory.moving));
        for (int g = 0; g < LD2410_GATE_COUNT; g++) {
            factory.still[g] = g < kFirstStillGate ? before.still[g] : kFactoryStill[g];
        }
        log_threshold_list("Thresholds until now", &before);
        if (same_thresholds(&before, &factory)) {
            ESP_LOGI(TAG, "The LD2410 holds the factory thresholds already, nothing written");
            restored = true;
        } else {
            restored = write_thresholds(&factory);
        }
        if (restored) {
            log_threshold_list("Factory thresholds", &factory);
        }
    }
    end_config();
    return restored;
}
