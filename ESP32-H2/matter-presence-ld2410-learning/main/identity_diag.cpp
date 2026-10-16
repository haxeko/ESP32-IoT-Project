// See identity_diag.h. ESP-IDF v6.0.2, esp-matter 1.6.1 (connectedhomeip 3d1c9e3c), ESP32-H2 with OpenThread FTD or
// ESP32-C3 with Wi-Fi.

#include <sdkconfig.h>

#include "identity_diag.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#if CONFIG_OPENTHREAD_ENABLED
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <openthread/dataset.h>
#include <openthread/ip6.h>
#include <openthread/link.h>
#include <openthread/netdata.h>
#include <openthread/srp_client.h>
#include <openthread/thread.h>
#if CONFIG_OPENTHREAD_FTD
#include <openthread/thread_ftd.h>
#endif
#elif CONFIG_ENABLE_WIFI_STATION
#include <esp_netif.h>
#include <esp_wifi.h>
#else
#error "identity_diag logs the Thread (CONFIG_OPENTHREAD_ENABLED) or Wi-Fi (CONFIG_ENABLE_WIFI_STATION) network"
#endif

#include <esp_matter.h> // esp-matter event types: kFabricRemoved, kFabricCommitted, ...

#include <app/InteractionModelEngine.h>
#include <app/ReadHandler.h>
#include <app/server/Server.h>
#include <lib/support/Iterators.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/ConfigurationManager.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <transport/SecureSession.h>

#ifndef CONFIG_IDENTITY_DIAG_SUBSCRIPTION_LOG
#define CONFIG_IDENTITY_DIAG_SUBSCRIPTION_LOG 0
#endif

using namespace chip;
using namespace chip::DeviceLayer;

static const char *TAG = "id_diag";

// Same priority as the Matter task (CONFIG_CHIP_TASK_PRIORITY = 1), below OpenThread or Wi-Fi; the task mostly sleeps.
static constexpr UBaseType_t kTaskPriority = tskIDLE_PRIORITY + 1;
static constexpr uint32_t kTaskStack = 4096;
// How long each lock is waited for before this round is skipped (and logged as such)
static constexpr TickType_t kLockWait = pdMS_TO_TICKS(2000);
// Event-triggered blocks are coalesced: at most one every kMinEventGap
static constexpr TickType_t kMinEventGap = pdMS_TO_TICKS(5000);
// NVS entries are counted per namespace in the first block and then every kNvsScanEvery periodic blocks
static constexpr uint32_t kNvsScanEvery = 12;

static constexpr uint32_t kNotifyFull = 1u << 0;
static constexpr uint32_t kNotifyNet = 1u << 1; // Thread or Wi-Fi block only

static TaskHandle_t s_task = nullptr;
static uint32_t s_period_s = 0;

// Written on the CHIP task only, read by the diagnostics task under the CHIP lock
static uint32_t s_sub_requested = 0;
static uint32_t s_sub_established = 0;
static uint32_t s_sub_terminated = 0;
static uint32_t s_case_established = 0;

// ---------------------------------------------------------------------------------------------------------------------
// Snapshots: static, only the diagnostics task touches them

struct FabricSnap
{
    uint8_t index;
    uint16_t vendor_id;
    uint64_t node_id;
    uint64_t fabric_id;
    uint64_t compressed_fabric_id;
    uint32_t subscriptions;
    uint16_t case_sessions;
    char label[kFabricLabelMaxLengthInBytes + 1];
};

struct ChipSnap
{
    bool valid;
    uint8_t fabric_count;
    uint8_t n;
    FabricSnap fabrics[CHIP_CONFIG_MAX_FABRICS];
    bool window_open;
    bool fail_safe_armed;
    uint32_t subs_total;
    uint32_t reads_total;
    uint16_t case_active;
    uint16_t pase_active;
    uint32_t sub_requested, sub_established, sub_terminated, case_established;
    uint16_t vid, pid;
    bool have_ids;
    char serial[ConfigurationManager::kMaxSerialNumberLength + 1];
    char unique_id[ConfigurationManager::kMaxUniqueIDLength + 1];
    bool have_disc;
    uint16_t discriminator;
};

#if CONFIG_OPENTHREAD_ENABLED
struct OtSnap
{
    bool valid;
    otDeviceRole role;
    otExtAddress ext;
    otExtAddress eui64;
    uint16_t rloc16;
    char mleid[OT_IP6_ADDRESS_STRING_SIZE];
    bool dataset_commissioned;
    uint32_t partition_id;
    uint8_t leader_router_id;
    char net_name[OT_NETWORK_NAME_MAX_SIZE + 1];
    otExtendedPanId xpanid;
    uint16_t pan_id;
    uint8_t channel;
    uint16_t nbr_routers, nbr_children;
    uint16_t routers_allocated;
    bool has_parent;
    otRouterInfo parent;
    int8_t parent_avg_rssi, parent_last_rssi;
    bool parent_avg_ok, parent_last_ok;
    uint32_t attach_duration_s;
    otMleCounters mle;
    uint16_t onmesh_prefixes, default_routes;
    bool has_omr;
    bool srp_running;
    char srp_server[OT_IP6_SOCK_ADDR_STRING_SIZE];
    char srp_host[64];
    const char *srp_host_state; // static string from otSrpClientItemStateToString()
    uint16_t srp_svc_total, srp_svc_registered;
};

