/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

// Latest target report from the HLK-LD2410 24 GHz presence radar.
typedef struct {
    bool moving;            // a moving target is detected
    bool still;             // a still (stationary) target is detected
    uint16_t moving_cm;     // distance of the moving target
    uint16_t still_cm;      // distance of the still target
    uint16_t detection_cm;  // "detection distance" reported by the radar
    int64_t timestamp_ms;   // esp_timer time of the last valid frame
} ld2410_target_t;

esp_err_t ld2410_init(int rx_pin, int tx_pin);

// Returns false when no valid frame has been received yet.
bool ld2410_get_target(ld2410_target_t *out);
