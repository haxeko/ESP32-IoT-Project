/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// The board of this build, chosen by the target (idf.py set-target): its name
// and network for the logs, and the pins the radar, the button and the LED
// must not use (checked at build time in app_main.cpp). The pins themselves
// are Kconfig options, set in sdkconfig.defaults.<target>.

#pragma once

#include <sdkconfig.h>

#if CONFIG_IDF_TARGET_ESP32H2
#include "board_esp32h2.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#include "board_esp32c3.h"
#else
#error "Unsupported target: build for esp32h2 or esp32c3 (idf.py set-target esp32h2 / esp32c3)"
#endif
