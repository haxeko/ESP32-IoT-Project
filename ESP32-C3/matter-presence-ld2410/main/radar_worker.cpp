/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <nvs.h>
#include <sdkconfig.h>

#include <algorithm>
#include <atomic>

#include <drivers/ld2410.h>
#include <drivers/status_led.h>

#include "quiet_learn.h"
#include "radar_worker.h"

#if CONFIG_PRESENCE_ENERGY_FILTER
#include "energy_filter.h"
#endif
#if CONFIG_PRESENCE_ESP_ENGINE
#include "esp_presence.h"
#endif

static const char *TAG = "radar_ctl";

static constexpr uint32_t kTaskStack = 4096;
// Above the Matter task (1), below the Wi-Fi and lwIP tasks, like the presence task
static constexpr UBaseType_t kTaskPriority = 2;
// Next reads while the radar does not answer (not connected, still starting)
static constexpr uint32_t kSyncRetryFirstMs = 15 * 1000;
static constexpr uint32_t kSyncRetryMaxMs = 5 * 60 * 1000;

static constexpr size_t kTextLen = 24;

// ---------------------------------------------------------------------------------------------------------------------
// Requests, from the Matter task and the button to the worker

// Empty room learning and factory thresholds, requested by Home Assistant (Matter task) or the button (esp_timer
// task) and run by the worker. Both change together under s_requests_lock, so a choice from Home Assistant and a
// click at the same moment cannot leave a request half made. The last choice wins: Factory thresholds cancels a
// learning, Learning or Off (or three clicks) drops a Factory thresholds that has not started.
enum LearnState : uint8_t { kLearnIdle, kLearnRequested, kLearnRunning, kLearnCancelling };
static portMUX_TYPE s_requests_lock = portMUX_INITIALIZER_UNLOCKED;
static LearnState s_learn_state = kLearnIdle; // s_requests_lock
static bool s_want_factory;                   // s_requests_lock: chosen, not written yet

// Range (gate), Radar resolution (gate length) and Radar Bluetooth (off / on) chosen in Home Assistant, RADAR_KEEP
// once taken.
static std::atomic<int32_t> s_want_gate{RADAR_KEEP};
static std::atomic<int32_t> s_want_gate_cm{RADAR_KEEP};
static std::atomic<int32_t> s_want_bluetooth{RADAR_KEEP};

// Set by the worker once the radar settings have been read.
static std::atomic<bool> s_synced{false};
static std::atomic<TaskHandle_t> s_task{nullptr};
static radar_worker_view_t s_view;

// s_requests_lock held. Learning starts a learning (or keeps a cancelled one running), Off cancels it.
static void request_learning_locked(bool on)
{
    const LearnState state = s_learn_state;
    if (on) {
        s_learn_state = state == kLearnIdle ? kLearnRequested : state == kLearnCancelling ? kLearnRunning : state;
    } else {
        s_learn_state = state == kLearnRequested ? kLearnIdle : state == kLearnRunning ? kLearnCancelling : state;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Bluetooth of the radar. Off until it is turned on from Home Assistant: the choice is kept in NVS and the worker
// brings the radar back to it at boot (the LD2410 leaves the factory with Bluetooth on). Every change is applied by
// a restart of the radar. Its password is left as stored in the radar (the key "bt_password" that firmware
// 1.0.111-1.0.115 stored in this namespace is left as it is).

static constexpr const char *kNvsNamespace = "ld2410";
static constexpr const char *kNvsBtOnKey = "bt_on"; // blob, 1 byte

// Worker only (and radar_worker_start() before the worker runs)
static bool s_bt_wanted; // chosen in Home Assistant, off until then
// The last Bluetooth on/off change did not work: not tried again before the next boot or the next choice in Home
// Assistant, so a radar that does not follow is not restarted over and over.
static bool s_bt_change_failed;

static void load_bt_choice()
{
    nvs_handle_t nvs;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &nvs) != ESP_OK) {
        return; // nothing stored yet: Bluetooth off
    }
    uint8_t on = 0;
    size_t len = sizeof(on);
    s_bt_wanted = nvs_get_blob(nvs, kNvsBtOnKey, &on, &len) == ESP_OK && len == sizeof(on) && on != 0;
    nvs_close(nvs);
}

