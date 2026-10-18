/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdlib.h>
#include <string.h>

#include "ld2450_proto.h"

// Each target: X (2), Y (2), speed (2), distance resolution (2)
static constexpr size_t kTargetLen = 8;

hlk_err_t ld2450_decode_report(const uint8_t *payload, size_t len, ld2450_report_t *out)
{
    if (len < LD2450_TARGETS * kTargetLen) {
        return HLK_ERR_SHORT;
    }
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < LD2450_TARGETS; i++) {
        const uint8_t *p = &payload[i * kTargetLen];
        ld2450_target_t &t = out->target[i];
        for (size_t b = 0; b < kTargetLen; b++) {
            t.present = t.present || p[b] != 0;
        }
        if (!t.present) {
            continue;
        }
        t.x_mm = ld2450_decode_signed(&p[0]);
        t.y_mm = ld2450_decode_signed(&p[2]);
        t.speed_cm_s = ld2450_decode_signed(&p[4]);
        t.resolution_mm = hlk_u16(&p[6]);
        out->count++;
    }
    return HLK_OK;
}

bool ld2450_target_plausible(const ld2450_target_t *t)
{
    if (!t->present) {
        return true;
    }
    return t->y_mm >= 0 && t->y_mm <= LD2450_MAX_Y_MM && abs(t->x_mm) <= LD2450_MAX_ABS_X_MM &&
           abs(t->speed_cm_s) <= LD2450_MAX_ABS_SPEED_CM_S;
}

hlk_err_t ld2450_decode_tracking(const hlk_ack_t *ack, uint16_t *mode)
{
    if (ack->value_len < 2) {
        return HLK_ERR_SHORT;
    }
    *mode = hlk_u16(ack->value);
    return *mode == LD2450_TRACKING_SINGLE || *mode == LD2450_TRACKING_MULTI ? HLK_OK : HLK_ERR_VALUE;
}

hlk_err_t ld2450_decode_zones(const hlk_ack_t *ack, ld2450_zones_t *out)
{
    // Zone type (2), then x1, y1, x2, y2 (int16, two's complement) of each zone
    if (ack->value_len < 2 + LD2450_ZONES * 8) {
        return HLK_ERR_SHORT;
    }
    const uint8_t *v = ack->value;
    out->type = hlk_u16(v);
    for (int i = 0; i < LD2450_ZONES; i++) {
        const uint8_t *z = &v[2 + i * 8];
        out->zone[i].x1_mm = (int16_t)hlk_u16(&z[0]);
        out->zone[i].y1_mm = (int16_t)hlk_u16(&z[2]);
        out->zone[i].x2_mm = (int16_t)hlk_u16(&z[4]);
        out->zone[i].y2_mm = (int16_t)hlk_u16(&z[6]);
    }
    return out->type <= LD2450_ZONE_EXCLUDE ? HLK_OK : HLK_ERR_VALUE;
}

size_t ld2450_value_zones(const ld2450_zones_t *zones, uint8_t out[26])
{
    if (zones->type > LD2450_ZONE_EXCLUDE) {
        return 0;
    }
    hlk_put_u16(out, zones->type);
    for (int i = 0; i < LD2450_ZONES; i++) {
        uint8_t *z = &out[2 + i * 8];
        hlk_put_u16(&z[0], (uint16_t)zones->zone[i].x1_mm);
        hlk_put_u16(&z[2], (uint16_t)zones->zone[i].y1_mm);
        hlk_put_u16(&z[4], (uint16_t)zones->zone[i].x2_mm);
        hlk_put_u16(&z[6], (uint16_t)zones->zone[i].y2_mm);
    }
    return 26;
}
