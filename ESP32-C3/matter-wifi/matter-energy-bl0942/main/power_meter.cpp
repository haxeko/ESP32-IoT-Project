/*
 * Publishes BL0942 readings over Matter:
 *   - Electrical Power Measurement: ActivePower (mW), Voltage (mV), ActiveCurrent (mA)
 *   - Electrical Energy Measurement: CumulativeEnergyImported (mWh), persisted in NVS
 */
#include <sdkconfig.h>
#if CONFIG_BL0942_ENABLE

#include "power_meter.h"
#include "bl0942.h"

#include <math.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <app/clusters/electrical-power-measurement-server/ElectricalPowerMeasurementDelegate.h>
#include <app/clusters/power-topology-server/PowerTopologyDelegate.h>
#include <app/reporting/reporting.h>
#include <data_model_provider/clusters/electrical_energy_measurement/integration.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

static const char *TAG = "power_meter";

static constexpr const char *k_nvs_namespace = "power_meter";
static constexpr const char *k_nvs_energy_key = "energy_mwh";
static constexpr int64_t k_energy_report_interval_us = 30LL * 1000 * 1000;
static constexpr int64_t k_energy_save_interval_us = 10LL * 60 * 1000 * 1000;

namespace {

namespace EPM = chip::app::Clusters::ElectricalPowerMeasurement;
namespace EEM = chip::app::Clusters::ElectricalEnergyMeasurement;

/* Power Topology with the NodeTopology feature: the measurement covers this whole node */
class NodeTopologyDelegate : public PowerTopology::Delegate {
public:
    CHIP_ERROR GetAvailableEndpointAtIndex(size_t index, EndpointId &endpointId) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR GetActiveEndpointAtIndex(size_t index, EndpointId &endpointId) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
};

const EPM::Structs::MeasurementAccuracyRangeStruct::Type k_power_ranges[] = {
    { .rangeMin = 0, .rangeMax = 3'500'000, .percentMax = MakeOptional(static_cast<Percent100ths>(200)) }, /* 0..3.5 kW, 2% */
};
const EPM::Structs::MeasurementAccuracyRangeStruct::Type k_current_ranges[] = {
    { .rangeMin = 0, .rangeMax = 16'000, .percentMax = MakeOptional(static_cast<Percent100ths>(200)) },    /* 0..16 A, 2% */
};
const EPM::Structs::MeasurementAccuracyRangeStruct::Type k_voltage_ranges[] = {
    { .rangeMin = 0, .rangeMax = 300'000, .percentMax = MakeOptional(static_cast<Percent100ths>(200)) },   /* 0..300 V, 2% */
};

const EPM::Structs::MeasurementAccuracyStruct::Type k_accuracies[] = {
    {
        .measurementType = EPM::MeasurementTypeEnum::kActivePower,
        .measured = true,
        .minMeasuredValue = 0,
        .maxMeasuredValue = 3'500'000,
        .accuracyRanges = DataModel::List<const EPM::Structs::MeasurementAccuracyRangeStruct::Type>(k_power_ranges),
    },
    {
        .measurementType = EPM::MeasurementTypeEnum::kActiveCurrent,
        .measured = true,
        .minMeasuredValue = 0,
        .maxMeasuredValue = 16'000,
        .accuracyRanges = DataModel::List<const EPM::Structs::MeasurementAccuracyRangeStruct::Type>(k_current_ranges),
    },
    {
        .measurementType = EPM::MeasurementTypeEnum::kVoltage,
        .measured = true,
        .minMeasuredValue = 0,
        .maxMeasuredValue = 300'000,
        .accuracyRanges = DataModel::List<const EPM::Structs::MeasurementAccuracyRangeStruct::Type>(k_voltage_ranges),
    },
};

/* Values are pushed in by the meter task; the cluster server reads them through the getters */
class Bl0942PowerDelegate : public EPM::Delegate {
public:
    EPM::PowerModeEnum GetPowerMode() override { return EPM::PowerModeEnum::kAc; }
    uint8_t GetNumberOfMeasurementTypes() override { return MATTER_ARRAY_SIZE(k_accuracies); }

    CHIP_ERROR StartAccuracyRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetAccuracyByIndex(uint8_t index, EPM::Structs::MeasurementAccuracyStruct::Type &accuracy) override
    {
        if (index >= MATTER_ARRAY_SIZE(k_accuracies)) {
            return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        }
        accuracy = k_accuracies[index];
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR EndAccuracyRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR StartRangesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetRangeByIndex(uint8_t, EPM::Structs::MeasurementRangeStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndRangesRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR StartHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicCurrentsByIndex(uint8_t, EPM::Structs::HarmonicMeasurementStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR StartHarmonicPhasesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicPhasesByIndex(uint8_t, EPM::Structs::HarmonicMeasurementStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndHarmonicPhasesRead() override { return CHIP_NO_ERROR; }

    DataModel::Nullable<int64_t> GetVoltage() override { return mVoltage; }
    DataModel::Nullable<int64_t> GetActiveCurrent() override { return mActiveCurrent; }
    DataModel::Nullable<int64_t> GetReactiveCurrent() override { return DataModel::NullNullable; }
    DataModel::Nullable<int64_t> GetApparentCurrent() override { return DataModel::NullNullable; }
    DataModel::Nullable<int64_t> GetActivePower() override { return mActivePower; }
    DataModel::Nullable<int64_t> GetReactivePower() override { return DataModel::NullNullable; }
    DataModel::Nullable<int64_t> GetApparentPower() override { return DataModel::NullNullable; }
    DataModel::Nullable<int64_t> GetRMSVoltage() override { return DataModel::NullNullable; }
    DataModel::Nullable<int64_t> GetRMSCurrent() override { return DataModel::NullNullable; }
    DataModel::Nullable<int64_t> GetRMSPower() override { return DataModel::NullNullable; }
    DataModel::Nullable<int64_t> GetFrequency() override { return DataModel::NullNullable; }
    DataModel::Nullable<int64_t> GetPowerFactor() override { return DataModel::NullNullable; }
    DataModel::Nullable<int64_t> GetNeutralCurrent() override { return DataModel::NullNullable; }

    /* Must be called with the Matter stack lock held */
    void SetActivePower(DataModel::Nullable<int64_t> v) { Set(mActivePower, v, EPM::Attributes::ActivePower::Id); }
    void SetVoltage(DataModel::Nullable<int64_t> v) { Set(mVoltage, v, EPM::Attributes::Voltage::Id); }
    void SetActiveCurrent(DataModel::Nullable<int64_t> v) { Set(mActiveCurrent, v, EPM::Attributes::ActiveCurrent::Id); }

private:
    void Set(DataModel::Nullable<int64_t> &field, DataModel::Nullable<int64_t> value, AttributeId attribute_id)
    {
        if (field != value) {
            field = value;
            MatterReportingAttributeChangeCallback(mEndpointId, EPM::Id, attribute_id);
        }
    }

    DataModel::Nullable<int64_t> mVoltage;
    DataModel::Nullable<int64_t> mActiveCurrent;
    DataModel::Nullable<int64_t> mActivePower;
};

NodeTopologyDelegate s_topology_delegate;
Bl0942PowerDelegate s_power_delegate;
uint16_t s_endpoint_id;

uint64_t load_energy()
{
    uint64_t energy = 0;
    nvs_handle_t handle;
    if (nvs_open(k_nvs_namespace, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u64(handle, k_nvs_energy_key, &energy);
        nvs_close(handle);
    }
    return energy;
}

void save_energy(uint64_t energy)
{
    nvs_handle_t handle;
    if (nvs_open(k_nvs_namespace, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u64(handle, k_nvs_energy_key, energy);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

bool report_energy(uint64_t energy_mwh, int64_t now_us)
{
    EEM::Structs::EnergyMeasurementStruct::Type imported;
    imported.energy = static_cast<int64_t>(energy_mwh);
    /* systime_ms: milliseconds since boot */
    imported.endSystime = MakeOptional(static_cast<uint64_t>(now_us / 1000));
    return EEM::NotifyCumulativeEnergyMeasured(s_endpoint_id, DataModel::MakeNullable(imported), DataModel::NullNullable);
}

/* The EEM cluster server is created when the endpoint comes up; returns false until it exists */
bool set_energy_accuracy()
{
    static const EEM::Structs::MeasurementAccuracyRangeStruct::Type ranges[] = {
        { .rangeMin = 0, .rangeMax = 1'000'000'000'000, .percentMax = MakeOptional(static_cast<Percent100ths>(200)) },
    };
    static const EEM::Structs::MeasurementAccuracyStruct::Type accuracy = {
        .measurementType = EEM::MeasurementTypeEnum::kElectricalEnergy,
        .measured = true,
        .minMeasuredValue = 0,
        .maxMeasuredValue = 1'000'000'000'000, /* 1 GWh in mWh */
        .accuracyRanges = DataModel::List<const EEM::Structs::MeasurementAccuracyRangeStruct::Type>(ranges),
    };
    CHIP_ERROR err = EEM::SetMeasurementAccuracy(s_endpoint_id, accuracy);
    if (err != CHIP_NO_ERROR && err != CHIP_ERROR_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to set energy accuracy: %" CHIP_ERROR_FORMAT, err.Format());
    }
    return err == CHIP_NO_ERROR;
}

/* Round so measurement jitter does not trigger a Matter report every cycle (same resolution as the ESPHome
 * config: 0.1 W, 1 V, 0.01 A) */
inline int64_t round_to(float value, float unit, int64_t milli_per_unit)
{
    return llroundf(value / unit) * milli_per_unit;
}

void meter_task(void *arg)
{
    uint64_t saved_energy = *static_cast<uint64_t *>(arg);
    uint64_t reported_energy = UINT64_MAX;
    int64_t last_energy_report = 0;
    int64_t last_energy_save = esp_timer_get_time();
    bool accuracy_set = false;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_BL0942_UPDATE_INTERVAL_MS));

        bl0942_reading_t r = {};
        bl0942_update(&r);
        int64_t now = esp_timer_get_time();

        int64_t power_mw = round_to(r.power_w, 0.1f, 100);
        int64_t voltage_mv = round_to(r.voltage_v, 1.0f, 1000);
        int64_t current_ma = round_to(r.current_a, 0.01f, 10);

        ESP_LOGD(TAG, "P=%.1f W U=%.1f V I=%.3f A E=%llu mWh", r.power_w, r.voltage_v, r.current_a,
                 (unsigned long long)r.energy_mwh);

        {
            esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
            if (!accuracy_set) {
                accuracy_set = set_energy_accuracy();
            }
            /* Always publish numbers, like the BL0937 build: Apple Home hides the power reading of an outlet
             * whose ActivePower is null. Without a reply from the BL0942, report 0. */
            if (!r.valid) {
                power_mw = 0;
                current_ma = 0;
                voltage_mv = 0;
            }
            s_power_delegate.SetActivePower(DataModel::MakeNullable(power_mw));
            s_power_delegate.SetActiveCurrent(DataModel::MakeNullable(current_ma));
            s_power_delegate.SetVoltage(DataModel::MakeNullable(voltage_mv));
            if (r.energy_mwh != reported_energy && now - last_energy_report >= k_energy_report_interval_us &&
                report_energy(r.energy_mwh, now)) {
                reported_energy = r.energy_mwh;
                last_energy_report = now;
            }
        }

        if (r.energy_mwh != saved_energy && now - last_energy_save >= k_energy_save_interval_us) {
            save_energy(r.energy_mwh);
            saved_energy = r.energy_mwh;
            last_energy_save = now;
        }
    }
}

} // namespace

esp_err_t power_meter_add_to_endpoint(esp_matter::endpoint_t *endpoint)
{
    namespace cluster = esp_matter::cluster;

    esp_matter::endpoint::electrical_sensor::config_t config;
    config.power_topology.feature_flags = cluster::power_topology::feature::node_topology::get_id();
    config.power_topology.delegate = &s_topology_delegate;

    auto &power = config.with_electrical_power_measurement();
    power.feature_flags = cluster::electrical_power_measurement::feature::alternating_current::get_id();
    power.delegate = &s_power_delegate;

    auto &energy = config.with_electrical_energy_measurement();
    energy.feature_flags = cluster::electrical_energy_measurement::feature::imported_energy::get_id() |
                           cluster::electrical_energy_measurement::feature::cumulative_energy::get_id();

    esp_err_t err = esp_matter::endpoint::electrical_sensor::add(endpoint, &config);
    if (err != ESP_OK) {
        return err;
    }

    /* Optional EPM attributes: the cluster server exposes the ones present on the endpoint */
    esp_matter::cluster_t *epm = cluster::get(endpoint, EPM::Id);
    if (!epm) {
        return ESP_FAIL;
    }
    nullable<int64_t> null_value;
    cluster::electrical_power_measurement::attribute::create_voltage(epm, null_value);
    cluster::electrical_power_measurement::attribute::create_active_current(epm, null_value);
    return ESP_OK;
}

esp_err_t power_meter_start(uint16_t endpoint_id)
{
    s_endpoint_id = endpoint_id;

    static uint64_t s_restored_energy;
    s_restored_energy = load_energy();
    ESP_LOGI(TAG, "Restored energy: %llu mWh", (unsigned long long)s_restored_energy);

    esp_err_t err = bl0942_init(s_restored_energy);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BL0942 init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(meter_task, "power_meter", 4096, &s_restored_energy, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#endif // CONFIG_BL0942_ENABLE
