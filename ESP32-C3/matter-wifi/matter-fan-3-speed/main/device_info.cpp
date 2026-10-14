// Basic Information values that controllers show for the accessory (Manufacturer, Model, name, hardware, serial).
// Everything not overridden here (vendor/product IDs, hardware version number...) keeps the defaults of the
// generic provider. The firmware version comes from PROJECT_VER / PROJECT_VER_NUMBER in CMakeLists.txt.

#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_matter_providers.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.h>

#include <device_info.h>

using namespace chip::DeviceLayer;

static const char *TAG = "device_info";

static_assert(sizeof(CONFIG_DEVICE_VENDOR_NAME) - 1 <= ConfigurationManager::kMaxVendorNameLength,
              "CONFIG_DEVICE_VENDOR_NAME exceeds the 32-character Basic Information VendorName limit");
static_assert(sizeof(CONFIG_DEVICE_PRODUCT_NAME) - 1 <= ConfigurationManager::kMaxProductNameLength,
              "CONFIG_DEVICE_PRODUCT_NAME exceeds the 32-character Basic Information ProductName limit");
static_assert(sizeof(CONFIG_DEVICE_DISPLAY_NAME) - 1 <= 32,
              "CONFIG_DEVICE_DISPLAY_NAME exceeds the 32-character NodeLabel / ProductLabel limit");
static_assert(sizeof(CONFIG_DEVICE_HARDWARE_VERSION_STRING) - 1 <= ConfigurationManager::kMaxHardwareVersionStringLength,
              "CONFIG_DEVICE_HARDWARE_VERSION_STRING exceeds the 64-character HardwareVersionString limit");

/* Wi-Fi MAC, e.g. "8ED0B2A8594B": unique per chip, unchanged by OTA and erase-flash */
static char s_serial_number[ConfigurationManager::kMaxSerialNumberLength + 1];

static CHIP_ERROR copy_string(const char *src, char *buf, size_t buf_size)
{
    const size_t len = strlen(src);
    VerifyOrReturnError(len < buf_size, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf, src, len + 1);
    return CHIP_NO_ERROR;
}

namespace {

class DeviceInstanceInfo : public Internal::GenericDeviceInstanceInfoProvider<Internal::ESP32Config> {
public:
    DeviceInstanceInfo()
        : Internal::GenericDeviceInstanceInfoProvider<Internal::ESP32Config>(
              ConfigurationManagerImpl::GetDefaultInstance())
    {
    }

    CHIP_ERROR GetVendorName(char *buf, size_t bufSize) override
    {
        return copy_string(CONFIG_DEVICE_VENDOR_NAME, buf, bufSize);
    }
    CHIP_ERROR GetProductName(char *buf, size_t bufSize) override
    {
        return copy_string(CONFIG_DEVICE_PRODUCT_NAME, buf, bufSize);
    }
    CHIP_ERROR GetProductLabel(char *buf, size_t bufSize) override
    {
        return copy_string(CONFIG_DEVICE_DISPLAY_NAME, buf, bufSize);
    }
    CHIP_ERROR GetHardwareVersionString(char *buf, size_t bufSize) override
    {
        return copy_string(CONFIG_DEVICE_HARDWARE_VERSION_STRING, buf, bufSize);
    }
    CHIP_ERROR GetSerialNumber(char *buf, size_t bufSize) override
    {
        return copy_string(s_serial_number, buf, bufSize);
    }
};

DeviceInstanceInfo s_provider;

} // namespace

void device_info_init(void)
{
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(s_serial_number, sizeof(s_serial_number), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5]);
    } else {
        /* Cosmetic field: never stop the device from booting because of it */
        snprintf(s_serial_number, sizeof(s_serial_number), "UNKNOWN");
    }
    ESP_LOGI(TAG, "Manufacturer: %s, Model: %s, Hardware: %s, Serial number: %s", CONFIG_DEVICE_VENDOR_NAME,
             CONFIG_DEVICE_PRODUCT_NAME, CONFIG_DEVICE_HARDWARE_VERSION_STRING, s_serial_number);
    esp_matter::set_custom_device_instance_info_provider(&s_provider);
}

void device_info_set_name(esp_matter::node::config_t *node_config)
{
    strlcpy(node_config->root_node.basic_information.node_label, CONFIG_DEVICE_DISPLAY_NAME,
            sizeof(node_config->root_node.basic_information.node_label));
}

void device_info_add_attributes(esp_matter::node_t *node)
{
    using namespace esp_matter;
    /* ProductLabel and SerialNumber are optional in Basic Information and root_node does not create them: without
     * the attributes the provider values are never visible. Must be added before esp_matter::start(), which reads
     * the optional attribute set once. The values are read from the provider above. */
    cluster_t *basic_info = cluster::get(endpoint::get(node, chip::kRootEndpointId),
                                         chip::app::Clusters::BasicInformation::Id);
    if (basic_info == nullptr ||
        cluster::basic_information::attribute::create_product_label(basic_info, nullptr, 0) == nullptr ||
        cluster::basic_information::attribute::create_serial_number(basic_info, nullptr, 0) == nullptr) {
        ESP_LOGE(TAG, "Failed to add the Basic Information ProductLabel / SerialNumber attributes");
    }
}