#else
struct WifiSnap
{
    bool connected; // associated with an access point
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    bool ps_ok;
    wifi_ps_type_t ps;
    bool has_netif;
    bool has_ip4;
    esp_ip4_addr_t ip4;
    uint8_t ip6_link_local, ip6_unique_local, ip6_global;
};
#endif

static ChipSnap s_chip;
#if CONFIG_OPENTHREAD_ENABLED
static OtSnap s_ot;
#else
static WifiSnap s_wifi;
#endif

static void to_hex(const uint8_t *b, size_t n, char *out, size_t out_size)
{
    size_t pos = 0;
    for (size_t i = 0; i < n && pos + 2 < out_size; i++) {
        pos += snprintf(&out[pos], out_size - pos, "%02X", b[i]);
    }
    out[pos < out_size ? pos : out_size - 1] = '\0';
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_EXT:
        return "external pin";
    case ESP_RST_SW:
        return "software (esp_restart)";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT:
        return "TASK WATCHDOG";
    case ESP_RST_WDT:
        return "OTHER WATCHDOG";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    case ESP_RST_USB:
        return "USB";
    case ESP_RST_JTAG:
        return "JTAG";
    case ESP_RST_EFUSE:
        return "eFuse error";
    case ESP_RST_PWR_GLITCH:
        return "POWER GLITCH";
    case ESP_RST_CPU_LOCKUP:
        return "CPU LOCKUP";
    default:
        return "unknown";
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Subscription log (ReadHandler::ApplicationCallback, CHIP task). Only observes: always lets the subscription proceed
// with the intervals negotiated by the SDK, and chains to a callback registered before it.

#if CONFIG_IDENTITY_DIAG_SUBSCRIPTION_LOG
namespace {

class SubscriptionLogger : public app::ReadHandler::ApplicationCallback
{
public:
    app::ReadHandler::ApplicationCallback *mPrev = nullptr;

    CHIP_ERROR OnSubscriptionRequested(app::ReadHandler &handler, Transport::SecureSession &session) override
    {
        uint16_t min_s = 0, max_s = 0;
        handler.GetReportingIntervals(min_s, max_s);
        s_sub_requested++;
        Remember(&handler, session.GetFabricIndex(), session.GetPeerNodeId());
        ESP_LOGI(TAG, "SUB request: fabric %u peer " ChipLogFormatX64 " min %u s max %u s (active %" PRIu32 ")",
                 session.GetFabricIndex(), ChipLogValueX64(session.GetPeerNodeId()), min_s, max_s,
                 app::InteractionModelEngine::GetInstance()->GetNumActiveReadHandlers(
                     app::ReadHandler::InteractionType::Subscribe));
        return mPrev ? mPrev->OnSubscriptionRequested(handler, session) : CHIP_NO_ERROR;
    }

    void OnSubscriptionEstablished(app::ReadHandler &handler) override
    {
        uint16_t min_s = 0, max_s = 0;
        handler.GetReportingIntervals(min_s, max_s);
        s_sub_established++;
        // A subscription resumed after a reboot (persisted subscriptions) has no OnSubscriptionRequested
        const Entry *e = Find(&handler);
        ESP_LOGI(TAG, "SUB established: fabric %u peer " ChipLogFormatX64 " min %u s max %u s%s", e ? e->fabric : 0,
                 ChipLogValueX64(e ? e->peer : 0), min_s, max_s, e ? "" : " (resumed)");
        if (mPrev) {
            mPrev->OnSubscriptionEstablished(handler);
        }
    }

    void OnSubscriptionTerminated(app::ReadHandler &handler) override
    {
        s_sub_terminated++;
        Entry *e = Find(&handler);
        ESP_LOGW(TAG, "SUB terminated: fabric %u peer " ChipLogFormatX64, e ? e->fabric : 0,
                 ChipLogValueX64(e ? e->peer : 0));
        if (e) {
            e->handler = nullptr;
        }
        if (mPrev) {
            mPrev->OnSubscriptionTerminated(handler);
        }
    }

private:
    // Fabric and peer of a subscription are public only in OnSubscriptionRequested (ReadHandler's getters are
    // private): remember them by handler address. Diagnostics only, a stale slot is simply overwritten.
    struct Entry
    {
        const app::ReadHandler *handler;
        FabricIndex fabric;
        NodeId peer;
    };
    static constexpr size_t kSlots = CHIP_IM_MAX_NUM_SUBSCRIPTIONS;
    Entry mEntries[kSlots] = {};
    size_t mNext = 0;

    void Remember(const app::ReadHandler *h, FabricIndex fabric, NodeId peer)
    {
        Entry *e = Find(h);
        if (!e) {
            e = Find(nullptr); // a slot never used or freed by OnSubscriptionTerminated
        }
        if (!e) {
            e = &mEntries[mNext]; // all in use (some may be stale): overwrite round-robin
            mNext = (mNext + 1) % kSlots;
        }
        *e = { h, fabric, peer };
    }

    Entry *Find(const app::ReadHandler *h)
    {
        for (Entry &e : mEntries) {
            if (e.handler == h) {
                return &e;
            }
        }
        return nullptr;
    }
};

SubscriptionLogger s_sub_logger;
bool s_sub_logger_installed = false;

} // namespace

// CHIP lock held
static void install_subscription_logger()
{
    if (s_sub_logger_installed) {
        return;
    }
    app::InteractionModelEngine *engine = app::InteractionModelEngine::GetInstance();
    // GetAppCallback() is public in ReadHandler::ManagementCallback (InteractionModelEngine overrides it privately)
    app::ReadHandler::ApplicationCallback *prev =
        static_cast<app::ReadHandler::ManagementCallback *>(engine)->GetAppCallback();
    if (prev != &s_sub_logger) {
        s_sub_logger.mPrev = prev;
        engine->RegisterReadHandlerAppCallback(&s_sub_logger);
    }
    s_sub_logger_installed = true;
}
#endif // CONFIG_IDENTITY_DIAG_SUBSCRIPTION_LOG

// ---------------------------------------------------------------------------------------------------------------------
// Matter snapshot: CHIP lock held, OpenThread lock NOT held

static void collect_chip_locked(ChipSnap &c)
{
    memset(&c, 0, sizeof(c));
    Server &server = Server::GetInstance();
    const FabricTable &fabrics = server.GetFabricTable();
    app::InteractionModelEngine *im = app::InteractionModelEngine::GetInstance();

#if CONFIG_IDENTITY_DIAG_SUBSCRIPTION_LOG
    install_subscription_logger();
#endif

    c.fabric_count = fabrics.FabricCount();
    for (const FabricInfo &fb : fabrics) {
        if (c.n >= CHIP_CONFIG_MAX_FABRICS) {
            break;
        }
        FabricSnap &f = c.fabrics[c.n++];
        f.index = fb.GetFabricIndex();
        f.vendor_id = static_cast<uint16_t>(fb.GetVendorId());
        f.node_id = fb.GetNodeId();
        f.fabric_id = fb.GetFabricId();
        f.compressed_fabric_id = fb.GetCompressedFabricId();
        f.subscriptions = im->GetNumActiveReadHandlers(app::ReadHandler::InteractionType::Subscribe, f.index);
        const CharSpan label = fb.GetFabricLabel();
        const size_t len = label.size() < sizeof(f.label) - 1 ? label.size() : sizeof(f.label) - 1;
        memcpy(f.label, label.data(), len);
        f.label[len] = '\0';
    }

    server.GetSecureSessionManager().GetSecureSessions().ForEachSession([&c](Transport::SecureSession *s) {
        if (!s->IsActiveSession()) {
            return Loop::Continue;
        }
        if (s->IsCASESession()) {
            c.case_active++;
            for (uint8_t i = 0; i < c.n; i++) {
                if (c.fabrics[i].index == s->GetFabricIndex()) {
                    c.fabrics[i].case_sessions++;
                }
            }
        } else if (s->IsPASESession()) {
            c.pase_active++;
        }
        return Loop::Continue;
    });

    c.window_open = server.GetCommissioningWindowManager().IsCommissioningWindowOpen();
    c.fail_safe_armed = server.GetFailSafeContext().IsFailSafeArmed();
    c.subs_total = im->GetNumActiveReadHandlers(app::ReadHandler::InteractionType::Subscribe);
    c.reads_total = im->GetNumActiveReadHandlers(app::ReadHandler::InteractionType::Read);
    c.sub_requested = s_sub_requested;
    c.sub_established = s_sub_established;
    c.sub_terminated = s_sub_terminated;
    c.case_established = s_case_established;

    DeviceInstanceInfoProvider *dii = GetDeviceInstanceInfoProvider();
    if (dii) {
        c.have_ids = dii->GetVendorId(c.vid) == CHIP_NO_ERROR && dii->GetProductId(c.pid) == CHIP_NO_ERROR;
        if (dii->GetSerialNumber(c.serial, sizeof(c.serial)) != CHIP_NO_ERROR) {
            strlcpy(c.serial, "?", sizeof(c.serial));
        }
    }
    if (ConfigurationMgr().GetUniqueId(c.unique_id, sizeof(c.unique_id)) != CHIP_NO_ERROR) {
        strlcpy(c.unique_id, "?", sizeof(c.unique_id));
    }
    // The discriminator is public (BLE and DNS-SD advertisements); the setup passcode is never read here
    CommissionableDataProvider *cdp = GetCommissionableDataProvider();
    c.have_disc = cdp && cdp->GetSetupDiscriminator(c.discriminator) == CHIP_NO_ERROR;
    c.valid = true;
}

static bool collect_chip(ChipSnap &c)
{
    // Polled instead of LockChipStack(): a stuck Matter task must not block the diagnostics too
    const TickType_t start = xTaskGetTickCount();
    while (!PlatformMgr().TryLockChipStack()) {
        if (xTaskGetTickCount() - start >= kLockWait) {
            c.valid = false;
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    collect_chip_locked(c);
    PlatformMgr().UnlockChipStack();
    return true;
}

#if CONFIG_OPENTHREAD_ENABLED
// ---------------------------------------------------------------------------------------------------------------------
// OpenThread snapshot: OpenThread lock held, CHIP lock NOT held

static bool is_link_local(const otIp6Address &a)
{
    return a.mFields.m8[0] == 0xfe && (a.mFields.m8[1] & 0xc0) == 0x80;
}

static void collect_ot_locked(otInstance *ot, OtSnap &o)
{
    memset(&o, 0, sizeof(o));
    o.role = otThreadGetDeviceRole(ot);
    memcpy(&o.ext, otLinkGetExtendedAddress(ot), sizeof(o.ext));
    otLinkGetFactoryAssignedIeeeEui64(ot, &o.eui64);
    o.rloc16 = otThreadGetRloc16(ot);
    otIp6AddressToString(otThreadGetMeshLocalEid(ot), o.mleid, sizeof(o.mleid));
    o.dataset_commissioned = otDatasetIsCommissioned(ot);

    // Individual getters, not otDatasetGetActive(): that would copy the network key and PSKc
    strlcpy(o.net_name, otThreadGetNetworkName(ot), sizeof(o.net_name));
    memcpy(&o.xpanid, otThreadGetExtendedPanId(ot), sizeof(o.xpanid));
    o.pan_id = otLinkGetPanId(ot);
    o.channel = otLinkGetChannel(ot);

    const bool attached =
        o.role == OT_DEVICE_ROLE_CHILD || o.role == OT_DEVICE_ROLE_ROUTER || o.role == OT_DEVICE_ROLE_LEADER;
    if (attached) {
        o.partition_id = otThreadGetPartitionId(ot);
        o.leader_router_id = otThreadGetLeaderRouterId(ot);
        o.attach_duration_s = otThreadGetCurrentAttachDuration(ot);
    }

    otNeighborInfoIterator it = OT_NEIGHBOR_INFO_ITERATOR_INIT;
    otNeighborInfo nbr;
    while (otThreadGetNextNeighborInfo(ot, &it, &nbr) == OT_ERROR_NONE) {
        if (nbr.mIsChild) {
            o.nbr_children++;
        } else {
            o.nbr_routers++;
        }
    }

#if CONFIG_OPENTHREAD_FTD
    if (o.role == OT_DEVICE_ROLE_ROUTER || o.role == OT_DEVICE_ROLE_LEADER) {
        const uint8_t max_id = otThreadGetMaxRouterId(ot);
        for (uint16_t id = 0; id <= max_id; id++) {
            otRouterInfo r;
            if (otThreadGetRouterInfo(ot, id, &r) == OT_ERROR_NONE && r.mAllocated) {
                o.routers_allocated++;
            }
        }
    }
#endif

    if (o.role == OT_DEVICE_ROLE_CHILD) {
        o.has_parent = otThreadGetParentInfo(ot, &o.parent) == OT_ERROR_NONE;
        o.parent_avg_ok = otThreadGetParentAverageRssi(ot, &o.parent_avg_rssi) == OT_ERROR_NONE;
        o.parent_last_ok = otThreadGetParentLastRssi(ot, &o.parent_last_rssi) == OT_ERROR_NONE;
    }

    memcpy(&o.mle, otThreadGetMleCounters(ot), sizeof(o.mle));

    // On-mesh prefixes published by Border Routers (OMR prefixes and default routes)
    otNetworkDataIterator nd_it = OT_NETWORK_DATA_ITERATOR_INIT;
    otBorderRouterConfig prefix;
    while (otNetDataGetNextOnMeshPrefix(ot, &nd_it, &prefix) == OT_ERROR_NONE) {
        o.onmesh_prefixes++;
        if (prefix.mDefaultRoute) {
            o.default_routes++;
        }
    }

    const otMeshLocalPrefix *ml = otThreadGetMeshLocalPrefix(ot);
    for (const otNetifAddress *a = otIp6GetUnicastAddresses(ot); a; a = a->mNext) {
        const bool mesh_local = ml && memcmp(a->mAddress.mFields.m8, ml->m8, sizeof(ml->m8)) == 0;
        if (!is_link_local(a->mAddress) && !mesh_local) {
            o.has_omr = true;
        }
    }

    o.srp_running = otSrpClientIsRunning(ot);
    if (o.srp_running) {
        otIp6SockAddrToString(otSrpClientGetServerAddress(ot), o.srp_server, sizeof(o.srp_server));
    } else {
        strlcpy(o.srp_server, "-", sizeof(o.srp_server));
    }
    const otSrpClientHostInfo *host = otSrpClientGetHostInfo(ot);
    if (host) {
        strlcpy(o.srp_host, host->mName ? host->mName : "(none)", sizeof(o.srp_host));
        o.srp_host_state = otSrpClientItemStateToString(host->mState);
    }
    for (const otSrpClientService *s = otSrpClientGetServices(ot); s; s = s->mNext) {
        o.srp_svc_total++;
        if (s->mState == OT_SRP_CLIENT_ITEM_STATE_REGISTERED) {
            o.srp_svc_registered++;
        }
    }
    o.valid = true;
}

static bool collect_ot(OtSnap &o)
{
    if (!esp_openthread_lock_acquire(kLockWait)) {
        o.valid = false;
        return false;
    }
    collect_ot_locked(esp_openthread_get_instance(), o);
    esp_openthread_lock_release();
    return true;
}

#else
// ---------------------------------------------------------------------------------------------------------------------
// Wi-Fi snapshot: no lock needed (the Wi-Fi driver and esp_netif getters can be called from any task), CHIP lock
// NOT held

static void collect_wifi(WifiSnap &w)
{
    memset(&w, 0, sizeof(w));
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        w.connected = true;
        static_assert(sizeof(ap.ssid) <= sizeof(w.ssid), "SSID buffer");
        memcpy(w.ssid, ap.ssid, sizeof(ap.ssid));
        w.ssid[sizeof(w.ssid) - 1] = '\0';
        memcpy(w.bssid, ap.bssid, sizeof(w.bssid));
        w.channel = ap.primary;
        w.rssi = ap.rssi;
    }
    w.ps_ok = esp_wifi_get_ps(&w.ps) == ESP_OK;

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return;
    }
    w.has_netif = true;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
        w.has_ip4 = true;
        w.ip4 = ip.ip;
    }
    esp_ip6_addr_t ip6[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
    const int n = esp_netif_get_all_ip6(netif, ip6);
    for (int i = 0; i < n; i++) {
        switch (esp_netif_ip6_get_addr_type(&ip6[i])) {
        case ESP_IP6_ADDR_IS_LINK_LOCAL:
            w.ip6_link_local++;
            break;
        case ESP_IP6_ADDR_IS_UNIQUE_LOCAL:
            w.ip6_unique_local++;
            break;
        case ESP_IP6_ADDR_IS_GLOBAL:
            w.ip6_global++;
            break;
        default:
            break;
        }
    }
}
#endif // CONFIG_OPENTHREAD_ENABLED

// ---------------------------------------------------------------------------------------------------------------------
// Logging, no lock held

static void log_hw(const ChipSnap &c)
{
    uint8_t mac[6] = {};
#if CONFIG_OPENTHREAD_ENABLED
    uint8_t radio[8] = {}; // IEEE 802.15.4 EUI-64
    const esp_mac_type_t radio_type = ESP_MAC_IEEE802154;
    const char *radio_name = "eui64";
#else
    uint8_t radio[6] = {};
    const esp_mac_type_t radio_type = ESP_MAC_WIFI_STA;
    const char *radio_name = "wifi_mac";
#endif
    char mac_s[13], radio_s[2 * sizeof(radio) + 1];
    const bool mac_ok = esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY) == ESP_OK;
    const bool radio_ok = esp_read_mac(radio, radio_type) == ESP_OK;
    to_hex(mac, sizeof(mac), mac_s, sizeof(mac_s));
    to_hex(radio, sizeof(radio), radio_s, sizeof(radio_s));
    ESP_LOGI(TAG, "hw   efuse_mac=%s %s=%s reset=%s up=%" PRIu32 "s heap=%" PRIu32 " min_heap=%" PRIu32,
             mac_ok ? mac_s : "?", radio_name, radio_ok ? radio_s : "?", reset_reason_str(esp_reset_reason()),
             (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ), esp_get_free_heap_size(),
             esp_get_minimum_free_heap_size());
    if (!c.valid) {
        return;
    }
    char disc[8] = "?";
    if (c.have_disc) {
        snprintf(disc, sizeof(disc), "%u", c.discriminator);
    }
    ESP_LOGI(TAG, "dev  vid=0x%04X pid=0x%04X serial=%s unique_id=%s discriminator=%s test_setup_code=%s",
             c.have_ids ? c.vid : 0, c.have_ids ? c.pid : 0, c.serial, c.unique_id, disc,
             CHIP_DEVICE_CONFIG_ENABLE_TEST_SETUP_PARAMS ? "yes" : "no");
}