static void set_bt_choice(bool on)
{
    if (on == s_bt_wanted) {
        return;
    }
    s_bt_wanted = on;
    const uint8_t value = on ? 1 : 0;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, kNvsBtOnKey, &value, sizeof(value));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not save %s in NVS, err:%d", kNvsBtOnKey, err);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Worker task

static radar_settings_t s_settings; // last settings read from the radar (s_synced)
static esp_err_t s_session_err;     // result of the last session

// A configuration session starts or ends: the ESP32 presence engine labels the silence of the radar meanwhile as a
// configuration, not as a fault of its own.
static void radar_busy(bool busy)
{
#if CONFIG_PRESENCE_ESP_ENGINE
    esp_presence_set_radar_busy(busy);
#else
    (void)busy;
#endif
}

#if CONFIG_PRESENCE_ENERGY_FILTER || CONFIG_PRESENCE_ESP_ENGINE
// The energy filter and the presence engine need the engineering mode, which the radar leaves at each restart
// (Radar resolution, Radar Bluetooth, power) and the learning on the ESP32 turns off: checked every 2 s, turned on
// again at most every 10 s.
static constexpr uint32_t kEngineeringCheckMs = 2000;
static constexpr int64_t kEngineeringRetryMs = 10 * 1000;
static int64_t s_engineering_tried_ms = -kEngineeringRetryMs;
#if CONFIG_PRESENCE_ENERGY_FILTER
static bool s_thresholds_changed; // a learning or the factory thresholds ran: read the thresholds again
static const char kEngineeringUser[] = "the energy filter";
static const char kEngineeringMeanwhile[] = "the radar decides alone meanwhile";
#else
static const char kEngineeringUser[] = "the ESP32 presence engine";
static const char kEngineeringMeanwhile[] = "presence stays as it is meanwhile";
#endif

static bool engineering_needed(int64_t now_ms)
{
#if CONFIG_PRESENCE_ENERGY_FILTER
    return energy_filter_needs_engineering(now_ms);
#else
    return esp_presence_needs_engineering();
#endif
}

static void keep_engineering_mode()
{
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (!engineering_needed(now_ms) || now_ms - s_engineering_tried_ms < kEngineeringRetryMs) {
        return;
    }
    s_engineering_tried_ms = now_ms;
    radar_busy(true);
    esp_err_t err = ld2410_config_begin();
    if (err == ESP_OK) {
        err = ld2410_set_engineering_mode(true);
        const esp_err_t end_err = ld2410_config_end();
        if (err == ESP_OK) {
            err = end_err;
        }
    }
    radar_busy(false);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Engineering mode on: the energy of every gate for %s", kEngineeringUser);
    } else {
        ESP_LOGW(TAG, "Could not turn the engineering mode on (err:%d), %s", err, kEngineeringMeanwhile);
    }
}
#endif

#if CONFIG_PRESENCE_ESP_ENGINE
// The radar sends nothing valid any more although its settings were read: the reader drops what waits in the UART
// and the radar is asked to leave the configuration mode (a radar left in it sends no reports, e.g. after the ESP32
// restarted in the middle of a session), then restarted (nothing is written to its flash) after 15 s, 30 s, 1 min...
// at most every 5 min. Presence stays as it is meanwhile (a sensor fault is no empty room).
static constexpr uint32_t kRecoverAfterMs = 5000;
static constexpr int64_t kRestartFirstMs = 15 * 1000;
static constexpr int64_t kRestartMaxMs = 5 * 60 * 1000;
static int s_recover_step;          // 0: reports arrive, 1: resynchronised, 2+: restarts sent
static int64_t s_recover_next_ms;
static int64_t s_restart_delay_ms;

