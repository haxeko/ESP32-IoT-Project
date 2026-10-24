/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "wifi_config.h"

#include <stdio.h>
#include <string.h>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include "sdkconfig.h"

static const char *TAG = "wifi_config";

void wifi_config_ssid(char ssid[WIFI_CONFIG_SSID_SIZE])
{
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, WIFI_CONFIG_SSID_SIZE, "%s%02X%02X", CONFIG_PRESENCE_AP_SSID_PREFIX, mac[4], mac[5]);
}

static void wifi_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base != WIFI_EVENT) {
        return;
    }
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        const auto *e = static_cast<wifi_event_ap_staconnected_t *>(data);
        ESP_LOGI(TAG, "Station " MACSTR " connected", MAC2STR(e->mac));
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const auto *e = static_cast<wifi_event_ap_stadisconnected_t *>(data);
        ESP_LOGI(TAG, "Station " MACSTR " left (reason %d)", MAC2STR(e->mac), e->reason);
    }
}

esp_err_t wifi_config_start(const char *password)
{
    const size_t pw_len = strlen(password);
    if (pw_len < 8 || pw_len > 63) {
        ESP_LOGE(TAG, "SoftAP password must be 8-63 characters: SoftAP not started");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (esp_netif_create_default_wifi_ap() == nullptr) {
        return ESP_FAIL;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = 0;
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        return err;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr, nullptr);

    char ssid[WIFI_CONFIG_SSID_SIZE];
    wifi_config_ssid(ssid);
    wifi_config_t cfg = {};
    memcpy(cfg.ap.ssid, ssid, strlen(ssid));
    cfg.ap.ssid_len = static_cast<uint8_t>(strlen(ssid));
    memcpy(cfg.ap.password, password, pw_len);
    cfg.ap.channel = CONFIG_PRESENCE_AP_CHANNEL;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.max_connection = CONFIG_PRESENCE_AP_MAX_CONNECTIONS;
    cfg.ap.pmf_cfg.required = false; // capable, not required: older phones still connect

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SoftAP \"%s\" on channel %d (WPA2), page at http://192.168.4.1", ssid,
                 CONFIG_PRESENCE_AP_CHANNEL);
    }
    return err;
}

int wifi_config_station_count(void)
{
    wifi_sta_list_t list = {};
    return esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}
