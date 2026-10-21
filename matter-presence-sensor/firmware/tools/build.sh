#!/bin/sh
# Builds the firmware for one board (boards/<board>.defaults), with optional features (features/<feature>.defaults):
#   tools/build.sh <board> [feature ...]
#   tools/build.sh c3-wroom-devkit
#   tools/build.sh c3-radar-r45 unique_setup
#   tools/build.sh h2-mini
# Run from anywhere, with ESP-IDF and esp-matter set up (README, "Build"). Build directory build/<name>, where <name> is
# the board and its features joined by "-"; the files to flash or to hand to the OTA provider are copied to dist/:
#   presence-<name>-<version>.ota                    Matter OTA image (Home Assistant Matter Server)
#   presence-<name>-<version>_app_0x20000.bin        app, for a USB flash that keeps the commissioning
#   presence-<name>-<version>_ota_data_initial_0x1d000.bin
#   presence-<name>-<version>_merged.bin             whole flash from 0x0 (erases the NVS: commission again)
set -e
cd "$(dirname "$0")/.."
if [ $# -lt 1 ] || [ ! -f "boards/$1.defaults" ]; then
    echo "usage: tools/build.sh <board> [feature ...]; boards: $(ls boards | sed 's/\.defaults$//' | tr '\n' ' ')" >&2
    exit 1
fi
board=$1
shift
case "$board" in
c3-*) target=esp32c3 ;;
h2-*) target=esp32h2 ;;
*)
    echo "board $board: the name must start with c3- or h2-" >&2
    exit 1
    ;;
esac
name=$board
defaults="sdkconfig.defaults;boards/$board.defaults"
for feature in "$@"; do
    if [ ! -f "features/$feature.defaults" ]; then
        echo "unknown feature $feature; features: $(ls features | sed 's/\.defaults$//' | tr '\n' ' ')" >&2
        exit 1
    fi
    if [ "$feature" = factory ] && [ "$target" != esp32h2 ]; then
        echo "feature factory: ESP32-H2 boards only (the SDK test PAI only covers the product ID 0x8000)" >&2
        exit 1
    fi
    name="$name-$feature"
    defaults="$defaults;features/$feature.defaults"
done
dir=build/$name
idf.py -B "$dir" -D IDF_TARGET="$target" -D SDKCONFIG="$dir/sdkconfig" -D SDKCONFIG_DEFAULTS="$defaults" build
idf.py -B "$dir" -D IDF_TARGET="$target" -D SDKCONFIG="$dir/sdkconfig" -D SDKCONFIG_DEFAULTS="$defaults" \
    merge-bin -o merged.bin
version=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['project_version'])" \
    "$dir/project_description.json")
out=dist/presence-$name-$version
mkdir -p dist
cp "$dir/matter_presence_sensor-ota.bin" "$out.ota"
cp "$dir/matter_presence_sensor.bin" "${out}_app_0x20000.bin"
cp "$dir/ota_data_initial.bin" "${out}_ota_data_initial_0x1d000.bin"
cp "$dir/merged.bin" "${out}_merged.bin"
echo "Firmware $version for $board: $out.ota (and _app, _ota_data_initial, _merged)"
