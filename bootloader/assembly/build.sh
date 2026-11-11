#!/bin/sh
# Build the bare-metal UART monitor: boot.s -> bootstrap.elf -> bootstrap.bin
#
# Load from BOLT with:
#   load -loader=raw -addr=0x01000000 usbdisk0:bootstrap.bin
#   go 0x01000000
#
# Override the toolchain prefix with CROSS=... if it lives elsewhere.
set -e

CROSS=${CROSS:-/opt/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-}
LOAD_ADDR=0x01000000

cd "$(dirname "$0")"

"${CROSS}as" boot.s -o boot.o
"${CROSS}ld" -Ttext=$LOAD_ADDR boot.o -o bootstrap.elf
"${CROSS}objcopy" -O binary bootstrap.elf bootstrap.bin
rm -f boot.o

echo "bootstrap.bin: $(wc -c < bootstrap.bin) bytes, load/go at $LOAD_ADDR"
