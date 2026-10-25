# Two-chip version of the BLE presence firmware, ESP32-C3 side: BLE scanning of the five iBeacons and the web
# configuration page (triple click on BOOT), no Matter. The presence of the five slots goes over a UART link to the
# ESP32-H2 (ESP32-H2/matter-ble-presence-h2), which is the Matter over Thread node.
# Thin project: the code (main), the partition table and the shared sdkconfig defaults are those of
# ESP32-C6/matter-ble-presence, built for esp32c3 with CONFIG_PRESENCE_ROLE_SCANNER. See README.md.
cmake_minimum_required(VERSION 3.16)

set(BLE_PRESENCE_WITH_MATTER OFF)
set(BLE_PRESENCE_PROJECT_NAME ble_presence_scanner_c3)
include(${CMAKE_CURRENT_LIST_DIR}/../../ESP32-C6/matter-ble-presence/ble_presence.cmake)

if(NOT IDF_TARGET STREQUAL "esp32c3")
    message(FATAL_ERROR "This project builds the ESP32-C3 scanner of the two-chip version (idf.py set-target esp32c3)")
endif()

project(${BLE_PRESENCE_PROJECT_NAME})

ble_presence_compile_options()