#if CONFIG_OPENTHREAD_ENABLED
static void log_ot(const OtSnap &o)
{
    if (!o.valid) {
        ESP_LOGW(TAG, "ot   OpenThread lock busy for %u ms, skipped", (unsigned)pdTICKS_TO_MS(kLockWait));
        return;
    }
    char ext_s[17], eui_s[17], xpan_s[17];
    to_hex(o.ext.m8, sizeof(o.ext.m8), ext_s, sizeof(ext_s));
    to_hex(o.eui64.m8, sizeof(o.eui64.m8), eui_s, sizeof(eui_s));
    to_hex(o.xpanid.m8, sizeof(o.xpanid.m8), xpan_s, sizeof(xpan_s));

    ESP_LOGI(TAG, "ot   role=%s rloc16=0x%04X ext_addr=%s eui64=%s ml_eid=%s", otThreadDeviceRoleToString(o.role),
             o.rloc16, ext_s, eui_s, o.mleid);
    ESP_LOGI(TAG,
             "ot   dataset=%s net=\"%s\" ext_panid=%s panid=0x%04X ch=%u partition=0x%08" PRIX32
             " leader_rid=%u attached=%" PRIu32 "s",
             o.dataset_commissioned ? "yes" : "no", o.net_name, xpan_s, o.pan_id, o.channel, o.partition_id,
             o.leader_router_id, o.attach_duration_s);
    ESP_LOGI(TAG, "ot   neighbours: routers=%u children=%u, routers in partition=%u", o.nbr_routers, o.nbr_children,
             o.routers_allocated);
    if (o.has_parent) {
        char p_ext[17];
        to_hex(o.parent.mExtAddress.m8, sizeof(o.parent.mExtAddress.m8), p_ext, sizeof(p_ext));
        ESP_LOGI(TAG, "ot   parent rloc16=0x%04X ext=%s lq_in=%u lq_out=%u rssi_avg=%d%s rssi_last=%d%s",
                 o.parent.mRloc16, p_ext, o.parent.mLinkQualityIn, o.parent.mLinkQualityOut, o.parent_avg_rssi,
                 o.parent_avg_ok ? "" : "(n/a)", o.parent_last_rssi, o.parent_last_ok ? "" : "(n/a)");
    }
    ESP_LOGI(TAG,
             "mle  roles: disabled=%u detached=%u child=%u router=%u leader=%u; attach_attempts=%u "
             "partition_changes=%u better_partition=%u parent_changes=%u",
             o.mle.mDisabledRole, o.mle.mDetachedRole, o.mle.mChildRole, o.mle.mRouterRole, o.mle.mLeaderRole,
             o.mle.mAttachAttempts, o.mle.mPartitionIdChanges, o.mle.mBetterPartitionAttachAttempts,
             o.mle.mParentChanges);
    ESP_LOGI(TAG, "br   on-mesh prefixes=%u (default route %u), OMR address=%s", o.onmesh_prefixes, o.default_routes,
             o.has_omr ? "yes" : "NO");
    ESP_LOGI(TAG, "srp  server=%s host=%s state=%s services=%u/%u registered", o.srp_server, o.srp_host,
             o.srp_host_state ? o.srp_host_state : "-", o.srp_svc_registered, o.srp_svc_total);
    if (o.dataset_commissioned && !o.has_omr) {
        ESP_LOGW(TAG, "No OMR address: no Border Router routes between the LAN and this Thread network");
    }
}
#else
static const char *ps_str(const WifiSnap &w)
{
    if (!w.ps_ok) {
        return "?";
    }
    switch (w.ps) {
    case WIFI_PS_NONE:
        return "none";
    case WIFI_PS_MIN_MODEM:
        return "min_modem";
    case WIFI_PS_MAX_MODEM:
        return "max_modem";
    default:
        return "?";
    }
}