static void recover_radar()
{
    const uint32_t silent = esp_presence_silent_ms();
    if (silent < kRecoverAfterMs) {
        if (s_recover_step != 0) {
            ESP_LOGI(TAG, "The LD2410 reports again");
        }
        s_recover_step = 0;
        return;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (s_recover_step == 0) {
        ESP_LOGW(TAG, "No valid report from the LD2410 for %" PRIu32 " s: resynchronising the UART and leaving the "
                      "configuration mode in case the radar was left in it",
                 silent / 1000);
        ld2410_resync();
        radar_busy(true);
        (void)ld2410_config_end();
        radar_busy(false);
        s_recover_step = 1;
        s_restart_delay_ms = kRestartFirstMs;
        s_recover_next_ms = now_ms + s_restart_delay_ms;
        return;
    }
    if (now_ms < s_recover_next_ms) {
        return;
    }
    ESP_LOGW(TAG, "No valid report from the LD2410 for %" PRIu32 " s: restarting the radar (check its power and the "
                  "wire from radar TX to GPIO%d)",
             silent / 1000, CONFIG_LD2410_UART_RX_PIN);
    radar_busy(true);
    if (ld2410_config_begin() == ESP_OK) {
        if (ld2410_restart() != ESP_OK) {
            (void)ld2410_config_end(); // refused: leave the configuration mode
        }
    }
    radar_busy(false);
    s_recover_step++;
    s_restart_delay_ms = std::min(s_restart_delay_ms * 2, kRestartMaxMs);
    s_recover_next_ms = now_ms + s_restart_delay_ms;
}
#endif

static void log_settings(const char *what, const radar_settings_t &settings)
{
    const ld2410_config_t &cfg = settings.config;
    char duration[kTextLen];
    char moving[kTextLen];
    char still[kTextLen];
    char mac[kTextLen];
    radar_format_duration(cfg.no_one_duration_s, duration, sizeof(duration));
    radar_format_distance((uint32_t)cfg.max_moving_gate * settings.gate_cm, moving, sizeof(moving));
    radar_format_distance((uint32_t)cfg.max_still_gate * settings.gate_cm, still, sizeof(still));
    radar_format_mac(settings.bt_mac, mac, sizeof(mac));
    ESP_LOGI(TAG, "%s: no-one duration %s, range moving %s (gate %u), still %s (gate %u), %u cm per gate%s, "
                  "Bluetooth %s%s", what, duration, moving, cfg.max_moving_gate, still, cfg.max_still_gate,
             settings.gate_cm, settings.resolution_settable ? "" : " (fixed)",
             !settings.bluetooth_known ? "not available" : settings.bluetooth_on ? "on, MAC " : "off",
             settings.bluetooth_known && settings.bluetooth_on ? mac : "");
}

static void show_settings(const radar_settings_t &settings)
{
    if (s_view.show_settings) {
        s_view.show_settings(&settings);
    }
}

static void show_learn_mode(uint8_t mode)
{
    if (s_view.show_learn_mode) {
        s_view.show_learn_mode(mode);
    }
}

static bool is_change(const radar_settings_request_t &want)
{
    return want.gate != RADAR_KEEP || want.gate_cm != RADAR_KEEP || want.bluetooth != RADAR_KEEP;
}

// Reads the radar settings, applies the requested ones and shows the result in Home Assistant. False when the
// radar did not answer as expected, so the settings must be read again.
static bool settings_session(const radar_settings_request_t &want)
{
    const bool change = is_change(want);
    radar_settings_t now = {};
    radar_busy(true);
    const esp_err_t err = radar_settings_session(&want, &now);
    radar_busy(false);
    s_session_err = err;
    if (err != ESP_OK) {
        if (change && s_synced) {
            ESP_LOGE(TAG, "Changing the radar settings failed, err:%d", err);
            show_settings(s_settings); // back to the settings last read from the radar
        }
        return err == ESP_ERR_NOT_SUPPORTED && s_synced; // nothing wrong with the radar
    }
    if (!s_synced || change) {
        log_settings(s_synced ? "New LD2410 settings" : "LD2410 settings", now);
    }
    s_settings = now;
    s_synced = true;
#if CONFIG_PRESENCE_ENERGY_FILTER
    energy_filter_configure(&now);
#endif
#if CONFIG_PRESENCE_ESP_ENGINE
    esp_presence_configure(&now);
#endif
    show_settings(now);
    return true;
}

// After a successful session: brings the radar's Bluetooth to the state chosen in Home Assistant (off by
// default). False when the radar did not answer as expected.
static bool fix_bluetooth()
{
    if (!s_settings.bluetooth_known || s_bt_change_failed || s_settings.bluetooth_on == s_bt_wanted) {
        return true;
    }
    ESP_LOGI(TAG, "Turning the LD2410 Bluetooth %s (%s)", s_bt_wanted ? "on" : "off",
             s_bt_wanted ? "as chosen in Home Assistant" : "default, or as chosen in Home Assistant");
    radar_settings_request_t fix = {};
    fix.bluetooth = s_bt_wanted ? RADAR_BLUETOOTH_ON : RADAR_BLUETOOTH_OFF;
    const bool answered = settings_session(fix);
    if (!answered || s_settings.bluetooth_on != s_bt_wanted) {
        s_bt_change_failed = true;
    }
    return answered;
}

static bool learning_cancelled()
{
    taskENTER_CRITICAL(&s_requests_lock);
    const bool cancelled = s_learn_state == kLearnCancelling;
    taskEXIT_CRITICAL(&s_requests_lock);
    return cancelled;
}

static void show_learning_phase(quiet_learn_phase_t phase)
{
    status_led_set_learning(phase == QUIET_LEARN_RECORDING ? STATUS_LED_LEARNING_RECORDING
                                                           : STATUS_LED_LEARNING_WAITING);
}

static void run_requested_learning()
{
    taskENTER_CRITICAL(&s_requests_lock);
    const bool start = s_learn_state == kLearnRequested;
    if (start) {
        s_learn_state = kLearnRunning;
    }
    taskEXIT_CRITICAL(&s_requests_lock);
    if (!start) {
        return;
    }
    // Shown already when chosen in Home Assistant; covers the button and a request made while the previous
    // learning ended.
    show_learn_mode(RADAR_LEARN_ON);
    // gate_cm is 0 (unknown) before the radar settings were read
    const quiet_learn_result_t result = quiet_learn_run(learning_cancelled, show_learning_phase, s_settings.gate_cm);
#if CONFIG_PRESENCE_ENERGY_FILTER
    s_thresholds_changed = true;
#endif
    // On or blinking for 10 s; back to normal at once when cancelled
    status_led_set_learning(result == QUIET_LEARN_SAVED    ? STATUS_LED_LEARNING_DONE
                            : result == QUIET_LEARN_FAILED ? STATUS_LED_LEARNING_FAILED
                                                           : STATUS_LED_LEARNING_NONE);
    taskENTER_CRITICAL(&s_requests_lock);
    s_learn_state = kLearnIdle;
    const bool factory = s_want_factory;
    taskEXIT_CRITICAL(&s_requests_lock);
    // Cancelled by Factory thresholds: the select keeps showing it while they are written.
    if (!factory) {
        show_learn_mode(RADAR_LEARN_OFF);
    }
}

// Factory thresholds chosen in Home Assistant or with four clicks: written, the LED blinks or shows a failure for a
// while, then the select shows Off (or Learning when chosen meanwhile).
static void run_requested_factory_thresholds()
{
    taskENTER_CRITICAL(&s_requests_lock);
    const bool wanted = s_want_factory;
    s_want_factory = false;
    taskEXIT_CRITICAL(&s_requests_lock);
    if (!wanted) {
        return;
    }
    // Shown already when chosen in Home Assistant; covers the button.
    show_learn_mode(RADAR_LEARN_FACTORY);
    const bool restored = quiet_learn_restore_factory();
#if CONFIG_PRESENCE_ENERGY_FILTER
    s_thresholds_changed = true;
#endif
    status_led_set_learning(restored ? STATUS_LED_LEARNING_FACTORY : STATUS_LED_LEARNING_FAILED);
    // Nothing chosen meanwhile (no learning, no new factory thresholds)
    taskENTER_CRITICAL(&s_requests_lock);
    const bool idle = s_learn_state == kLearnIdle && !s_want_factory;
    taskEXIT_CRITICAL(&s_requests_lock);
    if (idle) {
        show_learn_mode(RADAR_LEARN_OFF);
    }
}

static void radar_task(void *arg)
{
    bool stale = true; // the radar settings must be read (again)
    uint32_t retry_ms = kSyncRetryFirstMs;
    while (true) {
        // Settings first: a session takes a few seconds at most, a learning minutes.
        const int32_t bluetooth = s_want_bluetooth.exchange(RADAR_KEEP);
        if (bluetooth != RADAR_KEEP) {
            // Kept even if this session fails: applied again at the next boot.
            set_bt_choice(bluetooth == RADAR_BLUETOOTH_ON);
            s_bt_change_failed = false;
        }
        radar_settings_request_t want = {};
        want.gate = s_want_gate.exchange(RADAR_KEEP);
        want.gate_cm = s_want_gate_cm.exchange(RADAR_KEEP);
        want.bluetooth = bluetooth;
        if (stale || is_change(want)) {
            stale = !settings_session(want);
            if (bluetooth != RADAR_KEEP && (stale || s_settings.bluetooth_on != (bluetooth == RADAR_BLUETOOTH_ON))) {
                s_bt_change_failed = true; // not retried before the next boot or the next choice
            }
        }
        if (!stale) {
            stale = !fix_bluetooth();
        }
        run_requested_factory_thresholds();
        run_requested_learning();
#if CONFIG_PRESENCE_ENERGY_FILTER
        if (!stale && s_thresholds_changed) {
            s_thresholds_changed = false;
            stale = !settings_session(radar_settings_request_t{});
        }
#endif
#if CONFIG_PRESENCE_ESP_ENGINE
        if (!stale) {
            recover_radar();
        }
#endif
#if CONFIG_PRESENCE_ENERGY_FILTER || CONFIG_PRESENCE_ESP_ENGINE
        if (!stale) {
            keep_engineering_mode();
        }
#endif

        TickType_t wait = portMAX_DELAY;
        if (stale) {
            if (s_session_err == ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Could not read the LD2410 settings (check the wire from GPIO%d to the radar RX pin), "
                              "next try in %" PRIu32 " s", CONFIG_LD2410_UART_TX_PIN, retry_ms / 1000);
            } else {
                ESP_LOGW(TAG, "The LD2410 did not take the settings as expected (err:%d), read again in %" PRIu32 " s",
                         s_session_err, retry_ms / 1000);
            }
            wait = pdMS_TO_TICKS(retry_ms);
            retry_ms = std::min(retry_ms * 2, kSyncRetryMaxMs);
        } else {
            retry_ms = kSyncRetryFirstMs;
#if CONFIG_PRESENCE_ENERGY_FILTER || CONFIG_PRESENCE_ESP_ENGINE
            wait = pdMS_TO_TICKS(kEngineeringCheckMs);
#endif
        }
        ulTaskNotifyTake(pdTRUE, wait);
    }
}

