// Host stub of ESP-IDF esp_err.h for the host tests.
#pragma once

#include <stddef.h>

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105

static inline const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "error";
}

/* As on the device, a failed check stops the program (abort() -> reboot there). A test may define its own
 * ESP_ERROR_CHECK before including this file to record the failure instead. */
#ifndef ESP_ERROR_CHECK
#include <stdio.h>
#include <stdlib.h>
#define ESP_ERROR_CHECK(x)                                                                                             \
    do {                                                                                                               \
        esp_err_t err_chk_ = (x);                                                                                      \
        if (err_chk_ != ESP_OK) {                                                                                      \
            printf("ESP_ERROR_CHECK failed: 0x%x %s at %s:%d\n", err_chk_, #x, __FILE__, __LINE__);                    \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
#endif
