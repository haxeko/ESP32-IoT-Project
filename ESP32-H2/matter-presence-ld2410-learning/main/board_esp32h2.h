/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// ESP32-H2 SuperMini or ESP32-H2-DevKitM-1: Matter over Thread on the
// 802.15.4 radio, commissioning over BLE. Pins in sdkconfig.defaults.esp32h2:
// LD2410 on GPIO12 (radar TX) / GPIO13 (radar RX), BOOT button GPIO9, WS2812
// LED GPIO8 (both boards).
//
// Other pins to keep in mind: strapping pins GPIO2, GPIO3, GPIO8, GPIO9,
// GPIO25; console UART0 GPIO23 (RX) / GPIO24 (TX); USB-Serial-JTAG GPIO26 /
// GPIO27 (secondary console).

#pragma once

#define BOARD_NAME "ESP32-H2 SuperMini / DevKitM-1"
#define BOARD_NETWORK "Thread"

// GPIO15-GPIO21 connect the in-package flash of ESP32-H2. GPIO13/GPIO14 are
// the 32 kHz crystal pins, taken only when the RTC clock comes from an
// external crystal or oscillator (default: the internal RC).
#if CONFIG_RTC_CLK_SRC_EXT_CRYS || CONFIG_RTC_CLK_SRC_EXT_OSC
#define BOARD_PIN_RESERVED(gpio) (((gpio) >= 15 && (gpio) <= 21) || (gpio) == 13 || (gpio) == 14)
#define BOARD_PIN_RESERVED_TEXT "flash pins (GPIO15-GPIO21) or 32 kHz crystal pins (GPIO13, GPIO14)"
#else
#define BOARD_PIN_RESERVED(gpio) ((gpio) >= 15 && (gpio) <= 21)
#define BOARD_PIN_RESERVED_TEXT "flash pins (GPIO15-GPIO21)"
#endif
