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
# CD_FILE / OUT are passed through when set. The outputs are given back to the calling user (the container runs as
# root) and the folders holding DAC private keys are made private.
docker run --rm -e CD_FILE -e OUT -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    -v "$PWD":/work -v "$HERE":/tools:ro -w /work "$IMAGE" \
    bash -c '. /opt/espressif/esp-idf/export.sh >/dev/null 2>&1; . /opt/espressif/esp-matter/export.sh >/dev/null 2>&1
             cd /work && bash /tools/"$0" "$@"; rc=$?
             find /work/factory -type d -name internal -exec chmod -R go-rwx {} + 2>/dev/null
             chmod -R go-rwx /work/${OUT:-pki}/private 2>/dev/null
             chown -R "$HOST_UID:$HOST_GID" /work/${OUT:-pki} /work/factory 2>/dev/null
             exit $rc' \
    "$script" "$@"
