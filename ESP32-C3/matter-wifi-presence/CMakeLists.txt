# ESP32-C3 SuperMini Matter over Wi-Fi presence sensor (Occupancy Sensor) with an HLK-LD2410C radar,
# with empty room learning of the radar thresholds from Home Assistant.
# Thin project: the code (main), the partition table and the sdkconfig defaults are those of
# ESP32-H2/matter-presence-ld2410-learning, built for esp32c3 (main/board_esp32c3.h,
# sdkconfig.defaults.esp32c3 there), then sdkconfig.defaults of this directory.
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/../../ESP32-H2/matter-presence-ld2410-learning/presence.cmake)

if(NOT IDF_TARGET STREQUAL "esp32c3")
    message(FATAL_ERROR "This project builds the ESP32-C3 version only (idf.py set-target esp32c3). "
                        "Build the ESP32-H2 version in ESP32-H2/matter-presence-ld2410-learning.")
endif()

project(${PRESENCE_PROJECT_NAME})

presence_compile_options()
