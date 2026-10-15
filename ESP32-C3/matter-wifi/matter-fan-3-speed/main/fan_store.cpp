#include <fan_store.h>

#include <esp_log.h>
#include <nvs.h>

#include <fan_record.h>

static const char *TAG = "fan_store";

static constexpr const char *k_nvs_namespace = "fan";
static constexpr const char *k_nvs_key_state = "state";

bool fan_store_load(fan_state_t *state)
{
    nvs_handle_t handle;
    if (nvs_open(k_nvs_namespace, NVS_READONLY, &handle) != ESP_OK) {
        return false; /* nothing stored yet */
    }
    /* Read the whole record, whatever its length: a newer firmware may have stored a longer one */
    uint8_t buf[FAN_RECORD_MAX_SIZE];
    size_t len = sizeof(buf);
    const esp_err_t err = nvs_get_blob(handle, k_nvs_key_state, buf, &len);
    nvs_close(handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Fan state not loaded: %s", esp_err_to_name(err));
        }
        return false;
    }
    if (!fan_record_decode(buf, len, state)) {
        ESP_LOGW(TAG, "Unknown fan state record (version %u, %u bytes): ignored", len ? buf[0] : 0, (unsigned)len);
        return false;
    }
    return true;
}

esp_err_t fan_store_write(const fan_state_t *state)
{
    uint8_t buf[FAN_RECORD_SIZE];
    fan_record_encode(state, buf);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(k_nvs_namespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, k_nvs_key_state, buf, sizeof(buf));
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
