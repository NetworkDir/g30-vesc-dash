#!/bin/sh
# Build the secure DFU bootloader for the G30 dashboard.
#
# The bootloader is the stock nRF5 SDK 12.3 "bootloader_secure" example with
# three files overlaid from this directory:
#   dfu_public_key.c  - our DFU signing public key
#   main.c            - no pca10028 pin poking, lever-based DFU entry
#
# It needs the micro-ecc library built once:
#   (cd $SDK/external/micro-ecc && git clone https://github.com/kmackay/micro-ecc micro-ecc)
#   build $SDK/external/micro-ecc/nrf51_armgcc/armgcc  (arm-none-eabi toolchain)
#
# Usage: ./build_bootloader.sh [SDK_ROOT]
# Output: build/bootloader.hex (copied here)

set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SDK_ROOT=${1:-$HERE/../../../sdk/SDK12}
EX="$SDK_ROOT/examples/dfu/bootloader_secure"
MK="$EX/pca10028/armgcc"

if [ ! -f "$SDK_ROOT/external/micro-ecc/nrf51_armgcc/armgcc/micro_ecc_lib_nrf51.a" ]; then
	echo "micro-ecc library missing, see the header of this script" >&2
	exit 1
fi

# Overlay our files (keep a one-time backup of the originals)
for f in main.c dfu_public_key.c; do
	[ -f "$EX/$f.orig" ] || cp "$EX/$f" "$EX/$f.orig"
	cp "$HERE/$f" "$EX/$f"
done

( cd "$MK" && make -j8 )

mkdir -p "$HERE/../build/bl"
cp "$MK/_build/nrf51422_xxac_s130.hex" "$HERE/../build/bl/bootloader.hex"
echo "bootloader -> firmware/build/bl/bootloader.hex"
