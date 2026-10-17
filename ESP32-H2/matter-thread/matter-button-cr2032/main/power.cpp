// See power.h. ESP-IDF v6.0.2, esp-matter 1.6.1, ESP32-H2, OpenThread MTD (SED).

#include <sdkconfig.h>

#include "power.h"

#include <algorithm>
#include <inttypes.h>

#include <esp_ieee802154.h>
#include <esp_log.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <esp_pm.h>
#include <openthread/child_supervision.h>
#include <openthread/platform/radio.h>
#include <openthread/thread.h>

#include <app/icd/server/ICDServerConfig.h>
#include <app/server/Server.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ThreadStackManager.h>
#if CHIP_CONFIG_ENABLE_ICD_DSLS
#include <esp_matter.h> // kFabricRemoved / kFabricCommitted (esp-matter device events)

#include <app/icd/server/ICDMonitoringTable.h>
#include <app/icd/server/ICDNotifier.h>
#include <app/icd/server/ICDStateObserver.h>
#endif

using namespace chip;
using namespace chip::DeviceLayer;

static const char *TAG = "power";

void power_configure(void)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t cfg = {};
    cfg.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    // Same min frequency: the work done while awake is short, Light-sleep does the saving (as esp-matter icd_app)
    cfg.min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#if CONFIG_APP_LIGHT_SLEEP
    cfg.light_sleep_enable = true;
#else
    cfg.light_sleep_enable = false;
#endif
    const esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
    }
#endif
}

/* ---------------------------------------------------------------------------------------------------------------
 * TX power from the very first frame. esp_ieee802154_enable() (called by OpenThread's radio init inside
 * esp_matter::start()) resets the driver to its maximum power, and a provisioned device sends a Child Update Request
 * to its former parent 0..25 ms after Thread starts (OpenThread PrevRoleRestorer), often before app_main() gets the
 * OpenThread lock. A +20 dBm frame draws ~180 mA from the cell: enough to brown out a half-used CR2032 at every
 * boot. Linked with -Wl,--wrap=esp_ieee802154_enable (main/CMakeLists.txt). */

extern "C" esp_err_t __real_esp_ieee802154_enable(void);

extern "C" esp_err_t __wrap_esp_ieee802154_enable(void)
{
    const esp_err_t err = __real_esp_ieee802154_enable();
    esp_ieee802154_set_txpower(CONFIG_THREAD_TX_POWER_DBM);
    return err;
}

/* ---------------------------------------------------------------------------------------------------------------
 * Radio and child settings (OpenThread lock) */

