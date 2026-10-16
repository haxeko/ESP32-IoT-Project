/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// ESP32-C3 SuperMini: Matter over Wi-Fi (station), commissioning over BLE.
// ESP32-C3 has no 802.15.4 radio, so no Thread. Pins in
// sdkconfig.defaults.esp32c3: LD2410C on GPIO3 (radar TX) / GPIO4 (radar RX),
// BOOT button GPIO9, blue LED GPIO8 (on when the pin is low).
//
// Other pins to keep in mind: strapping pins GPIO2, GPIO8, GPIO9 (GPIO8 must
// be high and GPIO9 low to enter download mode); JTAG pads GPIO4-GPIO7 (free:
// JTAG goes through the USB port by default); UART0 GPIO20 (RX) / GPIO21 (TX),
// unused by the console here but the ROM prints its boot messages on GPIO21.

#pragma once

#define BOARD_NAME "ESP32-C3 SuperMini"
#define BOARD_NETWORK "Wi-Fi"

// GPIO11-GPIO17 connect the flash (GPIO11 = VDD_SPI). GPIO18/GPIO19 are the
// USB D-/D+ of the USB-C port, the only link to the computer on this board:
// console, logs and flashing (USB-Serial-JTAG).
#define BOARD_PIN_RESERVED(gpio) (((gpio) >= 11 && (gpio) <= 17) || (gpio) == 18 || (gpio) == 19)
#define BOARD_PIN_RESERVED_TEXT "flash pins (GPIO11-GPIO17) or USB pins (GPIO18, GPIO19)"
