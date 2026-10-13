/*
 * Custom DeviceInstanceInfoProvider: same as the default one (ESP32Config storage for the rotating device ID,
 * hardware version, ...), with the vendor/product names from Kconfig and the serial number from the MAC address.
 */
#include "device_info.h"

#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_matter_providers.h>

#include <platform/ESP32/ConfigurationManagerImpl.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.h>

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;

static const char *TAG = "device_info";

namespace {

CHIP_ERROR CopyString(const char *value, char *buf, size_t buf_size)
{
    const size_t len = strlen(value);
    VerifyOrReturnError(len < buf_size, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf, value, len + 1);
    return CHIP_NO_ERROR;
}

class PlugDeviceInstanceInfoProvider : public Internal::GenericDeviceInstanceInfoProvider<Internal::ESP32Config> {
public:
    PlugDeviceInstanceInfoProvider() :
        Internal::GenericDeviceInstanceInfoProvider<Internal::ESP32Config>(ConfigurationManagerImpl::GetDefaultInstance())
    {}

    void Init()
    {
        uint8_t mac[6] = {};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(mSerial, sizeof(mSerial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    const char *Serial() const { return mSerial; }

    CHIP_ERROR GetVendorName(char *buf, size_t bufSize) override
    {
        return CopyString(CONFIG_DEVICE_INFO_VENDOR_NAME, buf, bufSize);
    }
    CHIP_ERROR GetProductName(char *buf, size_t bufSize) override
    {
        return CopyString(CONFIG_DEVICE_INFO_PRODUCT_NAME, buf, bufSize);
    }
    CHIP_ERROR GetProductLabel(char *buf, size_t bufSize) override
    {
        return CopyString(CONFIG_DEVICE_INFO_PRODUCT_NAME, buf, bufSize);
    }
    CHIP_ERROR GetSerialNumber(char *buf, size_t bufSize) override { return CopyString(mSerial, buf, bufSize); }

private:
    char mSerial[13] = {};
};

PlugDeviceInstanceInfoProvider s_provider;

} // namespace

esp_err_t device_info_init(esp_matter::node_t *node)
{
    s_provider.Init();
    esp_matter::set_custom_device_instance_info_provider(&s_provider);

    /* SerialNumber and ProductLabel are optional in Basic Information: add them to the root endpoint so the
     * cluster server exposes them. Their values come from the provider above. */
    esp_matter::endpoint_t *root = esp_matter::endpoint::get(node, 0);
    esp_matter::cluster_t *basic = esp_matter::cluster::get(root, BasicInformation::Id);
    if (!basic) {
        return ESP_FAIL;
    }
    namespace attr = esp_matter::cluster::basic_information::attribute;
    char empty[1] = "";
    if (!esp_matter::attribute::get(basic, BasicInformation::Attributes::SerialNumber::Id)) {
        attr::create_serial_number(basic, empty, sizeof(empty));
    }
    if (!esp_matter::attribute::get(basic, BasicInformation::Attributes::ProductLabel::Id)) {
        attr::create_product_label(basic, empty, sizeof(empty));
    }

    ESP_LOGI(TAG, "%s %s, serial %s", CONFIG_DEVICE_INFO_VENDOR_NAME, CONFIG_DEVICE_INFO_PRODUCT_NAME,
             s_provider.Serial());
    return ESP_OK;
}