static void apply_thread_settings()
{
    otInstance *ot = esp_openthread_get_instance();
    if (!ot) {
        return;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    otPlatRadioSetTransmitPower(ot, CONFIG_THREAD_TX_POWER_DBM);
#if CONFIG_ENABLE_ICD_SERVER
    // The parent keeps the child for the child timeout without hearing from it, and the child re-attaches when it
    // gets no secure frame (data or supervision message) within the check timeout. Both must exceed the slow poll.
    const uint32_t poll_s = (CONFIG_ICD_SLOW_POLL_INTERVAL_MS + 999) / 1000;
    if (poll_s > 60) {
        otThreadSetChildTimeout(ot, std::max<uint32_t>(240, 4 * poll_s));
    }
    if (poll_s > 30) {
        const uint16_t interval = static_cast<uint16_t>(std::max<uint32_t>(129, poll_s));
        otChildSupervisionSetInterval(ot, interval);
        otChildSupervisionSetCheckTimeout(ot, static_cast<uint16_t>(interval + poll_s + 30));
    }
#endif
    int8_t power = 0;
    otPlatRadioGetTransmitPower(ot, &power);
    const uint32_t child_timeout = otThreadGetChildTimeout(ot);
    const uint16_t sup_interval = otChildSupervisionGetInterval(ot);
    const uint16_t sup_check = otChildSupervisionGetCheckTimeout(ot);
    esp_openthread_lock_release();
    ESP_LOGI(TAG, "802.15.4 TX power %d dBm, child timeout %" PRIu32 " s, supervision %u s / check %u s", power,
             child_timeout, sup_interval, sup_check);
}

/* ---------------------------------------------------------------------------------------------------------------
 * Network-down policy (CHIP task only) */

#if CONFIG_THREAD_PAUSE_WHEN_DETACHED
static bool s_paused = false;
static uint32_t s_retry_min = CONFIG_THREAD_RETRY_MIN_MIN;
static System::Clock::Timestamp s_detached_since = System::Clock::kZero;

// A window or a fail-safe blocking a pause is checked again after this long
static constexpr uint32_t k_pause_recheck_s = 300;

/* Commissioned, with a Thread network to attach to */
static bool pause_applicable()
{
    return Server::GetInstance().GetFabricTable().FabricCount() > 0 && ConnectivityMgr().IsThreadProvisioned();
}

/* Never during (re)commissioning */
static bool pause_blocked()
{
    Server &server = Server::GetInstance();
    return server.GetFailSafeContext().IsFailSafeArmed() ||
           server.GetCommissioningWindowManager().IsCommissioningWindowOpen();
}

static bool may_pause()
{
    return pause_applicable() && !pause_blocked();
}

static void resume_thread(const char *why);

static void retry_timer_cb(System::Layer *, void *)
{
    resume_thread("retry");
}

static void pause_timer_cb(System::Layer *, void *)
{
    if (s_paused || ThreadStackMgr().IsThreadAttached() || !pause_applicable()) {
        return;
    }
    if (pause_blocked()) {
        // Not dropped: OpenThread would keep retrying on its own backoff for as long as the outage lasts
        SystemLayer().StartTimer(System::Clock::Seconds32(k_pause_recheck_s), pause_timer_cb, nullptr);
        return;
    }
    ESP_LOGW(TAG, "Thread detached for too long: paused, next attach attempt in %" PRIu32 " min", s_retry_min);
    s_paused = true;
    LogErrorOnFailure(ThreadStackMgr().SetThreadEnabled(false));
    SystemLayer().StartTimer(System::Clock::Seconds32(s_retry_min * 60), retry_timer_cb, nullptr);
    s_retry_min = std::min<uint32_t>(s_retry_min * 2, CONFIG_THREAD_RETRY_MAX_MIN);
}

static void resume_thread(const char *why)
{
    SystemLayer().CancelTimer(retry_timer_cb, nullptr);
    if (!s_paused) {
        return;
    }
    ESP_LOGI(TAG, "Thread resumed (%s)", why);
    s_paused = false;
    LogErrorOnFailure(ThreadStackMgr().SetThreadEnabled(true));
    // Give the attach a few minutes before pausing again
    SystemLayer().StartTimer(System::Clock::Seconds32(CONFIG_THREAD_RETRY_ATTACH_WINDOW_S), pause_timer_cb, nullptr);
}

static void on_connectivity(bool attached)
{
    if (attached) {
        SystemLayer().CancelTimer(pause_timer_cb, nullptr);
        SystemLayer().CancelTimer(retry_timer_cb, nullptr);
        s_retry_min = CONFIG_THREAD_RETRY_MIN_MIN;
        s_detached_since = System::Clock::kZero;
        return;
    }
    if (s_paused) {
        return; // our own SetThreadEnabled(false)
    }
    s_detached_since = System::SystemClock().GetMonotonicTimestamp();
    SystemLayer().StartTimer(System::Clock::Seconds32(CONFIG_THREAD_DETACHED_PAUSE_MIN * 60), pause_timer_cb, nullptr);
}
#endif // CONFIG_THREAD_PAUSE_WHEN_DETACHED

void power_on_user_activity(void)
{
#if CONFIG_THREAD_PAUSE_WHEN_DETACHED
    if (s_paused) {
        resume_thread("button");
        return;
    }
    // Detached for a while, OpenThread waiting in its attach backoff (up to 20 min): restart the attach now
    if (s_detached_since != System::Clock::kZero && !ThreadStackMgr().IsThreadAttached() && may_pause() &&
        System::SystemClock().GetMonotonicTimestamp() - s_detached_since > System::Clock::Seconds32(CONFIG_THREAD_KICK_AFTER_S)) {
        ESP_LOGI(TAG, "Detached: restarting the Thread attach (button)");
        s_detached_since = System::SystemClock().GetMonotonicTimestamp();
        LogErrorOnFailure(ThreadStackMgr().SetThreadEnabled(false));
        LogErrorOnFailure(ThreadStackMgr().SetThreadEnabled(true));
    }
#endif
}

/* ---------------------------------------------------------------------------------------------------------------
 * LIT with Dynamic SIT LIT Support: stay SIT unless every fabric registered a Check-In client */

#if CHIP_CONFIG_ENABLE_ICD_DSLS
namespace {

/* Evaluated when a fabric is added or removed (esp-matter device events), when a Check-In client registers or
 * unregisters (ICDListener, kTableUpdated) and, as a safety net, each time the ICD enters Active mode */
class IcdModePolicy : public app::ICDStateObserver, public app::ICDListener
{
public:
    void Evaluate()
    {
        Server &server = Server::GetInstance();
        bool every_fabric = server.GetFabricTable().FabricCount() > 0;
        for (const auto &fabric : server.GetFabricTable()) {
            ICDMonitoringTable table(server.GetPersistentStorage(), fabric.GetFabricIndex(), 1,
                                     server.GetSessionKeystore());
            if (table.IsEmpty()) {
                every_fabric = false;
                break;
            }
        }
        // The flag first: the SIT request enters Active mode, which calls Evaluate() again
        if (every_fabric && mSitRequested) {
            mSitRequested = false;
            ESP_LOGI(TAG, "Every fabric has a Check-In client: LIT allowed");
            app::ICDNotifier::GetInstance().NotifySITModeRequestWithdrawal();
        } else if (!every_fabric && !mSitRequested) {
            mSitRequested = true;
            ESP_LOGI(TAG, "A fabric has no Check-In client: staying in SIT mode");
            app::ICDNotifier::GetInstance().NotifySITModeRequestNotification();
        }
    }

    // ICDStateObserver
    void OnEnterActiveMode() override { Evaluate(); }
    void OnEnterIdleMode() override {}
    void OnTransitionToIdle() override {}
    void OnICDModeChange() override {}

    // ICDListener
    void OnICDManagementServerEvent(ICDManagementEvents event) override
    {
        if (event == ICDManagementEvents::kTableUpdated) {
            // Raised inside the RegisterClient / UnregisterClient command: evaluate once the command has returned
            LogErrorOnFailure(PlatformMgr().ScheduleWork(
                [](intptr_t arg) { reinterpret_cast<IcdModePolicy *>(arg)->Evaluate(); },
                reinterpret_cast<intptr_t>(this)));
        }
    }
    void OnNetworkActivity() override {}
    void OnKeepActiveRequest(KeepActiveFlags) override {}
    void OnActiveRequestWithdrawal(KeepActiveFlags) override {}
    void OnSITModeRequest() override {}
    void OnSITModeRequestWithdrawal() override {}
    void OnSubscriptionReport() override {}
#if CHIP_CONFIG_ENABLE_ICD_SERVER && CHIP_CONFIG_ENABLE_ICD_CIP && CHIP_CONFIG_ENABLE_ICD_CHECK_IN_ON_REPORT_TIMEOUT
    void OnSendCheckIn(Optional<Access::SubjectDescriptor>) override {}
#endif

private:
    bool mSitRequested = false;
};

IcdModePolicy s_icd_policy;

} // namespace
#endif // CHIP_CONFIG_ENABLE_ICD_DSLS

void power_thread_setup(void)
{
    apply_thread_settings();
#if CHIP_CONFIG_ENABLE_ICD_DSLS
    LogErrorOnFailure(PlatformMgr().ScheduleWork([](intptr_t) {
        if (!Server::GetInstance().GetICDManager().RegisterObserver(&s_icd_policy)) {
            ESP_LOGE(TAG, "ICD observer pool full (CHIP_CONFIG_ICD_OBSERVERS_POOL_SIZE)");
        }
        if (app::ICDNotifier::GetInstance().Subscribe(&s_icd_policy) != CHIP_NO_ERROR) {
            ESP_LOGE(TAG, "ICD notifier full (CONFIG_ICD_MAX_NOTIFICATION_SUBSCRIBERS)");
        }
        s_icd_policy.Evaluate();
    }));
#endif
}

void power_on_event(const ChipDeviceEvent *event)
{
    switch (event->Type) {
    case DeviceEventType::kThreadConnectivityChange:
        if (event->ThreadConnectivityChange.Result == kConnectivity_Established) {
            // Also after a re-attach or a resumed Thread: keep the TX power at the configured value
            apply_thread_settings();
        }
#if CONFIG_THREAD_PAUSE_WHEN_DETACHED
        if (event->ThreadConnectivityChange.Result != kConnectivity_NoChange) {
            on_connectivity(event->ThreadConnectivityChange.Result == kConnectivity_Established);
        }
#endif
        break;
#if CONFIG_THREAD_PAUSE_WHEN_DETACHED
    case DeviceEventType::kServerReady:
        // Booted without a network (Border Router down, battery swapped or brownout during an outage): a device that
        // never attaches gets no connectivity change event, so start the detached timer here
        if (pause_applicable() && !ThreadStackMgr().IsThreadAttached() && s_detached_since == System::Clock::kZero) {
            on_connectivity(false);
        }
        break;
#endif
#if CHIP_CONFIG_ENABLE_ICD_DSLS
    case DeviceEventType::kFabricRemoved:
    case DeviceEventType::kFabricCommitted:
        s_icd_policy.Evaluate();
        break;
#endif
    default:
        break;
    }
}
