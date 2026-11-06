// See battery.h. ESP-IDF v6.0.2, esp-matter 1.6.1, ESP32-H2.

#include <sdkconfig.h>

#include "battery.h"

#include <string.h>

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#include <app/clusters/power-source-server/power-source-server.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>

#include "battery_report.h"
#include "button.h"
#include "status_led.h"

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "battery";

static constexpr int k_samples = 16;
static constexpr uint32_t k_rtc_magic = 0x42415454; // "BATT"
// A rest voltage change smaller than this is not reported again (ADC noise, temperature)
static constexpr uint32_t k_report_step_mv = 20;
// A stable run this long clears the brownout counter of the boot guard
static constexpr uint32_t k_stable_run_ms = 120 * 1000;
// Rest readings wait for a quiet radio: rechecked this often while busy, taken anyway after the longest wait (a low
// reading then only counts towards the drop confirmation)
static constexpr uint32_t k_quiet_ms = CONFIG_BATTERY_QUIET_MIN * 60 * 1000;
static constexpr uint32_t k_busy_recheck_ms = 60 * 1000;
static constexpr uint32_t k_max_quiet_wait_ms = 2 * 3600 * 1000;
// Readings after the first one after a boot, CONFIG_BATTERY_CONFIRM_INTERVAL_MIN apart: that first reading may come
// soon after a commissioning or a flash, before the cell recovered (a higher reading is reported at once)
static constexpr uint32_t k_follow_up_readings = 2;

// pdMS_TO_TICKS() multiplies in 32 bits (FreeRTOS projdefs.h): at CONFIG_FREERTOS_HZ=1000 it wraps above 4295 s, and
// 12 h became 250 s. Every delay here goes through this 64-bit conversion.
static constexpr TickType_t ms_to_ticks(uint64_t ms)
{
    return static_cast<TickType_t>(ms * configTICK_RATE_HZ / 1000);
}
static_assert(static_cast<uint64_t>(CONFIG_BATTERY_MEASURE_INTERVAL_H) * 3600 * configTICK_RATE_HZ < portMAX_DELAY,
              "BATTERY_MEASURE_INTERVAL_H does not fit in a tick count");
static_assert(static_cast<uint64_t>(CONFIG_BATTERY_CONFIRM_INTERVAL_MIN) * 60 * configTICK_RATE_HZ < portMAX_DELAY,
              "BATTERY_CONFIRM_INTERVAL_MIN does not fit in a tick count");

// The last reported values, kept in NVS across battery changes: version, half-percent, level, rest mV
static constexpr char k_nvs_namespace[] = "battery";
static constexpr char k_nvs_key[] = "reported";
struct SavedReport {
    uint8_t version;
    uint8_t half_percent;
    uint8_t level;
    uint8_t reserved;
    uint32_t mv;
};
static constexpr uint8_t k_saved_version = 1;

static adc_unit_t s_unit = ADC_UNIT_1;
static adc_channel_t s_chan = ADC_CHANNEL_0;
static adc_cali_handle_t s_cali = nullptr;
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_no_sleep_lock = nullptr;
static esp_pm_lock_handle_t s_cpu_max_lock = nullptr;
#endif

// Written by the battery task (and app_main before it starts), read anywhere: 32-bit values, no tearing
static volatile uint32_t s_boot_mv = 0;  // at boot, CPU running (loaded): boot guard only, never reported
static volatile uint32_t s_rest_mv = 0;  // reported rest voltage, 0 = nothing reported yet
static volatile uint32_t s_burst_mv = 0; // last predicted voltage at a radio burst, 0 = not tested yet
static volatile BatteryLevel s_level = BatteryLevel::Ok;
static volatile uint8_t s_half_percent = 200;
static TaskHandle_t s_task = nullptr;

// Battery task (and app_main before it starts)
static BatteryReport s_report(CONFIG_BATTERY_DROP_CONFIRM_COUNT);
static SavedReport s_saved = {};

