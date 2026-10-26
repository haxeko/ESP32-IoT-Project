/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "matter_bridge.h"

#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <nvs.h>

#include "crc32.h"
#include "presence_config.h"

static_assert(PLINK_SLOT_COUNT == PRESENCE_SLOT_COUNT, "one bridged device per presence slot");

static const char *TAG = "bridge";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static constexpr const char *kNamespace = "pbridge";
static constexpr const char *kKey = "table";
static constexpr uint32_t kMagic = 0x50425231; // "PBR1"

namespace {

struct Entry {
    uint16_t endpoint; // 0: no device
    uint8_t mac[PLINK_MAC_LEN];
};

// NVS blob
struct Table {
    uint32_t magic;
    Entry entries[PLINK_SLOT_COUNT];
    uint32_t crc; // of magic and entries
};

} // namespace

static node_t *s_node;
static uint16_t s_aggregator = chip::kInvalidEndpointId;
static uint32_t s_occupancy_feature;
static Entry s_entries[PLINK_SLOT_COUNT];
static bool s_ready;
static bool s_reachable = true;

static uint32_t table_crc(const Table &t)
{
    return crc32_ieee(reinterpret_cast<const uint8_t *>(&t), offsetof(Table, crc));
}

static void save_table()
{
    Table t = {};
    t.magic = kMagic;
    memcpy(t.entries, s_entries, sizeof(t.entries));
    t.crc = table_crc(t);
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, kKey, &t, sizeof(t));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Saving the bridged device table failed: %s (the devices come back from the scanner's list)",
                 esp_err_to_name(err));
    }
}

static bool load_table(Table *t)
{
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) {
        return false; // nothing saved yet
    }
    size_t len = sizeof(*t);
    const esp_err_t err = nvs_get_blob(h, kKey, t, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(*t) || t->magic != kMagic || t->crc != table_crc(*t)) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Bridged device table unreadable (%s): rebuilt from the scanner's list", esp_err_to_name(err));
        }
        return false;
    }
    return true;
}

