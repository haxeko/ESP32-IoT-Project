#!/bin/bash
# Run one of the scripts of this folder inside the esp-matter Docker image used by the firmware builds
# (ESP-IDF v6.0.2 + esp-matter 1.6.1: chip-cert, chip-tool and esp-matter-mfg-tool are preinstalled).
# The current directory is mounted as /work; outputs are written there.
#   ./in_docker.sh gen_pki.sh
#   ./in_docker.sh gen_factory_data.sh -t occupancy -n 2
set -euo pipefail
IMAGE=${IMAGE:-espressif/esp-matter:release-v1.6.1_idf_v6.0.2}
HERE=$(cd "$(dirname "$0")" && pwd)
script=$1
shift
docker run --rm -v "$PWD":/work -v "$HERE":/tools:ro -w /work "$IMAGE" \
    bash -c '. /opt/espressif/esp-idf/export.sh >/dev/null 2>&1; . /opt/espressif/esp-matter/export.sh >/dev/null 2>&1
             cd /work && bash /tools/"$0" "$@"' \
    "$script" "$@"
