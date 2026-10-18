/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>

#include "ld2410_proto.h"

// Report payload: TYPE AA STATE MOV_CM(2) MOV_E STILL_CM(2) STILL_E DET_CM(2)
// [engineering: N_MOV N_STILL MOV_E[N_MOV+1] STILL_E[N_STILL+1] extra] 55 00
static constexpr size_t kBasicLen = 13;
static constexpr size_t kEngineeringStart = 11; // N_MOV, after the basic target data

hlk_err_t ld2410_decode_report(const uint8_t *p, size_t len, ld2410_report_t *out)
{
    if (len < kBasicLen) {
        return HLK_ERR_SHORT;
    }
    if (p[0] != LD2410_TYPE_ENGINEERING && p[0] != LD2410_TYPE_BASIC) {
        return HLK_ERR_TYPE;
    }
    if (p[1] != 0xAA || p[len - 2] != 0x55 || p[len - 1] != 0x00) {
        return HLK_ERR_MARKER;
    }
    memset(out, 0, sizeof(*out));
    out->type = p[0];
    out->state = p[2];
    out->moving_cm = hlk_u16(&p[3]);
    out->moving_energy = p[5];
    out->still_cm = hlk_u16(&p[6]);
    out->still_energy = p[8];
    out->detection_cm = hlk_u16(&p[9]);
    if (out->type == LD2410_TYPE_BASIC) {
        out->extra_len = (uint8_t)(len - kBasicLen);
        return HLK_OK;
    }

    if (len < kBasicLen + 2) {
        return HLK_ERR_SHORT;
    }
    const size_t moving_gates = (size_t)p[kEngineeringStart] + 1;
    const size_t still_gates = (size_t)p[kEngineeringStart + 1] + 1;
    const size_t energies = kEngineeringStart + 2;
    if (moving_gates > LD2410_GATES || still_gates > LD2410_GATES ||
        energies + moving_gates + still_gates + 2 > len) {
        return HLK_ERR_GATES;
    }
    out->moving_gates = (uint8_t)moving_gates;
    out->still_gates = (uint8_t)still_gates;
    memcpy(out->moving_gate_energy, &p[energies], moving_gates);
    memcpy(out->still_gate_energy, &p[energies + moving_gates], still_gates);
    const size_t extra = energies + moving_gates + still_gates;
    out->extra_len = (uint8_t)(len - 2 - extra);
    if (out->extra_len >= 2) {
        out->light = p[extra];
        out->out_pin = p[extra + 1];
    }
    return HLK_OK;
}

hlk_err_t ld2410_decode_params(const hlk_ack_t *ack, ld2410_params_t *out)
{
    // 0xAA, highest gate N, max moving gate, max still gate, moving thresholds
    // of gates 0..N, still thresholds of gates 0..N, no-one duration (2)
    const uint8_t *v = ack->value;
    if (ack->value_len < 4) {
        return HLK_ERR_SHORT;
    }
    if (v[0] != 0xAA) {
        return HLK_ERR_MARKER;
    }
    const size_t gates = (size_t)v[1] + 1;
    if (gates > LD2410_GATES) {
        return HLK_ERR_GATES;
    }
    if (ack->value_len < 4 + 2 * gates + 2) {
        return HLK_ERR_SHORT;
    }
    memset(out, 0, sizeof(*out));
    out->gates = (uint8_t)gates;
    out->max_moving_gate = v[2];
    out->max_still_gate = v[3];
    memcpy(out->moving_threshold, &v[4], gates);
    memcpy(out->still_threshold, &v[4 + gates], gates);
    out->no_one_duration_s = hlk_u16(&v[4 + 2 * gates]);
    return HLK_OK;
}

hlk_err_t ld2410_decode_resolution(const hlk_ack_t *ack, uint16_t *gate_cm)
{
    if (ack->value_len < 2) {
        return HLK_ERR_SHORT;
    }
    switch (hlk_u16(ack->value)) {
    case 0:
        *gate_cm = 75;
        return HLK_OK;
    case 1:
        *gate_cm = 20;
        return HLK_OK;
    default:
        return HLK_ERR_VALUE;
    }
}

hlk_err_t ld2410_decode_aux_control(const hlk_ack_t *ack, ld2410_aux_control_t *out)
{
    if (ack->value_len < 3) {
        return HLK_ERR_SHORT;
    }
    out->light_mode = ack->value[0];
    out->light_threshold = ack->value[1];
    out->out_idle_level = ack->value[2];
    return out->light_mode <= 2 && out->out_idle_level <= 1 ? HLK_OK : HLK_ERR_VALUE;
}

hlk_err_t ld2410_decode_noise_status(const hlk_ack_t *ack, uint16_t *status)
{
    if (ack->value_len < 2) {
        return HLK_ERR_SHORT;
    }
    *status = hlk_u16(ack->value);
    return *status <= 2 ? HLK_OK : HLK_ERR_VALUE;
}

// Three (parameter word (2), value (4)) pairs, as commands 0x0060 and 0x0064 take them
static size_t three_words(uint32_t a, uint32_t b, uint32_t c, uint8_t out[18])
{
    const uint32_t values[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        uint8_t *w = &out[i * 6];
        hlk_put_u16(w, (uint16_t)i);
        w[2] = (uint8_t)(values[i] & 0xFF);
        w[3] = (uint8_t)((values[i] >> 8) & 0xFF);
        w[4] = (uint8_t)((values[i] >> 16) & 0xFF);
        w[5] = (uint8_t)((values[i] >> 24) & 0xFF);
    }
    return 18;
}

size_t ld2410_value_sensitivity(uint16_t gate, uint8_t moving, uint8_t still, uint8_t out[18])
{
    if ((gate >= LD2410_GATES && gate != 0xFFFF) || moving > LD2410_MAX_ENERGY || still > LD2410_MAX_ENERGY) {
        return 0;
    }
    return three_words(gate, moving, still, out);
}

size_t ld2410_value_max_gates(uint8_t max_moving_gate, uint8_t max_still_gate, uint16_t no_one_duration_s,
                              uint8_t out[18])
{
    if (max_moving_gate < 2 || max_moving_gate > 8 || max_still_gate < 2 || max_still_gate > 8) {
        return 0;
    }
    return three_words(max_moving_gate, max_still_gate, no_one_duration_s, out);
}
