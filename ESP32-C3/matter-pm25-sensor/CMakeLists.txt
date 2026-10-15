# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

# Firmware version, e.g. 1.0.49: see tools/firmware_version.cmake. Change
# FIRMWARE_VERSION_BASE for a major/minor release.
set(FIRMWARE_VERSION_BASE "1.0")
include(${CMAKE_CURRENT_LIST_DIR}/tools/firmware_version.cmake)
firmware_version(${CMAKE_CURRENT_LIST_DIR} ${FIRMWARE_VERSION_BASE} PROJECT_VER PROJECT_VER_NUMBER)
message(STATUS "Firmware version ${PROJECT_VER} (${PROJECT_VER_NUMBER})")

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    ${extra_components_dirs_append})

project(matter_pm25_sensor)

# build/firmware/<vendor>_<model>_<tag>_v<version>_<target>_merged.bin after
# every build. The tag tells these files apart from those of the radar version
# (ESP32-C3/matter-wifi/matter-pm25-sensor), which have the same model name.
set(FIRMWARE_FILE_TAG "button")
firmware_files_target(${CMAKE_CURRENT_LIST_DIR})

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various
# flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
