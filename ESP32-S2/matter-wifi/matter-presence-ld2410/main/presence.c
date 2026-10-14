/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "presence.h"

bool presence_radar_fresh(bool has_frame, int64_t last_frame_ms, int64_t now_ms, int64_t timeout_ms)
{
    const int64_t since_ms = has_frame ? last_frame_ms : 0;
    return now_ms - since_ms <= timeout_ms;
}

bool presence_detected(const ld2410_report_t *report, unsigned max_distance_cm)
{
    if (max_distance_cm == 0) {
        return report->moving || report->still;
    }
    return (report->moving && report->moving_cm <= max_distance_cm) ||
           (report->still && report->still_cm <= max_distance_cm);
}
