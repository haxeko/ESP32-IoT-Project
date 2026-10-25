# ESP32-C3 Matter over Wi-Fi device with five Occupancy Sensor endpoints fed by five iBeacons (BLE passive scanning),
# the beacons assigned from a web page served on a Wi-Fi SoftAP (CONFIG mode, triple click on BOOT).
# Thin project: the code (main), the partition table and the shared sdkconfig defaults are those of
# ESP32-C6/matter-ble-presence (the Matter over Thread version), built for esp32c3, then the sdkconfig.defaults of this
# directory (Wi-Fi, product ID, RAM). See README.md.
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/../../ESP32-C6/matter-ble-presence/ble_presence.cmake)

if(NOT IDF_TARGET STREQUAL "esp32c3")
    message(FATAL_ERROR "This project builds the ESP32-C3 version only (idf.py set-target esp32c3). "
                        "Build the ESP32-C6 version (Matter over Thread) in ESP32-C6/matter-ble-presence.")
endif()

project(${BLE_PRESENCE_PROJECT_NAME})

ble_presence_compile_options()
