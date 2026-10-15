# Firmware version and flash files, included by CMakeLists.txt.
#
# Version <major>.<minor>.<build>, e.g. 1.0.49:
# - <major>.<minor> is FIRMWARE_VERSION_BASE in CMakeLists.txt, changed by hand.
# - <build> is the number of commits in the repository: a commit always gets
#   the same version and a newer commit a higher one. It is also the Matter
#   SoftwareVersion that OTA updates compare.
# - "-dev" is appended when the firmware sources have uncommitted changes.
#
# This is the only source of the version: Apple Home and Home Assistant show it
# as "Firmware" (SoftwareVersionString), the boot log prints it, and every
# build writes the flash files named with it into build/firmware/.

# What goes into the firmware, relative to the project directory.
set(FIRMWARE_SOURCE_PATHS main CMakeLists.txt sdkconfig.defaults partitions.csv)

# Sets <out_var> to the uncommitted changes of the firmware sources ("git
# status" lines), or to an empty string when there are none or no git.
function(firmware_uncommitted_changes project_dir out_var)
    execute_process(COMMAND git status --porcelain -- ${FIRMWARE_SOURCE_PATHS}
                    WORKING_DIRECTORY "${project_dir}"
                    OUTPUT_VARIABLE changes
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE result
                    ERROR_QUIET)
    if(NOT result EQUAL 0)
        set(changes "")
    endif()
    set(${out_var} "${changes}" PARENT_SCOPE)
endfunction()

# Sets <version_var> (e.g. "1.0.49") and <number_var> (e.g. 49) from <base> (e.g. "1.0").
function(firmware_version project_dir base version_var number_var)
    execute_process(COMMAND git rev-list --count HEAD
                    WORKING_DIRECTORY "${project_dir}"
                    OUTPUT_VARIABLE build_number
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE result
                    ERROR_QUIET)
    set(suffix "")
    if(NOT result EQUAL 0 OR build_number STREQUAL "")
        message(WARNING "Not a git checkout, firmware build number set to 0")
        set(build_number 0)
    else()
        # Re-run CMake (and pick up the new version) after every commit or checkout.
        execute_process(COMMAND git rev-parse --absolute-git-dir
                        WORKING_DIRECTORY "${project_dir}"
                        OUTPUT_VARIABLE git_dir
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(EXISTS "${git_dir}/logs/HEAD")
            set_property(DIRECTORY "${project_dir}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/logs/HEAD")
        endif()
        # A shallow clone (e.g. CI with fetch-depth 1) only counts the fetched commits.
        execute_process(COMMAND git rev-parse --is-shallow-repository
                        WORKING_DIRECTORY "${project_dir}"
                        OUTPUT_VARIABLE git_shallow
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        if(git_shallow STREQUAL "true")
            message(WARNING "Shallow git clone: build number ${build_number} is too low, devices would "
                            "refuse this firmware as an OTA update. Run 'git fetch --unshallow' first.")
        endif()
        firmware_uncommitted_changes("${project_dir}" changes)
        if(NOT changes STREQUAL "")
            set(suffix "-dev")
        endif()
    endif()
    set(${version_var} "${base}.${build_number}${suffix}" PARENT_SCOPE)
    set(${number_var} ${build_number} PARENT_SCOPE)
endfunction()

# Adds the "firmware_files" target, run at the end of every build. It empties
# build/firmware/ and writes the flash files of this version (see
# tools/firmware_files.cmake). Must be called after project().
function(firmware_files_target project_dir)
    idf_build_get_property(python PYTHON)
    idf_build_get_property(build_dir BUILD_DIR)
    # e.g. "haxeko_pm5003_v1.0.49_esp32c3"
    string(TOLOWER "${CONFIG_DEVICE_VENDOR_NAME}_${CONFIG_DEVICE_PRODUCT_NAME}" prefix)
    string(REGEX REPLACE "[^a-z0-9_.-]+" "-" prefix "${prefix}")
    add_custom_target(firmware_files ALL
        COMMAND ${CMAKE_COMMAND}
                -D "PROJECT_DIR=${project_dir}"
                -D "BUILD_DIR=${build_dir}"
                -D "PYTHON=${python}"
                -D "TARGET=${IDF_TARGET}"
                -D "APP_BIN=${CMAKE_PROJECT_NAME}.bin"
                -D "NAME=${prefix}_v${PROJECT_VER}_${IDF_TARGET}"
                -D "VERSION=${PROJECT_VER}"
                -P "${project_dir}/tools/firmware_files.cmake"
        VERBATIM)
    add_dependencies(firmware_files gen_project_binary bootloader partition_table_bin)
    if(TARGET blank_ota_data)
        add_dependencies(firmware_files blank_ota_data)
    endif()
endfunction()