static void log_wifi(const WifiSnap &w)
{
    if (w.connected) {
        char bssid_s[13];
        to_hex(w.bssid, sizeof(w.bssid), bssid_s, sizeof(bssid_s));
        ESP_LOGI(TAG, "wifi connected ssid=\"%s\" bssid=%s ch=%u rssi=%d power_save=%s", w.ssid, bssid_s, w.channel,
                 w.rssi, ps_str(w));
    } else {
        ESP_LOGI(TAG, "wifi not connected (connecting, not commissioned yet, or the access point is out of reach) "
                      "power_save=%s",
                 ps_str(w));
    }
    if (!w.has_netif) {
        ESP_LOGW(TAG, "wifi no station interface");
        return;
    }
    char ip4_s[16] = "-";
    if (w.has_ip4) {
        snprintf(ip4_s, sizeof(ip4_s), IPSTR, IP2STR(&w.ip4));
    }
    ESP_LOGI(TAG, "ip   ipv4=%s ipv6: link-local=%u unique-local=%u global=%u", ip4_s, w.ip6_link_local,
             w.ip6_unique_local, w.ip6_global);
    if (w.connected && w.ip6_link_local == 0) {
        ESP_LOGW(TAG, "No IPv6 link-local address yet: Matter controllers reach the device over IPv6");
    }
}
#endif // CONFIG_OPENTHREAD_ENABLED

