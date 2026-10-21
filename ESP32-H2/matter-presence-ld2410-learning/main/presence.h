/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <drivers/ld2410.h>

// Someone is there: a moving or still target (target states 0-3 only, see
// ld2410_target_t), and when max_cm is not 0 only targets at max_cm or closer
// (CONFIG_PRESENCE_MAX_DISTANCE_CM).
inline bool presence_from_target(const ld2410_target_t *t, uint16_t max_cm)
{
    if (max_cm == 0) {
        return t->moving || t->still;
    }
    return (t->moving && t->moving_cm <= max_cm) || (t->still && t->still_cm <= max_cm);
}
