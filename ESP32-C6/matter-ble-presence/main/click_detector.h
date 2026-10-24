/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Multi-click detection on a polled button. No ESP-IDF dependency (host tests); button_handler.cpp feeds it.
//
//   - debounce: a level counts once it held for debounce_ms; bounces in between are ignored
//   - a click is a complete press and release; a press longer than max_press_ms is a hold, never a click, and it
//     cancels the clicks counted so far (a long hold can never add up to several clicks)
//   - clicks_needed clicks with the last release within window_ms of the first press fire once; the next press
//     starts a new sequence. A sequence that runs out of time is dropped, the press in progress included.
//   - a button already pressed when the detector starts (or at boot) is ignored until it is released

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t debounce_ms;
    uint32_t window_ms;
    uint32_t max_press_ms;
    uint8_t clicks_needed;
} click_detector_config_t;

typedef struct {
    click_detector_config_t cfg;
    bool raw;            // last sampled level
    uint32_t raw_since_ms;
    bool pressed;        // debounced level
    bool armed;          // false until a release is seen after start or after a hold
    bool hold;           // current press already longer than max_press_ms
    uint32_t press_start_ms;
    uint32_t sequence_start_ms;
    uint8_t clicks;
} click_detector_t;

void click_detector_init(click_detector_t *cd, const click_detector_config_t *cfg, bool pressed_now, uint32_t now_ms);

// Feeds one sample (true = pressed). Returns true once when clicks_needed clicks completed in the window.
// Times may wrap: only differences are used.
bool click_detector_update(click_detector_t *cd, bool pressed, uint32_t now_ms);
