#include <fan_matter.h>

#include <atomic>

#include <esp_log.h>
#include <esp_timer.h>

#include <app/clusters/fan-control-server/CodegenIntegration.h>
#include <app/clusters/fan-control-server/FanControlCluster.h>
#include <app/clusters/fan-control-server/fan-control-delegate.h>
#include <platform/CHIPDeviceLayer.h>

#include <fan_ctrl.h>
#include <fan_logic.h>
#include <status_led.h>

using namespace esp_matter;
using namespace chip::app::Clusters;
using chip::Protocols::InteractionModel::Status;
namespace FC = chip::app::Clusters::FanControl;

static const char *TAG = "fan_matter";

#if CONFIG_FAN_MATTER_ROCKING
static constexpr const char *k_rocking_name = " Rocking";
#else
static constexpr const char *k_rocking_name = "";
#endif
#if CONFIG_FAN_MATTER_AIRFLOW_DIRECTION
static constexpr const char *k_airflow_name = " AirflowDirection";
#else
static constexpr const char *k_airflow_name = "";
#endif
#if CONFIG_FAN_MATTER_AUTO_NATURAL
static constexpr const char *k_auto_name = " Auto";
#else
static constexpr const char *k_auto_name = "";
#endif
#if CONFIG_FAN_MATTER_WIND_NATURAL
static constexpr const char *k_wind_name = " Wind";
#else
static constexpr const char *k_wind_name = "";
#endif

namespace {

uint16_t s_endpoint_id = chip::kInvalidEndpointId;

/* CHIP task only */
bool s_following;    /* the fan state has been written into the cluster once: follow controller changes */
bool s_writing;      /* our own writes into the cluster are in progress: their notifications are not commands */
bool s_on_requested; /* the controller write being processed is FanMode On (or Smart without Auto) */

/* Any task */
std::atomic<bool> s_started{false};        /* esp_matter::start() has returned: the CHIP task takes work */
std::atomic<bool> s_write_scheduled{false}; /* a write_state() is queued on the CHIP task */
esp_timer_handle_t s_retry_timer;           /* retries a write_state() that could not be queued */

void schedule_write();

/* A controller change for the fan task. When it cannot be queued, the cluster (which already took the new value)
 * is set back to what the fan does. */
void post(fan_cmd_type_t type, uint8_t arg)
{
    if (!fan_ctrl_post_type(type, arg, FAN_SRC_MATTER)) {
        schedule_write();
    }
}

/* Controller changes, called by FanControlCluster on the CHIP task */
class FanDelegate : public FC::Delegate {
public:
    Status HandleStep(FC::StepDirectionEnum direction, bool wrap, bool lowestOff) override
    {
        fan_cmd_t cmd = {};
        cmd.type = direction == FC::StepDirectionEnum::kDecrease ? FAN_CMD_STEP_DOWN : FAN_CMD_STEP_UP;
        cmd.wrap = wrap;
        cmd.lowest_off = lowestOff;
        return fan_ctrl_post(&cmd, FAN_SRC_MATTER) ? Status::Success : Status::Busy;
    }

    void OnFanDriveStateChanged(const FC::FanDriveState &state) override
    {
        if (!s_following || s_writing) {
            return;
        }
        const bool on_requested = s_on_requested;
        s_on_requested = false;
        uint8_t speed;
        if (state.mode == FC::FanModeEnum::kOff) {
            speed = 0;
        } else if (state.mode == FC::FanModeEnum::kAuto) {
            /* FanMode Auto (Apple Home Auto switch, FanMode Smart): natural wind */
            post(FAN_CMD_SET_NATURAL, 1);
            return;
        } else if (!state.percentSetting.IsNull()) {
            /* Snap to the nearest of 33 / 66 / 100 %; write_state() then moves the slider there */
            speed = fan_percent_to_speed(state.percentSetting.Value());
        } else if (!state.speedSetting.IsNull()) {
            speed = state.speedSetting.Value() > FAN_SPEED_COUNT ? FAN_SPEED_COUNT : state.speedSetting.Value();
        } else {
            return;
        }
        if (on_requested && speed) {
            /* FanMode On: the cluster chose High, resume the last speed instead (CONFIG_FAN_ON_RESUMES_LAST_SPEED).
             * A mode other than Auto is a manual setting: it ends natural wind. */
            if (fan_ctrl_get_state().natural) {
                post(FAN_CMD_SET_NATURAL, 0);
            }
            post(FAN_CMD_ON, 0);
            return;
        }
        /* A speed ends natural wind (fan_logic.h); Off keeps it for the next On */
        post(FAN_CMD_SET_SPEED, speed);
    }