static void log_chip(const ChipSnap &c)
{
    if (!c.valid) {
        ESP_LOGW(TAG, "mtr  Matter stack lock busy for %u ms, skipped", (unsigned)pdTICKS_TO_MS(kLockWait));
        return;
    }
    ESP_LOGI(TAG,
             "mtr  commissioned=%s fabrics=%u window_open=%s failsafe=%s sessions: case=%u pase=%u, subscriptions=%" PRIu32
             " reads=%" PRIu32,
             c.fabric_count ? "yes" : "no", c.fabric_count, c.window_open ? "yes" : "no",
             c.fail_safe_armed ? "yes" : "no", c.case_active, c.pase_active, c.subs_total, c.reads_total);
    ESP_LOGI(TAG,
             "mtr  since boot: subscribe requests=%" PRIu32 " established=%" PRIu32 " terminated=%" PRIu32
             ", CASE sessions established=%" PRIu32,
             c.sub_requested, c.sub_established, c.sub_terminated, c.case_established);
    for (uint8_t i = 0; i < c.n; i++) {
        const FabricSnap &f = c.fabrics[i];
        ESP_LOGI(TAG,
                 "fab  index=%u vendor=0x%04X node_id=" ChipLogFormatX64 " fabric_id=" ChipLogFormatX64
                 " compressed_fabric_id=" ChipLogFormatX64 " case=%u subscriptions=%" PRIu32 " label=\"%s\"",
                 f.index, f.vendor_id, ChipLogValueX64(f.node_id), ChipLogValueX64(f.fabric_id),
                 ChipLogValueX64(f.compressed_fabric_id), f.case_sessions, f.subscriptions, f.label);
    }
}

