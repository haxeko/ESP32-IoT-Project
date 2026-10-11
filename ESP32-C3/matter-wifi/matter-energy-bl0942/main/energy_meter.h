/*
 * Đo điện bằng BL0942, cập nhật các cluster Matter trên endpoint ổ cắm:
 *   - Electrical Power Measurement  (điện áp, dòng, công suất, tần số, hệ số công suất)
 *   - Electrical Energy Measurement (tổng điện năng tiêu thụ)
 *   - Power Topology
 */
#pragma once

#include <esp_err.h>
#include <esp_matter.h>

/* Thêm device type Electrical Sensor và các cluster đo điện vào endpoint (ổ cắm).
 * Gọi trước esp_matter::start(). */
esp_err_t energy_meter_add_to_endpoint(esp_matter::endpoint_t *endpoint);

/* Khởi tạo BL0942 + task đo. Gọi sau esp_matter::start(). */
esp_err_t energy_meter_start();