// Radio activity that loads the cell: written on the CHIP task, read by the battery task (32-bit values)
static volatile uint32_t s_busy = 0;        // bit per BatteryBusy
static volatile uint32_t s_activity_ms = 0; // last start or end of a busy period, or a short burst; 0 = boot

// Survives software, panic and Deep-sleep resets (not power-on); maybe not a brownout reset on every chip
static RTC_NOINIT_ATTR uint32_t s_rtc_magic;
static RTC_NOINIT_ATTR uint32_t s_rtc_brownouts;

static void lock_acquire(bool cpu_max)
{
#if CONFIG_PM_ENABLE
    if (s_no_sleep_lock) {
        esp_pm_lock_acquire(s_no_sleep_lock);
    }
    if (cpu_max && s_cpu_max_lock) {
        esp_pm_lock_acquire(s_cpu_max_lock);
    }
#endif
}

static void lock_release(bool cpu_max)
{
#if CONFIG_PM_ENABLE
    if (cpu_max && s_cpu_max_lock) {
        esp_pm_lock_release(s_cpu_max_lock);
    }
    if (s_no_sleep_lock) {
        esp_pm_lock_release(s_no_sleep_lock);
    }
#endif
}

/* One averaged reading of the sense pin, scaled to VBAT. No Light-sleep while the ADC exists. */
static esp_err_t read_vbat_mv(uint32_t &mv)
{
    adc_oneshot_unit_handle_t unit = nullptr;
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = s_unit;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12; // 0..3.3 V (ESP32-H2 datasheet Table 5-5, ATTEN3)
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;

    lock_acquire(false);
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &unit);
    int64_t sum = 0;
    if (err == ESP_OK) {
        err = adc_oneshot_config_channel(unit, s_chan, &chan_cfg);
        for (int i = 0; err == ESP_OK && i < k_samples; i++) {
            int raw = 0;
            err = adc_oneshot_read(unit, s_chan, &raw);
            sum += raw;
        }
        adc_oneshot_del_unit(unit);
    }
    lock_release(false);
    if (err != ESP_OK) {
        return err;
    }

    const int raw = static_cast<int>(sum / k_samples);
    int pin_mv = 0;
    if (s_cali) {
        err = adc_cali_raw_to_voltage(s_cali, raw, &pin_mv);
        if (err != ESP_OK) {
            return err;
        }
    } else {
        pin_mv = raw * 3300 / 4095; // uncalibrated estimate
    }
    mv = static_cast<uint32_t>(pin_mv) * CONFIG_BATTERY_SENSE_RATIO_X1000 / 1000;
    return ESP_OK;
}

static BatteryLevel level_from(uint32_t rest_mv, uint32_t burst_mv)
{
    BatteryLevel level = BatteryLevel::Ok;
    if (rest_mv && rest_mv < CONFIG_BATTERY_WARNING_REST_MV) {
        level = BatteryLevel::Warning;
    }
    if (burst_mv && burst_mv < CONFIG_BATTERY_WARNING_BURST_MV) {
        level = BatteryLevel::Warning;
    }
    if ((rest_mv && rest_mv < CONFIG_BATTERY_CRITICAL_REST_MV) ||
        (burst_mv && burst_mv < CONFIG_BATTERY_CRITICAL_BURST_MV)) {
        level = BatteryLevel::Critical;
    }
    return level;
}

/* Internal resistance test: the drop under a known CPU load, extrapolated to a radio burst. The sense node
 * follows within 4 RC time constants (400 ms), so the default 500 ms load reads the settled loaded voltage. */
static void health_test(uint32_t rest_mv)
{
    lock_acquire(true);
    const int64_t end = esp_timer_get_time() + CONFIG_BATTERY_TEST_LOAD_MS * 1000LL;
    volatile uint32_t spin = 0;
    while (esp_timer_get_time() < end) {
        spin = spin + 1;
    }
    uint32_t loaded_mv = 0;
    const esp_err_t err = read_vbat_mv(loaded_mv);
    lock_release(true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Load test: ADC error %s", esp_err_to_name(err));
        return;
    }
    const uint32_t drop = rest_mv > loaded_mv ? rest_mv - loaded_mv : 0;
    const uint32_t burst_drop = drop * CONFIG_BATTERY_BURST_RATIO_X100 / 100;
    s_burst_mv = rest_mv > burst_drop ? rest_mv - burst_drop : 1;
    ESP_LOGI(TAG, "Load test: rest %" PRIu32 " mV, loaded %" PRIu32 " mV, predicted at a radio burst %" PRIu32 " mV",
             rest_mv, loaded_mv, s_burst_mv);
}

