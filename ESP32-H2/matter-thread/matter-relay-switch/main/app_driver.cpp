#include <esp_attr.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/gpio_reg.h>
#include <soc/reset_reasons.h>
#include <soc/soc.h>

#include <app_priv.h>
#include <common_macros.h>
#include <esp_matter.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>

#include <button_gpio.h>
#include <device.h>
#include <iot_button.h>
#if CONFIG_STATUS_LED_ENABLE
#include <led_driver.h>
#endif

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t relay_endpoint_id;

static constexpr gpio_num_t k_relay_gpio = (gpio_num_t)CONFIG_RELAY_GPIO;

#if CONFIG_STATUS_LED_ENABLE
static led_driver_handle_t s_led_handle = NULL;
/* CHIP task only (Identify callbacks, commissioning events, attribute updates) */
static bool s_identifying = false;
static bool s_pairing = false;
#endif

/* Written in the esp_timer (button) task, read in the CHIP and factory reset tasks */
static volatile bool s_factory_reset_armed = false;
static volatile bool s_factory_reset_started = false;

/* CHIP task only (attribute updates; read by Identify to restore the LED) */
static bool s_relay_on = false;

/* Survives CPU-only resets: set at relay init, cleared once Matter has applied the stored OnOff state
 * (app_driver_relay_boot_done). Still set at the next boot = the previous boot crashed during startup. */
static RTC_NOINIT_ATTR uint32_t s_boot_pending;
static constexpr uint32_t k_boot_pending_magic = 0x52454C59; /* "RELY" */

/* Status LED colors (hue): relay ON = green, pairing and Identify = blue (blinking), factory reset = red */
static constexpr uint16_t k_hue_relay_on = 120;
static constexpr uint16_t k_hue_identify = 240;
static constexpr uint16_t k_hue_reset = 0;

static void app_driver_led_set(bool on, uint16_t hue)
{
#if CONFIG_STATUS_LED_ENABLE
    if (s_led_handle) {
        led_driver_set_hue(s_led_handle, hue);
        led_driver_set_power(s_led_handle, on);
    }
#endif
}

#if CONFIG_STATUS_LED_ENABLE
/* CHIP task only. armed_cb (esp_timer task, priority 22) can run during the write, and its own red write can be
 * rejected while our refresh is in flight. Re-check afterwards so the solid red "release now" cue is the last write. */
static void app_driver_led_set_unless_reset(bool on, uint16_t hue)
{
    if (s_factory_reset_armed) {
        return;
    }
    app_driver_led_set(on, hue);
    if (s_factory_reset_armed) {
        app_driver_led_set(true, k_hue_reset);
    }
}
#endif

static esp_err_t app_driver_relay_set_power(bool on)
{
    int level = on ? CONFIG_RELAY_ACTIVE_LEVEL : !CONFIG_RELAY_ACTIVE_LEVEL;
    ESP_LOGI(TAG, "Relay (GPIO%d) -> %s", (int)k_relay_gpio, on ? "ON" : "OFF");
    s_relay_on = on;
#if CONFIG_STATUS_LED_ENABLE
    /* The hue is green outside pairing, Identify and factory reset, so one refresh is enough */
    if (s_led_handle && !s_factory_reset_armed && !s_identifying && !s_pairing) {
        led_driver_set_power(s_led_handle, on);
        if (s_factory_reset_armed) {
            /* armed_cb preempted us mid-write: restore the solid red "release now" indication */
            app_driver_led_set(true, k_hue_reset);
        }
    }
#endif
    return gpio_set_level(k_relay_gpio, level);
}

