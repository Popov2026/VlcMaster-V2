#!/bin/bash
# Télécharge VLCKit (xcframework macOS, arm64 + x86_64) dans Vendor/ — même archive
# que le pod CocoaPods « VLCKit », vérifiée par son SHA-256.
set -euo pipefail
cd "$(dirname "$0")/.."

VER=3.7.3
URL="https://download.videolan.org/pub/cocoapods/prod/VLCKit-3.7.3-319ed2c0-79128878.tar.xz"
SHA=019afdae4e2e2d0f3ac325fac8f7ba0af25dca70b9d157df7d60db88e0be8e5d

if [ -d Vendor/VLCKit.xcframework ]; then
  echo "VLCKit déjà présent (Vendor/VLCKit.xcframework)"
  exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
echo "Téléchargement de VLCKit ${VER}…"
curl -fL --retry 3 -o "$TMP/vlckit.tar.xz" "$URL"
echo "$SHA  $TMP/vlckit.tar.xz" | shasum -a 256 -c -
mkdir -p "$TMP/x"
tar -xJf "$TMP/vlckit.tar.xz" -C "$TMP/x"
XC=$(find "$TMP/x" -maxdepth 4 -type d -name VLCKit.xcframework | head -1)
if [ -z "$XC" ]; then
  echo "VLCKit.xcframework introuvable dans l'archive :" >&2
  find "$TMP/x" -maxdepth 3 >&2
  exit 1
fi
mkdir -p Vendor
mv "$XC" Vendor/
[ -f "$(dirname "$XC")/COPYING.txt" ] && cp "$(dirname "$XC")/COPYING.txt" Vendor/VLCKit-COPYING.txt || true
echo "VLCKit : $(ls Vendor/VLCKit.xcframework)"
