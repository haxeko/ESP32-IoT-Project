# Matter over Thread device with five Occupancy Sensor endpoints fed by five iBeacons (BLE passive scanning), the
# beacons assigned from a web page served on a Wi-Fi SoftAP (CONFIG mode, triple click on BOOT). See README.md.
# This directory builds the ESP32-C6 version (Thread); the same code builds the ESP32-C3 version (Matter over Wi-Fi)
# in ESP32-C3/matter-ble-presence (see ble_presence.cmake).
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/ble_presence.cmake)

if(NOT IDF_TARGET STREQUAL "esp32c6")
    message(FATAL_ERROR "This directory builds the ESP32-C6 version (idf.py set-target esp32c6). "
                        "Build the ESP32-C3 version (Matter over Wi-Fi) in ESP32-C3/matter-ble-presence.")
endif()

project(${BLE_PRESENCE_PROJECT_NAME})

ble_presence_compile_options()
