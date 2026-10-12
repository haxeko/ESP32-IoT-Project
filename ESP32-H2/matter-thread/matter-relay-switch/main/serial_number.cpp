#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <esp_mac.h>

#include <esp_matter_providers.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.h>

#include <serial_number.h>

using namespace chip::DeviceLayer;

static const char *TAG = "serial_number";

/* Matter Basic Information SerialNumber is at most 32 characters */
static char s_serial_number[33];

/* Same data as the default (test) provider, except the serial number */
class MacSerialNumberProvider : public Internal::GenericDeviceInstanceInfoProvider<Internal::ESP32Config> {
public:
    MacSerialNumberProvider() :
        Internal::GenericDeviceInstanceInfoProvider<Internal::ESP32Config>(ConfigurationManagerImpl::GetDefaultInstance())
    {}

    CHIP_ERROR GetSerialNumber(char *buf, size_t bufSize) override
    {
        size_t len = strlen(s_serial_number);
        VerifyOrReturnError(len < bufSize, CHIP_ERROR_BUFFER_TOO_SMALL);
        memcpy(buf, s_serial_number, len + 1);
        return CHIP_NO_ERROR;
    }
};

static MacSerialNumberProvider s_provider;

esp_err_t serial_number_provider_init()
{
    uint8_t mac[6];
    /* Factory MAC burned in eFuse: unique per chip, survives erase-flash */
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC, err:%d", err);
        return err;
    }
    snprintf(s_serial_number, sizeof(s_serial_number), "%s%02X%02X%02X%02X%02X%02X", CONFIG_SERIAL_NUMBER_PREFIX,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Serial number: %s", s_serial_number);

    esp_matter::set_custom_device_instance_info_provider(&s_provider);
    return ESP_OK;
}
