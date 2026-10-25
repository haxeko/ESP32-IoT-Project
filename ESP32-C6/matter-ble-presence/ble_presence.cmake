# Build setup shared by the projects of the BLE presence firmware, included before project() by their top-level
# CMakeLists.txt:
#   - this directory: ESP32-C6, Matter over Thread (idf.py set-target esp32c6)
#   - ESP32-C3/matter-ble-presence: ESP32-C3, Matter over Wi-Fi
#   - two-chip version: ESP32-C3/ble-presence-scanner (no Matter) + ESP32-H2/matter-ble-presence-h2 (Matter node)
# The other projects build the code (main), the partition table and the sdkconfig defaults of this directory.
# Before including it a project may set BLE_PRESENCE_WITH_MATTER to OFF (no esp-matter components) and
# BLE_PRESENCE_PROJECT_NAME (name of the build output). Defines ble_presence_compile_options() to call after
# project().

if(NOT DEFINED BLE_PRESENCE_WITH_MATTER)
    set(BLE_PRESENCE_WITH_MATTER ON)
endif()
if(BLE_PRESENCE_WITH_MATTER AND NOT DEFINED ENV{ESP_MATTER_PATH})
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

if(BLE_PRESENCE_WITH_MATTER)
    set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
    set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)
endif()

# sdkconfig defaults, in this order: this directory's sdkconfig.defaults (shared by every version; ESP-IDF adds
# sdkconfig.defaults.<target> right after it when it exists: sdkconfig.defaults.esp32c6, the Thread settings of this
# directory's version), sdkconfig.defaults.matter for the projects with Matter, the sdkconfig.defaults of a project in
# another directory (target, role, network, product ID, paths of the shared partition table and CHIP project config),
# then any file given with -D SDKCONFIG_DEFAULTS=... or the SDKCONFIG_DEFAULTS environment variable.
get_filename_component(ble_presence_dir_real "${BLE_PRESENCE_DIR}" REALPATH)
get_filename_component(ble_presence_source_dir_real "${CMAKE_SOURCE_DIR}" REALPATH)
set(ble_presence_defaults "${BLE_PRESENCE_DIR}/sdkconfig.defaults")
if(BLE_PRESENCE_WITH_MATTER)
    list(APPEND ble_presence_defaults "${BLE_PRESENCE_DIR}/sdkconfig.defaults.matter")
endif()
set(ble_presence_main_component "")
if(NOT ble_presence_source_dir_real STREQUAL ble_presence_dir_real)
    set(ble_presence_main_component "${BLE_PRESENCE_DIR}/main")
    list(APPEND ble_presence_defaults "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
endif()
if(SDKCONFIG_DEFAULTS)
    set(ble_presence_given "${SDKCONFIG_DEFAULTS}")
elseif(DEFINED ENV{SDKCONFIG_DEFAULTS})
    set(ble_presence_given "$ENV{SDKCONFIG_DEFAULTS}")
else()
    set(ble_presence_given "")
endif()
foreach(ble_presence_file ${ble_presence_given})
    get_filename_component(ble_presence_file_real "${ble_presence_file}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
    set(ble_presence_known OFF)
    foreach(ble_presence_listed ${ble_presence_defaults})
        get_filename_component(ble_presence_listed_real "${ble_presence_listed}" REALPATH)
        if(ble_presence_file_real STREQUAL ble_presence_listed_real)
            set(ble_presence_known ON)
        endif()
    endforeach()
    if(NOT ble_presence_known)
        list(APPEND ble_presence_defaults "${ble_presence_file}")
    endif()
endforeach()
set(SDKCONFIG_DEFAULTS ${ble_presence_defaults})

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(BLE_PRESENCE_WITH_MATTER)
    set(EXTRA_COMPONENT_DIRS
        "${MATTER_SDK_PATH}/config/esp32/components"
        "${ESP_MATTER_PATH}/components"
        ${ble_presence_main_component})
else()
    set(EXTRA_COMPONENT_DIRS ${ble_presence_main_component})
endif()

# The build output is named after the project: build/<name>.bin
if(DEFINED BLE_PRESENCE_PROJECT_NAME)
    # set by the project
elseif(IDF_TARGET STREQUAL "esp32c6")
    set(BLE_PRESENCE_PROJECT_NAME matter_ble_presence)
elseif(IDF_TARGET STREQUAL "esp32c3")
    set(BLE_PRESENCE_PROJECT_NAME matter_ble_presence_c3)
else()
    message(FATAL_ERROR "Target ${IDF_TARGET} is not supported: build the ESP32-C6 version (Matter over Thread) in "
                        "ESP32-C6/matter-ble-presence or the ESP32-C3 version (Matter over Wi-Fi) in "
                        "ESP32-C3/matter-ble-presence")
endif()

macro(ble_presence_compile_options)
    idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-Wno-overloaded-virtual" APPEND)
    if(BLE_PRESENCE_WITH_MATTER)
        idf_build_set_property(CXX_COMPILE_OPTIONS "-DCHIP_HAVE_CONFIG_H" APPEND)
    endif()
    idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
    # For RISC-V chips, project_include.cmake of esp-matter sets -Wno-format, but does not clear the flags that depend
    # on -Wformat
    idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
endmacro()