/* CHIP task: publish the values that changed */
static void publish_work(intptr_t)
{
    static uint32_t last_mv = 0;
    static uint8_t last_half_percent = 0xFF;
    static int last_level = -1;

    const uint32_t mv = s_rest_mv;
    const uint8_t half_percent = s_half_percent;
    const BatteryLevel level = s_level;

    if (mv && (last_mv == 0 || (mv > last_mv ? mv - last_mv : last_mv - mv) >= k_report_step_mv)) {
        esp_matter_attr_val_t val = esp_matter_nullable_uint32(nullable<uint32_t>(mv));
        if (attribute::update(chip::kRootEndpointId, PowerSource::Id, PowerSource::Attributes::BatVoltage::Id, &val) ==
            ESP_OK) {
            last_mv = mv;
        }
    }
    if (mv && half_percent != last_half_percent) { // mv 0: nothing reported yet, the percentage stays null
        esp_matter_attr_val_t val = esp_matter_nullable_uint8(nullable<uint8_t>(half_percent));
        if (attribute::update(chip::kRootEndpointId, PowerSource::Id,
                              PowerSource::Attributes::BatPercentRemaining::Id, &val) == ESP_OK) {
            last_half_percent = half_percent;
        }
    }
    if (static_cast<int>(level) != last_level) {
        esp_matter_attr_val_t val = esp_matter_enum8(static_cast<uint8_t>(level));
        esp_err_t err = attribute::update(chip::kRootEndpointId, PowerSource::Id,
                                          PowerSource::Attributes::BatChargeLevel::Id, &val);
        val = esp_matter_bool(level == BatteryLevel::Critical);
        if (err == ESP_OK && attribute::update(chip::kRootEndpointId, PowerSource::Id,
                                               PowerSource::Attributes::BatReplacementNeeded::Id, &val) == ESP_OK) {
            last_level = static_cast<int>(level);
        }
    }
}

static const char *level_name(BatteryLevel level)
{
    return level == BatteryLevel::Ok ? "OK" : level == BatteryLevel::Warning ? "WARNING" : "CRITICAL";
}

/* NVS (after nvs_flash_init()): the values reported before the battery was taken out */
static void restore_saved()
{
    nvs_handle_t nvs;
    if (nvs_open(k_nvs_namespace, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "No saved battery values: BatPercentRemaining unknown until the first rest reading");
        return;
    }
    SavedReport saved = {};
    size_t size = sizeof(saved);
    const esp_err_t err = nvs_get_blob(nvs, k_nvs_key, &saved, &size);
    nvs_close(nvs);
    if (err != ESP_OK || size != sizeof(saved) || saved.version != k_saved_version || saved.mv == 0 ||
        saved.half_percent > 200 || saved.level > static_cast<uint8_t>(BatteryLevel::Critical)) {
        ESP_LOGI(TAG, "No saved battery values: BatPercentRemaining unknown until the first rest reading");
        return;
    }
    s_saved = saved;
    s_report.Restore(saved.mv, saved.half_percent);
    s_rest_mv = saved.mv;
    s_half_percent = saved.half_percent;
    s_level = static_cast<BatteryLevel>(saved.level);
    ESP_LOGI(TAG, "Last reported %" PRIu32 " mV, %u %%, level %s: kept until a rest reading", saved.mv,
             saved.half_percent / 2, level_name(s_level));
}

