# ESP32-H2-WROOM-03 (2 MB flash) Matter over Thread plug (On/Off Plug-in Unit): relay GPIO10, button GPIO4,
# status LED GPIO5, wall switch GPIO25
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

# Firmware version, shown as "Firmware" in Apple Home (Basic Information SoftwareVersionString):
# <major>.<minor>.<UTC build time yyMMddHHmm>, e.g. 1.0.2610131450. PROJECT_VER_NUMBER (Matter SoftwareVersion,
# used by OTA) is the same timestamp, so it always increases.
# The build time is taken when CMake configures. A new commit or checkout re-runs CMake on the next build;
# otherwise run "idf.py reconfigure" (or build in a fresh tree, as CI does) to get a new version.
set(FW_VERSION_MAJOR_MINOR "1.0")
string(TIMESTAMP FW_BUILD_TIME "%y%m%d%H%M" UTC)

# Re-run CMake (and so take a new build time) whenever git HEAD moves to another commit
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --absolute-git-dir
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE FW_GIT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE FW_GIT_RESULT ERROR_QUIET)
    if(FW_GIT_RESULT EQUAL 0)
        execute_process(COMMAND ${GIT_EXECUTABLE} symbolic-ref -q HEAD
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        OUTPUT_VARIABLE FW_GIT_REF OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FW_GIT_DIR}/HEAD")
        if(FW_GIT_REF AND EXISTS "${FW_GIT_DIR}/${FW_GIT_REF}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FW_GIT_DIR}/${FW_GIT_REF}")
        endif()
        # Git appends to HEAD's reflog on every commit/checkout/reset/pull, even with packed refs or in a
        # worktree (HEAD and logs/HEAD are per-worktree, under --absolute-git-dir)
        if(EXISTS "${FW_GIT_DIR}/logs/HEAD")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FW_GIT_DIR}/logs/HEAD")
        endif()
    endif()
endif()

set(PROJECT_VER "${FW_VERSION_MAJOR_MINOR}.${FW_BUILD_TIME}")
set(PROJECT_VER_NUMBER ${FW_BUILD_TIME})
message(STATUS "Firmware version: ${PROJECT_VER} (${PROJECT_VER_NUMBER})")

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)
include($ENV{ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake)

# examples/common (app_reset, app_bridge) is not used: only examples/common/utils headers, see main/CMakeLists.txt
set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${ESP_MATTER_PATH}/device_hal/device"
    ${extra_components_dirs_append})

project(matter_relay_h2)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)

# Complete flash image named after the firmware version, so the files of different builds can be told apart:
#   build/matter_relay_h2-1.0.2610131450.bin   bootloader + partition table + app, flash at 0x0
#                                                  (same content as "idf.py merge-bin")
# Regenerated on every build; images left from earlier versions are removed at configure time.
idf_build_get_property(fw_build_dir BUILD_DIR)
idf_build_get_property(fw_python PYTHON)
idf_build_get_property(fw_target IDF_TARGET)
set(FW_IMAGE "${fw_build_dir}/${CMAKE_PROJECT_NAME}-${PROJECT_VER}.bin")
file(GLOB fw_stale_images "${fw_build_dir}/${CMAKE_PROJECT_NAME}-[0-9]*.bin")
if(fw_stale_images)
    file(REMOVE ${fw_stale_images})
endif()
add_custom_target(versioned_image ALL
    COMMAND ${fw_python} -m esptool --chip ${fw_target} merge-bin -o "${FW_IMAGE}" @flash_args
    WORKING_DIRECTORY "${fw_build_dir}"
    COMMENT "Firmware ${PROJECT_VER}: ${FW_IMAGE}"
    VERBATIM)
add_dependencies(versioned_image gen_project_binary bootloader partition_table_bin)
