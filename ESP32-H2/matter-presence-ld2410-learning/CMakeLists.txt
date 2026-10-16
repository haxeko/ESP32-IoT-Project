# ESP32-H2 Matter over Thread presence sensor (Occupancy Sensor) with an HLK-LD2410 radar,
# with empty room learning of the radar thresholds from Home Assistant.
# The same code builds the ESP32-C3 Matter over Wi-Fi version: idf.py set-target esp32c3
# here, or the project in ESP32-C3/matter-wifi-presence (see presence.cmake).
cmake_minimum_required(VERSION 3.5)

include(${CMAKE_CURRENT_LIST_DIR}/presence.cmake)

project(${PRESENCE_PROJECT_NAME})

presence_compile_options()
