/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Basic Information values that controllers show for the accessory. Everything
// not overridden here (IDs, numeric hardware version...) keeps the defaults of
// the generic provider. The firmware version comes from
// PROJECT_VER / PROJECT_VER_NUMBER in the top-level CMakeLists.txt.

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_matter_providers.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.h>
#include <stdio.h>
#include <string.h>

#include "device_info.h"

using namespace chip;
using namespace chip::DeviceLayer;

static const char *TAG = "device_info";

namespace {

class DeviceInstanceInfo : public Internal::GenericDeviceInstanceInfoProvider<Internal::ESP32Config>
{
public:
    DeviceInstanceInfo() :
        Internal::GenericDeviceInstanceInfoProvider<Internal::ESP32Config>(ConfigurationManagerImpl::GetDefaultInstance())
    {}

    CHIP_ERROR GetVendorName(char * buf, size_t bufSize) override { return CopyString(buf, bufSize, CONFIG_DEVICE_VENDOR_NAME); }
    CHIP_ERROR GetProductName(char * buf, size_t bufSize) override { return CopyString(buf, bufSize, CONFIG_DEVICE_PRODUCT_NAME); }
    CHIP_ERROR GetProductLabel(char * buf, size_t bufSize) override { return CopyString(buf, bufSize, CONFIG_DEVICE_DISPLAY_NAME); }
    CHIP_ERROR GetHardwareVersionString(char * buf, size_t bufSize) override
    {
        return CopyString(buf, bufSize, CONFIG_DEVICE_HARDWARE_VERSION_STRING);
    }
    CHIP_ERROR GetSerialNumber(char * buf, size_t bufSize) override
    {
        char serial[ConfigurationManager::kMaxSerialNumberLength + 1];
        device_info_get_serial(serial, sizeof(serial));
        return CopyString(buf, bufSize, serial);
    }

private:
    static CHIP_ERROR CopyString(char * buf, size_t bufSize, const char * value)
    {
        VerifyOrReturnError(bufSize > strlen(value), CHIP_ERROR_BUFFER_TOO_SMALL);
        strcpy(buf, value);
        return CHIP_NO_ERROR;
    }
};

} // namespace

// Basic Information limits (VendorName, ProductName, NodeLabel: 32 characters).
static_assert(sizeof(CONFIG_DEVICE_VENDOR_NAME) - 1 <= 32, "Manufacturer must be at most 32 characters");
static_assert(sizeof(CONFIG_DEVICE_PRODUCT_NAME) - 1 <= 32, "Model must be at most 32 characters");
static_assert(sizeof(CONFIG_DEVICE_DISPLAY_NAME) - 1 <= 32, "Device name must be at most 32 characters");
static_assert(sizeof(CONFIG_DEVICE_HARDWARE_VERSION_STRING) - 1 <= ConfigurationManager::kMaxHardwareVersionStringLength,
              "Hardware must be at most 64 characters");

void device_info_get_serial(char *buf, size_t size)
{
    // Wi-Fi MAC, e.g. "8ED0B2A8594B": unique per chip.
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read the Wi-Fi MAC, the serial number is not unique");
    }
    snprintf(buf, size, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void device_info_init(void)
{
    static DeviceInstanceInfo s_device_instance_info;
    esp_matter::set_custom_device_instance_info_provider(&s_device_instance_info);

    char serial[ConfigurationManager::kMaxSerialNumberLength + 1];
    device_info_get_serial(serial, sizeof(serial));
    ESP_LOGI(TAG, "Serial number: %s", serial);
}

void device_info_set_name(esp_matter::node::config_t *node_config)
{
    strlcpy(node_config->root_node.basic_information.node_label, CONFIG_DEVICE_DISPLAY_NAME,
            sizeof(node_config->root_node.basic_information.node_label));
}

void device_info_add_attributes(esp_matter::node_t *node)
{
    using namespace esp_matter;
    endpoint_t *root = endpoint::get(node, 0);
    cluster_t *basic_info = cluster::get(root, chip::app::Clusters::BasicInformation::Id);
    if (basic_info) {
        // The values are read from GetProductLabel() / GetSerialNumber() above.
        cluster::basic_information::attribute::create_product_label(basic_info, NULL, 0);
        cluster::basic_information::attribute::create_serial_number(basic_info, NULL, 0);
    }
}
