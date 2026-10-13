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
# Every build gets a new version: <major>.<minor>.<UTC build time yyMMddHHmm>-<git commit>[-dirty].
# "-dirty" marks uncommitted changes in this project directory at the last CMake configure.
# PROJECT_VER_NUMBER (Matter SoftwareVersion, used by OTA) is the timestamp alone, so it always increases.
# The version is taken when CMake configures. A new commit or checkout re-runs CMake on the next build;
# otherwise run "idf.py fullclean" (or build in a fresh tree, as CI does) to get a new version.
set(FW_VERSION_MAJOR_MINOR "1.0")
string(TIMESTAMP FW_BUILD_TIME "%y%m%d%H%M" UTC)

set(FW_GIT_COMMIT "nogit")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE FW_GIT_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE FW_GIT_RESULT ERROR_QUIET)
    if(FW_GIT_RESULT EQUAL 0)
        set(FW_GIT_COMMIT ${FW_GIT_HEAD})
        execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no -- .
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        OUTPUT_VARIABLE FW_GIT_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT "${FW_GIT_STATUS}" STREQUAL "")
            set(FW_GIT_COMMIT "${FW_GIT_COMMIT}-dirty")
        endif()

        # Re-run CMake (and so refresh the version) whenever HEAD moves to another commit
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --absolute-git-dir
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        OUTPUT_VARIABLE FW_GIT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
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

# esp_app_desc_t.version holds at most 31 characters: "1.0.2610121430-abcdef1-dirty" is 28
set(PROJECT_VER "${FW_VERSION_MAJOR_MINOR}.${FW_BUILD_TIME}-${FW_GIT_COMMIT}")
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

# Versioned copies of the build outputs, so the files of different builds can be told apart, e.g.
#   build/matter_relay_h2-1.0.2610131335-c8656ca.bin         app image (idf.py flash / write-flash @flash_args)
#   build/matter_relay_h2-1.0.2610131335-c8656ca-merged.bin  bootloader + partition table + otadata + app, flash at 0x0
#   build/matter_relay_h2-1.0.2610131335-c8656ca-ota.bin     Matter OTA image (CONFIG_CHIP_OTA_IMAGE_BUILD)
# Regenerated on every build; copies left from earlier versions are removed at configure time.
idf_build_get_property(fw_build_dir BUILD_DIR)
idf_build_get_property(fw_python PYTHON)
idf_build_get_property(fw_target IDF_TARGET)
set(FW_ARTIFACT_PREFIX "${fw_build_dir}/${CMAKE_PROJECT_NAME}-${PROJECT_VER}")
file(GLOB fw_stale_artifacts "${fw_build_dir}/${CMAKE_PROJECT_NAME}-[0-9]*.bin")
if(fw_stale_artifacts)
    file(REMOVE ${fw_stale_artifacts})
endif()

set(fw_artifact_commands
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_PROJECT_NAME}.bin" "${FW_ARTIFACT_PREFIX}.bin"
    COMMAND ${fw_python} -m esptool --chip ${fw_target} merge-bin -o "${FW_ARTIFACT_PREFIX}-merged.bin" @flash_args)
if(CONFIG_CHIP_OTA_IMAGE_BUILD)
    list(APPEND fw_artifact_commands
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_PROJECT_NAME}-ota.bin" "${FW_ARTIFACT_PREFIX}-ota.bin")
endif()
add_custom_target(versioned_artifacts ALL
    ${fw_artifact_commands}
    WORKING_DIRECTORY "${fw_build_dir}"
    COMMENT "Firmware ${PROJECT_VER}: ${FW_ARTIFACT_PREFIX}-merged.bin"
    VERBATIM)
add_dependencies(versioned_artifacts gen_project_binary bootloader partition_table_bin blank_ota_data)
if(CONFIG_CHIP_OTA_IMAGE_BUILD)
    add_dependencies(versioned_artifacts chip-ota-image)
endif()