/* Battery task: write the reported values when they changed (a few times a year on a draining cell) */
static void save_reported()
{
    const SavedReport now = {k_saved_version, s_half_percent, static_cast<uint8_t>(s_level), 0, s_rest_mv};
    const uint32_t mv_diff = now.mv > s_saved.mv ? now.mv - s_saved.mv : s_saved.mv - now.mv;
    if (s_saved.version == k_saved_version && now.half_percent == s_saved.half_percent &&
        now.level == s_saved.level && mv_diff < k_report_step_mv) {
        return;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(k_nvs_namespace, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs, k_nvs_key, &now, sizeof(now));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err == ESP_OK) {
        s_saved = now;
    } else {
        ESP_LOGW(TAG, "Battery values not saved: %s", esp_err_to_name(err));
    }
}

static uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

/* Until no commissioning window or OTA download is running and nothing has loaded the cell (boot, Thread attach,
 * button report) for CONFIG_BATTERY_QUIET_MIN: a CR2032 takes minutes to recover from the 25-40 mA bursts, a
 * reading taken earlier is too low. Gives up after k_max_quiet_wait_ms of activity. */
static void wait_quiet()
{
    const uint32_t start = now_ms();
    for (;;) {
        const uint32_t activity = s_activity_ms; // before now_ms(): never "in the future"
        const uint32_t busy = s_busy;
        const uint32_t now = now_ms();
        const uint32_t quiet = now - activity;
        if (!busy && quiet >= k_quiet_ms) {
            return;
        }
        if (now - start >= k_max_quiet_wait_ms) {
            ESP_LOGW(TAG, "Radio busy for %" PRIu32 " min: rest reading taken anyway", (now - start) / 60000);
            return;
        }
        // Light-sleep friendly: the task timeout is the only wakeup
        vTaskDelay(ms_to_ticks(busy ? k_busy_recheck_ms : k_quiet_ms - quiet));
    }
}

static void measure(bool load_test)
{
    uint32_t rest_mv = 0;
    const esp_err_t err = read_vbat_mv(rest_mv);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC error %s", esp_err_to_name(err));
        return;
    }
    if (load_test) {
        health_test(rest_mv);
    }
    // Same or higher percentage: reported now; lower: only once CONFIG_BATTERY_DROP_CONFIRM_COUNT readings in a
    // row are lower (battery_report.h). The level follows the reported rest voltage and the latest burst test.
    s_report.OnReading(rest_mv);
    s_rest_mv = s_report.ReportedMv();
    s_half_percent = s_report.ReportedHalfPercent();
    const BatteryLevel level = level_from(s_rest_mv, s_burst_mv);
    if (level != s_level) {
        ESP_LOGW(TAG, "Battery level %s -> %s", level_name(s_level), level_name(level));
    }
    s_level = level;
    if (s_report.PendingLower()) {
        ESP_LOGI(TAG, "VBAT rest %" PRIu32 " mV (~%u %%): lower than the reported %" PRIu32 " mV (~%u %%), %u/%d in a "
                      "row, next reading in %d min", rest_mv, battery_soc_from_rest(rest_mv) / 2, s_rest_mv,
                 s_half_percent / 2, s_report.PendingLower(), CONFIG_BATTERY_DROP_CONFIRM_COUNT,
                 CONFIG_BATTERY_CONFIRM_INTERVAL_MIN);
    } else {
        ESP_LOGI(TAG, "VBAT rest %" PRIu32 " mV, reported %" PRIu32 " mV (~%u %%), level %s", rest_mv, s_rest_mv,
                 s_half_percent / 2, level_name(level));
    }
    save_reported();
    LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(publish_work));
}

