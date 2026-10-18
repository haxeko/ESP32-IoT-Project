// Basic Information values that controllers show for the accessory (Manufacturer, Model, name, serial).
// Everything not overridden here (vendor/product IDs, hardware version...) keeps the defaults of the
// generic provider. The firmware version comes from PROJECT_VER / PROJECT_VER_NUMBER in CMakeLists.txt.

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_matter_providers.h>
#include <inttypes.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.h>
#include <stdio.h>
#include <string.h>

#include <app/InteractionModelEngine.h>
#include <app/server/Server.h>

#include "device_info.h"

using namespace chip;
using namespace chip::DeviceLayer;

static const char *TAG = "device_info";

/* CHIP test setup code: every unit uses it until a factory partition gives each one its own */
static constexpr uint32_t k_test_setup_passcode = 20202021;

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
        return CopyString(buf, bufSize, CONFIG_DEVICE_VENDOR_NAME);
    }
    CHIP_ERROR GetProductName(char *buf, size_t bufSize) override
    {
        return CopyString(buf, bufSize, CONFIG_DEVICE_PRODUCT_NAME);
    }
    CHIP_ERROR GetProductLabel(char *buf, size_t bufSize) override
    {
        return CopyString(buf, bufSize, CONFIG_DEVICE_DISPLAY_NAME);
    }
    CHIP_ERROR GetSerialNumber(char *buf, size_t bufSize) override
    {
        char serial[ConfigurationManager::kMaxSerialNumberLength + 1];
        device_info_get_serial(serial, sizeof(serial));
        return CopyString(buf, bufSize, serial);
    }

private:
    static CHIP_ERROR CopyString(char *buf, size_t bufSize, const char *value)
    {
        VerifyOrReturnError(bufSize > strlen(value), CHIP_ERROR_BUFFER_TOO_SMALL);
        strcpy(buf, value);
        return CHIP_NO_ERROR;
    }
};

} // namespace

/* Basic Information limits: VendorName, ProductName, ProductLabel and NodeLabel are at most 32 characters */
static_assert(sizeof(CONFIG_DEVICE_VENDOR_NAME) - 1 <= 32, "Manufacturer must be at most 32 characters");
static_assert(sizeof(CONFIG_DEVICE_PRODUCT_NAME) - 1 <= 32, "Model must be at most 32 characters");
static_assert(sizeof(CONFIG_DEVICE_DISPLAY_NAME) - 1 <= 32, "Device name must be at most 32 characters");

void device_info_get_serial(char *buf, size_t size)
{
    /* Wi-Fi MAC: unique per chip, e.g. "8ED0B2A8594B" */
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, size, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void device_info_init(void)
{
    static DeviceInstanceInfo s_device_instance_info;
    esp_matter::set_custom_device_instance_info_provider(&s_device_instance_info);

    char serial[ConfigurationManager::kMaxSerialNumberLength + 1];
    device_info_get_serial(serial, sizeof(serial));
    ESP_LOGI(TAG, "%s %s, serial number %s", CONFIG_DEVICE_VENDOR_NAME, CONFIG_DEVICE_PRODUCT_NAME, serial);
}

void device_info_log_identity(void)
{
    char serial[ConfigurationManager::kMaxSerialNumberLength + 1];
    device_info_get_serial(serial, sizeof(serial));
    char unique_id[ConfigurationManager::kMaxUniqueIDLength + 1];
    if (ConfigurationMgr().GetUniqueId(unique_id, sizeof(unique_id)) != CHIP_NO_ERROR) {
        strlcpy(unique_id, "unknown", sizeof(unique_id));
    }
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    DeviceInstanceInfoProvider *info = GetDeviceInstanceInfoProvider();
    if (info) {
        (void)info->GetVendorId(vendor_id);
        (void)info->GetProductId(product_id);
    }
    uint16_t discriminator = 0;
    uint32_t passcode = 0;
    bool test_passcode = false;
    CommissionableDataProvider *commissionable = GetCommissionableDataProvider();
    if (commissionable) {
        (void)commissionable->GetSetupDiscriminator(discriminator);
        test_passcode = commissionable->GetSetupPasscode(passcode) == CHIP_NO_ERROR && passcode == k_test_setup_passcode;
    }
    ESP_LOGI(TAG, "Identity: serial number %s, unique ID %s, vendor 0x%04X, product 0x%04X, discriminator %u%s", serial,
             unique_id, vendor_id, product_id, discriminator, test_passcode ? ", test setup code" : "");

    const FabricTable &fabrics = Server::GetInstance().GetFabricTable();
    ESP_LOGI(TAG, "%u fabric(s)", (unsigned)fabrics.FabricCount());
    for (const FabricInfo &fabric : fabrics) {
        ESP_LOGI(TAG,
                 "Fabric %u: node ID 0x%016" PRIX64 ", compressed fabric ID 0x%016" PRIX64 ", admin vendor 0x%04X, "
                 "%" PRIu32 " subscription(s)",
                 fabric.GetFabricIndex(), fabric.GetNodeId(), fabric.GetCompressedFabricId(),
                 to_underlying(fabric.GetVendorId()),
                 app::InteractionModelEngine::GetInstance()->GetNumActiveReadHandlers(
                     app::ReadHandler::InteractionType::Subscribe, fabric.GetFabricIndex()));
    }
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
        /* The values are read from GetProductLabel() / GetSerialNumber() above */
        cluster::basic_information::attribute::create_product_label(basic_info, NULL, 0);
        cluster::basic_information::attribute::create_serial_number(basic_info, NULL, 0);
    }
}
