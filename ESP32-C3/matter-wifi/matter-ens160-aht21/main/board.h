/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// ESP32-C3 boards (flash 4 MB), Matter over Wi-Fi (station), commissioning over BLE. The pins are Kconfig options
// (main/Kconfig.projbuild), by default those of the ESP32-C3 SuperMini: ENS160 + AHT21 module on I2C SDA GPIO5 /
// SCL GPIO6, BOOT button GPIO9, blue LED GPIO8 (on when the pin is low). Checked here at build time.
//
// Other pins to keep in mind: strapping pins GPIO2, GPIO8, GPIO9 (GPIO8 must be high and GPIO9 low to enter the
// download mode, so nothing else goes on GPIO8 and GPIO9); UART0 GPIO20 (RX) / GPIO21 (TX): the ROM prints its boot
// messages on GPIO21. GPIO4-GPIO7 are the JTAG pads, free since JTAG goes through the USB port.

#pragma once

#include <sdkconfig.h>

#if !CONFIG_IDF_TARGET_ESP32C3
#error "ESP32-C3 only: idf.py set-target esp32c3"
#endif
#if !CONFIG_ENABLE_WIFI_STATION
#error "Matter over Wi-Fi: CONFIG_ENABLE_WIFI_STATION must stay on"
#endif

// GPIO11-GPIO17 connect the flash (GPIO11 = VDD_SPI). GPIO18/GPIO19 are the USB D-/D+ (USB-Serial-JTAG: logs and
// flashing on the SuperMini), kept free.
constexpr bool board_pin_reserved(int gpio)
{
    return (gpio >= 11 && gpio <= 17) || gpio == 18 || gpio == 19;
}
#define BOARD_PIN_RESERVED_TEXT "flash pins (GPIO11-GPIO17) or USB pins (GPIO18, GPIO19)"

static_assert(CONFIG_SENSOR_I2C_SDA_GPIO != CONFIG_SENSOR_I2C_SCL_GPIO, "I2C SDA and SCL must be different pins");
static_assert(!board_pin_reserved(CONFIG_SENSOR_I2C_SDA_GPIO) && !board_pin_reserved(CONFIG_SENSOR_I2C_SCL_GPIO),
              "I2C pins must not be " BOARD_PIN_RESERVED_TEXT);
// -1: no button, no LED
static_assert(CONFIG_RESET_BUTTON_GPIO != CONFIG_SENSOR_I2C_SDA_GPIO &&
                  CONFIG_RESET_BUTTON_GPIO != CONFIG_SENSOR_I2C_SCL_GPIO &&
                  !board_pin_reserved(CONFIG_RESET_BUTTON_GPIO),
              "Button pin conflicts with the I2C bus or " BOARD_PIN_RESERVED_TEXT);
static_assert(CONFIG_STATUS_LED_GPIO < 0 ||
                  (CONFIG_STATUS_LED_GPIO != CONFIG_SENSOR_I2C_SDA_GPIO &&
                   CONFIG_STATUS_LED_GPIO != CONFIG_SENSOR_I2C_SCL_GPIO &&
                   CONFIG_STATUS_LED_GPIO != CONFIG_RESET_BUTTON_GPIO && !board_pin_reserved(CONFIG_STATUS_LED_GPIO)),
              "Status LED pin conflicts with the I2C bus, the button or " BOARD_PIN_RESERVED_TEXT);
