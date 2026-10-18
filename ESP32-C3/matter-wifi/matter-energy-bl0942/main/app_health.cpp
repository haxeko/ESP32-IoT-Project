#include <app_health.h>

#include <inttypes.h>
#include <stdio.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#if CONFIG_BT_ENABLED
#include <esp_bt.h>
#endif

#include <app/InteractionModelEngine.h>
#include <app/server/Server.h>

#include <device_info.h>

using chip::DeviceLayer::ChipDeviceEvent;
using chip::DeviceLayer::DeviceEventType::kBLEDeinitialized;
using chip::DeviceLayer::DeviceEventType::kCommissioningComplete;
using chip::DeviceLayer::DeviceEventType::kESPSystemEvent;
using chip::DeviceLayer::DeviceEventType::kFabricRemoved;
using chip::DeviceLayer::DeviceEventType::kServerReady;

static const char *TAG = "app_health";

/* The Matter thread feeds the task watchdog every 5 s, far below CONFIG_ESP_TASK_WDT_TIMEOUT_S (30 s): a busy
 * moment (commissioning crypto, an OTA block written to flash) never restarts the device, a stuck thread does. */
static constexpr uint64_t k_tick_period_us = 5 * 1000 * 1000;
static constexpr uint32_t k_status_every_ticks = 12; /* one status line every 60 s */

static esp_task_wdt_user_handle_t s_matter_wdt;
static uint32_t s_ticks;
static uint32_t s_wifi_drops; /* connection lost after being connected, since boot */

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "power on";
    case ESP_RST_EXT:
        return "reset pin";
    case ESP_RST_SW:
        return "software restart";
    case ESP_RST_PANIC:
        return "crash (panic)";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "other watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    case ESP_RST_BROWNOUT:
        return "brownout (supply voltage dropped)";
    case ESP_RST_SDIO:
        return "SDIO";
    case ESP_RST_USB:
        return "USB";
    case ESP_RST_JTAG:
        return "JTAG";
    case ESP_RST_EFUSE:
        return "eFuse error";
    case ESP_RST_PWR_GLITCH:
        return "power glitch";
    case ESP_RST_CPU_LOCKUP:
        return "CPU lockup";
    default:
        return "unknown";
    }
}

void app_health_log_reset_reason()
{
    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason) {
    case ESP_RST_POWERON:
    case ESP_RST_EXT:
    case ESP_RST_SW: /* esp_restart(): OTA update, factory reset */
    case ESP_RST_USB:
    case ESP_RST_JTAG:
        ESP_LOGI(TAG, "Reset reason: %s (%d)", reset_reason_name(reason), reason);
        break;
    default:
        ESP_LOGW(TAG, "Reset reason: %s (%d)", reset_reason_name(reason), reason);
        break;
    }
}

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
static bool s_wifi_connected;

/* Before Wi-Fi starts the limit comes from the PHY init data (CONFIG_ESP_PHY_MAX_WIFI_TX_POWER, 20 dBm) */
static void set_wifi_tx_power()
{
#if CONFIG_APP_WIFI_TX_POWER_QUARTER_DBM < 80
    esp_err_t err = esp_wifi_set_max_tx_power(CONFIG_APP_WIFI_TX_POWER_QUARTER_DBM);
    int8_t power = 0;
    if (err == ESP_OK) {
        err = esp_wifi_get_max_tx_power(&power);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi max transmit power %d.%02d dBm", power / 4, (power % 4) * 25);
    } else {
        ESP_LOGW(TAG, "Failed to set the Wi-Fi transmit power: %s", esp_err_to_name(err));
    }
#endif
}

/* BLE is off: never started (already commissioned) or stopped after commissioning. The controller status is kept
 * outside the BLE memory that is given back to the heap. */
static bool ble_off()
{
#if CONFIG_BT_ENABLED
    return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE;
#else
    return true;
#endif
}

/* ESP-IDF lets the Wi-Fi radio sleep between beacons by default (WIFI_PS_MIN_MODEM): multicast frames wait for the
 * next DTIM beacon, and can get lost with some routers. Controllers find the device with multicast (mDNS, IPv6
 * neighbor discovery), so a sleeping radio can leave it "No Response". It is mains powered: keep the radio on.
 * Only while BLE is off, since Wi-Fi and BLE share the radio during commissioning. */
static void set_wifi_power_save_off()
{
#if CONFIG_APP_WIFI_POWER_SAVE_OFF
    wifi_ps_type_t ps = WIFI_PS_MIN_MODEM;
    if (!ble_off() || (esp_wifi_get_ps(&ps) == ESP_OK && ps == WIFI_PS_NONE)) {
        return;
    }
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi power save off");
    } else {
        ESP_LOGW(TAG, "Failed to turn Wi-Fi power save off: %s", esp_err_to_name(err));
    }
#endif
}

