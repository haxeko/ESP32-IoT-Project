#include <stdio.h>
#include <string.h>

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_mac.h>

#include <esp_matter_providers.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.h>

#include <device_info.h>

using namespace chip::DeviceLayer;

static const char *TAG = "device_info";

/* Matter Basic Information SerialNumber is at most 32 characters */
static char s_serial_number[33];

static CHIP_ERROR copy_string(const char *src, char *buf, size_t bufSize)
{
    size_t len = strlen(src);
    VerifyOrReturnError(len < bufSize, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf, src, len + 1);
    return CHIP_NO_ERROR;
}

/* Same data as the default (test) provider, except vendor name, product name and serial number */
class AppDeviceInstanceInfoProvider : public Internal::GenericDeviceInstanceInfoProvider<Internal::ESP32Config> {
public:
    AppDeviceInstanceInfoProvider() :
        Internal::GenericDeviceInstanceInfoProvider<Internal::ESP32Config>(ConfigurationManagerImpl::GetDefaultInstance())
    {}

    CHIP_ERROR GetVendorName(char *buf, size_t bufSize) override
    {
        return copy_string(CONFIG_DEVICE_VENDOR_NAME, buf, bufSize);
    }

    CHIP_ERROR GetProductName(char *buf, size_t bufSize) override
    {
        return copy_string(CONFIG_DEVICE_PRODUCT_NAME, buf, bufSize);
    }

    CHIP_ERROR GetSerialNumber(char *buf, size_t bufSize) override
    {
        return copy_string(s_serial_number, buf, bufSize);
    }
};

static AppDeviceInstanceInfoProvider s_provider;

static esp_err_t make_serial_number()
{
#if CONFIG_SERIAL_NUMBER_FROM_GIT_COMMIT
    /* App version is "<major>.<minor>.<yyMMddHHmm>-<commit>[-dirty]" (see CMakeLists.txt):
     * the serial number is everything after the first '-' */
    const char *version = esp_app_get_description()->version;
    const char *commit = strchr(version, '-');
    snprintf(s_serial_number, sizeof(s_serial_number), "%s%s", CONFIG_SERIAL_NUMBER_PREFIX,
             commit ? commit + 1 : version);
#else
    uint8_t mac[6];
    /* Factory MAC burned in eFuse: unique per chip, survives erase-flash */
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC, err:%d", err);
        return err;
    }
    snprintf(s_serial_number, sizeof(s_serial_number), "%s%02X%02X%02X%02X%02X%02X", CONFIG_SERIAL_NUMBER_PREFIX,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
    return ESP_OK;
}

esp_err_t device_info_provider_init()
{
    esp_err_t err = make_serial_number();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "Manufacturer: %s, Model: %s, Serial number: %s", CONFIG_DEVICE_VENDOR_NAME,
             CONFIG_DEVICE_PRODUCT_NAME, s_serial_number);

    esp_matter::set_custom_device_instance_info_provider(&s_provider);
    return ESP_OK;
}