static void log_nvs(bool scan_namespaces)
{
    nvs_stats_t st;
    esp_err_t err = nvs_get_stats(NVS_DEFAULT_PART_NAME, &st);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs  nvs_get_stats failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "nvs  entries used=%u free=%u available=%u total=%u namespaces=%u", (unsigned)st.used_entries,
             (unsigned)st.free_entries, (unsigned)st.available_entries, (unsigned)st.total_entries,
             (unsigned)st.namespace_count);
    if (!scan_namespaces) {
        return;
    }

    // Entry counts only, never keys' values
    struct NsCount
    {
        char name[NVS_NS_NAME_MAX_SIZE];
        uint16_t count;
    };
    static NsCount ns[12];
    size_t n_ns = 0;
    memset(ns, 0, sizeof(ns));

    nvs_iterator_t it = nullptr;
    err = nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY, &it);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        if (nvs_entry_info(it, &info) == ESP_OK) {
            size_t i = 0;
            while (i < n_ns && strcmp(ns[i].name, info.namespace_name) != 0) {
                i++;
            }
            if (i == n_ns && n_ns < sizeof(ns) / sizeof(ns[0])) {
                strlcpy(ns[n_ns].name, info.namespace_name, sizeof(ns[n_ns].name));
                n_ns++;
            }
            if (i < n_ns) {
                ns[i].count++;
            }
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    char line[192];
    size_t pos = 0;
    line[0] = '\0';
    for (size_t i = 0; i < n_ns && pos < sizeof(line); i++) {
        pos += snprintf(&line[pos], sizeof(line) - pos, " %s:%u", ns[i].name, ns[i].count);
    }
    ESP_LOGI(TAG, "nvs  entries per namespace:%s", line);
}

