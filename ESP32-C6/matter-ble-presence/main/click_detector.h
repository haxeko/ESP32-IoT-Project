/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Multi-click detection on a polled button. No ESP-IDF dependency (host tests); button_handler.cpp feeds it.
//
//   - debounce: a level counts once it held for debounce_ms; bounces in between are ignored. Press and release
//     times are those of the level change itself, not of the end of the debounce.
//   - a click is a complete press and release; a press longer than max_press_ms is a hold, never a click, and it
//     cancels the clicks counted so far (a long hold can never add up to several clicks)
//   - clicks may be slow: each press only has to start within gap_ms of the previous release. A longer pause ends
//     the sequence and that press starts a new one, so there is no limit on the sequence as a whole.
//   - clicks_needed clicks fire once; the next press starts a new sequence
//   - a button already pressed when the detector starts (or at boot) is ignored until it is released

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t debounce_ms;
    uint32_t gap_ms;       // longest pause from a release to the next press of the same sequence
    uint32_t max_press_ms;
    uint8_t clicks_needed;
} click_detector_config_t;

typedef struct {
    click_detector_config_t cfg;
    bool raw;            // last sampled level
    uint32_t raw_since_ms;
    bool pressed;        // debounced level
    bool armed;          // false until a release is seen after start
    uint32_t press_start_ms;
    uint32_t release_ms; // release of the last click counted
    uint8_t clicks;      // clicks counted in the current sequence
} click_detector_t;

void click_detector_init(click_detector_t *cd, const click_detector_config_t *cfg, bool pressed_now, uint32_t now_ms);

// Feeds one sample (true = pressed). Returns true once when the clicks_needed-th click of a sequence is released.
// Times may wrap: only differences are used.
bool click_detector_update(click_detector_t *cd, bool pressed, uint32_t now_ms);
