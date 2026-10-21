/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <app/reporting/reporting.h>
#include <clusters/GeneralDiagnostics/Ids.h>
#include <clusters/WiFiNetworkDiagnostics/Ids.h>
#include <esp_log.h>
#include <inttypes.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/DiagnosticDataProvider.h>
#include <sdkconfig.h>

#include "diag_report.h"

static const char *TAG = "diag_report";

static constexpr chip::System::Clock::Seconds32 kTickPeriod{ 60 };

static diag_report_policy_t s_policy;

static void schedule_tick();

// Matter task (Matter timer). Marking an attribute changed only queues it for the next report of each subscription
// that covers it; nothing is sent while no controller subscribes. The RSSI is read from the same place as the
// attribute (the access point record of the Wi-Fi driver).
static void tick(chip::System::Layer *, void *)
{
    int8_t rssi = 0;
    const bool connected = chip::DeviceLayer::GetDiagnosticDataProvider().GetWiFiRssi(rssi) == CHIP_NO_ERROR;
    const diag_report_due_t due = diag_report_tick(s_policy, connected, rssi);
    if (due.rssi) {
        ESP_LOGD(TAG, "Wi-Fi RSSI %d dBm reported", rssi);
        MatterReportingAttributeChangeCallback(chip::kRootEndpointId, chip::app::Clusters::WiFiNetworkDiagnostics::Id,
                                               chip::app::Clusters::WiFiNetworkDiagnostics::Attributes::Rssi::Id);
    }
    if (due.up_time) {
        MatterReportingAttributeChangeCallback(chip::kRootEndpointId, chip::app::Clusters::GeneralDiagnostics::Id,
                                               chip::app::Clusters::GeneralDiagnostics::Attributes::UpTime::Id);
    }
    schedule_tick();
}

static void schedule_tick()
{
    const CHIP_ERROR err = chip::DeviceLayer::SystemLayer().StartTimer(kTickPeriod, tick, nullptr);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Wi-Fi RSSI and uptime reports stopped, no timer: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void diag_report_start()
{
    s_policy = {};
    s_policy.rssi_step_db = CONFIG_DIAG_REPORT_RSSI_STEP_DB;
    s_policy.refresh_ticks = CONFIG_DIAG_REPORT_REFRESH_MIN;
    if (s_policy.rssi_step_db == 0 && s_policy.refresh_ticks == 0) {
        return;
    }
    if (s_policy.refresh_ticks == 0) {
        ESP_LOGI(TAG, "Wi-Fi RSSI reported again on a %d dB change", s_policy.rssi_step_db);
    } else if (s_policy.rssi_step_db == 0) {
        ESP_LOGI(TAG, "Wi-Fi RSSI and uptime reported again every %" PRIu32 " min", s_policy.refresh_ticks);
    } else {
        ESP_LOGI(TAG, "Wi-Fi RSSI reported again on a %d dB change, RSSI and uptime every %" PRIu32 " min",
                 s_policy.rssi_step_db, s_policy.refresh_ticks);
    }
    schedule_tick();
}
