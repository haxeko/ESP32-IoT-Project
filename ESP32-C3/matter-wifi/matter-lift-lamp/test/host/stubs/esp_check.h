// Host stub of ESP-IDF esp_check.h.
#pragma once

#include "esp_err.h"
#include "esp_log.h"

#define ESP_RETURN_ON_FALSE(cond, err, tag, ...)                                                                       \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            ESP_LOGE(tag, __VA_ARGS__);                                                                                \
            return (err);                                                                                              \
        }                                                                                                              \
    } while (0)

#define ESP_RETURN_ON_ERROR(x, tag, ...)                                                                               \
    do {                                                                                                               \
        esp_err_t err_rc_ = (x);                                                                                       \
        if (err_rc_ != ESP_OK) {                                                                                       \
            ESP_LOGE(tag, __VA_ARGS__);                                                                                \
            return err_rc_;                                                                                            \
        }                                                                                                              \
    } while (0)
