# Build setup shared by the two projects of the BLE presence firmware, included before project() by their top-level
# CMakeLists.txt:
#   - this directory: ESP32-C6, Matter over Thread (idf.py set-target esp32c6)
#   - ESP32-C3/matter-ble-presence: ESP32-C3, Matter over Wi-Fi (idf.py set-target esp32c3), built from the code
#     (main), the partition table and the sdkconfig defaults of this directory
# Sets BLE_PRESENCE_PROJECT_NAME for project() and defines ble_presence_compile_options() to call after it.

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif()

set(BLE_PRESENCE_DIR "${CMAKE_CURRENT_LIST_DIR}")

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

# A project in another directory (ESP32-C3/matter-ble-presence) builds the main component of this one. Its sdkconfig
# defaults: this directory's sdkconfig.defaults, then its own sdkconfig.defaults (target, network, paths of the shared
# partition table and CHIP project config), then any file given with -D SDKCONFIG_DEFAULTS=... or the
# SDKCONFIG_DEFAULTS environment variable (relative to that project). ESP-IDF adds sdkconfig.defaults.<target> after
# each of them when it exists (this directory: sdkconfig.defaults.esp32c6, the Thread settings).
get_filename_component(ble_presence_dir_real "${BLE_PRESENCE_DIR}" REALPATH)
get_filename_component(ble_presence_source_dir_real "${CMAKE_SOURCE_DIR}" REALPATH)
set(ble_presence_main_component "")
if(NOT ble_presence_source_dir_real STREQUAL ble_presence_dir_real)
    set(ble_presence_main_component "${BLE_PRESENCE_DIR}/main")
    if(SDKCONFIG_DEFAULTS)
        set(ble_presence_given "${SDKCONFIG_DEFAULTS}")
    elseif(DEFINED ENV{SDKCONFIG_DEFAULTS})
        set(ble_presence_given "$ENV{SDKCONFIG_DEFAULTS}")
    else()
        set(ble_presence_given "")
    endif()
    set(ble_presence_defaults "${BLE_PRESENCE_DIR}/sdkconfig.defaults" "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
    foreach(ble_presence_file ${ble_presence_given})
        get_filename_component(ble_presence_file_real "${ble_presence_file}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
        if(NOT ble_presence_file_real STREQUAL "${ble_presence_dir_real}/sdkconfig.defaults" AND
           NOT ble_presence_file_real STREQUAL "${ble_presence_source_dir_real}/sdkconfig.defaults")
            list(APPEND ble_presence_defaults "${ble_presence_file}")
        endif()
    endforeach()
    set(SDKCONFIG_DEFAULTS ${ble_presence_defaults})
endif()

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    ${ble_presence_main_component})

# The build output is named after the project: build/<name>.bin
if(IDF_TARGET STREQUAL "esp32c6")
    set(BLE_PRESENCE_PROJECT_NAME matter_ble_presence)
elseif(IDF_TARGET STREQUAL "esp32c3")
    set(BLE_PRESENCE_PROJECT_NAME matter_ble_presence_c3)
else()
    message(FATAL_ERROR "Target ${IDF_TARGET} is not supported: build the ESP32-C6 version (Matter over Thread) in "
                        "ESP32-C6/matter-ble-presence or the ESP32-C3 version (Matter over Wi-Fi) in "
                        "ESP32-C3/matter-ble-presence")
endif()

macro(ble_presence_compile_options)
    idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
    idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
    # For RISC-V chips, project_include.cmake of esp-matter sets -Wno-format, but does not clear the flags that depend
    # on -Wformat
    idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
endmacro()