    void OnRockSettingChanged(chip::BitMask<FC::RockBitmap> value) override
    {
        if (s_following && !s_writing) {
            post(FAN_CMD_SET_SWING, value.Raw() != 0);
        }
    }

    void OnWindSettingChanged(chip::BitMask<FC::WindBitmap> value) override
    {
        if (s_following && !s_writing) {
            post(FAN_CMD_SET_NATURAL, value.Has(FC::WindBitmap::kNaturalWind));
        }
    }

    void OnAirflowDirectionChanged(FC::AirflowDirectionEnum value) override
    {
        /* The rotation direction button is the oscillation switch: Reverse = on */
        if (s_following && !s_writing) {
            post(FAN_CMD_SET_SWING, value == FC::AirflowDirectionEnum::kReverse);
        }
    }
};

FanDelegate s_delegate;

/* CHIP task: write the fan state into the cluster. Also snaps PercentSetting to 33 / 66 / 100 %: the cluster then
 * derives FanMode (Low / Medium / High) and SpeedSetting (1 / 2 / 3) from it. Natural wind is FanMode Auto (the
 * cluster nulls PercentSetting and SpeedSetting); PercentCurrent and SpeedCurrent show the speed of the moment. */
void write_state(intptr_t)
{
    s_write_scheduled = false; /* a change from now on schedules another write */
    FanControlCluster *cluster = FC::FindClusterOnEndpoint(s_endpoint_id);
    if (cluster == nullptr) {
        ESP_LOGE(TAG, "Fan Control cluster not found on endpoint %u", s_endpoint_id);
        return;
    }
    const fan_state_t st = fan_ctrl_get_state();
    const uint8_t percent = fan_speed_to_percent(st.speed);
    const uint8_t drive = fan_ctrl_get_drive_speed();

    s_writing = true;
#if CONFIG_FAN_MATTER_AUTO_NATURAL
    const bool auto_mode = fan_state_natural_running(&st);
#else
    const bool auto_mode = false;
#endif
    if (auto_mode) {
        cluster->SetFanMode(FC::FanModeEnum::kAuto);
    } else {
        const chip::app::DataModel::Nullable<chip::Percent> setting = cluster->GetPercentSetting();
        if (setting.IsNull() || setting.Value() != percent) {
            /* Also leaves Auto: the cluster derives Low / Medium / High from it */
            cluster->SetPercentSetting(chip::app::DataModel::MakeNullable(percent));
        }
    }
    cluster->SetPercentCurrent(fan_speed_to_percent(drive));
    cluster->SetSpeedCurrent(drive);
#if CONFIG_FAN_MATTER_WIND_NATURAL
    cluster->SetWindSetting(st.natural ? chip::BitMask<FC::WindBitmap>(FC::WindBitmap::kNaturalWind)
                                       : chip::BitMask<FC::WindBitmap>());
#endif
#if CONFIG_FAN_MATTER_ROCKING
    cluster->SetRockSetting(st.swing ? chip::BitMask<FC::RockBitmap>(FC::RockBitmap::kRockLeftRight)
                                     : chip::BitMask<FC::RockBitmap>());
#endif
#if CONFIG_FAN_MATTER_AIRFLOW_DIRECTION
    cluster->SetAirflowDirection(st.swing ? FC::AirflowDirectionEnum::kReverse : FC::AirflowDirectionEnum::kForward);
#endif
    s_writing = false;

    if (!s_following) {
        s_following = true;
        ESP_LOGI(TAG, "Fan state written to Matter: %u%%%s, oscillation %s", percent,
                 st.natural ? " (natural wind)" : "", st.swing ? "on" : "off");
    }
}

/* Any task. Coalesced: the queued write reads the latest state when it runs. */
void schedule_write()
{
    if (!s_started || s_write_scheduled.exchange(true)) {
        return;
    }
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork(write_state) != CHIP_NO_ERROR) {
        /* CHIP event queue full: try again shortly, or the cluster would stay out of date until the next change */
        s_write_scheduled = false;
        ESP_LOGW(TAG, "Failed to schedule the Matter update, retrying");
        if (s_retry_timer) {
            esp_timer_start_once(s_retry_timer, 500 * 1000);
        }
    }
}

void retry_cb(void *)
{
    schedule_write();
}

/* Fan task, after every command (also unchanged ones: a 45 % write must still snap to 33 %) */
void on_fan_state(const fan_state_t *, fan_src_t, bool)
{
    schedule_write();
}

} // namespace

