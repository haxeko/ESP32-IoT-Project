/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// The chip of the board (flash 4 MB): its network for the logs, and the pins the radar, the button and the LED must
// not use, checked here at build time. The pins themselves are Kconfig options (main/Kconfig.projbuild) set by
// boards/<board>.defaults.
//
// ESP32-C3: Matter over Wi-Fi (station), commissioning over BLE. E.g. the SuperMini: LD2410C on GPIO3 (radar TX) /
// GPIO4 (radar RX), BOOT button GPIO9, blue LED GPIO8 (on when the pin is low). Other pins to keep in mind:
// strapping pins GPIO2, GPIO8, GPIO9 (GPIO8 must be high and GPIO9 low to enter download mode, so nothing else goes
// on GPIO8 and GPIO9); JTAG pads GPIO4-GPIO7 (free: JTAG goes through the USB port); UART0 GPIO20 (RX) / GPIO21 (TX):
// the ROM prints its boot messages on GPIO21, and boards without a USB port to the computer (c3-radar r45) log there.
//
// ESP32-H2: Matter over Thread on the 802.15.4 radio, commissioning over BLE. The H2 mini (ESP32-H2 SuperMini):
// LD2410C on GPIO11 (radar TX) / GPIO12 (radar RX), BOOT button GPIO9, WS2812 LED GPIO8. Other pins to keep in mind:
// strapping pins GPIO2, GPIO3, GPIO8, GPIO9, GPIO25; console UART0 GPIO23 (RX) / GPIO24 (TX); USB-Serial-JTAG
// GPIO26 / GPIO27 (secondary console).

#pragma once

#include <sdkconfig.h>

// Shown in the boot log (CONFIG_BOARD_NAME, set by boards/<board>.defaults)
#define BOARD_NAME CONFIG_BOARD_NAME

#if CONFIG_IDF_TARGET_ESP32C3
#if !CONFIG_ENABLE_WIFI_STATION
#error "ESP32-C3: Matter over Wi-Fi, CONFIG_ENABLE_WIFI_STATION must stay on"
#endif
#define BOARD_NETWORK "Wi-Fi"

// GPIO11-GPIO17 connect the flash (GPIO11 = VDD_SPI). GPIO18/GPIO19 are the USB D-/D+ (USB-Serial-JTAG: logs and
// flashing on the SuperMini and the WROOM devkit), kept free on every board.
constexpr bool board_pin_reserved(int gpio)
{
    return (gpio >= 11 && gpio <= 17) || gpio == 18 || gpio == 19;
}
#define BOARD_PIN_RESERVED_TEXT "flash pins (GPIO11-GPIO17) or USB pins (GPIO18, GPIO19)"

#elif CONFIG_IDF_TARGET_ESP32H2
#if !CONFIG_OPENTHREAD_ENABLED
#error "ESP32-H2: Matter over Thread, CONFIG_OPENTHREAD_ENABLED must stay on"
#endif
#define BOARD_NETWORK "Thread"

// GPIO15-GPIO21 connect the in-package flash of the ESP32-H2.
constexpr bool board_pin_reserved(int gpio)
{
    return gpio >= 15 && gpio <= 21;
}
#define BOARD_PIN_RESERVED_TEXT "flash pins (GPIO15-GPIO21)"

#else
#error "ESP32-C3 or ESP32-H2 boards only: tools/build.sh c3-<board> or h2-<board>"
#endif

static_assert(CONFIG_LD2410_UART_RX_PIN != CONFIG_LD2410_UART_TX_PIN, "LD2410 RX and TX must be different pins");
static_assert(!board_pin_reserved(CONFIG_LD2410_UART_RX_PIN) && !board_pin_reserved(CONFIG_LD2410_UART_TX_PIN),
              "LD2410 pins must not be " BOARD_PIN_RESERVED_TEXT);
// -1: no button
static_assert(CONFIG_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_RX_PIN &&
                  CONFIG_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_TX_PIN &&
                  !board_pin_reserved(CONFIG_RESET_BUTTON_GPIO),
              "Button pin conflicts with the radar or " BOARD_PIN_RESERVED_TEXT);
#if !CONFIG_STATUS_LED_TYPE_NONE
static_assert(CONFIG_STATUS_LED_GPIO != CONFIG_LD2410_UART_RX_PIN && CONFIG_STATUS_LED_GPIO != CONFIG_LD2410_UART_TX_PIN &&
                  CONFIG_STATUS_LED_GPIO != CONFIG_RESET_BUTTON_GPIO && !board_pin_reserved(CONFIG_STATUS_LED_GPIO),
              "Status LED pin conflicts with the radar, the button or " BOARD_PIN_RESERVED_TEXT);
#endif
