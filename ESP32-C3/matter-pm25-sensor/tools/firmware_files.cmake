# Run with "cmake -P" by the firmware_files target (tools/firmware_version.cmake)
# at the end of every build. build/firmware/ then only holds this version:
#   <NAME>_merged.bin  bootloader + partition table + everything up to the app,
#                      flash at 0x0. It also erases NVS (Wi-Fi and Matter
#                      pairing), so it is for a new or wiped device.
#   <NAME>_update.bin  OTA data + app, flash at its offset (0x1D000 here). NVS
#                      is not touched, so the device stays paired. Enough as
#                      long as the bootloader and partition table do not change.
# where <NAME> is e.g. haxeko_pm5003_button_v1.0.49_esp32c3.

include("${CMAKE_CURRENT_LIST_DIR}/firmware_version.cmake")

set(out_dir "${BUILD_DIR}/firmware")
file(REMOVE_RECURSE "${out_dir}")
file(MAKE_DIRECTORY "${out_dir}")

function(run_esptool)
    execute_process(COMMAND "${PYTHON}" -m esptool --chip ${TARGET} ${ARGN}
                    WORKING_DIRECTORY "${BUILD_DIR}"
                    OUTPUT_QUIET
                    RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "esptool ${ARGV0} failed (${result})")
    endif()
endfunction()

# Same as "idf.py merge-bin": the offsets come from build/flash_args.
run_esptool(merge_bin -o "${out_dir}/${NAME}_merged.bin" "@flash_args")

# Offsets of the OTA data and of the app, from build/flash_args.
file(STRINGS "${BUILD_DIR}/flash_args" flash_args)
foreach(line IN LISTS flash_args)
    if(line MATCHES "^(0x[0-9a-fA-F]+) (.+)$")
        if(CMAKE_MATCH_2 STREQUAL "ota_data_initial.bin")
            set(ota_data_offset ${CMAKE_MATCH_1})
        elseif(CMAKE_MATCH_2 STREQUAL APP_BIN)
            set(app_offset ${CMAKE_MATCH_1})
        endif()
    endif()
endforeach()
if(NOT app_offset)
    message(FATAL_ERROR "${APP_BIN} not found in build/flash_args")
endif()
if(ota_data_offset)
    # The initial OTA data selects ota_0, where the app is written, even if a
    # Matter OTA update had switched to ota_1.
    run_esptool(merge_bin --target-offset ${ota_data_offset} -o "${out_dir}/${NAME}_update.bin"
                ${ota_data_offset} ota_data_initial.bin ${app_offset} ${APP_BIN})
    set(update_offset ${ota_data_offset})
else()
    run_esptool(merge_bin --target-offset ${app_offset} -o "${out_dir}/${NAME}_update.bin"
                ${app_offset} ${APP_BIN})
    set(update_offset ${app_offset})
endif()

# The version is set when CMake configures the project (after each commit or
# checkout), so edits made after that are not in it.
firmware_uncommitted_changes("${PROJECT_DIR}" changes)
if(NOT changes STREQUAL "" AND NOT VERSION MATCHES "-dev$")
    message(WARNING "The firmware sources changed after the version ${VERSION} was set: commit "
                    "them, or run 'idf.py reconfigure' to mark this build ${VERSION}-dev.")
endif()

message(STATUS "Firmware ${VERSION}: build/firmware/${NAME}_merged.bin (new device, flash at 0x0), "
               "${NAME}_update.bin (keeps pairing, flash at ${update_offset})")
