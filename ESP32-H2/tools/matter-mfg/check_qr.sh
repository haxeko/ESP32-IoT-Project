#!/bin/bash
# Decode a QR payload (MT:...) and print VID, PID, the 12-bit discriminator, passcode, flow and discovery
# capabilities, e.g. to compare a printed label with the board's serial log or onb_codes.csv.
# An 11-digit manual code also works but carries only the passcode and the top 4 discriminator bits
# ("Short discriminator" = discriminator >> 8; VID/PID print as 0): check labels by their QR code.
#   check_qr.sh MT:Y.K9042C00KA0648G00
# Build the payload of known values (discriminator 12 bit, rendezvous 2 = BLE, commissioning mode 0 = standard):
#   chip-tool payload generate-qrcode --vendor-id 0xFFF1 --product-id 0x8000 --discriminator 3840 \
#     --setup-pin-code 20202021 --version 0 --commissioning-mode 0 --rendezvous 2
#   -> MT:Y.K9042C00KA0648G00 (the SDK test code of the default firmware)
set -euo pipefail
chip-tool payload parse-setup-payload "$1"
