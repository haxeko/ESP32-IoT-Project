# Two-chip version of the BLE presence firmware, ESP32-H2 side: the Matter over Thread node with five Occupancy
# Sensor endpoints, fed over a UART link by the ESP32-C3 scanner (ESP32-C3/ble-presence-scanner).
# Thin project: the code (main), the partition table and the shared sdkconfig defaults are those of
# ESP32-C6/matter-ble-presence, built for esp32h2 with CONFIG_PRESENCE_ROLE_MATTER_UART. See README.md.
cmake_minimum_required(VERSION 3.16)

set(BLE_PRESENCE_PROJECT_NAME matter_ble_presence_h2)
include(${CMAKE_CURRENT_LIST_DIR}/../../ESP32-C6/matter-ble-presence/ble_presence.cmake)

if(NOT IDF_TARGET STREQUAL "esp32h2")
    message(FATAL_ERROR "This project builds the ESP32-H2 Matter node of the two-chip version (idf.py set-target esp32h2)")
endif()

project(${BLE_PRESENCE_PROJECT_NAME})

ble_presence_compile_options()
