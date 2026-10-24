/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "click_detector.h"

#include <string.h>

void click_detector_init(click_detector_t *cd, const click_detector_config_t *cfg, bool pressed_now, uint32_t now_ms)
{
    memset(cd, 0, sizeof(*cd));
    cd->cfg = *cfg;
    cd->raw = pressed_now;
    cd->raw_since_ms = now_ms;
    cd->pressed = pressed_now;
    cd->armed = !pressed_now;
}

static void reset_sequence(click_detector_t *cd)
{
    cd->clicks = 0;
}

bool click_detector_update(click_detector_t *cd, bool pressed, uint32_t now_ms)
{
    // Debounce
    if (pressed != cd->raw) {
        cd->raw = pressed;
        cd->raw_since_ms = now_ms;
    }
    bool press_edge = false;
    bool release_edge = false;
    if (cd->raw != cd->pressed && now_ms - cd->raw_since_ms >= cd->cfg.debounce_ms) {
        cd->pressed = cd->raw;
        press_edge = cd->pressed;
        release_edge = !cd->pressed;
    }

    // A sequence that ran out of time is over, whatever comes next. A press still down at that moment belonged to
    // it, so its release is not the first click of a new one.
    if (cd->clicks > 0 && now_ms - cd->sequence_start_ms > cd->cfg.window_ms) {
        reset_sequence(cd);
        if (cd->pressed && !press_edge) {
            cd->hold = true;
        }
    }

    if (press_edge) {
        cd->hold = false;
        cd->press_start_ms = now_ms;
        if (cd->armed && cd->clicks == 0) {
            cd->sequence_start_ms = now_ms;
        }
        return false;
    }

    if (cd->pressed) {
        if (!cd->hold && now_ms - cd->press_start_ms > cd->cfg.max_press_ms) {
            cd->hold = true; // a hold: neither a click nor part of a sequence
            reset_sequence(cd);
        }
        return false;
    }

    if (!release_edge) {
        return false;
    }
    if (!cd->armed) {
        cd->armed = true; // first release after start-up or after a completed sequence
        return false;
    }
    if (cd->hold) {
        return false;
    }
    cd->clicks++;
    if (cd->clicks < cd->cfg.clicks_needed) {
        return false;
    }
    const bool in_window = now_ms - cd->sequence_start_ms <= cd->cfg.window_ms;
    reset_sequence(cd);
    return in_window;
}
