#pragma once
#include "sim.h"
#define ESP_LOGE(tag, fmt, ...) sim_log('E', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) sim_log('W', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) sim_log('I', tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) sim_log('D', tag, fmt, ##__VA_ARGS__)
