#!/bin/sh
# Produce the DFU deliverables for one link variant (hd or fd):
#
#   <variant>_factory.hex   SoftDevice + bootloader + app + settings, for the
#                           one-time ST-Link install (full chip erase)
#   <variant>_dfu.zip       signed app-only package for BLE updates
#                           (nRF Connect / nRF Toolbox "DFU")
#
# Needs: a built app (make LINK=...), a built bootloader
# (bootloader/build_bootloader.sh), the S130 hex, nrfutil 6.x and a signing
# key at dfu_key/dfu_private.pem.
#
# Usage: ./package_dfu.sh <hd|fd> [app-version]
#        APP=eload ./package_dfu.sh hd   (m365-eload dashboard build)

set -e

LINK=${1:-hd}
APPVER=${2:-1}

HERE=$(cd "$(dirname "$0")" && pwd)
SDK_ROOT=${SDK_ROOT:-$HERE/../../sdk/SDK12}
NRFUTIL=${NRFUTIL:-"python -m nordicsemi"}
SD_HEX="$SDK_ROOT/components/softdevice/s130/hex/s130_nrf51_2.0.1_softdevice.hex"
KEY="$HERE/dfu_key/dfu_private.pem"

APP=${APP:-vesc}
if [ "$APP" = eload ]; then
	BASE=g30_eload_dash
	OUT="$HERE/build/eload_$LINK"
else
	BASE=g30_vesc_dash
	OUT="$HERE/build/$LINK"
fi
APP_HEX="$OUT/${BASE}_$LINK.hex"
BL_HEX="$HERE/build/bl/bootloader.hex"

# S130 2.0.1 firmware id, for the DFU init-packet SoftDevice requirement.
SD_REQ=0x87
BL_VERSION=1

for f in "$APP_HEX" "$BL_HEX" "$SD_HEX" "$KEY"; do
	[ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

# 1. Bootloader settings page that marks the flashed app as valid (CRC).
$NRFUTIL settings generate \
	--family NRF51 \
	--application "$APP_HEX" \
	--application-version "$APPVER" \
	--bootloader-version "$BL_VERSION" \
	--bl-settings-version 1 \
	"$OUT/settings.hex"

# nrfutil also writes a backup copy of the settings one page below (0x3EC00),
# which SDK 12 does not use and which lands inside the bootloader code. Keep
# only the real settings page at 0x3FC00.
python "$HERE/tools/hexfilter.py" "$OUT/settings.hex" 0x3FC00 0x40000 "$OUT/settings_main.hex"

# 2. One-time factory image: SoftDevice + bootloader + app + settings.
python "$HERE/tools/mergehex.py" "$SD_HEX" "$BL_HEX" "$APP_HEX" "$OUT/settings_main.hex" \
	"$OUT/${BASE}_${LINK}_factory.hex"

# 3. Signed app-only DFU package for BLE updates.
$NRFUTIL pkg generate \
	--hw-version 51 \
	--sd-req "$SD_REQ" \
	--application "$APP_HEX" \
	--application-version "$APPVER" \
	--key-file "$KEY" \
	"$OUT/${BASE}_${LINK}_dfu.zip"

echo "built:"
echo "  $OUT/${BASE}_${LINK}_factory.hex"
echo "  $OUT/${BASE}_${LINK}_dfu.zip"
