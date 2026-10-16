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
// Home Assistant names the device after NodeLabel (else ProductLabel) and
// shows ProductLabel (else ProductName) as its model.

#include <access/SubjectDescriptor.h>
#include <app/AttributeValueDecoder.h>
#include <app/ConcreteAttributePath.h>
#include <app/data-model-provider/OperationTypes.h>
#include <app/persistence/AttributePersistenceProviderInstance.h>
#include <data_model_provider/esp_matter_data_model_provider.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_matter.h>
#include <esp_matter_providers.h>
#include <lib/core/TLV.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.h>
#include <soc/soc_caps.h>
#include <stdio.h>
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
    // The model, like ProductName
    CHIP_ERROR GetProductLabel(char * buf, size_t bufSize) override { return CopyString(buf, bufSize, CONFIG_DEVICE_PRODUCT_NAME); }
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

// Basic Information string limits (Matter spec): longer values fail to read.
static_assert(sizeof(CONFIG_DEVICE_VENDOR_NAME) - 1 <= 32, "Manufacturer must be at most 32 characters");
static_assert(sizeof(CONFIG_DEVICE_PRODUCT_NAME) - 1 <= 32, "Model must be at most 32 characters");
static_assert(sizeof(CONFIG_DEVICE_DISPLAY_NAME) - 1 <= 32, "Device name must be at most 32 characters");
// The Bluetooth name (prefix + 4 serial characters) must be shorter than 16 characters.
static_assert(sizeof(CONFIG_DEVICE_NAME_PREFIX) - 1 <= 11, "Device name prefix must be at most 11 characters");
static_assert(sizeof(CONFIG_DEVICE_HARDWARE_VERSION_STRING) - 1 <= ConfigurationManager::kMaxHardwareVersionStringLength,
              "Hardware must be at most 64 characters");

void device_info_get_serial(char *buf, size_t size)
{
    // The factory base MAC burned in eFuse, e.g. "744DBDA1B2C3". Unique per
    // chip, survives erase-flash.
#if SOC_IEEE802154_SUPPORTED
    // ESP32-H2: on 802.15.4 chips the MAC comes as an EUI-64: the 3 OUI bytes,
    // the 2 MAC_EXT bytes (FF FE), then the 3 bytes that differ from chip to
    // chip.
    uint8_t eui64[8] = {};
    esp_err_t err = esp_efuse_mac_get_default(eui64);
    if (err != ESP_OK) {
        ESP_LOGE("device_info", "Failed to read the MAC address from eFuse, err:%d", err);
    }
    snprintf(buf, size, "%02X%02X%02X%02X%02X%02X", eui64[0], eui64[1], eui64[2], eui64[5], eui64[6], eui64[7]);
#else
    // ESP32-C3: 6 bytes, also the Wi-Fi station MAC shown by the router.
    uint8_t mac[6] = {};
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        ESP_LOGE("device_info", "Failed to read the MAC address from eFuse, err:%d", err);
    }
    snprintf(buf, size, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
}

void device_info_get_name(char *buf, size_t size)
{
    if (sizeof(CONFIG_DEVICE_NAME_PREFIX) == 1) {
        snprintf(buf, size, "%s", CONFIG_DEVICE_DISPLAY_NAME);
        return;
    }
    // Last 4 characters of the serial number: differ from chip to chip
    char serial[ConfigurationManager::kMaxSerialNumberLength + 1];
    device_info_get_serial(serial, sizeof(serial));
    const size_t len = strlen(serial);
    snprintf(buf, size, "%s%s", CONFIG_DEVICE_NAME_PREFIX, &serial[len > 4 ? len - 4 : 0]);
}

void device_info_init(void)
{
    static DeviceInstanceInfo s_device_instance_info;
    esp_matter::set_custom_device_instance_info_provider(&s_device_instance_info);

    char serial[ConfigurationManager::kMaxSerialNumberLength + 1];
    device_info_get_serial(serial, sizeof(serial));
    char name[kDeviceNameSize];
    device_info_get_name(name, sizeof(name));
    ESP_LOGI("device_info", "Serial number: %s, device name: %s", serial, name);
}

// Matter stack lock held. NodeLabel lives in the attribute storage of the
// Basic Information cluster, empty until a controller writes it. When nothing
// is stored yet (new board, after a factory reset), the device name is written
// the way a controller write is handled; a stored label is kept.
static void set_default_node_label(const char *name)
{
    using namespace chip::app;
    using namespace chip::app::Clusters;
    const ConcreteAttributePath path(kRootEndpointId, BasicInformation::Id, BasicInformation::Attributes::NodeLabel::Id);
    AttributePersistenceProvider *storage = GetAttributePersistenceProvider();
    if (storage == nullptr) {
        return;
    }
    uint8_t stored[BasicInformation::Attributes::NodeLabel::TypeInfo::MaxLength() + 1];
    MutableByteSpan stored_span(stored);
    if (storage->ReadValue(path, stored_span) != CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
        return;
    }
    uint8_t tlv[kDeviceNameSize + 8];
    TLV::TLVWriter writer;
    writer.Init(tlv);
    TLV::TLVReader reader;
    CHIP_ERROR err = writer.PutString(TLV::AnonymousTag(), name);
    if (err == CHIP_NO_ERROR) {
        err = writer.Finalize();
    }
    if (err == CHIP_NO_ERROR) {
        reader.Init(tlv, writer.GetLengthWritten());
        err = reader.Next();
    }
    if (err == CHIP_NO_ERROR) {
        const Access::SubjectDescriptor subject{}; // written by the device itself
        AttributeValueDecoder decoder(reader, subject);
        DataModel::WriteAttributeRequest request(ConcreteDataAttributePath(path.mEndpointId, path.mClusterId,
                                                                           path.mAttributeId), subject);
        if (!esp_matter::data_model::provider::get_instance().WriteAttribute(request, decoder).IsSuccess()) {
            err = CHIP_ERROR_INTERNAL;
        }
    }
    if (err == CHIP_NO_ERROR) {
        ESP_LOGI("device_info", "Device name (NodeLabel): %s", name);
    } else {
        ESP_LOGW("device_info", "Device name (NodeLabel) not set: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

// Matter stack lock held. Only in the GAP Device Name characteristic: the
// CHIPoBLE advertisement carries no name.
static void set_ble_name(const char *name)
{
    CHIP_ERROR err = ConnectivityMgr().SetBLEDeviceName(name);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW("device_info", "Bluetooth name not set: %" CHIP_ERROR_FORMAT, err.Format());
        return;
    }
    // The GAP name is set again on each advertising refresh: an advertisement
    // already started with the default name is restarted with this one.
    if (ConnectivityMgr().IsBLEAdvertising()) {
        err = ConnectivityMgr().SetBLEAdvertisingMode(ConnectivityManager::BLEAdvertisingMode::kFastAdvertising);
        if (err != CHIP_NO_ERROR) {
            ESP_LOGW("device_info", "Bluetooth advertising not refreshed: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }
}

void device_info_apply_name(void)
{
    char name[kDeviceNameSize];
    device_info_get_name(name, sizeof(name));
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    set_default_node_label(name);
    if (sizeof(CONFIG_DEVICE_NAME_PREFIX) > 1) {
        set_ble_name(name); // else the default "<CONFIG_BLE_DEVICE_NAME_PREFIX><discriminator>"
    }
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
