# ESP32-C3 SuperMini Matter over Wi-Fi presence sensor (Occupancy Sensor 0x0107) with an HLK-LD2410C radar, with
# empty room learning, Range, Radar resolution and Radar Bluetooth as Home Assistant selects (Mode Select).
# ESP32-C3 only: idf.py set-target esp32c3
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif()

# Firmware version, shown by controllers as "Firmware" (SoftwareVersionString): 1.0.<git commit count>.
# PROJECT_VER_NUMBER (SoftwareVersion, compared by OTA) is the same count, so every new commit builds a newer
# version. Bump FIRMWARE_VERSION_BASE for a major/minor release.
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
# Branch claude/c3-wroom-esp-filter (experimental ESP32 energy filter): the version string ends in "-espfilter" so
# that this firmware is never mistaken for the stable 1.0.x one. The number compared by OTA stays the commit count.
set(FIRMWARE_VERSION_LABEL "-espfilter")
set(PROJECT_VER "${FIRMWARE_VERSION_BASE}.${FIRMWARE_BUILD_NUMBER}${FIRMWARE_VERSION_LABEL}")
set(PROJECT_VER_NUMBER ${FIRMWARE_BUILD_NUMBER})
message(STATUS "Firmware version ${PROJECT_VER} (${PROJECT_VER_NUMBER})")

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(NOT IDF_TARGET STREQUAL "esp32c3")
    message(FATAL_ERROR "Target ${IDF_TARGET} is not supported: this project builds the ESP32-C3 firmware only "
                        "(idf.py set-target esp32c3)")
endif()

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components")

# Output files build/matter_presence_c3_wifi.bin and build/matter_presence_c3_wifi-ota.bin, as in firmware 1.0.132
project(matter_presence_c3_wifi)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISC-V chips, project_include.cmake of esp-matter sets -Wno-format, but does not clear the flags that depend
# on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
