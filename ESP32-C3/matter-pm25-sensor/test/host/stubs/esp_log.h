#pragma once
#include "sim.h"
#define SIM_LOG(level, tag, fmt, ...) sim_log(level, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) SIM_LOG('E', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) SIM_LOG('W', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) SIM_LOG('I', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) do { } while (0)
