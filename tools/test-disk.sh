#!/bin/sh
# Builds and runs the macOS disk layer tests (platform/mac/tests/test_disk_mac.m).
#   tools/test-disk.sh            read-only safety checks, never writes to a device
#   tools/test-disk.sh --images   also creates, attaches (hdiutil), writes and detaches 64 MB
#                                 disk images (the only devices ever written)
set -e
cd "$(dirname "$0")/.."
BUILD="${BUILD:-$HOME/.cache/picostation-mh-build}"
mkdir -p "$BUILD/tests"
OUT="$BUILD/tests/test_disk_mac"
clang -std=gnu11 -D_DARWIN_C_SOURCE -fobjc-arc -g -O1 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Icore/include -Iplatform/mac \
  platform/mac/tests/test_disk_mac.m platform/mac/mh_disk_mac.m platform/mac/mh_disk_mac.c \
  core/src/base/*.c core/src/blockdev/*.c \
  -framework Foundation -framework DiskArbitration -framework IOKit -framework CoreFoundation \
  -o "$OUT"
if [ "$1" = "--images" ]; then
  MH_DISK_TEST_IMAGES=1 "$OUT"
else
  "$OUT"
fi
