#include <sdkconfig.h>
#include <thread_diag.h>

#if CONFIG_THREAD_DIAG_LOG
#include <string.h>

#include <esp_log.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <openthread/ip6.h>
#include <openthread/netdata.h>
#include <openthread/srp_client.h>
#include <openthread/thread.h>

static const char *TAG = "thread_diag";
static constexpr uint32_t k_interval_ms = 10000;

/* One-way flag set once commissioned. stop() can arrive (kServerReady) before start(); nothing ever clears it,
 * so a stop() racing with start() is still seen by the task at its next tick */
static volatile bool s_done = false;
static TaskHandle_t s_task = NULL;

static bool is_link_local(const otIp6Address &addr)
{
    return addr.mFields.m8[0] == 0xfe && (addr.mFields.m8[1] & 0xc0) == 0x80;
}

static bool is_mesh_local(otInstance *instance, const otIp6Address &addr)
{
    const otMeshLocalPrefix *ml_prefix = otThreadGetMeshLocalPrefix(instance);
    return ml_prefix && memcmp(addr.mFields.m8, ml_prefix->m8, sizeof(ml_prefix->m8)) == 0;
}

static void thread_diag_dump()
{
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        return;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);

    otDeviceRole role = otThreadGetDeviceRole(instance);
    if (role == OT_DEVICE_ROLE_DISABLED || role == OT_DEVICE_ROLE_DETACHED) {
        ESP_LOGI(TAG, "Thread %s: waiting for network credentials (BLE commissioning)",
                 otThreadDeviceRoleToString(role));
        esp_openthread_lock_release();
        return;
    }

    char buf[OT_IP6_ADDRESS_STRING_SIZE];
    ESP_LOGI(TAG, "===== Thread diagnostics =====");
    ESP_LOGI(TAG, "Role: %s, RLOC16: 0x%04x", otThreadDeviceRoleToString(role), otThreadGetRloc16(instance));

    bool has_omr = false;
    for (const otNetifAddress *a = otIp6GetUnicastAddresses(instance); a; a = a->mNext) {
        const char *kind;
        if (is_link_local(a->mAddress)) {
            kind = "link-local";
        } else if (is_mesh_local(instance, a->mAddress)) {
            kind = a->mRloc ? "mesh-local RLOC" : "mesh-local";
        } else {
            kind = "OMR (reachable from Wi-Fi/LAN)";
            has_omr = true;
        }
        otIp6AddressToString(&a->mAddress, buf, sizeof(buf));
        ESP_LOGI(TAG, "Address: %s  [%s]", buf, kind);
    }

    otNetworkDataIterator it = OT_NETWORK_DATA_ITERATOR_INIT;
    otBorderRouterConfig prefix;
    int prefix_count = 0;
    char prefix_buf[OT_IP6_PREFIX_STRING_SIZE];
    while (otNetDataGetNextOnMeshPrefix(instance, &it, &prefix) == OT_ERROR_NONE) {
        otIp6PrefixToString(&prefix.mPrefix, prefix_buf, sizeof(prefix_buf));
        ESP_LOGI(TAG, "On-mesh prefix: %s (border router RLOC16 0x%04x%s%s)", prefix_buf, prefix.mRloc16,
                 prefix.mSlaac ? ", SLAAC" : "", prefix.mDefaultRoute ? ", default route" : "");
        prefix_count++;
    }

    const char *host_state = "not started";
    const char *service_state = "none";
    if (otSrpClientIsRunning(instance)) {
        const otSockAddr *server = otSrpClientGetServerAddress(instance);
        otIp6AddressToString(&server->mAddress, buf, sizeof(buf));
        ESP_LOGI(TAG, "SRP server: [%s]:%u", buf, server->mPort);

        const otSrpClientHostInfo *host = otSrpClientGetHostInfo(instance);
        host_state = otSrpClientItemStateToString(host->mState);
        ESP_LOGI(TAG, "SRP host: %s, state: %s", host->mName ? host->mName : "(none)", host_state);

        for (const otSrpClientService *s = otSrpClientGetServices(instance); s; s = s->mNext) {
            service_state = otSrpClientItemStateToString(s->mState);
            ESP_LOGI(TAG, "SRP service: %s.%s, state: %s", s->mInstanceName, s->mName, service_state);
        }
    }

    ESP_LOGI(TAG, "Summary: OMR address %s, on-mesh prefixes %d, SRP host %s, SRP service %s",
             has_omr ? "YES" : "NO", prefix_count, host_state, service_state);
    if (!has_omr) {
        ESP_LOGW(TAG, "No OMR address: the Border Router does not route Wi-Fi/LAN <-> Thread, "
                      "phones cannot reach this device");
    }
    ESP_LOGI(TAG, "==============================");

    esp_openthread_lock_release();
}

static void thread_diag_task(void *arg)
{
    while (!s_done) {
        vTaskDelay(pdMS_TO_TICKS(k_interval_ms));
        if (s_done) {
            break;
        }
        thread_diag_dump();
    }
    ESP_LOGI(TAG, "Commissioned, Thread diagnostics stopped");
    s_task = NULL;
    vTaskDelete(NULL);
}

void thread_diag_start()
{
    if (s_task || s_done) {
        return;
    }
    if (xTaskCreate(thread_diag_task, "thread_diag", 4096, NULL, 2, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create diagnostics task");
    }
}

void thread_diag_stop()
{
    /* Only a flag here: this runs on the CHIP task, the OpenThread lock is taken by the diag task alone */
    s_done = true;
}

#else // CONFIG_THREAD_DIAG_LOG

void thread_diag_start() {}
void thread_diag_stop() {}

#endif // CONFIG_THREAD_DIAG_LOG
