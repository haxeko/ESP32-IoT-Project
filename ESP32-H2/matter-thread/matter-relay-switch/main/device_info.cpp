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
static char s_serial_number[ConfigurationManager::kMaxSerialNumberLength + 1];

static_assert(sizeof(CONFIG_DEVICE_VENDOR_NAME) - 1 <= ConfigurationManager::kMaxVendorNameLength,
              "CONFIG_DEVICE_VENDOR_NAME exceeds the 32-character Basic Information VendorName limit");
static_assert(sizeof(CONFIG_DEVICE_PRODUCT_NAME) - 1 <= ConfigurationManager::kMaxProductNameLength,
              "CONFIG_DEVICE_PRODUCT_NAME exceeds the 32-character Basic Information ProductName limit");
static_assert(sizeof(CONFIG_DEVICE_HARDWARE_VERSION_STRING) - 1 <= ConfigurationManager::kMaxHardwareVersionStringLength,
              "CONFIG_DEVICE_HARDWARE_VERSION_STRING exceeds the 64-character HardwareVersionString limit");
#if CONFIG_SERIAL_NUMBER_FROM_MAC
static_assert(sizeof(CONFIG_SERIAL_NUMBER_PREFIX) - 1 + 12 <= ConfigurationManager::kMaxSerialNumberLength,
              "CONFIG_SERIAL_NUMBER_PREFIX longer than 20 characters would truncate the MAC-based serial number");
#elif CONFIG_SERIAL_NUMBER_FROM_GIT_COMMIT
/* Worst case "<7-hex commit>-dirty" = 13 characters (see CMakeLists.txt) */
static_assert(sizeof(CONFIG_SERIAL_NUMBER_PREFIX) - 1 + 13 <= ConfigurationManager::kMaxSerialNumberLength,
              "CONFIG_SERIAL_NUMBER_PREFIX longer than 19 characters would truncate the git-commit serial number");
#endif

static CHIP_ERROR copy_string(const char *src, char *buf, size_t bufSize)
{
    size_t len = strlen(src);
    VerifyOrReturnError(len < bufSize, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf, src, len + 1);
    return CHIP_NO_ERROR;
}

/* Same data as the default (test) provider, except vendor name, product name, hardware version string
 * and serial number */
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

    CHIP_ERROR GetHardwareVersionString(char *buf, size_t bufSize) override
    {
        return copy_string(CONFIG_DEVICE_HARDWARE_VERSION_STRING, buf, bufSize);
    }

    CHIP_ERROR GetSerialNumber(char *buf, size_t bufSize) override
    {
        return copy_string(s_serial_number, buf, bufSize);
    }
};

static AppDeviceInstanceInfoProvider s_provider;

static void make_serial_number()
{
#if CONFIG_SERIAL_NUMBER_FROM_GIT_COMMIT
    /* App version is "<major>.<minor>.<yyMMddHHmm>-<commit>[-dirty]" (see CMakeLists.txt):
     * the serial number is everything after the first '-' */
    const char *version = esp_app_get_description()->version;
    const char *commit = strchr(version, '-');
    /* Checking the result also tells GCC the truncation is handled (-Wformat-truncation): the static_assert above
     * covers a 7-digit hash, but git prints more digits when 7 are ambiguous */
    int len = snprintf(s_serial_number, sizeof(s_serial_number), "%s%s", CONFIG_SERIAL_NUMBER_PREFIX,
                       commit ? commit + 1 : version);
    if (len >= (int)sizeof(s_serial_number)) {
        ESP_LOGW(TAG, "Serial number truncated to %u characters", (unsigned)sizeof(s_serial_number) - 1);
    }
#else
    /* 6-byte factory MAC from eFuse (esptool "BASE MAC"): unique per chip, survives OTA and erase-flash.
     * Not esp_efuse_mac_get_default(): on 802.15.4 chips it returns the 8-byte EUI-64 (MAC with FF:FE inserted). */
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    if (err != ESP_OK) {
        /* Cosmetic field: never stop the device from booting because of it */
        ESP_LOGE(TAG, "Failed to read factory MAC, err:%d", err);
        snprintf(s_serial_number, sizeof(s_serial_number), "%sUNKNOWN", CONFIG_SERIAL_NUMBER_PREFIX);
        return;
    }
    snprintf(s_serial_number, sizeof(s_serial_number), "%s%02X%02X%02X%02X%02X%02X", CONFIG_SERIAL_NUMBER_PREFIX,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
}

void device_info_provider_init()
{
    make_serial_number();
    ESP_LOGI(TAG, "Manufacturer: %s, Model: %s, Hardware: %s, Serial number: %s", CONFIG_DEVICE_VENDOR_NAME,
             CONFIG_DEVICE_PRODUCT_NAME, CONFIG_DEVICE_HARDWARE_VERSION_STRING, s_serial_number);

    esp_matter::set_custom_device_instance_info_provider(&s_provider);
}
