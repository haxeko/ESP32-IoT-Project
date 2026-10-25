/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Two-chip version, Matter node (CONFIG_PRESENCE_ROLE_MATTER_UART, ESP32-H2): Matter over Thread with the five
// Occupancy Sensor endpoints ("Presence 1".."Presence 5"), whose states come from the ESP32-C3 scanner over the UART
// link (presence_link.h). The H2 radio serves Thread only (BLE just for commissioning): no BLE scanning, no web page
// and no CONFIG mode here; the beacons are assigned on the scanner's page.

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "sdkconfig.h"

#include "matter_device.h"
#include "presence_link.h"

static const char *TAG = "app_main";

static void check(esp_err_t err, const char *what)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err));
    }
}

// Link task: content of the STATUS frame sent to the scanner every second
static void link_status(plink_status_t *st)
{
    bool started = false;
    bool commissioned = false;
    bool network_up = false;
    uint8_t fabrics = 0;
    matter_device_get_status(&started, &commissioned, &network_up, &fabrics);
    st->flags = (started ? PLINK_STATUS_MATTER_STARTED : 0) | (commissioned ? PLINK_STATUS_COMMISSIONED : 0) |
                (network_up ? PLINK_STATUS_NETWORK_UP : 0);
    st->fabrics = fabrics;
}

static void diag_task(void *)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_PRESENCE_DIAG_PERIOD_SEC * 1000));
        ESP_LOGI("diag", "Uptime %lld s, heap free %u, min %u, largest %u", esp_timer_get_time() / 1000000,
                 static_cast<unsigned>(esp_get_free_heap_size()), static_cast<unsigned>(esp_get_minimum_free_heap_size()),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        presence_link_log_diagnostics();
        matter_device_log_diagnostics();
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "%s %s (two-chip version, Matter node), firmware %s, ESP-IDF %s", CONFIG_PRESENCE_VENDOR_NAME,
             CONFIG_PRESENCE_PRODUCT_NAME, esp_app_get_description()->version, esp_get_idf_version());

    // "nvs" holds the Matter fabrics and keys, the Thread dataset and the PHY calibration: never erased on error (a
    // pairing may still be recoverable); Matter is not started on it then.
    const esp_err_t nvs_err = nvs_flash_init();

    // The link first: states received before Matter is up are applied as soon as it is (retried every 100 ms)
    check(presence_link_start_matter(matter_device_set_occupancy, link_status), "UART link");
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "Matter NOT started: partition 'nvs' (fabrics, Thread) cannot be opened (%s). It is not erased "
                      "automatically.",
                 esp_err_to_name(nvs_err));
    } else {
        check(matter_device_start(), "Matter");
    }
    if (CONFIG_PRESENCE_DIAG_PERIOD_SEC > 0) {
        check(xTaskCreate(diag_task, "diag", 3072, nullptr, 2, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM,
              "Diagnostics");
    }
}
