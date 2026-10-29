# ESP32-H2 Matter over Thread contact sensor (reed switch on a GPIO), powered from USB.
# Same boilerplate as esp-matter release/v1.6.1 examples/sensors, without examples/common (no app_reset/bsp).
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif()

# Basic Information SoftwareVersionString / SoftwareVersion. Raise PROJECT_VER_NUMBER for every release
# that will be delivered as a Matter OTA image.
set(PROJECT_VER "1.0.0")
set(PROJECT_VER_NUMBER 1)

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(NOT IDF_TARGET STREQUAL "esp32h2")
    message(FATAL_ERROR "This project is for ESP32-H2 (Matter over Thread) only: idf.py set-target esp32h2")
endif()

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components")

project(esp32h2_matter_contact_sensor)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISC-V chips, project_include.cmake sets -Wno-format, but does not clear various
# flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
