#include "fan_record.h"

void fan_record_encode(const fan_state_t *state, uint8_t buf[FAN_RECORD_SIZE])
{
    buf[0] = FAN_RECORD_VERSION;
    buf[1] = state->speed;
    buf[2] = state->last_speed;
    buf[3] = state->swing ? 1 : 0;
    buf[4] = state->natural ? 1 : 0;
}

bool fan_record_decode(const uint8_t *buf, size_t len, fan_state_t *state)
{
    if (buf == NULL || len == 0) {
        return false;
    }
    const uint8_t version = buf[0];
    const bool v1 = version == 1 && len == FAN_RECORD_V1_SIZE;
    const bool current = version >= FAN_RECORD_VERSION && len >= FAN_RECORD_SIZE;
    if (!v1 && !current) {
        return false;
    }
    fan_state_t st;
    st.speed = buf[1];
    st.last_speed = buf[2];
    st.swing = buf[3] != 0;
    st.natural = current && buf[4] != 0; /* version 1: no natural wind */
    fan_state_sanitize(&st);
    *state = st;
    return true;
}
