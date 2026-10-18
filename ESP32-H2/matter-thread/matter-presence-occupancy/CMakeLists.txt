# ESP32-H2 Matter over Thread presence sensor (Occupancy Sensor) with an HLK-LD2410 radar
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

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

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    ${extra_components_dirs_append})

project(matter_presence_h2)

# build/matter_presence_h2_update.bin: OTA data + app, flash at 0x1D000 to update a sensor in use over USB: keeps the
# Thread network and Matter pairing (NVS not touched). The merged image written at 0x0 erases NVS as well: the Thread
# network and pairing.
idf_build_get_property(fw_build_dir BUILD_DIR)
idf_component_get_property(fw_esptool_cmd esptool_py ESPTOOLPY_CMD)
partition_table_get_partition_info(fw_otadata_offset "--partition-type data --partition-subtype ota" "offset")
partition_table_get_partition_info(fw_app_offset "--partition-boot-default" "offset")
if(NOT fw_otadata_offset OR NOT fw_app_offset)
    message(FATAL_ERROR "partitions.csv needs an otadata partition and an app partition")
endif()
add_custom_target(update_bin ALL
    # The initial (empty) OTA data makes the bootloader start ota_0, where the app is written, even after a
    # Matter OTA update has switched to ota_1; writing only the app at 0x20000 would then not be used.
    COMMAND ${fw_esptool_cmd} --chip ${IDF_TARGET} merge-bin --target-offset ${fw_otadata_offset}
            -o "${CMAKE_PROJECT_NAME}_update.bin"
            ${fw_otadata_offset} ota_data_initial.bin ${fw_app_offset} "${CMAKE_PROJECT_NAME}.bin"
    WORKING_DIRECTORY "${fw_build_dir}"
    COMMENT "USB update image (keeps the pairing): ${CMAKE_PROJECT_NAME}_update.bin, flash at ${fw_otadata_offset}"
    VERBATIM)
add_dependencies(update_bin gen_project_binary blank_ota_data)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various
# flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
