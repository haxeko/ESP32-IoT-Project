#pragma once
#include <stdint.h>
#include <string>
#include "esp_err.h"
#include "sim.h"
#define ESP_ERR_NVS_NOT_FOUND   0x1102
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
// Values are stored in g_sim.nvs as "<namespace>/<key>"; one handle open at a time.
static std::string s_sim_nvs_namespace;
static inline esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    const std::string prefix = std::string(name) + "/";
    bool exists = false;
    for (const auto &kv : g_sim.nvs) {
        exists |= kv.first.compare(0, prefix.size(), prefix) == 0;
    }
    if (mode == NVS_READONLY && !exists) {
        return ESP_ERR_NVS_NOT_FOUND;   // as ESP-IDF: a read-only open needs an existing namespace
    }
    s_sim_nvs_namespace = name;
    *handle = 1;
    return ESP_OK;
}
static inline esp_err_t nvs_get_u8(nvs_handle_t, const char *key, uint8_t *value)
{
    auto it = g_sim.nvs.find(s_sim_nvs_namespace + "/" + key);
    if (it == g_sim.nvs.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *value = it->second;
    return ESP_OK;
}
static inline esp_err_t nvs_set_u8(nvs_handle_t, const char *key, uint8_t value)
{
    g_sim.nvs[s_sim_nvs_namespace + "/" + key] = value;
    g_sim.nvs_writes++;
    return ESP_OK;
}
static inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
static inline void nvs_close(nvs_handle_t) {}