static void app_driver_button_toggle_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Toggle button pressed");
    /* Read-modify-write on the CHIP task: atomic with controller On/Off/Toggle commands,
     * and the esp_timer task never blocks on the CHIP lock or the NVS write */
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
        attribute_t *attribute = attribute::get(relay_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        if (attribute::get_val(attribute, &val) != ESP_OK) {
            return;
        }
        val.val.b = !val.val.b;
        /* update() runs the attribute callback (drives the relay) and reports the new state to controllers */
        attribute::update(relay_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
    });
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Toggle ignored, Matter stack not ready: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void app_driver_button_pairing_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Pairing button pressed");
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
        chip::Server &server = chip::Server::GetInstance();
        chip::CommissioningWindowManager &cwm = server.GetCommissioningWindowManager();
        if (cwm.IsCommissioningWindowOpen()) {
            ESP_LOGI(TAG, "Commissioning window already open");
            return;
        }
        /* Not commissioned yet: BLE + DNS-SD, as at first boot. Commissioned: BLE has been shut down
         * (CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING), so advertise on the Thread network only. That is enough to add
         * the device to a second controller (Apple Home, Google Home, ...) with its original QR / pairing code. */
        const bool commissioned = server.GetFabricTable().FabricCount() > 0;
        CHIP_ERROR err = cwm.OpenBasicCommissioningWindow(
            chip::System::Clock::Seconds32(CHIP_DEVICE_CONFIG_DISCOVERY_TIMEOUT_SECS),
            commissioned ? chip::CommissioningWindowAdvertisement::kDnssdOnly
                         : chip::CommissioningWindowAdvertisement::kAllSupported);
        if (err == CHIP_NO_ERROR) {
            ESP_LOGI(TAG, "Commissioning window opened for %d s (%s)", CHIP_DEVICE_CONFIG_DISCOVERY_TIMEOUT_SECS,
                     commissioned ? "Thread/DNS-SD" : "BLE + DNS-SD");
        } else {
            /* e.g. a commissioning (fail-safe) still in progress */
            ESP_LOGW(TAG, "Failed to open commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
        }
    });
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Pairing ignored, Matter stack not ready: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void app_driver_factory_reset_task(void *arg)
{
    /* 3 red blinks confirm the reset, then erase all Matter/Thread data and reboot */
    for (int i = 0; i < 3; i++) {
        app_driver_led_set(true, k_hue_reset);
        vTaskDelay(pdMS_TO_TICKS(250));
        app_driver_led_set(false, k_hue_reset);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    ESP_LOGI(TAG, "Starting factory reset");
    esp_matter::factory_reset();
    vTaskDelete(NULL);
}

static void app_driver_button_factory_reset_armed_cb(void *arg, void *data)
{
    if (!s_factory_reset_armed) {
        s_factory_reset_armed = true;
        ESP_LOGI(TAG, "Factory reset triggered. Release the button to start factory reset.");
        /* Solid red: release now */
        app_driver_led_set(true, k_hue_reset);
    }
}

static void app_driver_button_factory_reset_release_cb(void *arg, void *data)
{
    if (s_factory_reset_armed && !s_factory_reset_started) {
        s_factory_reset_started = true;
        /* Blink from a task: button callbacks run in the esp_timer task and must not block */
        if (xTaskCreate(app_driver_factory_reset_task, "factory_reset", 4096, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create factory reset task, resetting now");
            esp_matter::factory_reset();
        }
    }
}

#if CONFIG_STATUS_LED_ENABLE
/* Pairing and Identify: blue blinks on the CHIP SystemLayer timer, so they and the relay LED writes are serialised on
 * the CHIP task. The factory reset path (esp_timer task, factory_reset task) also writes the LED; the CHIP-side writes
 * re-check s_factory_reset_armed afterwards. Priority: factory reset (red) > Identify / pairing (blue) > relay (green). */
static bool s_blink_phase = false;
static uint16_t s_blinks_left = 0; /* Identify only. 0 = blink until STOP */

static void app_driver_blink_cb(chip::System::Layer *, void *)
{
    if ((!s_identifying && !s_pairing) || s_factory_reset_armed) {
        return;
    }
    s_blink_phase = !s_blink_phase;
    app_driver_led_set_unless_reset(s_blink_phase, k_hue_identify);
    if (s_identifying && !s_blink_phase && s_blinks_left && --s_blinks_left == 0) {
        app_driver_identify_stop();
        return;
    }
    chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(500), app_driver_blink_cb,
                                                nullptr);
}

/* Start blinking from the ON half, or go back to the relay indication when nothing needs the blink any more */
static void app_driver_blink_refresh()
{
    chip::DeviceLayer::SystemLayer().CancelTimer(app_driver_blink_cb, nullptr);
    if (s_identifying || s_pairing) {
        s_blink_phase = false;
        app_driver_blink_cb(nullptr, nullptr);
    } else {
        app_driver_led_set_unless_reset(s_relay_on, k_hue_relay_on);
    }
}

void app_driver_identify_start(uint16_t blinks)
{
    s_identifying = true;
    s_blinks_left = blinks;
    app_driver_blink_refresh();
}

void app_driver_identify_stop()
{
    if (s_identifying) {
        s_identifying = false;
        /* Pairing still active: keep blinking blue */
        app_driver_blink_refresh();
    } else if (!s_pairing) {
        chip::DeviceLayer::SystemLayer().CancelTimer(app_driver_blink_cb, nullptr);
    }
}

void app_driver_identify_finish()
{
    if (s_identifying && s_blink_phase && !s_factory_reset_armed) {
        /* blink_cb stops at the end of this ON half (<= 500 ms) */
        s_blinks_left = 1;
    } else {
        /* Between blinks (LED already dark) or nothing running */
        app_driver_identify_stop();
    }
}

void app_driver_pairing_indicate(bool pairing)
{
    if (pairing == s_pairing) {
        return;
    }
    ESP_LOGI(TAG, "Pairing LED %s", pairing ? "on (blinking blue)" : "off");
    s_pairing = pairing;
    if (!s_identifying) {
        app_driver_blink_refresh();
    }
}
#else
void app_driver_identify_start(uint16_t blinks) {}
void app_driver_identify_stop() {}
void app_driver_identify_finish() {}
void app_driver_pairing_indicate(bool pairing) {}
#endif // CONFIG_STATUS_LED_ENABLE

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    if (endpoint_id == relay_endpoint_id && cluster_id == OnOff::Id &&
        attribute_id == OnOff::Attributes::OnOff::Id) {
        err = app_driver_relay_set_power(val->val.b);
    }
    return err;
}