static void battery_task(void *)
{
    vTaskDelay(ms_to_ticks(k_stable_run_ms));
    s_rtc_brownouts = 0;

    // First rest reading once the start-up activity (Thread attach, CASE, reports, a commissioning window) is over
    wait_quiet();
    measure(true);

    const uint64_t measure_ms = CONFIG_BATTERY_MEASURE_INTERVAL_H * 3600ULL * 1000;
    const uint32_t tests_every = CONFIG_BATTERY_TEST_INTERVAL_H / CONFIG_BATTERY_MEASURE_INTERVAL_H;
    uint32_t follow_ups = k_follow_up_readings;
    uint32_t round = 0;
    for (;;) {
        if (follow_ups || s_report.PendingLower()) {
            // Following up the first reading, or confirming a lower one: sooner than the regular interval, no
            // load test
            if (follow_ups) {
                follow_ups--;
            }
            vTaskDelay(ms_to_ticks(CONFIG_BATTERY_CONFIRM_INTERVAL_MIN * 60ULL * 1000));
            wait_quiet();
            measure(false);
            continue;
        }
        // Light-sleep friendly: the task timeout is the only wakeup (twice a day by default)
        vTaskDelay(ms_to_ticks(measure_ms));
        wait_quiet();
        round++;
        measure(tests_every > 0 && round % tests_every == 0);
    }
}

void battery_init(void)
{
    esp_err_t err = adc_oneshot_io_to_channel(CONFIG_BATTERY_SENSE_GPIO, &s_unit, &s_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d is not an ADC pin", CONFIG_BATTERY_SENSE_GPIO);
        return;
    }
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = s_unit;
    cali_cfg.chan = s_chan;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "No ADC calibration in eFuse: voltages are estimates");
        s_cali = nullptr;
    }
#if CONFIG_PM_ENABLE
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "battery", &s_no_sleep_lock);
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "battery_load", &s_cpu_max_lock);
#endif
    // Loaded (CPU running, C9 still charging after a battery change): for the boot guard only, never reported
    uint32_t mv = 0;
    if (read_vbat_mv(mv) == ESP_OK) {
        s_boot_mv = mv;
        ESP_LOGI(TAG, "VBAT at boot (CPU running) %" PRIu32 " mV", mv);
    }
}

void battery_boot_guard(void)
{
    const esp_reset_reason_t reason = esp_reset_reason();
    if (s_rtc_magic != k_rtc_magic || reason == ESP_RST_POWERON) {
        // New or replaced battery (power-on reset), or LP memory lost
        s_rtc_magic = k_rtc_magic;
        s_rtc_brownouts = 0;
    }
    if (reason == ESP_RST_BROWNOUT) {
        s_rtc_brownouts++;
        ESP_LOGW(TAG, "Brownout reset (%" PRIu32 " in a row)", s_rtc_brownouts);
    }

    const bool weak = s_boot_mv != 0 && s_boot_mv < CONFIG_BATTERY_BOOT_MIN_MV;
    const bool looping = s_rtc_brownouts >= CONFIG_BATTERY_BROWNOUT_LIMIT;
    if (!weak && !looping) {
        return;
    }
    ESP_LOGW(TAG, "Battery too weak (%" PRIu32 " mV loaded, %" PRIu32 " brownouts): Deep-sleep %d s, the button wakes",
             s_boot_mv, s_rtc_brownouts, CONFIG_BATTERY_RECOVERY_SLEEP_S);
    if (looping) {
        // One more try after the rest; a further brownout sends it back to sleep
        s_rtc_brownouts = CONFIG_BATTERY_BROWNOUT_LIMIT - 1;
    }
    status_led_flash(LedFlash::LowBattery);
    vTaskDelay(ms_to_ticks(status_led_flash_duration_ms(LedFlash::LowBattery) + 50));
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(CONFIG_BATTERY_RECOVERY_SLEEP_S) * 1000000ULL);
#if !CONFIG_BUTTON_INTERNAL_PULLUP
    // The button also wakes the chip, but only with the external pull-up (defined level in Deep-sleep, no pad hold
    // needed) and when it is released now: a held or stuck button would wake it at once, again and again
    if (!button_is_pressed_raw()) {
        esp_sleep_enable_ext1_wakeup_io(1ULL << CONFIG_BUTTON_GPIO, ESP_EXT1_WAKEUP_ANY_LOW);
    }
#endif
    esp_deep_sleep_start();
}

