// NVS kept in g_sim.nvs (blobs only, what the application stores).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include "esp_err.h"
#include "sim.h"
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
// Handle = index + 1 in this list of open namespaces
static std::vector<std::string> s_nvs_open;
static inline esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    if (mode == NVS_READONLY && g_sim.nvs.find(ns) == g_sim.nvs.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    s_nvs_open.push_back(ns);
    *handle = (nvs_handle_t)s_nvs_open.size();
    return ESP_OK;
}
static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    auto &ns = g_sim.nvs[s_nvs_open[h - 1]];
    auto it = ns.find(key);
    if (it == ns.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (*len < it->second.size()) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out, it->second.data(), it->second.size());
    *len = it->second.size();
    return ESP_OK;
}
static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    g_sim.nvs[s_nvs_open[h - 1]][key] = std::vector<uint8_t>(p, p + len);
    return ESP_OK;
}
static inline esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    auto &ns = g_sim.nvs[s_nvs_open[h - 1]];
    return ns.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
static inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
static inline void nvs_close(nvs_handle_t) {}
