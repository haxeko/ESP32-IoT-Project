/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Basic Information values that controllers show for the accessory. Everything
// not overridden here (IDs, serial number, hardware version...) keeps the
// defaults of the generic provider. The firmware version comes from
// PROJECT_VER / PROJECT_VER_NUMBER in the top-level CMakeLists.txt.

#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.h>
#include <string.h>

#include "device_info.h"

using namespace chip;
using namespace chip::DeviceLayer;

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

private:
    static CHIP_ERROR CopyString(char * buf, size_t bufSize, const char * value)
    {
        ReturnErrorCodeIf(bufSize <= strlen(value), CHIP_ERROR_BUFFER_TOO_SMALL);
        strcpy(buf, value);
        return CHIP_NO_ERROR;
    }
};

} // namespace

static_assert(sizeof(CONFIG_DEVICE_DISPLAY_NAME) - 1 <= 32, "Device name must be at most 32 characters");

void device_info_init(void)
{
    static DeviceInstanceInfo s_device_instance_info;
    esp_matter::set_custom_device_instance_info_provider(&s_device_instance_info);
}

void device_info_set_name(esp_matter::node::config_t *node_config)
{
    strlcpy(node_config->root_node.basic_information.node_label, CONFIG_DEVICE_DISPLAY_NAME,
            sizeof(node_config->root_node.basic_information.node_label));
}

void device_info_add_product_label(esp_matter::node_t *node)
{
    using namespace esp_matter;
    endpoint_t *root = endpoint::get(node, 0);
    cluster_t *basic_info = cluster::get(root, chip::app::Clusters::BasicInformation::Id);
    if (basic_info) {
        // The value itself is read from GetProductLabel() above.
        cluster::basic_information::attribute::create_product_label(basic_info, NULL, 0);
    }
}