esp_err_t battery_create_power_source(node_t *node)
{
    // Initial values: the ones reported before the battery was taken out, not the loaded boot reading
    restore_saved();
    endpoint_t *root = endpoint::get(node, chip::kRootEndpointId);
    if (!root) {
        return ESP_ERR_INVALID_STATE;
    }
    endpoint::power_source::config_t cfg;
    cfg.power_source.status = chip::to_underlying(PowerSource::PowerSourceStatusEnum::kActive);
    cfg.power_source.order = 0;
    strlcpy(cfg.power_source.description, "Battery", sizeof(cfg.power_source.description));
    cfg.power_source.feature_flags =
        cluster::power_source::feature::battery::get_id() | cluster::power_source::feature::replaceable::get_id();
    cfg.power_source.features.battery.bat_charge_level = static_cast<uint8_t>(s_level);
    cfg.power_source.features.battery.bat_replacement_needed = s_level == BatteryLevel::Critical;
    cfg.power_source.features.battery.bat_replaceability =
        chip::to_underlying(PowerSource::BatReplaceabilityEnum::kUserReplaceable);
    strlcpy(cfg.power_source.features.replaceable.bat_replacement_description, "CR2032 3V lithium coin cell",
            sizeof(cfg.power_source.features.replaceable.bat_replacement_description));
    cfg.power_source.features.replaceable.bat_quantity = 1;
    esp_err_t err = endpoint::power_source::add(root, &cfg);
    if (err != ESP_OK) {
        return err;
    }

    cluster_t *ps = cluster::get(root, PowerSource::Id);
    if (!ps) {
        return ESP_ERR_INVALID_STATE;
    }
    namespace psa = cluster::power_source::attribute;
    const uint32_t mv = s_rest_mv;
    psa::create_bat_voltage(ps, mv ? nullable<uint32_t>(mv) : nullable<uint32_t>(), nullable<uint32_t>(0),
                            nullable<uint32_t>(5000));
    psa::create_bat_percent_remaining(ps, mv ? nullable<uint8_t>(s_half_percent) : nullable<uint8_t>(),
                                      nullable<uint8_t>(0), nullable<uint8_t>(200));
    psa::create_bat_present(ps, true);
    psa::create_bat_iec_designation(ps, "CR2032", strlen("CR2032"));
    psa::create_bat_approved_chemistry(
        ps, chip::to_underlying(PowerSource::BatApprovedChemistryEnum::kLithiumManganeseDioxide), 0, UINT8_MAX);
    psa::create_bat_capacity(ps, CONFIG_BATTERY_CAPACITY_MAH, 0, UINT32_MAX);
    return ESP_OK;
}

/* CHIP task: the cluster powers the whole node, endpoint 0 and the switch endpoint */
static void register_work(intptr_t)
{
    static chip::EndpointId s_endpoints[] = {chip::kRootEndpointId, 1};
    const CHIP_ERROR err =
        PowerSourceServer::Instance().SetEndpointList(chip::kRootEndpointId, chip::Span<chip::EndpointId>(s_endpoints));
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Power Source EndpointList not set: %" CHIP_ERROR_FORMAT, err.Format());
    }
    publish_work(0);
}

void battery_start(void)
{
    LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork(register_work));
    // 4 KB: ADC, NVS writes and logging
    if (xTaskCreate(battery_task, "battery", 4096, nullptr, tskIDLE_PRIORITY + 1, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start the battery task");
    }
}

BatteryLevel battery_level(void)
{
    return s_level;
}

uint32_t battery_rest_mv(void)
{
    return s_rest_mv;
}

bool battery_ota_allowed(void)
{
    return s_level == BatteryLevel::Ok;
}

void battery_set_busy(BatteryBusy what, bool busy)
{
    const uint32_t bit = 1u << static_cast<unsigned>(what);
    const uint32_t before = s_busy;
    const uint32_t after = busy ? (before | bit) : (before & ~bit);
    if (after != before) {
        s_busy = after;
        s_activity_ms = now_ms();
    }
}

void battery_note_activity(void)
{
    s_activity_ms = now_ms();
}