endpoint_t *fan_matter_create(node_t *node)
{
    endpoint::fan::config_t config;
    /* Identify blinks the status LED; never identify by switching the fan motor */
    config.identify.identify_type = chip::to_underlying(status_led_present() ? Identify::IdentifyTypeEnum::kVisibleIndicator
                                                                             : Identify::IdentifyTypeEnum::kNone);
    config.fan_control.fan_mode = chip::to_underlying(FC::FanModeEnum::kOff);
    /* 3 speeds: Low = speed 1 (33 %), Medium = speed 2 (66 %), High = speed 3 (100 %); Auto = natural wind */
#if CONFIG_FAN_MATTER_AUTO_NATURAL
    config.fan_control.fan_mode_sequence = chip::to_underlying(FC::FanModeSequenceEnum::kOffLowMedHighAuto);
#else
    config.fan_control.fan_mode_sequence = chip::to_underlying(FC::FanModeSequenceEnum::kOffLowMedHigh);
#endif
    config.fan_control.percent_setting = nullable<uint8_t>(0);
    config.fan_control.percent_current = 0;
    config.fan_control.delegate = &s_delegate;

    endpoint_t *endpoint = endpoint::fan::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
    if (endpoint == nullptr) {
        ESP_LOGE(TAG, "Failed to create the fan endpoint");
        return nullptr;
    }
    s_endpoint_id = endpoint::get_id(endpoint);

    /* Features must be in place before esp_matter::start(): the cluster reads its feature map once */
    cluster_t *fan_control = cluster::get(endpoint, FanControl::Id);
    cluster::fan_control::feature::multi_speed::config_t multi_speed;
    multi_speed.speed_max = FAN_SPEED_COUNT;
    esp_err_t err = cluster::fan_control::feature::multi_speed::add(fan_control, &multi_speed);
#if CONFIG_FAN_MATTER_ROCKING
    if (err == ESP_OK) {
        cluster::fan_control::feature::rocking::config_t rocking;
        rocking.rock_support = chip::to_underlying(FC::RockBitmap::kRockLeftRight);
        rocking.rock_setting = 0;
        err = cluster::fan_control::feature::rocking::add(fan_control, &rocking);
    }
#endif
#if CONFIG_FAN_MATTER_AIRFLOW_DIRECTION
    if (err == ESP_OK) {
        cluster::fan_control::feature::airflow_direction::config_t airflow;
        airflow.airflow_direction = chip::to_underlying(FC::AirflowDirectionEnum::kForward);
        err = cluster::fan_control::feature::airflow_direction::add(fan_control, &airflow);
    }
#endif
#if CONFIG_FAN_MATTER_AUTO_NATURAL
    if (err == ESP_OK) {
        err = cluster::fan_control::feature::fan_auto::add(fan_control);
    }
#endif
#if CONFIG_FAN_MATTER_WIND_NATURAL
    if (err == ESP_OK) {
        cluster::fan_control::feature::wind::config_t wind;
        wind.wind_support = chip::to_underlying(FC::WindBitmap::kNaturalWind);
        wind.wind_setting = 0;
        err = cluster::fan_control::feature::wind::add(fan_control, &wind);
    }
#endif
    if (err == ESP_OK) {
        err = cluster::fan_control::feature::step::add(fan_control);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add the Fan Control features: %s", esp_err_to_name(err));
        return nullptr;
    }

    fan_ctrl_set_listener(on_fan_state);
    ESP_LOGI(TAG, "Fan endpoint %u: %d speeds, oscillation switch:%s%s, natural wind:%s%s", s_endpoint_id,
             FAN_SPEED_COUNT, k_rocking_name, k_airflow_name, k_auto_name, k_wind_name);
    return endpoint;
}

void fan_matter_pre_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                           const esp_matter_attr_val_t *val)
{
    if (endpoint_id != s_endpoint_id || cluster_id != FanControl::Id) {
        return;
    }
    s_on_requested = false;
#if CONFIG_FAN_ON_RESUMES_LAST_SPEED
    if (attribute_id == FanControl::Attributes::FanMode::Id && val != nullptr) {
        const auto mode = static_cast<FC::FanModeEnum>(val->val.u8);
        /* With Auto, the cluster turns Smart into Auto (natural wind), handled as such */
        s_on_requested = mode == FC::FanModeEnum::kOn || mode == FC::FanModeEnum::kSmart;
    }
#else
    (void)attribute_id;
    (void)val;
#endif
}

void fan_matter_post_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id)
{
    (void)attribute_id;
    if (endpoint_id == s_endpoint_id && cluster_id == FanControl::Id) {
        /* Not consumed when the write changed nothing (FanMode On while already at High) */
        s_on_requested = false;
    }
}

void fan_matter_start()
{
    const esp_timer_create_args_t args = {
        .callback = retry_cb,
        .name = "fan_matter_retry",
    };
    if (esp_timer_create(&args, &s_retry_timer) != ESP_OK) {
        s_retry_timer = nullptr;
    }
    s_started = true;
    schedule_write();
}