esp_err_t app_driver_relay_set_defaults(uint16_t endpoint_id)
{
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t *attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    esp_err_t err = attribute::get_val(attribute, &val);
    if (err != ESP_OK) {
        return err;
    }
    return app_driver_relay_set_power(val.val.b);
}

void app_driver_relay_boot_done()
{
    s_boot_pending = 0;
}

app_driver_handle_t app_driver_relay_init()
{
    /* esp_restart() and panics (OTA apply, factory reset, crash, watchdog) reset only the CPU: GPIO_OUT,
     * GPIO_ENABLE and IO_MUX survive ROM and bootloader. Keep the relay level then, instead of switching the
     * load OFF until Matter restores it. Power-on, brownout and other resets start from OFF.
     * A reset before the previous boot reached app_driver_relay_boot_done() (crash/abort/watchdog loop during
     * startup) also falls back to OFF, instead of holding the load ON with no way to control it. */
    const uint32_t mask = 1UL << k_relay_gpio;
    const bool prev_boot_done = s_boot_pending != k_boot_pending_magic;
    s_boot_pending = k_boot_pending_magic;
    const bool keep = prev_boot_done && esp_rom_get_reset_reason(0) == RESET_REASON_CPU0_SW &&
                      (REG_READ(GPIO_ENABLE_REG) & mask);
    const bool on = keep && (((REG_READ(GPIO_OUT_REG) & mask) ? 1 : 0) == CONFIG_RELAY_ACTIVE_LEVEL);
    if (!keep) {
        /* Latch the OFF level before switching the pad to output, so the relay never clicks on at boot.
         * gpio_reset_pin() is not used: it enables the pull-up, which would turn an active-high relay on. */
        gpio_set_level(k_relay_gpio, !CONFIG_RELAY_ACTIVE_LEVEL);
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << k_relay_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

#if CONFIG_STATUS_LED_ENABLE
    led_driver_config_t led_cfg = led_driver_get_config();
    s_led_handle = led_driver_init(&led_cfg);
    if (s_led_handle) {
        /* Drop the per-update "led set r:g:b" INFO lines */
        esp_log_level_set("led_driver_ws2812", ESP_LOG_WARN);
        /* Dim green while the relay is ON */
        led_driver_set_hue(s_led_handle, k_hue_relay_on);
        led_driver_set_saturation(s_led_handle, 100);
        led_driver_set_brightness(s_led_handle, 20);
    }
#endif

    app_driver_relay_set_power(on);
    return (app_driver_handle_t)k_relay_gpio;
}

app_driver_handle_t app_driver_button_init()
{
    button_handle_t handle = NULL;
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = button_driver_get_config();

    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button device");
        return NULL;
    }

    iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, NULL, app_driver_button_toggle_cb, NULL);
    /* Double click: open the commissioning window (pairing mode), LED blinks blue until commissioned or timed out */
    iot_button_register_cb(handle, BUTTON_DOUBLE_CLICK, NULL, app_driver_button_pairing_cb, NULL);
    /* Hold CONFIG_BUTTON_LONG_PRESS_TIME_MS (5 s): LED turns red; release: 3 red blinks, then factory reset */
    iot_button_register_cb(handle, BUTTON_LONG_PRESS_START, NULL, app_driver_button_factory_reset_armed_cb, NULL);
    iot_button_register_cb(handle, BUTTON_PRESS_UP, NULL, app_driver_button_factory_reset_release_cb, NULL);
    return (app_driver_handle_t)handle;
}