// The endpoint of a bridged device: Bridged Node + Occupancy Sensor, child of the Aggregator, named after the MAC.
// resume_id: the endpoint ID it had before the restart, or kInvalidEndpointId for a new one.
static endpoint_t *build_device(int slot, const uint8_t mac[PLINK_MAC_LEN], uint16_t resume_id)
{
    bridged_node::config_t config;
    config.bridged_device_basic_information.reachable = s_reachable;
    // Stable and unique per device, as the specification asks: the slot and the beacon
    snprintf(config.bridged_device_basic_information.unique_id, sizeof(config.bridged_device_basic_information.unique_id),
             "presence-%d-%02X%02X%02X%02X%02X%02X", slot + 1, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const uint8_t flags = ENDPOINT_FLAG_DESTROYABLE | ENDPOINT_FLAG_BRIDGE;
    endpoint_t *ep = resume_id == chip::kInvalidEndpointId
        ? bridged_node::create(s_node, &config, flags, nullptr)
        : bridged_node::resume(s_node, &config, flags, resume_id, nullptr);
    if (ep == nullptr) {
        // bridged_node::resume() can fail after the endpoint was added to the node (cluster allocation): drop it
        if (resume_id != chip::kInvalidEndpointId) {
            endpoint_t *left = endpoint::get(s_node, resume_id);
            if (left != nullptr) {
                endpoint::destroy(s_node, left);
            }
        }
        return nullptr;
    }
    char name[PLINK_MAC_STR_SIZE];
    plink_mac_string(mac, name);
    cluster_t *info = cluster::get(ep, BridgedDeviceBasicInformation::Id);
    occupancy_sensor::config_t sensor;
    sensor.occupancy_sensing.feature_flags = s_occupancy_feature;
    if (info == nullptr ||
        cluster::bridged_device_basic_information::attribute::create_node_label(info, name, strlen(name)) == nullptr ||
        occupancy_sensor::add(ep, &sensor) != ESP_OK ||
        set_parent_endpoint(ep, endpoint::get(s_node, s_aggregator)) != ESP_OK) {
        endpoint::destroy(s_node, ep);
        return nullptr;
    }
    endpoint::enable(ep); // after esp_matter::start(): runs the cluster init callbacks, reports the new PartsList
    return ep;
}

static void remove_device(int slot)
{
    Entry &e = s_entries[slot];
    endpoint_t *ep = endpoint::get(s_node, e.endpoint);
    char name[PLINK_MAC_STR_SIZE];
    plink_mac_string(e.mac, name);
    if (ep != nullptr && endpoint::destroy(s_node, ep) != ESP_OK) {
        ESP_LOGE(TAG, "Presence %d: endpoint %u could not be removed", slot + 1, e.endpoint);
    } else {
        ESP_LOGI(TAG, "Presence %d: device %s removed (endpoint %u)", slot + 1, name, e.endpoint);
    }
    e = {};
}

static bool add_device(int slot, const uint8_t mac[PLINK_MAC_LEN])
{
    char name[PLINK_MAC_STR_SIZE];
    plink_mac_string(mac, name);
    endpoint_t *ep = build_device(slot, mac, chip::kInvalidEndpointId);
    if (ep == nullptr) {
        ESP_LOGE(TAG, "Presence %d: device %s could not be added (CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT?)",
                 slot + 1, name);
        return false;
    }
    s_entries[slot].endpoint = endpoint::get_id(ep);
    memcpy(s_entries[slot].mac, mac, PLINK_MAC_LEN);
    ESP_LOGI(TAG, "Presence %d: device %s added on endpoint %u", slot + 1, name, s_entries[slot].endpoint);
    return true;
}

esp_err_t matter_bridge_create_aggregator(node_t *node, uint32_t occupancy_feature)
{
    aggregator::config_t config;
    endpoint_t *ep = aggregator::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
    if (ep == nullptr) {
        ESP_LOGE(TAG, "Cannot create the Aggregator endpoint");
        return ESP_FAIL;
    }
    s_node = node;
    s_aggregator = endpoint::get_id(ep);
    s_occupancy_feature = occupancy_feature;
    ESP_LOGI(TAG, "Matter bridge: Aggregator on endpoint %u", s_aggregator);
    return ESP_OK;
}

void matter_bridge_restore(void)
{
    Table t;
    if (load_table(&t)) {
        bool changed = false;
        for (int i = 0; i < PLINK_SLOT_COUNT; i++) {
            const Entry &e = t.entries[i];
            if (e.endpoint == 0) {
                continue;
            }
            char name[PLINK_MAC_STR_SIZE];
            plink_mac_string(e.mac, name);
            if (build_device(i, e.mac, e.endpoint) != nullptr) {
                s_entries[i] = e;
                ESP_LOGI(TAG, "Presence %d: device %s on endpoint %u", i + 1, name, e.endpoint);
            } else {
                // e.g. the Matter data was erased: the scanner's list adds the device again on a new endpoint
                ESP_LOGW(TAG, "Presence %d: device %s on endpoint %u cannot be restored", i + 1, name, e.endpoint);
                changed = true;
            }
        }
        if (changed) {
            save_table();
        }
    }
    s_ready = true;
}

bool matter_bridge_sync(const plink_slots_t *want, uint8_t *changed)
{
    *changed = 0;
    if (!s_ready) {
        return false;
    }
    bool dirty = false;
    bool complete = true;
    for (int i = 0; i < PLINK_SLOT_COUNT; i++) {
        const bool active = (want->active_mask >> i) & 1;
        switch (plink_slot_action(s_entries[i].endpoint != 0, s_entries[i].mac, active, want->mac[i])) {
        case PLINK_SLOT_KEEP:
            break;
        case PLINK_SLOT_REMOVE:
            remove_device(i);
            dirty = true;
            break;
        case PLINK_SLOT_REPLACE:
            remove_device(i);
            dirty = true;
            if (add_device(i, want->mac[i])) {
                *changed |= static_cast<uint8_t>(1u << i);
            } else {
                complete = false;
            }
            break;
        case PLINK_SLOT_ADD:
            if (add_device(i, want->mac[i])) {
                *changed |= static_cast<uint8_t>(1u << i);
                dirty = true;
            } else {
                complete = false;
            }
            break;
        }
    }
    if (dirty) {
        save_table();
    }
    return complete;
}

bool matter_bridge_ready(void)
{
    return s_ready;
}

uint16_t matter_bridge_endpoint(int slot)
{
    return slot >= 0 && slot < PLINK_SLOT_COUNT ? s_entries[slot].endpoint : 0;
}

void matter_bridge_get(uint8_t *bridged_mask, uint16_t endpoint[PLINK_SLOT_COUNT])
{
    *bridged_mask = 0;
    for (int i = 0; i < PLINK_SLOT_COUNT; i++) {
        endpoint[i] = s_entries[i].endpoint;
        if (endpoint[i] != 0) {
            *bridged_mask |= static_cast<uint8_t>(1u << i);
        }
    }
}

void matter_bridge_set_reachable(bool reachable)
{
    s_reachable = reachable;
    for (int i = 0; i < PLINK_SLOT_COUNT; i++) {
        if (s_entries[i].endpoint == 0) {
            continue;
        }
        esp_matter_attr_val_t val = esp_matter_bool(reachable);
        attribute::update(s_entries[i].endpoint, BridgedDeviceBasicInformation::Id,
                          BridgedDeviceBasicInformation::Attributes::Reachable::Id, &val);
    }
}

void matter_bridge_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    memset(s_entries, 0, sizeof(s_entries));
}
