#include <app_health.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include <openthread/ip6.h>
#include <openthread/srp_client.h>
#include <openthread/thread.h>

#include <app/InteractionModelEngine.h>
#include <app/server/Server.h>

#include <device_info.h>

using chip::DeviceLayer::ChipDeviceEvent;
using chip::DeviceLayer::DeviceEventType::kCommissioningComplete;
using chip::DeviceLayer::DeviceEventType::kFabricRemoved;
using chip::DeviceLayer::DeviceEventType::kServerReady;
using chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange;

static const char *TAG = "app_health";

// The Matter thread feeds the task watchdog every 5 s, far below CONFIG_ESP_TASK_WDT_TIMEOUT_S (30 s): a busy
// moment (commissioning crypto, an OTA block written to flash) never restarts the device, a stuck thread does.
static constexpr uint64_t k_tick_period_us = 5 * 1000 * 1000;
static constexpr uint32_t k_status_every_ticks = 12; // one status line every 60 s

static esp_task_wdt_user_handle_t s_matter_wdt;
static uint32_t s_ticks;
static uint32_t s_thread_drops; // detached after being attached, since boot

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
    case ESP_RST_SW: // esp_restart(): OTA update, factory reset
    case ESP_RST_USB:
    case ESP_RST_JTAG:
        ESP_LOGI(TAG, "Reset reason: %s (%d)", reset_reason_name(reason), reason);
        break;
    default:
        ESP_LOGW(TAG, "Reset reason: %s (%d)", reset_reason_name(reason), reason);
        break;
    }
}

void app_health_handle_event(const ChipDeviceEvent *event)
{
    // The fabrics are loaded, added or removed: log what tells this unit and its pairings apart
    if (event->Type == kServerReady || event->Type == kCommissioningComplete || event->Type == kFabricRemoved) {
        device_info_log_identity();
    }
    if (event->Type == kThreadConnectivityChange) {
        if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Lost) {
            s_thread_drops++;
            ESP_LOGW(TAG, "Thread connection lost (%" PRIu32 " since boot)", s_thread_drops);
        } else if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established) {
            ESP_LOGI(TAG, "Thread attached");
        }
    }
}

// Matter thread. Subscriptions per fabric, e.g. "fabric 1: 2, fabric 2: 1": a controller that stays at 0 while the
// others are subscribed has stopped subscribing (its own network or hub), while the device keeps working.
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

// Matter thread. Thread role and RLOC16, OMR address (reachable from the home network through the border router)
// and SRP registration (how controllers find the device), e.g. "Thread router 0x5c00, OMR yes, SRP Registered".
static void format_thread(char *buf, size_t size)
{
    otInstance *instance = esp_openthread_get_instance();
    // Short wait only: the Matter thread must not stall behind a busy OpenThread task
    if (!instance || !esp_openthread_lock_acquire(pdMS_TO_TICKS(100))) {
        snprintf(buf, size, "Thread busy");
        return;
    }
    const otDeviceRole role = otThreadGetDeviceRole(instance);
    const uint16_t rloc16 = otThreadGetRloc16(instance);
    const otMeshLocalPrefix *ml_prefix = otThreadGetMeshLocalPrefix(instance);
    bool omr = false;
    for (const otNetifAddress *a = otIp6GetUnicastAddresses(instance); a; a = a->mNext) {
        const bool link_local = a->mAddress.mFields.m8[0] == 0xfe && (a->mAddress.mFields.m8[1] & 0xc0) == 0x80;
        const bool mesh_local = ml_prefix && memcmp(a->mAddress.mFields.m8, ml_prefix->m8, sizeof(ml_prefix->m8)) == 0;
        if (!link_local && !mesh_local) {
            omr = true;
        }
    }
    const char *srp = "not started";
    if (otSrpClientIsRunning(instance)) {
        srp = otSrpClientItemStateToString(otSrpClientGetHostInfo(instance)->mState);
    }
    esp_openthread_lock_release();
    snprintf(buf, size, "Thread %s 0x%04x, OMR %s, SRP %s", otThreadDeviceRoleToString(role), rloc16,
             omr ? "yes" : "no", srp);
}

// Matter thread
static void log_status()
{
    char thread[64];
    format_thread(thread, sizeof(thread));
    char subscriptions[80];
    format_subscriptions(subscriptions, sizeof(subscriptions));
    ESP_LOGI(TAG, "up %" PRIu32 " s | heap %u free, %u min, %u largest | %s, %" PRIu32 " drops | subscriptions %s",
             (uint32_t)(esp_timer_get_time() / 1000000), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), thread, s_thread_drops, subscriptions);
}

// Matter thread, stack lock held
static void tick_work(intptr_t)
{
    if (s_matter_wdt) {
        esp_task_wdt_reset_user(s_matter_wdt);
    }
    if (++s_ticks % k_status_every_ticks == 0) {
        log_status();
    }
}

// esp_timer task. Only queue the work: if the Matter thread is stuck, the watchdog is not fed.
static void tick_cb(void *arg)
{
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(tick_work);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Could not queue work for the Matter thread: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void app_health_start()
{
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
    // Only watch the Matter thread while the timer that makes it feed the watchdog runs
    if (esp_task_wdt_add_user("matter", &s_matter_wdt) != ESP_OK) {
        s_matter_wdt = nullptr; // task watchdog disabled in menuconfig
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