static void handle_wifi_event(const ChipDeviceEvent *event)
{
    switch (event->Platform.ESPSystemEvent.Id) {
    case WIFI_EVENT_STA_START:
        set_wifi_tx_power();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        s_wifi_connected = true;
        set_wifi_power_save_off();
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        /* Reason codes: wifi_err_reason_t in esp_wifi_types_generic.h, e.g. 200 beacon timeout, 201 no AP found */
        const wifi_event_sta_disconnected_t &info = event->Platform.ESPSystemEvent.Data.WiFiStaDisconnected;
        if (s_wifi_connected) {
            s_wifi_connected = false;
            s_wifi_drops++;
            ESP_LOGW(TAG, "Wi-Fi connection lost: reason %u, RSSI %d dBm (%" PRIu32 " since boot)", info.reason,
                     info.rssi, s_wifi_drops);
        } else {
            ESP_LOGI(TAG, "Wi-Fi connection attempt failed: reason %u, RSSI %d dBm", info.reason, info.rssi);
        }
        break;
    }
    default:
        break;
    }
}

/* Matter thread. Wi-Fi may have started, even connected, before app_event_cb was registered: its events were not
 * seen, apply the settings now. */
static void apply_wifi_settings(intptr_t)
{
    int8_t power = 0;
    if (esp_wifi_get_max_tx_power(&power) == ESP_OK) { /* fails until Wi-Fi has started */
        set_wifi_tx_power();
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        s_wifi_connected = true;
        set_wifi_power_save_off();
    }
}
#endif /* CHIP_DEVICE_CONFIG_ENABLE_WIFI */

void app_health_handle_event(const ChipDeviceEvent *event)
{
    /* The fabrics are loaded, added or removed: log what tells this unit and its pairings apart */
    if (event->Type == kServerReady || event->Type == kCommissioningComplete || event->Type == kFabricRemoved) {
        device_info_log_identity();
    }
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    if (event->Type == kESPSystemEvent && event->Platform.ESPSystemEvent.Base == WIFI_EVENT) {
        handle_wifi_event(event);
    } else if (event->Type == kBLEDeinitialized) {
        set_wifi_power_save_off();
    }
#endif
}

/* Matter thread. Subscriptions per fabric, e.g. "fabric 1: 2, fabric 2: 1": a controller that stays at 0 while the
 * others are subscribed has stopped subscribing (its own network or hub), while the device keeps working. */
static void format_subscriptions(char *buf, size_t size)
{
    size_t len = 0;
    buf[0] = '\0';
    for (const chip::FabricInfo &fabric : chip::Server::GetInstance().GetFabricTable()) {
        uint32_t count = chip::app::InteractionModelEngine::GetInstance()->GetNumActiveReadHandlers(
            chip::app::ReadHandler::InteractionType::Subscribe, fabric.GetFabricIndex());
        int written = snprintf(buf + len, size - len, "%sfabric %u: %" PRIu32, len ? ", " : "",
                               fabric.GetFabricIndex(), count);
        if (written < 0 || (size_t)written >= size - len) {
            break;
        }
        len += (size_t)written;
    }
    if (len == 0) {
        snprintf(buf, size, "no fabric");
    }
}

/* Matter thread */
static void log_status()
{
    char wifi[56] = "no Wi-Fi";
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        esp_ip6_addr_t ip6[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        int ip6_count = netif ? esp_netif_get_all_ip6(netif, ip6) : 0;
        snprintf(wifi, sizeof(wifi), "Wi-Fi %d dBm ch %u, %d IPv6", ap.rssi, ap.primary, ip6_count);
    } else {
        snprintf(wifi, sizeof(wifi), "Wi-Fi not connected");
    }
#endif
    char subscriptions[80];
    format_subscriptions(subscriptions, sizeof(subscriptions));
    ESP_LOGI(TAG, "up %" PRIu32 " s | heap %u free, %u min, %u largest | %s, %" PRIu32 " drops | subscriptions %s",
             (uint32_t)(esp_timer_get_time() / 1000000), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), wifi, s_wifi_drops, subscriptions);
}

/* Matter thread, stack lock held */
static void tick_work(intptr_t)
{
    if (s_matter_wdt) {
        esp_task_wdt_reset_user(s_matter_wdt);
    }
    if (++s_ticks % k_status_every_ticks == 0) {
        log_status();
    }
}

/* esp_timer task. Only queue the work: if the Matter thread is stuck, the watchdog is not fed. */
static void tick_cb(void *arg)
{
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(tick_work);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Could not queue work for the Matter thread: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void app_health_start()
{
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    CHIP_ERROR chip_err = chip::DeviceLayer::PlatformMgr().ScheduleWork(apply_wifi_settings);
    if (chip_err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Could not queue the Wi-Fi settings: %" CHIP_ERROR_FORMAT, chip_err.Format());
    }
#endif

    static esp_timer_handle_t s_timer;
    const esp_timer_create_args_t args = {
        .callback = tick_cb,
        .name = "app_health",
    };
    esp_err_t err = esp_timer_create(&args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Health timer: %s, the Matter thread is not watched", esp_err_to_name(err));
        return;
    }
    /* Only watch the Matter thread while the timer that makes it feed the watchdog runs */
    if (esp_task_wdt_add_user("matter", &s_matter_wdt) != ESP_OK) {
        s_matter_wdt = nullptr; /* task watchdog disabled in menuconfig */
    }
    err = esp_timer_start_periodic(s_timer, k_tick_period_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Health timer: %s, the Matter thread is not watched", esp_err_to_name(err));
        if (s_matter_wdt) {
            esp_task_wdt_delete_user(s_matter_wdt);
            s_matter_wdt = nullptr;
        }
    }
}
