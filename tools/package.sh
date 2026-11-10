#!/bin/sh
# Build the arm64 app bundle and zip it for a GitHub release.
# Usage: tools/package.sh [version]   (output: dist/PicoStation-Multi-House-V3-macOS-arm64.zip)
set -eu
cd "$(dirname "$0")/.."
VERSION="${1:-0.1.0}"
BUILD="${BUILD:-$HOME/.cache/picostation-mh-build}"
make app VERSION="$VERSION" BUILD="$BUILD"
APP="$BUILD/PicoStation Multi House V3.app"
mkdir -p dist
OUT="dist/PicoStation-Multi-House-V3-macOS-arm64.zip"
rm -f "$OUT"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$OUT"
shasum -a 256 "$OUT" | tee "$OUT.sha256"
