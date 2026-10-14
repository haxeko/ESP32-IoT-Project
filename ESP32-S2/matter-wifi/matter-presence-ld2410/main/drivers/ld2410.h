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

#include "ld2410_parser.h"

// Latest target report from the HLK-LD2410 24 GHz presence radar.
typedef struct {
    ld2410_report_t report;
    int64_t timestamp_ms;   // esp_timer time of the last valid frame
} ld2410_target_t;

esp_err_t ld2410_init(int rx_pin, int tx_pin);

// Returns false when no valid frame has been received yet.
bool ld2410_get_target(ld2410_target_t *out);
