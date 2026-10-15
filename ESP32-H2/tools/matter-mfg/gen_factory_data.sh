#!/bin/bash
# Per-board factory partitions ("fctry") for the manufacturing build (sdkconfig.defaults.factory).
# Each board gets its own DAC + DAC private key (signed by the PAI), random passcode, discriminator and SPAKE2+
# salt/verifier; the CD and the PAI certificate are the same for all boards. The partition also holds a random
# "serial-num" that these firmwares do not use: they report the eFuse-MAC serial number (main/device_info.cpp).
#
#   gen_factory_data.sh -t occupancy|plug -n COUNT [--private]
#     -t occupancy  ESP32-H2 presence sensors (device type 0x0107)
#     -t plug       ESP32-H2 relay / plug (device type 0x010A)
#     --private     sign the DACs with pki/private/PAI (gen_pki.sh --private) instead of the SDK test PAI
#
# Output: factory/<type>-<date>/fff1_8000/<uuid>/<uuid>-partition.bin   flash at the fctry offset
#                                                 <uuid>-onb_codes.csv    QR code + manual code
#                                                 <uuid>-qrcode.png       QR label
#         factory/<type>-<date>/fff1_8000/summary-*.csv                   one line per board
# Keep each partition.bin with its QR label: after erase-flash the same file must be flashed again.
# Run it in a working folder OUTSIDE the git repository: the output contains private keys and passcodes.
set -euo pipefail
TYPE="" COUNT=1 PRIVATE=0
while [ $# -gt 0 ]; do
    case $1 in
        -t) TYPE=$2; shift 2 ;;
        -n) COUNT=$2; shift 2 ;;
        --private) PRIVATE=1; shift ;;
        *) echo "unknown option $1"; exit 1 ;;
    esac
done
case $TYPE in
    occupancy) CD=pki/cd/CD_example_FFF1.der; NAME="Presence Sensor" ;;
    plug) CD=pki/cd/CD_example_FFF1.der; NAME="ESP32-H2" ;;
    *) echo "usage: $0 -t occupancy|plug -n COUNT [--private]"; exit 1 ;;
esac
# Default CD: the SDK example CD the firmware sends today (accepted by the same controllers as today).
# CD=pki/cd/CD_test_occupancy_0107.der / CD_test_plug_010A.der: signed with the SDK test CD key instead.
CD=${CD_FILE:-$CD}
[ -f "$CD" ] || { echo "$CD missing: run gen_pki.sh first"; exit 1; }
echo "Using CD: $CD" 

CRED=${ESP_MATTER_PATH:-/opt/espressif/esp-matter}/connectedhomeip/connectedhomeip/credentials
if [ $PRIVATE = 1 ]; then
    PAI_CERT=pki/private/PAI-Cert.pem PAI_KEY=pki/private/PAI-Key.pem PAA=pki/private/PAA-Cert.pem
else
    PAI_CERT=$CRED/test/attestation/Chip-Test-PAI-FFF1-8000-Cert.pem
    PAI_KEY=$CRED/test/attestation/Chip-Test-PAI-FFF1-8000-Key.pem
    PAA=$CRED/test/attestation/Chip-Test-PAA-FFF1-Cert.pem
fi
OUTDIR=factory/$TYPE-$(date +%Y%m%d-%H%M%S)

# VID/PID must stay 0xFFF1/0x8000: the firmware reports them from its own config (device_info.cpp)
esp-matter-mfg-tool --target esp32h2 -n "$COUNT" --pai -c "$PAI_CERT" -k "$PAI_KEY" -cd "$CD" \
    -v 0xFFF1 -p 0x8000 --vendor-name Haxeko --product-name "$NAME" --outdir "$OUTDIR"

echo "Checking every DAC chain against $PAA"
fail=0
for dac in $(find "$OUTDIR" -name DAC_cert.der); do
    if chip-cert validate-att-cert -d "$dac" -i "$(dirname "$dac")/PAI_cert.der" -a "$PAA"; then
        echo "  OK  $dac"
    else
        echo "  INVALID chain: $dac"; fail=1
    fi
done
echo "The DAC private keys are in <uuid>/internal: delete those folders once the boards are flashed."
echo "Boards (uuid = partition file name; label = QR code + manual code):"
python3 - "$OUTDIR"/fff1_8000 <<'PY' || fail=1
import collections, csv, glob, os, sys
seen = collections.Counter()
for f in sorted(glob.glob(os.path.join(sys.argv[1], '*', '*-onb_codes.csv'))):
    row = next(csv.DictReader(open(f)))
    seen[row['discriminator']] += 1
    print(f"  {os.path.basename(os.path.dirname(f))}  discriminator {row['discriminator']:>4}  {row['qrcode']}  {row['manualcode']}")
dup = [d for d, n in seen.items() if n > 1]
if dup:
    print(f"ERROR: discriminator repeated in this batch: {', '.join(dup)}; delete this output and run again")
    sys.exit(2)
PY
exit $fail
