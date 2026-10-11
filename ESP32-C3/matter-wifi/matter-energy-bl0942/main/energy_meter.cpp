/*
 * Đo điện bằng BL0942 và cập nhật các cluster đo điện Matter trên endpoint ổ cắm.
 */
#include "energy_meter.h"
#include "bl0942.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#include <app-common/zap-generated/cluster-objects.h>
#include <app/clusters/electrical-energy-measurement-server/ElectricalEnergyMeasurementCluster.h>
#include <app/clusters/electrical-energy-measurement-server/electrical-energy-measurement-server.h>
#include <app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h>
#include <app/reporting/reporting.h>
#include <platform/CHIPDeviceLayer.h>
#include <system/SystemClock.h>

using namespace esp_matter;
using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

static const char *TAG = "energy_meter";

/* ------------------------------------------------------------------------- */
/* Giá trị đo (đơn vị Matter)                                                 */
/* ------------------------------------------------------------------------- */

typedef struct {
    bool valid;
    int64_t voltage_mv;       /* mV */
    int64_t current_ma;       /* mA */
    int64_t power_mw;         /* mW (công suất tác dụng) */
    int64_t apparent_mva;     /* mVA */
    int64_t frequency_mhz;    /* mHz */
    int64_t power_factor;     /* -10000..10000 (10000 = 1.0) */
    int64_t energy_mwh;       /* mWh, tích luỹ */
} meter_values_t;

static portMUX_TYPE s_values_lock = portMUX_INITIALIZER_UNLOCKED;
static meter_values_t s_values;

static uint16_t s_sensor_endpoint_id;

/* ------------------------------------------------------------------------- */
/* Electrical Power Measurement delegate                                      */
/* ------------------------------------------------------------------------- */

