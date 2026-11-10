#!/bin/bash
# Installs the latest PicoStation Multi House V3 release into /Applications without the
# quarantine flag, so Gatekeeper does not reject the ad-hoc signed app.
set -euo pipefail
url="https://github.com/orenatobr/PicoStation-MultiHouse-macOS/releases/latest/download/PicoStation-Multi-House-V3-macOS-arm64.zip"
name="PicoStation Multi House V3.app"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fL --progress-bar -o "$tmp/app.zip" "$url"
curl -fsSL -o "$tmp/app.zip.sha256" "$url.sha256"
[ "$(shasum -a 256 "$tmp/app.zip" | cut -d" " -f1)" = "$(cut -d" " -f1 "$tmp/app.zip.sha256")" ] || { echo "SHA-256 mismatch, aborting" >&2; exit 1; }
ditto -x -k "$tmp/app.zip" "$tmp/out"
app=$(find "$tmp/out" -maxdepth 2 -name "$name" | head -1)
[ -n "$app" ] || { echo "$name not found in the zip, aborting" >&2; exit 1; }
rm -rf "/Applications/$name"
ditto "$app" "/Applications/$name"
xattr -dr com.apple.quarantine "/Applications/$name" 2>/dev/null || true
echo "Installed: /Applications/$name"
