# Matter over Thread device with five Occupancy Sensor endpoints fed by five iBeacons (BLE passive scanning), the
# beacons assigned from a web page served on a Wi-Fi SoftAP (CONFIG mode, triple click on BOOT). ESP32-C6 only: the
# one chip of the family with Thread (802.15.4), BLE and Wi-Fi. See README.md.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif()

# Firmware version, shown by controllers as "Firmware" (SoftwareVersionString): 1.0.<git commit count>, as the other
# firmware of this repository. PROJECT_VER_NUMBER (SoftwareVersion) is the same count.
set(FIRMWARE_VERSION_BASE "1.0")
execute_process(COMMAND git rev-list --count HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE FIRMWARE_BUILD_NUMBER
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE git_result
                ERROR_QUIET)
if(NOT git_result EQUAL 0 OR FIRMWARE_BUILD_NUMBER STREQUAL "")
    message(WARNING "Not a git checkout, firmware build number set to 0")
    set(FIRMWARE_BUILD_NUMBER 0)
else()
    # Re-run CMake (and pick up the new version) after every commit or checkout.
    execute_process(COMMAND git rev-parse --absolute-git-dir
                    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                    OUTPUT_VARIABLE git_dir
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(EXISTS "${git_dir}/logs/HEAD")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/logs/HEAD")
    endif()
endif()
set(PROJECT_VER "${FIRMWARE_VERSION_BASE}.${FIRMWARE_BUILD_NUMBER}")
set(PROJECT_VER_NUMBER ${FIRMWARE_BUILD_NUMBER})
message(STATUS "Firmware version ${PROJECT_VER} (${PROJECT_VER_NUMBER})")

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(NOT IDF_TARGET STREQUAL "esp32c6")
    message(FATAL_ERROR "Target ${IDF_TARGET} is not supported: this firmware needs Thread, BLE and Wi-Fi on one chip "
                        "(idf.py set-target esp32c6)")
endif()

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components")

project(matter_ble_presence)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISC-V chips, project_include.cmake of esp-matter sets -Wno-format, but does not clear the flags that depend
# on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
