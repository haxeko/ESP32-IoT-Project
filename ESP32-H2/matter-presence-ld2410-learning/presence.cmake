# Build setup shared by the two projects of the LD2410 presence sensor, included
# before project() by their top-level CMakeLists.txt:
#   - this directory: ESP32-H2 Matter over Thread (idf.py set-target esp32h2),
#     or ESP32-C3 Matter over Wi-Fi (idf.py set-target esp32c3)
#   - ESP32-C3/matter-wifi-presence: ESP32-C3 only, with the code (main), the
#     partition table and the sdkconfig defaults of this directory
# Sets PRESENCE_PROJECT_NAME for project() and defines presence_compile_options()
# to call after it.

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

set(PRESENCE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Firmware version, shown by controllers as "Firmware" (SoftwareVersionString).
# The last number is the git commit count, so every new commit gets a new,
# increasing version; PROJECT_VER_NUMBER (SoftwareVersion, used for OTA) is the
# same count. Bump FIRMWARE_VERSION_BASE for a major/minor release.
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

# A project in another directory (ESP32-C3/matter-wifi-presence) builds the main
# component of this one. Its sdkconfig defaults: this directory's
# sdkconfig.defaults (and sdkconfig.defaults.<target>), then its own
# sdkconfig.defaults (the target and the paths of the shared partition table and
# CHIP project config), then any other file given with -D SDKCONFIG_DEFAULTS=...
# or the SDKCONFIG_DEFAULTS environment variable (relative to that project).
get_filename_component(presence_dir_real "${PRESENCE_DIR}" REALPATH)
get_filename_component(presence_source_dir_real "${CMAKE_SOURCE_DIR}" REALPATH)
set(presence_main_component "")
if(NOT presence_source_dir_real STREQUAL presence_dir_real)
    set(presence_main_component "${PRESENCE_DIR}/main")
    if(SDKCONFIG_DEFAULTS)
        set(presence_given "${SDKCONFIG_DEFAULTS}")
    elseif(DEFINED ENV{SDKCONFIG_DEFAULTS})
        set(presence_given "$ENV{SDKCONFIG_DEFAULTS}")
    else()
        set(presence_given "")
    endif()
    set(presence_defaults "${PRESENCE_DIR}/sdkconfig.defaults" "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
    foreach(presence_file ${presence_given})
        get_filename_component(presence_file_real "${presence_file}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
        if(NOT presence_file_real STREQUAL "${presence_dir_real}/sdkconfig.defaults" AND
           NOT presence_file_real STREQUAL "${presence_source_dir_real}/sdkconfig.defaults")
            list(APPEND presence_defaults "${presence_file}")
        endif()
    endforeach()
    set(SDKCONFIG_DEFAULTS ${presence_defaults})
endif()

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    ${presence_main_component}
    ${extra_components_dirs_append})

# The build output is named after the project: build/<name>.bin, build/<name>-ota.bin
if(IDF_TARGET STREQUAL "esp32h2")
    set(PRESENCE_PROJECT_NAME matter_presence_h2_learn)
elseif(IDF_TARGET STREQUAL "esp32c3")
    set(PRESENCE_PROJECT_NAME matter_presence_c3_wifi)
else()
    message(FATAL_ERROR "Target ${IDF_TARGET} is not supported, build for esp32h2 (Thread) or esp32c3 (Wi-Fi): "
                        "idf.py set-target esp32h2 / idf.py set-target esp32c3")
endif()

macro(presence_compile_options)
    idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
    idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
    # For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various
    # flags that depend on -Wformat
    idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
endmacro()
