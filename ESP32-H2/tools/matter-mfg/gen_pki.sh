#!/bin/bash
# Development attestation material for VID 0xFFF1 / PID 0x8000 (the projects' test VID/PID).
#
#   gen_pki.sh            Certification Declarations (CD) only, for the SDK test PAI chain (recommended):
#                         pki/cd/CD_example_FFF1.der  the CD the default firmware already sends (SDK example,
#                                                     VID FFF1, PID 0x8000-0x8063, extracted from the SDK)
#                         pki/cd/CD_test_occupancy_0107.der, CD_test_plug_010A.der
#                                                     new CDs signed with the SDK TEST CD signing key
#   gen_pki.sh --private  also a private development chain pki/private/PAA -> PAI (VID FFF1, PID 8000).
#                         No controller trusts this PAA unless you install it (chip-tool --paa-trust-store-path).
#
# Never use any of this as a production identity: the SDK test keys are public.
set -euo pipefail
CRED=${ESP_MATTER_PATH:-/opt/espressif/esp-matter}/connectedhomeip/connectedhomeip/credentials
EXAMPLES=${ESP_MATTER_PATH:-/opt/espressif/esp-matter}/connectedhomeip/connectedhomeip/src/credentials/examples
CDKEY=$CRED/test/certification-declaration/Chip-Test-CD-Signing-Key.pem
CDCERT=$CRED/test/certification-declaration/Chip-Test-CD-Signing-Cert.pem
OUT=${OUT:-pki}
mkdir -p "$OUT/cd"

# CD compiled into the firmware today (CONFIG_EXAMPLE_DAC_PROVIDER, VID 0xFFF1 branch)
python3 - "$EXAMPLES/DeviceAttestationCredsExample.cpp" "$OUT/cd/CD_example_FFF1.der" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
tail = src.split('Fall back to the VID=0xFFF1 CD', 1)[1]
body = re.search(r'kCdForAllExamples\[\] = \{(.*?)\};', tail, re.S).group(1)
open(sys.argv[2], 'wb').write(bytes(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]{2}', body)))
PY
chip-cert print-cd "$OUT/cd/CD_example_FFF1.der" | head -3

# -f 1: format version, -l 0 -i 0: security level/info, -n 1: version, -t 0: certification type "development"
chip-cert gen-cd -K "$CDKEY" -C "$CDCERT" -O "$OUT/cd/CD_test_occupancy_0107.der" -f 1 -V FFF1 -p 8000 -d 0107 \
    -c CSA00000SWC00000-01 -l 0 -i 0 -n 1 -t 0
chip-cert gen-cd -K "$CDKEY" -C "$CDCERT" -O "$OUT/cd/CD_test_plug_010A.der" -f 1 -V FFF1 -p 8000 -d 010A \
    -c CSA00000SWC00000-02 -l 0 -i 0 -n 1 -t 0

if [ "${1:-}" = "--private" ]; then
    mkdir -p "$OUT/private"
    chip-cert gen-att-cert -t a -c "Dev PAA (not trusted by controllers)" -V FFF1 \
        -o "$OUT/private/PAA-Cert.pem" -O "$OUT/private/PAA-Key.pem" -f "2026-01-01 00:00:00" -l 4294967295
    chip-cert gen-att-cert -t i -c "Dev PAI" -V FFF1 -P 8000 -C "$OUT/private/PAA-Cert.pem" -K "$OUT/private/PAA-Key.pem" \
        -o "$OUT/private/PAI-Cert.pem" -O "$OUT/private/PAI-Key.pem" -f "2026-01-01 00:00:00" -l 4294967295
    echo "Private chain in $OUT/private (keep PAA-Key.pem / PAI-Key.pem offline)"
fi
echo "Done: $OUT"
