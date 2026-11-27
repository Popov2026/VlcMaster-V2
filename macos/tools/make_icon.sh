#!/bin/bash
# Crée Resources/AppIcon.icns à partir de l'icône Windows (res/app.ico) avec sips + iconutil.
set -euo pipefail
cd "$(dirname "$0")/.."
ICO=../res/app.ico
[ -f "$ICO" ] || { echo "pas de $ICO"; exit 1; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
sips -s format png "$ICO" --out "$TMP/icon.png" > /dev/null
SET="$TMP/AppIcon.iconset"
mkdir -p "$SET"
for s in 16 32 128 256 512; do
  sips -z $s $s "$TMP/icon.png" --out "$SET/icon_${s}x${s}.png" > /dev/null
  d=$((s * 2))
  sips -z $d $d "$TMP/icon.png" --out "$SET/icon_${s}x${s}@2x.png" > /dev/null
done
iconutil -c icns "$SET" -o Resources/AppIcon.icns
echo "Resources/AppIcon.icns créé"