// ---------------------------------------------------------------------------------------------------------------------
// Task

static void dump(bool full, bool scan_nvs)
{
    // Strictly one lock after the other: CHIP lock taken and released, then the OpenThread lock
    if (full) {
        collect_chip(s_chip);
    }
#if CONFIG_OPENTHREAD_ENABLED
    collect_ot(s_ot);
#else
    collect_wifi(s_wifi);
#endif

    ESP_LOGI(TAG, "===== identity diagnostics =====");
    if (full) {
        log_hw(s_chip);
        log_chip(s_chip);
    }
#if CONFIG_OPENTHREAD_ENABLED
    log_ot(s_ot);
#else
    log_wifi(s_wifi);
#endif
    if (full) {
        log_nvs(scan_nvs);
    }
    ESP_LOGI(TAG, "================================");
}

static void identity_diag_task(void *)
{
    // Seconds to ticks directly: pdMS_TO_TICKS() multiplies in 32 bits and wraps above ~42950 s
    const TickType_t period = (TickType_t)s_period_s * (TickType_t)configTICK_RATE_HZ;
    uint32_t blocks = 0;
    TickType_t last_event_dump = 0;
    dump(true, true);
    blocks++;
    // Absolute deadline: event-triggered blocks must not push the periodic full block back forever
    TickType_t next_full = xTaskGetTickCount() + period;

    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        const TickType_t remaining = (int32_t)(next_full - now) > 0 ? next_full - now : 0;
        uint32_t bits = 0;
        if (remaining > 0 && xTaskNotifyWait(0, UINT32_MAX, &bits, remaining) == pdTRUE) {
#if !CONFIG_OPENTHREAD_ENABLED
            // Wi-Fi: always wait the full gap. When the connection event arrives the IPv6 link-local address is
            // still tentative (duplicate address detection) and the address events are still to come.
            last_event_dump = xTaskGetTickCount();
#endif
            // Coalesce bursts (role change + address change + network data change ...)
            const TickType_t since = xTaskGetTickCount() - last_event_dump;
            if (since < kMinEventGap) {
                vTaskDelay(kMinEventGap - since);
                uint32_t more = 0;
                xTaskNotifyWait(0, UINT32_MAX, &more, 0);
                bits |= more;
            }
            last_event_dump = xTaskGetTickCount();
            dump((bits & kNotifyFull) != 0, false);
            continue;
        }
        dump(true, (blocks % kNvsScanEvery) == 0);
        blocks++;
        next_full = xTaskGetTickCount() + period;
    }
}

