#!/bin/bash
# Construit HomeRun Player pour macOS (universel arm64 + x86_64) et le .dmg non signé.
#
#   ./build.sh [version]        (version par défaut : MARKETING_VERSION de project.yml)
#
# Prérequis : Xcode (xcodebuild), XcodeGen (brew install xcodegen).
# Résultat : dist/HomeRunPlayer-<version>-macos.dmg
set -euo pipefail
cd "$(dirname "$0")"

VERSION="${1:-$(sed -n 's/^ *MARKETING_VERSION: *"\(.*\)"/\1/p' project.yml)}"
VERSION="${VERSION#v}"
echo "== HomeRun Player $VERSION =="

tools/fetch_vlckit.sh
tools/make_icon.sh || echo "(icône non générée)"

xcodegen generate

rm -rf build
mkdir -p build
if ! xcodebuild -project HomeRunPlayer.xcodeproj -scheme HomeRunPlayer -configuration Release   -derivedDataPath build/dd   ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO   MARKETING_VERSION="$VERSION"   CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY=""   build > build/xcodebuild.log 2>&1; then
  grep -E "error:|BUILD FAILED" build/xcodebuild.log | head -150
  echo "Échec de xcodebuild (journal complet : build/xcodebuild.log)" >&2
  exit 1
fi
grep -E "warning: " build/xcodebuild.log | grep "\.swift" | sort -u | head -80 || true

APP="build/dd/Build/Products/Release/HomeRun Player.app"
test -d "$APP"

# Signature ad hoc (obligatoire pour lancer du code arm64 ; pas de certificat Apple)
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
echo "Architectures :"
lipo -info "$APP/Contents/MacOS/HomeRun Player"
lipo -info "$APP/Contents/Frameworks/VLCKit.framework/VLCKit" || true

mkdir -p dist build/dmg
rm -rf build/dmg/*
cp -R "$APP" build/dmg/
ln -s /Applications build/dmg/Applications
DMG="dist/HomeRunPlayer-$VERSION-macos.dmg"
rm -f "$DMG"
hdiutil create -volname "HomeRun Player $VERSION" -srcfolder build/dmg -ov -format UDZO "$DMG"
ls -l dist
