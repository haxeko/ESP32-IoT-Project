# Radar probe (Phase 1): records the frames of an HLK-LD2410 and an HLK-LD2450
# read by an ESP32-S3, for the capability measurements of docs/phase1.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components)
# Only main and what it needs
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(WARNING "The probe is written for the ESP32-S3 (idf.py set-target esp32s3); "
                    "other chips need other default pins")
endif()

project(radar_probe)
