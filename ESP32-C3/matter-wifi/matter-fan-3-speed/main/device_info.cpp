// Basic Information values that controllers show for the accessory (Manufacturer, Model, name, hardware, serial).
// Everything not overridden here (vendor/product IDs, hardware version number...) keeps the defaults of the
// generic provider. The firmware version comes from PROJECT_VER / PROJECT_VER_NUMBER in CMakeLists.txt.

#include <stdio.h>
#include <string.h>

#include <esp_efuse.h>
#include <esp_efuse_table.h>
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

/* The chip's own identity from eFuse (CONFIG_DEVICE_SERIAL_NUMBER_SOURCE), in upper-case hex: unique per chip,
 * unchanged by OTA, erase-flash, factory reset and a custom MAC. ESP32-C3 has no separate serial number. */
static char s_serial_number[ConfigurationManager::kMaxSerialNumberLength + 1];

static constexpr size_t k_unique_id_size = 16; /* eFuse OPTIONAL_UNIQUE_ID, 128 bits */
static_assert(2 * k_unique_id_size <= ConfigurationManager::kMaxSerialNumberLength,
              "the unique ID in hex exceeds the 32-character SerialNumber limit");

static CHIP_ERROR copy_string(const char *src, char *buf, size_t buf_size)
{
    const size_t len = strlen(src);
    VerifyOrReturnError(len < buf_size, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf, src, len + 1);
    return CHIP_NO_ERROR;
}

/* bytes in order, two upper-case hex digits each, into s_serial_number */
static void set_serial_number(const uint8_t *bytes, size_t len)
{
    static constexpr char k_digits[] = "0123456789ABCDEF";
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 2 < sizeof(s_serial_number); i++) {
        s_serial_number[pos++] = k_digits[bytes[i] >> 4];
        s_serial_number[pos++] = k_digits[bytes[i] & 0x0F];
    }
    s_serial_number[pos] = '\0';
}

/* The MAC address Espressif burned into eFuse BLOCK1, as "esptool read-mac" (or "chip-id", on ESP32-C3) prints it.
 * The Wi-Fi MAC is this address too, unless a custom MAC is configured. */
static bool read_factory_mac()
{
    uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        return false;
    }
    set_serial_number(mac, sizeof(mac));
    return true;
}

#if CONFIG_DEVICE_SERIAL_NUMBER_UNIQUE_ID
/* eFuse OPTIONAL_UNIQUE_ID (BLOCK2), byte 0 first as "espefuse summary" prints it. Blank (all zero) on some chips. */
static bool read_unique_id()
{
    uint8_t id[k_unique_id_size] = {};
    if (esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, id, sizeof(id) * 8) != ESP_OK) {
        return false;
    }
    uint8_t any = 0;
    for (uint8_t byte : id) {
        any |= byte;
    }
    if (any == 0) {
        return false;
    }
    set_serial_number(id, sizeof(id));
    return true;
}
#endif

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
    const char *source = "eFuse factory MAC";
    bool found = false;
#if CONFIG_DEVICE_SERIAL_NUMBER_UNIQUE_ID
    found = read_unique_id();
    if (found) {
        source = "eFuse unique ID";
    } else {
        ESP_LOGW(TAG, "No unique ID in eFuse: serial number from the factory MAC");
    }
#endif
    if (!found) {
        found = read_factory_mac();
    }
    if (!found) {
        /* Cosmetic field: never stop the device from booting because of it */
        snprintf(s_serial_number, sizeof(s_serial_number), "UNKNOWN");
        source = "eFuse not readable";
    }
    ESP_LOGI(TAG, "Manufacturer: %s, Model: %s, Hardware: %s, Serial number: %s (%s)", CONFIG_DEVICE_VENDOR_NAME,
             CONFIG_DEVICE_PRODUCT_NAME, CONFIG_DEVICE_HARDWARE_VERSION_STRING, s_serial_number, source);
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