// ---------------------------------------------------------------------------------------------------------------------

esp_err_t radar_worker_start(const radar_worker_view_t *view)
{
    s_view = *view;
#if CONFIG_PRESENCE_ESP_ENGINE
    // The engine runs the learning; the select shows it (Learning, then Off)
    esp_presence_set_learn_view(view->show_learn_mode);
#endif
    load_bt_choice();
    TaskHandle_t task = nullptr;
    if (xTaskCreate(radar_task, "radar_cfg", kTaskStack, nullptr, kTaskPriority, &task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_task = task;
    return ESP_OK;
}

esp_err_t radar_worker_select(radar_select_t select, uint8_t mode)
{
    TaskHandle_t task = s_task;
    if (select == RADAR_SELECT_LEARN) {
        if (mode != RADAR_LEARN_OFF && mode != RADAR_LEARN_ON && mode != RADAR_LEARN_FACTORY) {
            return ESP_ERR_INVALID_ARG;
        }
#if CONFIG_PRESENCE_ESP_ENGINE
        // The ESP32 learns the empty room itself: Learning = LEARN_EMPTY, Off = cancel, Factory thresholds = back to
        // the built-in thresholds of the engine (the radar's own thresholds are not used).
        const esp_presence_command_t command = mode == RADAR_LEARN_ON        ? ESP_PRESENCE_LEARN
                                               : mode == RADAR_LEARN_FACTORY ? ESP_PRESENCE_PROFILE_RESET
                                                                             : ESP_PRESENCE_LEARN_CANCEL;
        return esp_presence_command(command) ? ESP_OK : ESP_ERR_INVALID_STATE;
#endif
        if (mode != RADAR_LEARN_OFF && task == nullptr) {
            ESP_LOGE(TAG, "Cannot %s: the radar task is not running",
                     mode == RADAR_LEARN_FACTORY ? "restore the factory thresholds" : "start the empty room learning");
            return ESP_ERR_INVALID_STATE;
        }
        // The last choice wins (see s_want_factory)
        taskENTER_CRITICAL(&s_requests_lock);
        s_want_factory = mode == RADAR_LEARN_FACTORY;
        request_learning_locked(mode == RADAR_LEARN_ON);
        taskEXIT_CRITICAL(&s_requests_lock);
    } else {
        if (mode == RADAR_MODE_CUSTOM) {
            return ESP_OK; // the radar's own value: nothing to change
        }
        if (task == nullptr || !s_synced) {
            ESP_LOGW(TAG, "The LD2410 settings have not been read yet, change ignored");
            return ESP_ERR_INVALID_STATE;
        }
        switch (select) {
        case RADAR_SELECT_RANGE:
            if (mode < LD2410_MIN_MAX_GATE || mode > LD2410_MAX_MAX_GATE) {
                return ESP_ERR_INVALID_ARG;
            }
            s_want_gate = mode;
            break;
        case RADAR_SELECT_RESOLUTION:
            if (mode != RADAR_RESOLUTION_COARSE && mode != RADAR_RESOLUTION_FINE) {
                return ESP_ERR_INVALID_ARG;
            }
            s_want_gate_cm = mode == RADAR_RESOLUTION_FINE ? LD2410_FINE_GATE_CM : LD2410_DEFAULT_GATE_CM;
            break;
        default: // RADAR_SELECT_BLUETOOTH
            if (mode != RADAR_BLUETOOTH_OFF && mode != RADAR_BLUETOOTH_ON) {
                return ESP_ERR_INVALID_ARG;
            }
            s_want_bluetooth = mode;
            break;
        }
    }
    if (task != nullptr) {
        xTaskNotifyGive(task);
    }
    return ESP_OK;
}

bool radar_worker_request_learning(void)
{
#if CONFIG_PRESENCE_ESP_ENGINE
    return esp_presence_command(ESP_PRESENCE_LEARN);
#endif
    TaskHandle_t task = s_task;
    if (task == nullptr) {
        ESP_LOGW(TAG, "Cannot start the empty room learning: the radar task is not running");
        status_led_set_learning(STATUS_LED_LEARNING_FAILED);
        return false;
    }
    // Only from idle; a factory thresholds request is dropped (the last choice wins)
    taskENTER_CRITICAL(&s_requests_lock);
    const bool requested = s_learn_state == kLearnIdle;
    if (requested) {
        s_learn_state = kLearnRequested;
        s_want_factory = false;
    }
    taskEXIT_CRITICAL(&s_requests_lock);
    if (!requested) {
        ESP_LOGI(TAG, "Empty room learning already requested or running");
        return false;
    }
    xTaskNotifyGive(task);
    return true;
}

bool radar_worker_request_factory_thresholds(void)
{
#if CONFIG_PRESENCE_ESP_ENGINE
    return esp_presence_command(ESP_PRESENCE_PROFILE_RESET);
#endif
    TaskHandle_t task = s_task;
    if (task == nullptr) {
        ESP_LOGW(TAG, "Cannot restore the factory thresholds: the radar task is not running");
        status_led_set_learning(STATUS_LED_LEARNING_FAILED);
        return false;
    }
    // The last choice wins, as in Home Assistant: a learning requested or running is cancelled first.
    taskENTER_CRITICAL(&s_requests_lock);
    s_want_factory = true;
    request_learning_locked(false);
    taskEXIT_CRITICAL(&s_requests_lock);
    xTaskNotifyGive(task);
    return true;
}
