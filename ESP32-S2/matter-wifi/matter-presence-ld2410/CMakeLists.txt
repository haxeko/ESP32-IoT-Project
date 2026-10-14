# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
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

project(matter_presence_ld2410)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various
# flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)

# Complete flash image named after the firmware version, so the files of different builds can be told apart:
#   build/matter_presence_ld2410-1.0.40.bin   bootloader + partition table + otadata + app, flash at 0x0
#                                             (same content as "idf.py merge-bin")
# Regenerated on every build; images left from earlier versions are removed at configure time.
# The app image (matter_presence_ld2410.bin) and the Matter OTA image (matter_presence_ld2410-ota.bin)
# keep their usual names.
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
add_dependencies(versioned_image gen_project_binary bootloader partition_table_bin blank_ota_data)
