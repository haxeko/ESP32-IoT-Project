# ESP32-C3 Matter over Wi-Fi lift lamp (Extended Color Light): a step motor raises the upper part, the WS2812
# strip lights the part it exposes
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

if(NOT "${IDF_TARGET}" STREQUAL "esp32c3" AND NOT "${IDF_TARGET}" STREQUAL "")
    message(FATAL_ERROR "This project targets esp32c3 only")
endif()

# Firmware version: <major>.<minor>.<UTC build time yyMMddHHmm>, PROJECT_VER_NUMBER (OTA) = same timestamp.
set(FW_VERSION_MAJOR_MINOR "1.0")
string(TIMESTAMP FW_BUILD_TIME "%y%m%d%H%M" UTC)
set(PROJECT_VER "${FW_VERSION_MAJOR_MINOR}.${FW_BUILD_TIME}")
set(PROJECT_VER_NUMBER ${FW_BUILD_TIME})
message(STATUS "Firmware version: ${PROJECT_VER} (${PROJECT_VER_NUMBER})")

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)

# The WS2812 strip (espressif/led_strip), the step motor and the button (espressif/button) are driven directly
# from main/, so esp-matter's device_hal is not needed.
set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components")

project(matter_lift_lamp)

# Every build also writes copies of the firmware named with the version:
#   build/matter_lift_lamp_esp32c3_<version>.bin         app image (source of a Matter OTA image)
#   build/matter_lift_lamp_esp32c3_update_<version>.bin  OTA data + app, flash at 0x1D000: USB update that keeps
#                                                         Wi-Fi and Matter pairing (NVS not touched)
#   build/matter_lift_lamp_esp32c3_merged_<version>.bin  bootloader + partitions + app, flash at 0x0: new device
#                                                         (it also erases NVS: Wi-Fi and pairing)
idf_build_get_property(fw_build_dir BUILD_DIR)
idf_component_get_property(fw_esptool_cmd esptool_py ESPTOOLPY_CMD)
partition_table_get_partition_info(fw_otadata_offset "--partition-type data --partition-subtype ota" "offset")
partition_table_get_partition_info(fw_app_offset "--partition-boot-default" "offset")
if(NOT fw_otadata_offset OR NOT fw_app_offset)
    message(FATAL_ERROR "partitions.csv needs an otadata partition and an app partition")
endif()
set(FW_BIN_PREFIX "${CMAKE_PROJECT_NAME}_${IDF_TARGET}")
add_custom_target(versioned_bins ALL
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_PROJECT_NAME}.bin" "${FW_BIN_PREFIX}_${PROJECT_VER}.bin"
    # The initial (empty) OTA data makes the bootloader start ota_0, where the app is written, even after a
    # Matter OTA update has switched to ota_1; writing only the app at 0x20000 would then not be used.
    COMMAND ${fw_esptool_cmd} --chip ${IDF_TARGET} merge-bin --target-offset ${fw_otadata_offset}
            -o "${FW_BIN_PREFIX}_update_${PROJECT_VER}.bin"
            ${fw_otadata_offset} ota_data_initial.bin ${fw_app_offset} "${CMAKE_PROJECT_NAME}.bin"
    COMMAND ${fw_esptool_cmd} --chip ${IDF_TARGET} merge-bin -o "${FW_BIN_PREFIX}_merged_${PROJECT_VER}.bin" "@flash_args"
    WORKING_DIRECTORY "${fw_build_dir}"
    COMMENT "Firmware ${PROJECT_VER}: ${FW_BIN_PREFIX}_[update_|merged_]${PROJECT_VER}.bin"
    VERBATIM)
add_dependencies(versioned_bins gen_project_binary bootloader partition_table_bin blank_ota_data)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