esp_err_t identity_diag_start(uint32_t period_s)
{
    if (period_s == 0 || s_task) {
        return ESP_OK;
    }
    s_period_s = period_s;
    if (xTaskCreate(identity_diag_task, "id_diag", kTaskStack, nullptr, kTaskPriority, &s_task) != pdPASS) {
        s_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void request(uint32_t bits)
{
    if (s_task) {
        xTaskNotify(s_task, bits, eSetBits);
    }
}

void identity_diag_request_dump(void)
{
    request(kNotifyFull);
}

// CHIP task, CHIP lock held: only event data and CHIP-owned data, never the OpenThread lock
void identity_diag_on_event(const ChipDeviceEvent *event)
{
    if (!s_task || !event) {
        return;
    }
    switch (event->Type) {
#if CONFIG_OPENTHREAD_ENABLED
    case DeviceEventType::kThreadStateChange:
        if (event->ThreadStateChange.RoleChanged) {
            ESP_LOGI(TAG, "EV Thread role changed");
            request(kNotifyNet);
        }
        break;

    case DeviceEventType::kThreadConnectivityChange:
        ESP_LOGI(TAG, "EV Thread connectivity %s",
                 event->ThreadConnectivityChange.Result == kConnectivity_Established ? "established"
                 : event->ThreadConnectivityChange.Result == kConnectivity_Lost      ? "LOST"
                                                                                     : "unchanged");
        request(kNotifyNet);
        break;
#else
    case DeviceEventType::kWiFiConnectivityChange:
        ESP_LOGI(TAG, "EV Wi-Fi connectivity %s",
                 event->WiFiConnectivityChange.Result == kConnectivity_Established ? "established"
                 : event->WiFiConnectivityChange.Result == kConnectivity_Lost      ? "LOST"
                                                                                   : "unchanged");
        request(kNotifyNet);
        break;

    case DeviceEventType::kInterfaceIpAddressChanged:
        // An IPv4 or IPv6 address assigned or lost: Wi-Fi block with the new addresses
        request(kNotifyNet);
        break;
#endif

    case DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "EV commissioning complete: node " ChipLogFormatX64 " fabric %u",
                 ChipLogValueX64(event->CommissioningComplete.nodeId), event->CommissioningComplete.fabricIndex);
        request(kNotifyFull);
        break;

    case DeviceEventType::kSecureSessionEstablished: {
        const auto &s = event->SecureSessionEstablished;
        const bool is_case = s.SecureSessionType == to_underlying(Transport::SecureSession::Type::kCASE);
        if (is_case) {
            s_case_established++;
        }
        ESP_LOGI(TAG, "EV %s session established: peer " ChipLogFormatX64 " fabric %u", is_case ? "CASE" : "PASE",
                 ChipLogValueX64(s.PeerNodeId), s.FabricIndex);
        break;
    }

    case DeviceEventType::kFabricRemoved:
    case DeviceEventType::kFabricCommitted:
    case DeviceEventType::kFabricUpdated:
        // esp-matter posts these without the fabric index
        ESP_LOGI(TAG, "EV fabric %s, fabrics now %u",
                 event->Type == DeviceEventType::kFabricRemoved     ? "removed"
                 : event->Type == DeviceEventType::kFabricCommitted ? "added"
                                                                    : "updated",
                 Server::GetInstance().GetFabricTable().FabricCount());
        request(kNotifyFull);
        break;

    case DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "EV fail-safe expired (fabric %u)", event->FailSafeTimerExpired.fabricIndex);
        break;

    case DeviceEventType::kServerReady:
#if CONFIG_IDENTITY_DIAG_SUBSCRIPTION_LOG
        install_subscription_logger();
#endif
        request(kNotifyFull);
        break;

    default:
        break;
    }
}
