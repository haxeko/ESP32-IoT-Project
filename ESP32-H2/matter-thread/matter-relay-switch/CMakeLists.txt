# ESP32-H2 Matter over Thread relay switch (On/Off Plug-in Unit), relay on GPIO4
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

if(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})
    if("${IDF_TARGET}" STREQUAL "esp32h2" OR "${IDF_TARGET}" STREQUAL "")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32h2_devkit_c)
    else()
        message(FATAL_ERROR "This project targets esp32h2 only")
    endif()
endif(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})

# Firmware version, shown as "Firmware" in Apple Home (Basic Information SoftwareVersionString).
# Every build gets a new version: <major>.<minor>.<UTC build time yyMMddHHmm>.
# PROJECT_VER_NUMBER (Matter SoftwareVersion, used by OTA) is the same timestamp, so it always increases.
# The timestamp is taken when CMake configures: run "idf.py fullclean" (or build in a fresh tree, as CI does)
# to get a new version on a local rebuild.
set(FW_VERSION_MAJOR_MINOR "1.0")
string(TIMESTAMP FW_BUILD_TIME "%y%m%d%H%M" UTC)
set(PROJECT_VER "${FW_VERSION_MAJOR_MINOR}.${FW_BUILD_TIME}")
set(PROJECT_VER_NUMBER ${FW_BUILD_TIME})
message(STATUS "Firmware version: ${PROJECT_VER} (${PROJECT_VER_NUMBER})")

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)
include($ENV{ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake)

set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${ESP_MATTER_PATH}/device_hal/device"
    ${extra_components_dirs_append})

project(matter_relay_h2)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
