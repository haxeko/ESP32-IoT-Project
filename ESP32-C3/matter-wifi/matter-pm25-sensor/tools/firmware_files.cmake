# Run with "cmake -P" by the firmware_files target (tools/firmware_version.cmake)
# at the end of every build. build/firmware/ then only holds this version:
#   <NAME>_merged.bin  bootloader + partition table + app: flash at 0x0
#   <NAME>_app.bin     app only: flash at 0x20000, or input of an OTA image
# where <NAME> is e.g. haxeko_pm5003_v1.0.49_esp32c3.

include("${CMAKE_CURRENT_LIST_DIR}/firmware_version.cmake")

set(out_dir "${BUILD_DIR}/firmware")
file(REMOVE_RECURSE "${out_dir}")
file(MAKE_DIRECTORY "${out_dir}")

configure_file("${BUILD_DIR}/${APP_BIN}" "${out_dir}/${NAME}_app.bin" COPYONLY)

# Same as "idf.py merge-bin": the offsets come from build/flash_args.
execute_process(COMMAND "${PYTHON}" -m esptool --chip ${TARGET} merge_bin
                        -o "${out_dir}/${NAME}_merged.bin" "@flash_args"
                WORKING_DIRECTORY "${BUILD_DIR}"
                OUTPUT_QUIET
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "esptool merge_bin failed (${result})")
endif()

# The version is set when CMake configures the project (after each commit or
# checkout), so edits made after that are not in it.
firmware_uncommitted_changes("${PROJECT_DIR}" changes)
if(NOT changes STREQUAL "" AND NOT VERSION MATCHES "-dev$")
    message(WARNING "The firmware sources changed after the version ${VERSION} was set: commit "
                    "them, or run 'idf.py reconfigure' to mark this build ${VERSION}-dev.")
endif()

message(STATUS "Firmware ${VERSION}: build/firmware/${NAME}_merged.bin (flash at 0x0)")