namespace {

using namespace chip::app::Clusters::ElectricalPowerMeasurement;
using EpmAccuracyRange = ElectricalPowerMeasurement::Structs::MeasurementAccuracyRangeStruct::Type;
using EpmAccuracy = ElectricalPowerMeasurement::Structs::MeasurementAccuracyStruct::Type;

/* BL0942: sai số điển hình < 1% trong dải động 4000:1 */
constexpr Percent100ths kPercentMax = 200;  /* 2%   */
constexpr Percent100ths kPercentMin = 50;   /* 0.5% */
constexpr Percent100ths kPercentTyp = 100;  /* 1%   */

#define ACCURACY_RANGE(min, max)                                                                                         \
    {                                                                                                                    \
        .rangeMin = (min), .rangeMax = (max), .percentMax = MakeOptional(kPercentMax),                                  \
        .percentMin = MakeOptional(kPercentMin), .percentTypical = MakeOptional(kPercentTyp)                            \
    }

const EpmAccuracyRange kVoltageRanges[] = { ACCURACY_RANGE(0, 300'000) };          /* 0..300 V    */
const EpmAccuracyRange kCurrentRanges[] = { ACCURACY_RANGE(0, 30'000) };           /* 0..30 A     */
const EpmAccuracyRange kPowerRanges[] = { ACCURACY_RANGE(-9'000'000, 9'000'000) }; /* +-9 kW      */
const EpmAccuracyRange kApparentRanges[] = { ACCURACY_RANGE(0, 9'000'000) };       /* 0..9 kVA    */
const EpmAccuracyRange kFrequencyRanges[] = { ACCURACY_RANGE(40'000, 70'000) };    /* 40..70 Hz   */
const EpmAccuracyRange kPowerFactorRanges[] = { ACCURACY_RANGE(-10'000, 10'000) }; /* -1.0..1.0   */

const EpmAccuracy kAccuracies[] = {
    { .measurementType = MeasurementTypeEnum::kVoltage, .measured = true, .minMeasuredValue = 0,
      .maxMeasuredValue = 300'000, .accuracyRanges = DataModel::List<const EpmAccuracyRange>(kVoltageRanges) },
    { .measurementType = MeasurementTypeEnum::kActiveCurrent, .measured = true, .minMeasuredValue = 0,
      .maxMeasuredValue = 30'000, .accuracyRanges = DataModel::List<const EpmAccuracyRange>(kCurrentRanges) },
    { .measurementType = MeasurementTypeEnum::kActivePower, .measured = true, .minMeasuredValue = -9'000'000,
      .maxMeasuredValue = 9'000'000, .accuracyRanges = DataModel::List<const EpmAccuracyRange>(kPowerRanges) },
    { .measurementType = MeasurementTypeEnum::kApparentPower, .measured = false, .minMeasuredValue = 0,
      .maxMeasuredValue = 9'000'000, .accuracyRanges = DataModel::List<const EpmAccuracyRange>(kApparentRanges) },
    { .measurementType = MeasurementTypeEnum::kFrequency, .measured = true, .minMeasuredValue = 40'000,
      .maxMeasuredValue = 70'000, .accuracyRanges = DataModel::List<const EpmAccuracyRange>(kFrequencyRanges) },
    { .measurementType = MeasurementTypeEnum::kPowerFactor, .measured = false, .minMeasuredValue = -10'000,
      .maxMeasuredValue = 10'000, .accuracyRanges = DataModel::List<const EpmAccuracyRange>(kPowerFactorRanges) },
};

class Bl0942PowerDelegate : public ElectricalPowerMeasurement::Delegate {
public:
    PowerModeEnum GetPowerMode() override { return PowerModeEnum::kAc; }
    uint8_t GetNumberOfMeasurementTypes() override { return MATTER_ARRAY_SIZE(kAccuracies); }

    CHIP_ERROR StartAccuracyRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetAccuracyByIndex(uint8_t index, EpmAccuracy &accuracy) override
    {
        VerifyOrReturnError(index < MATTER_ARRAY_SIZE(kAccuracies), CHIP_ERROR_PROVIDER_LIST_EXHAUSTED);
        accuracy = kAccuracies[index];
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR EndAccuracyRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR StartRangesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetRangeByIndex(uint8_t, Structs::MeasurementRangeStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndRangesRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR StartHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicCurrentsByIndex(uint8_t, Structs::HarmonicMeasurementStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }

    CHIP_ERROR StartHarmonicPhasesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicPhasesByIndex(uint8_t, Structs::HarmonicMeasurementStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndHarmonicPhasesRead() override { return CHIP_NO_ERROR; }

    DataModel::Nullable<int64_t> GetVoltage() override { return mVoltage; }
    DataModel::Nullable<int64_t> GetActiveCurrent() override { return mCurrent; }
    DataModel::Nullable<int64_t> GetReactiveCurrent() override { return {}; }
    DataModel::Nullable<int64_t> GetApparentCurrent() override { return {}; }
    DataModel::Nullable<int64_t> GetActivePower() override { return mActivePower; }
    DataModel::Nullable<int64_t> GetReactivePower() override { return {}; }
    DataModel::Nullable<int64_t> GetApparentPower() override { return mApparentPower; }
    DataModel::Nullable<int64_t> GetRMSVoltage() override { return mVoltage; }
    DataModel::Nullable<int64_t> GetRMSCurrent() override { return mCurrent; }
    DataModel::Nullable<int64_t> GetRMSPower() override { return {}; }
    DataModel::Nullable<int64_t> GetFrequency() override { return mFrequency; }
    DataModel::Nullable<int64_t> GetPowerFactor() override { return mPowerFactor; }
    DataModel::Nullable<int64_t> GetNeutralCurrent() override { return {}; }

    /* Gọi trong ngữ cảnh Matter (đã giữ chip stack lock) */
    void Update(const meter_values_t &v)
    {
        auto val = [&v](int64_t x) {
            return v.valid ? DataModel::MakeNullable(x) : DataModel::Nullable<int64_t>();
        };
        Set(mVoltage, val(v.voltage_mv), { Attributes::Voltage::Id, Attributes::RMSVoltage::Id });
        Set(mCurrent, val(v.current_ma), { Attributes::ActiveCurrent::Id, Attributes::RMSCurrent::Id });
        Set(mActivePower, val(v.power_mw), { Attributes::ActivePower::Id });
        Set(mApparentPower, val(v.apparent_mva), { Attributes::ApparentPower::Id });
        Set(mFrequency, val(v.frequency_mhz), { Attributes::Frequency::Id });
        Set(mPowerFactor, val(v.power_factor), { Attributes::PowerFactor::Id });
    }

private:
    void Set(DataModel::Nullable<int64_t> &field, DataModel::Nullable<int64_t> value,
             std::initializer_list<AttributeId> attrs)
    {
        if (field == value) {
            return;
        }
        field = value;
        for (AttributeId id : attrs) {
            MatterReportingAttributeChangeCallback(mEndpointId, ElectricalPowerMeasurement::Id, id);
        }
    }

    DataModel::Nullable<int64_t> mVoltage;
    DataModel::Nullable<int64_t> mCurrent;
    DataModel::Nullable<int64_t> mActivePower;
    DataModel::Nullable<int64_t> mApparentPower;
    DataModel::Nullable<int64_t> mFrequency;
    DataModel::Nullable<int64_t> mPowerFactor;
};

Bl0942PowerDelegate s_power_delegate;

/* ------------------------------------------------------------------------- */
/* Electrical Energy Measurement                                              */
/* ------------------------------------------------------------------------- */

using EemAccuracyRange = ElectricalEnergyMeasurement::Structs::MeasurementAccuracyRangeStruct::Type;
using EemAccuracy = ElectricalEnergyMeasurement::Structs::MeasurementAccuracyStruct::Type;
using EnergyMeasurement = ElectricalEnergyMeasurement::Structs::EnergyMeasurementStruct::Type;

const EemAccuracyRange kEnergyRanges[] = { ACCURACY_RANGE(0, 1'000'000'000'000) }; /* 0..1 GWh */

ElectricalEnergyMeasurement::ElectricalEnergyMeasurementAttrAccess *s_energy_attr_access;

int64_t s_reported_energy_mwh = -1;
int64_t s_last_energy_event_ms;
int64_t s_last_energy_report_ms;

} // namespace

/* ------------------------------------------------------------------------- */
/* Lưu tổng điện năng vào NVS                                                 */
/* ------------------------------------------------------------------------- */

#define ENERGY_NVS_NAMESPACE "energy_meter"
#define ENERGY_NVS_KEY "total_mwh"

static int64_t energy_nvs_load()
{
    nvs_handle_t h;
    int64_t v = 0;
    if (nvs_open(ENERGY_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i64(h, ENERGY_NVS_KEY, &v);
        nvs_close(h);
    }
    return v;
}

static void energy_nvs_save(int64_t mwh)
{
    nvs_handle_t h;
    if (nvs_open(ENERGY_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_i64(h, ENERGY_NVS_KEY, mwh) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

/* ------------------------------------------------------------------------- */
/* Cập nhật Matter (chạy trên Matter thread)                                  */
/* ------------------------------------------------------------------------- */

static void report_energy(const meter_values_t &v, bool send_event)
{
    const int64_t now_ms = static_cast<int64_t>(System::SystemClock().GetMonotonicMilliseconds64().count());

    EnergyMeasurement energy;
    energy.energy = v.energy_mwh;
    energy.endSystime = MakeOptional(static_cast<uint64_t>(now_ms));
    Optional<EnergyMeasurement> imported(energy);
    Optional<EnergyMeasurement> exported;

    if (send_event) {
        /* Ghi giá trị + phát event CumulativeEnergyMeasured */
        ElectricalEnergyMeasurement::NotifyCumulativeEnergyMeasured(s_sensor_endpoint_id, imported, exported);
    } else {
        ElectricalEnergyMeasurement::MeasurementData *data =
            ElectricalEnergyMeasurement::MeasurementDataForEndpoint(s_sensor_endpoint_id);
        if (data) {
            data->cumulativeImported = imported;
        }
    }
    MatterReportingAttributeChangeCallback(s_sensor_endpoint_id, ElectricalEnergyMeasurement::Id,
                                           ElectricalEnergyMeasurement::Attributes::CumulativeEnergyImported::Id);
}

static void matter_update_work(intptr_t)
{
    meter_values_t v;
    taskENTER_CRITICAL(&s_values_lock);
    v = s_values;
    taskEXIT_CRITICAL(&s_values_lock);

    s_power_delegate.Update(v);

    /* Điện năng: báo cáo khi thay đổi (tối đa 10 s/lần), event theo chu kỳ cấu hình */
    if (s_energy_attr_access) {
        const int64_t now_ms = static_cast<int64_t>(System::SystemClock().GetMonotonicMilliseconds64().count());
        const bool event_due = (now_ms - s_last_energy_event_ms) >= CONFIG_ENERGY_EVENT_INTERVAL_S * 1000LL ||
            s_reported_energy_mwh < 0;
        const bool report_due = v.energy_mwh != s_reported_energy_mwh && (now_ms - s_last_energy_report_ms) >= 10'000;
        if (event_due || report_due) {
            report_energy(v, event_due);
            s_reported_energy_mwh = v.energy_mwh;
            s_last_energy_report_ms = now_ms;
            if (event_due) {
                s_last_energy_event_ms = now_ms;
            }
        }
    }

}

static void energy_cluster_init_work(intptr_t)
{
    s_energy_attr_access = new ElectricalEnergyMeasurement::ElectricalEnergyMeasurementAttrAccess(
        BitMask<ElectricalEnergyMeasurement::Feature, uint32_t>(ElectricalEnergyMeasurement::Feature::kImportedEnergy,
                                                                ElectricalEnergyMeasurement::Feature::kCumulativeEnergy),
        BitMask<ElectricalEnergyMeasurement::OptionalAttributes, uint32_t>());
    if (s_energy_attr_access->Init() != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to init Electrical Energy Measurement cluster");
        delete s_energy_attr_access;
        s_energy_attr_access = nullptr;
        return;
    }

    EemAccuracy accuracy;
    accuracy.measurementType = ElectricalEnergyMeasurement::MeasurementTypeEnum::kElectricalEnergy;
    accuracy.measured = true;
    accuracy.minMeasuredValue = 0;
    accuracy.maxMeasuredValue = 1'000'000'000'000;
    accuracy.accuracyRanges = DataModel::List<const EemAccuracyRange>(kEnergyRanges);
    CHIP_ERROR err = ElectricalEnergyMeasurement::SetMeasurementAccuracy(s_sensor_endpoint_id, accuracy);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "SetMeasurementAccuracy failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

/* ------------------------------------------------------------------------- */
/* Task đọc BL0942                                                            */
/* ------------------------------------------------------------------------- */

static float cfg_float(const char *s, float fallback)
{
    float v = strtof(s, nullptr);
    return v > 0 ? v : fallback;
}

static void meter_task(void *)
{
    const float voltage_ref = cfg_float(CONFIG_BL0942_VOLTAGE_REF, 15873.35944f);
    const float current_ref = cfg_float(CONFIG_BL0942_CURRENT_REF, 251213.46469f);
    const float power_ref = cfg_float(CONFIG_BL0942_POWER_REF, 596.0f);
    /* Mỗi xung CF ứng với 1638.4 * 256 / POWER_REF (W.s) */
    const float energy_ref = cfg_float(CONFIG_BL0942_ENERGY_REF, power_ref * 3600000.0f / 419430.4f);
    const double mwh_per_pulse = 1e6 / energy_ref;

    ESP_LOGI(TAG, "Calibration: Vref=%.3f Iref=%.3f Pref=%.3f Eref=%.3f pulse/kWh", voltage_ref, current_ref,
             power_ref, energy_ref);

    double energy_mwh = static_cast<double>(energy_nvs_load());
    int64_t saved_mwh = static_cast<int64_t>(energy_mwh);
    ESP_LOGI(TAG, "Total energy restored from NVS: %.3f kWh", energy_mwh / 1e6);

    int32_t last_cf = -1;
    int fail_count = 0;
    /* Giới hạn hợp lý số xung CF giữa 2 lần đọc (~ 20 kW), lớn hơn coi như BL0942 vừa reset */
    const uint32_t max_pulses =
        static_cast<uint32_t>(energy_ref * 20.0f * CONFIG_BL0942_READ_INTERVAL_MS / 3600000.0f) + 10;

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_BL0942_READ_INTERVAL_MS));

        bl0942_raw_t raw;
        meter_values_t v = {};
        if (bl0942_read_raw(&raw, 200) == ESP_OK) {
            fail_count = 0;
            const double volts = raw.v_rms / voltage_ref;
            double amps = raw.i_rms / current_ref;
            double watts = raw.watt / power_ref;
            if (amps * 1000.0 < CONFIG_BL0942_CURRENT_NOISE_MA) {
                amps = 0;
            }
            if (fabs(watts) * 1000.0 < CONFIG_BL0942_POWER_NOISE_MW) {
                watts = 0;
            }
            const double va = volts * amps;

            /* Điện năng từ bộ đếm xung CF_CNT (24 bit) */
            const uint32_t cf = raw.cf_cnt & 0xFFFFFF;
            if (last_cf >= 0) {
                const uint32_t delta = (cf - static_cast<uint32_t>(last_cf)) & 0xFFFFFF;
                if (delta <= max_pulses) {
                    energy_mwh += delta * mwh_per_pulse;
                } else {
                    ESP_LOGW(TAG, "CF_CNT jump (%" PRIu32 " -> %" PRIu32 "), ignored", (uint32_t)last_cf, cf);
                }
            }
            last_cf = static_cast<int32_t>(cf);

            v.valid = true;
            v.voltage_mv = llround(volts * 1000.0);
            v.current_ma = llround(amps * 1000.0);
            v.power_mw = llround(watts * 1000.0);
            v.apparent_mva = llround(va * 1000.0);
            v.frequency_mhz = raw.freq ? llround(1e9 / raw.freq) : 0;
            if (va > 0.5) {
                double pf = watts / va;
                pf = pf > 1.0 ? 1.0 : (pf < -1.0 ? -1.0 : pf);
                v.power_factor = llround(pf * 10000.0);
            } else {
                v.power_factor = 10000;
            }
            v.energy_mwh = static_cast<int64_t>(energy_mwh);

            ESP_LOGD(TAG, "U=%.1fV I=%.3fA P=%.1fW S=%.1fVA f=%.2fHz E=%.3fkWh", volts, amps, watts, va,
                     v.frequency_mhz / 1000.0, energy_mwh / 1e6);

            if (v.energy_mwh - saved_mwh >= CONFIG_ENERGY_NVS_SAVE_DELTA_WH * 1000LL) {
                energy_nvs_save(v.energy_mwh);
                saved_mwh = v.energy_mwh;
            }
        } else {
            /* Sau 5 lần lỗi liên tiếp, báo giá trị null để controller biết mất kết nối */
            if (++fail_count < 5) {
                continue;
            }
            if (fail_count == 5) {
                ESP_LOGE(TAG, "BL0942 not responding, check wiring (TX->GPIO%d, RX->GPIO%d)",
                         CONFIG_BL0942_UART_RX_GPIO, CONFIG_BL0942_UART_TX_GPIO);
            }
            v.valid = false;
            v.energy_mwh = static_cast<int64_t>(energy_mwh);
        }

        taskENTER_CRITICAL(&s_values_lock);
        s_values = v;
        taskEXIT_CRITICAL(&s_values_lock);
        DeviceLayer::PlatformMgr().ScheduleWork(matter_update_work, 0);
    }
}

/* ------------------------------------------------------------------------- */
/* API                                                                        */
/* ------------------------------------------------------------------------- */

esp_err_t energy_meter_add_to_endpoint(endpoint_t *ep)
{
    /* Thêm device type Electrical Sensor (0x0510) vào cùng endpoint với ổ cắm:
     *   - Power Topology (Node): phép đo áp dụng cho chính thiết bị này
     *   - Electrical Power Measurement (AC)
     *   - Electrical Energy Measurement (Imported + Cumulative)
     */
    endpoint::electrical_sensor::config_t sensor_config;
    sensor_config.electrical_power_measurement.feature_flags =
        cluster::electrical_power_measurement::feature::alternating_current::get_id();
    sensor_config.electrical_power_measurement.delegate = &s_power_delegate;

    esp_err_t err = endpoint::electrical_sensor::add(ep, &sensor_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add electrical sensor device type");
        return err;
    }
    s_sensor_endpoint_id = endpoint::get_id(ep);

    cluster_t *epm = cluster::get(ep, ElectricalPowerMeasurement::Id);
    nullable<int64_t> null_value;
    cluster::electrical_power_measurement::attribute::create_voltage(epm, null_value);
    cluster::electrical_power_measurement::attribute::create_active_current(epm, null_value);
    cluster::electrical_power_measurement::attribute::create_apparent_power(epm, null_value);
    cluster::electrical_power_measurement::attribute::create_rms_voltage(epm, null_value);
    cluster::electrical_power_measurement::attribute::create_rms_current(epm, null_value);
    cluster::electrical_power_measurement::attribute::create_frequency(epm, null_value);
    cluster::electrical_power_measurement::attribute::create_power_factor(epm, null_value);

    cluster::electrical_energy_measurement::config_t eem_config;
    eem_config.feature_flags = cluster::electrical_energy_measurement::feature::imported_energy::get_id() |
        cluster::electrical_energy_measurement::feature::cumulative_energy::get_id();
    if (!cluster::electrical_energy_measurement::create(ep, &eem_config, CLUSTER_FLAG_SERVER)) {
        ESP_LOGE(TAG, "Failed to create electrical energy measurement cluster");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Power/energy measurement added to endpoint %d", s_sensor_endpoint_id);
    return ESP_OK;
}

esp_err_t energy_meter_start()
{
    DeviceLayer::PlatformMgr().ScheduleWork(energy_cluster_init_work, 0);

    const bl0942_config_t cfg = {
        .uart_port = static_cast<uart_port_t>(CONFIG_BL0942_UART_PORT),
        .rx_gpio = CONFIG_BL0942_UART_RX_GPIO,
        .tx_gpio = CONFIG_BL0942_UART_TX_GPIO,
        .baud_rate = CONFIG_BL0942_UART_BAUD,
        .address = CONFIG_BL0942_ADDRESS,
    };
    esp_err_t err = bl0942_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(meter_task, "bl0942", 4096, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create meter task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
