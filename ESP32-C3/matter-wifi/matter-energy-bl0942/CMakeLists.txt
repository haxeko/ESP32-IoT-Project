# ESP32-C3 Matter over Wi-Fi smart plug (On/Off Plug-in Unit) + BL0942 power meter
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

if(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})
    if("${IDF_TARGET}" STREQUAL "esp32c3" OR "${IDF_TARGET}" STREQUAL "")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c3_devkit_m)
    else()
        message(FATAL_ERROR "This project targets esp32c3 only")
    endif()
endif(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})

# Firmware version, shown as "Firmware" in Apple Home (Basic Information SoftwareVersionString).
# Every build gets a new version: <major>.<minor>.<UTC build time yyMMddHHmm>.
# PROJECT_VER_NUMBER (Matter SoftwareVersion, used by OTA) is the same timestamp, so it always increases.
# The timestamp is taken when CMake configures: run "idf.py fullclean" (or build in a fresh tree, as CI does)
# to get a new version on a local rebuild.
set(FW_VERSION_MAJOR_MINOR "1.1")
string(TIMESTAMP FW_BUILD_TIME "%y%m%d%H%M" UTC)
set(PROJECT_VER "${FW_VERSION_MAJOR_MINOR}.${FW_BUILD_TIME}")
set(PROJECT_VER_NUMBER ${FW_BUILD_TIME})
message(STATUS "Firmware version: ${PROJECT_VER} (${PROJECT_VER_NUMBER})")

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)
include($ENV{ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake)

set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${ESP_MATTER_PATH}/device_hal/device"
    ${extra_components_dirs_append})

project(matter_plug_bl0942)

# build/matter_plug_bl0942_update.bin: OTA data + app, flash at 0x1D000 to update a plug in use over USB: keeps Wi-Fi and
# Matter pairing (NVS not touched). The merged image written at 0x0 erases NVS as well: Wi-Fi and pairing.
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
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
