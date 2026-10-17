/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// ESP32-C3 SuperMini (flash 4 MB): Matter over Wi-Fi (station), commissioning over BLE. The pins are Kconfig
// options (main/Kconfig.projbuild): LD2410C on GPIO3 (radar TX) / GPIO4 (radar RX), BOOT button GPIO9, blue LED
// GPIO8 (on when the pin is low). Checked here at build time.
//
// Other pins to keep in mind: strapping pins GPIO2, GPIO8, GPIO9 (GPIO8 must be high and GPIO9 low to enter
// download mode, so nothing else goes on GPIO8 and GPIO9); JTAG pads GPIO4-GPIO7 (free: JTAG goes through the USB
// port); UART0 GPIO20 (RX) / GPIO21 (TX), unused by the console but the ROM prints its boot messages on GPIO21.

#pragma once

#include <sdkconfig.h>

#if !CONFIG_IDF_TARGET_ESP32C3
#error "This firmware is for the ESP32-C3 SuperMini: idf.py set-target esp32c3"
#endif
#if !CONFIG_ENABLE_WIFI_STATION
#error "Matter over Wi-Fi: CONFIG_ENABLE_WIFI_STATION must stay on"
#endif

#define BOARD_NAME "ESP32-C3 SuperMini"

// GPIO11-GPIO17 connect the flash (GPIO11 = VDD_SPI). GPIO18/GPIO19 are the USB D-/D+ of the USB-C port, the only
// link to the computer on this board: logs and flashing (USB-Serial-JTAG).
constexpr bool board_pin_reserved(int gpio)
{
    return (gpio >= 11 && gpio <= 17) || gpio == 18 || gpio == 19;
}
#define BOARD_PIN_RESERVED_TEXT "flash pins (GPIO11-GPIO17) or USB pins (GPIO18, GPIO19)"

static_assert(CONFIG_LD2410_UART_RX_PIN != CONFIG_LD2410_UART_TX_PIN, "LD2410 RX and TX must be different pins");
static_assert(!board_pin_reserved(CONFIG_LD2410_UART_RX_PIN) && !board_pin_reserved(CONFIG_LD2410_UART_TX_PIN),
              "LD2410 pins must not be " BOARD_PIN_RESERVED_TEXT);
// -1: no button
static_assert(CONFIG_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_RX_PIN &&
                  CONFIG_RESET_BUTTON_GPIO != CONFIG_LD2410_UART_TX_PIN &&
                  !board_pin_reserved(CONFIG_RESET_BUTTON_GPIO),
              "Button pin conflicts with the radar or " BOARD_PIN_RESERVED_TEXT);
static_assert(CONFIG_STATUS_LED_GPIO != CONFIG_LD2410_UART_RX_PIN && CONFIG_STATUS_LED_GPIO != CONFIG_LD2410_UART_TX_PIN &&
                  CONFIG_STATUS_LED_GPIO != CONFIG_RESET_BUTTON_GPIO && !board_pin_reserved(CONFIG_STATUS_LED_GPIO),
              "Status LED pin conflicts with the radar, the button or " BOARD_PIN_RESERVED_TEXT);
