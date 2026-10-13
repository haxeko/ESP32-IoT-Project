# ESP32-C3 Matter over Wi-Fi RGB light (Extended Color Light), 30 x WS2812 on GPIO4
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

if(NOT "${IDF_TARGET}" STREQUAL "esp32c3" AND NOT "${IDF_TARGET}" STREQUAL "")
    message(FATAL_ERROR "This project targets esp32c3 only")
endif()

# Firmware version: <major>.<minor>.<UTC build time yyMMddHHmm>, PROJECT_VER_NUMBER (OTA) = same timestamp.
set(FW_VERSION_MAJOR_MINOR "1.0")
string(TIMESTAMP FW_BUILD_TIME "%y%m%d%H%M" UTC)
set(PROJECT_VER "${FW_VERSION_MAJOR_MINOR}.${FW_BUILD_TIME}")
set(PROJECT_VER_NUMBER ${FW_BUILD_TIME})
message(STATUS "Firmware version: ${PROJECT_VER} (${PROJECT_VER_NUMBER})")

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)

# The WS2812 strip (espressif/led_strip) and the button (espressif/button) are driven directly
# from main/, so esp-matter's device_hal is not needed.
set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components")

project(matter_rgb_light_ws2812)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
