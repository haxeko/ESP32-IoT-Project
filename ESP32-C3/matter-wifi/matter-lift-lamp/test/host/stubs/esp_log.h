// Host stub of ESP-IDF esp_log.h: logs are dropped (the arguments are still type-checked by the compiler).
#pragma once

#include <stdio.h>

#define ESP_HOST_LOG(tag, ...)                                                                                         \
    do {                                                                                                               \
        if (0) {                                                                                                       \
            printf(__VA_ARGS__);                                                                                       \
        }                                                                                                              \
        (void)(tag);                                                                                                   \
    } while (0)

#define ESP_LOGE(tag, ...) ESP_HOST_LOG(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ESP_HOST_LOG(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ESP_HOST_LOG(tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) ESP_HOST_LOG(tag, __VA_ARGS__)
