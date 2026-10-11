/*
 * Matter Energy Meter: đọc BL0942 và cập nhật các cluster Matter
 *   - Electrical Power Measurement  (điện áp, dòng, công suất, tần số, hệ số công suất)
 *   - Electrical Energy Measurement (tổng điện năng tiêu thụ)
 *   - Illuminance Measurement (tuỳ chọn): công suất W hiển thị dưới dạng lux cho Apple Home
 */
#pragma once

#include <esp_err.h>
#include <esp_matter.h>

/* Tạo các endpoint Matter. Gọi trước esp_matter::start(). */
esp_err_t energy_meter_create_endpoints(esp_matter::node_t *node);

/* Khởi tạo BL0942 + task đo. Gọi sau esp_matter::start(). */
esp_err_t energy_meter_start();
