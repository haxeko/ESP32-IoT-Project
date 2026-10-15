#include <fan_store.h>

#include <esp_log.h>
#include <nvs.h>

static const char *TAG = "fan_store";

namespace {

/* Layout stored as is: keep it. Version 1 was the first 4 bytes (no natural byte). */
struct StoredState {
    uint8_t version;
    uint8_t speed;
    uint8_t last_speed;
    uint8_t swing;
    uint8_t natural;
};
static_assert(sizeof(StoredState) == 5, "NVS record layout changed");

constexpr uint8_t k_state_version = 2;
constexpr size_t k_state_v1_size = 4;
constexpr const char *k_nvs_namespace = "fan";
constexpr const char *k_nvs_key_state = "state";

} // namespace

bool fan_store_load(fan_state_t *state)
{
    nvs_handle_t handle;
    if (nvs_open(k_nvs_namespace, NVS_READONLY, &handle) != ESP_OK) {
        return false; /* nothing stored yet */
    }
    StoredState stored = {};
    size_t len = sizeof(stored);
    const esp_err_t err = nvs_get_blob(handle, k_nvs_key_state, &stored, &len);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }
    const bool v1 = stored.version == 1 && len == k_state_v1_size; /* before natural wind: off */
    if (!v1 && (stored.version != k_state_version || len != sizeof(stored))) {
        ESP_LOGW(TAG, "Unknown fan state record (version %u, %u bytes): ignored", stored.version, (unsigned)len);
        return false;
    }
    state->speed = stored.speed;
    state->last_speed = stored.last_speed;
    state->swing = stored.swing != 0;
    state->natural = !v1 && stored.natural != 0;
    fan_state_sanitize(state);
    return true;
}

esp_err_t fan_store_write(const fan_state_t *state)
{
    const StoredState stored = {k_state_version, state->speed, state->last_speed, (uint8_t)state->swing,
                                (uint8_t)state->natural};
    nvs_handle_t handle;
    esp_err_t err = nvs_open(k_nvs_namespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, k_nvs_key_state, &stored, sizeof(stored));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store the fan state: %s", esp_err_to_name(err));
    }
    return err;
}
