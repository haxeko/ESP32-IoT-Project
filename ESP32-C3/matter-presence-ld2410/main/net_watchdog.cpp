/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <app/InteractionModelEngine.h>
#include <app/ReadHandler.h>
#include <app/server/Dnssd.h>
#include <app/server/Server.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <inttypes.h>
#include <platform/CHIPDeviceLayer.h>
#include <sdkconfig.h>

#include <atomic>

#include "net_watchdog.h"

static const char *TAG = "net_wd";

static constexpr uint64_t kCheckPeriodUs = 30 * 1000 * 1000;
static constexpr uint32_t kReadvertiseSec = 5 * 60;
// The Matter task runs the check within milliseconds; even an OTA block or a CASE handshake takes seconds at most.
static constexpr uint32_t kStuckSec = 3 * 60;

// Kept across esp_restart() (not across a power cut): why this watchdog restarted the device, logged at the next
// boot. Any other value is ignored.
static constexpr uint32_t kRestartNoSubscription = 0x4E574431; // "NWD1"
static constexpr uint32_t kRestartStuck = 0x4E574432;          // "NWD2"
static RTC_NOINIT_ATTR uint32_t s_restart_marker;

static net_watchdog_policy_t s_policy;     // Matter task
static std::atomic<uint32_t> s_heartbeat; // seconds since boot when the Matter task last ran check()

static uint32_t now_s()
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static void restart(uint32_t marker)
{
    s_restart_marker = marker;
    esp_restart();
}

// Matter task
static void check(intptr_t)
{
    const uint32_t now = now_s();
    s_heartbeat = now;
    const bool commissioned = chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
    const bool subscribed = chip::app::InteractionModelEngine::GetInstance()->GetNumActiveReadHandlers(
                                chip::app::ReadHandler::InteractionType::Subscribe) > 0;
    const net_watchdog_action_t action = net_watchdog_step(s_policy, now, commissioned, subscribed);
    const uint32_t idle_min = (now - s_policy.idle_since) / 60;
    switch (action) {
    case NET_WD_READVERTISE:
        ESP_LOGW(TAG, "No controller subscribed for %" PRIu32 " min: announcing the device again (DNS-SD)", idle_min);
        chip::app::DnssdServer::Instance().StartServer();
        break;
    case NET_WD_RECONNECT: {
        ESP_LOGW(TAG, "No controller subscribed for %" PRIu32 " min: reconnecting Wi-Fi", idle_min);
        // The Matter stack sees the disconnection and connects again at once (scan of all channels, strongest
        // access point first), then announces the device on the new address.
        const esp_err_t err = esp_wifi_disconnect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_disconnect() failed: %s", esp_err_to_name(err));
        }
        break;
    }
    case NET_WD_RESTART:
        ESP_LOGE(TAG, "No controller subscribed for %" PRIu32 " min: restarting", idle_min);
        restart(kRestartNoSubscription);
        break;
    case NET_WD_NONE:
        break;
    }
}

// esp_timer task: must not block. The Matter task does the check; a Matter task that has not run it for kStuckSec
// is stuck or starved (the presence, radar and LD2410 tasks have a higher priority).
static void on_timer(void *)
{
    if (CONFIG_NET_WATCHDOG_RESTART_MIN > 0 && net_watchdog_stuck(now_s(), s_heartbeat, kStuckSec)) {
        ESP_LOGE(TAG, "Matter task stuck for %" PRIu32 " s: restarting", now_s() - s_heartbeat);
        restart(kRestartStuck);
    }
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(check) != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Matter event queue full, check skipped");
    }
}

esp_err_t net_watchdog_start()
{
    if (esp_reset_reason() == ESP_RST_SW &&
        (s_restart_marker == kRestartNoSubscription || s_restart_marker == kRestartStuck)) {
        ESP_LOGW(TAG, "Restarted by the connection watchdog: %s",
                 s_restart_marker == kRestartStuck ? "Matter task stuck" : "no controller subscribed");
    }
    s_restart_marker = 0;

    s_policy = {};
    s_policy.readvertise_s = kReadvertiseSec;
    s_policy.reconnect_s = CONFIG_NET_WATCHDOG_RECONNECT_MIN * 60;
    s_policy.restart_s = CONFIG_NET_WATCHDOG_RESTART_MIN * 60;
    s_heartbeat = now_s();

    const esp_timer_create_args_t args = {
        .callback = on_timer,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "net_wd",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer;
    esp_err_t err = esp_timer_create(&args, &timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(timer, kCheckPeriodUs);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG,
                 "Connection watchdog: without any controller subscription, DNS-SD again every %" PRIu32
                 " min, Wi-Fi reconnect every %d min, restart after %d min (0 = never)",
                 kReadvertiseSec / 60, CONFIG_NET_WATCHDOG_RECONNECT_MIN, CONFIG_NET_WATCHDOG_RESTART_MIN);
    }
    return err;
}
