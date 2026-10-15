#include <wifi_power.h>

#include <sdkconfig.h>

#if CONFIG_WIFI_NO_POWER_SAVE
#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>
#if CONFIG_BT_ENABLED
#include <esp_bt.h>
#endif

static const char *TAG = "wifi_power";
#endif

void wifi_power_update()
{
#if CONFIG_WIFI_NO_POWER_SAVE
#if CONFIG_BT_ENABLED
    /* Bluetooth still on (commissioning, or its shutdown not finished): keep modem sleep */
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        return;
    }
#endif
    wifi_ps_type_t type;
    if (esp_wifi_get_ps(&type) != ESP_OK || type == WIFI_PS_NONE) {
        return; /* Wi-Fi not initialized yet, or power save already off */
    }
    const esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi power save off (mains powered): the radio stays on");
    } else {
        ESP_LOGW(TAG, "Failed to turn Wi-Fi power save off: %s", esp_err_to_name(err));
    }
#endif
}
