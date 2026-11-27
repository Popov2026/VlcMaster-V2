#!/bin/sh
# Construit les paquets de HomeRun Player pour Linux à partir d'un dossier de compilation Meson.
#
#   packaging/make_dist.sh <version> <dossier build> <dossier de sortie>
#
# Produit :
#   HomeRunPlayer-<version>-linux-<arch>.tar.gz   (binaire + install.sh, dépendances du système)
#   homerun-player_<version>_<arch-deb>.deb       (Debian 13, Ubuntu 24.04 ou plus récents)
set -eu

VER=$1
BUILD=$2
OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
ARCH=$(uname -m)
DEBARCH=$(dpkg --print-architecture 2>/dev/null || echo amd64)
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$OUT"

# ---- arborescence installée (/usr)
DESTDIR="$STAGE/root" meson install -C "$BUILD" --no-rebuild --quiet
ROOT="$STAGE/root/usr"
[ -x "$ROOT/bin/homerun-player" ] || { echo "binaire absent de $ROOT/bin" >&2; exit 1; }
strip "$ROOT/bin/homerun-player" || true

# ---- .tar.gz
NAME="HomeRunPlayer-$VER-linux-$ARCH"
mkdir -p "$STAGE/$NAME"
cp -r "$ROOT/bin" "$ROOT/share" "$STAGE/$NAME/"
cp "$HERE/install.sh" "$STAGE/$NAME/install.sh"
chmod +x "$STAGE/$NAME/install.sh"
cp "$HERE/../README.md" "$STAGE/$NAME/README.md"
tar -C "$STAGE" -czf "$OUT/$NAME.tar.gz" "$NAME"

# ---- .deb
DEB="$STAGE/deb"
mkdir -p "$DEB/DEBIAN"
cp -r "$STAGE/root/usr" "$DEB/usr"
SIZE=$(du -sk "$DEB/usr" | cut -f1)
cat > "$DEB/DEBIAN/control" <<EOF
Package: homerun-player
Version: $VER
Section: video
Priority: optional
Architecture: $DEBARCH
Depends: libgtk-4-1 (>= 4.14), libvlc5 (>= 3.0), vlc-plugin-base, libcurl4t64 | libcurl4, libglib2.0-0t64 | libglib2.0-0
Installed-Size: $SIZE
Maintainer: Popov2026 <noreply@github.com>
Homepage: https://github.com/Popov2026/VlcMaster-V2
Description: TV player for HDHomeRun tuners
 Channel list, zapping, TV guide, audio and subtitle languages,
 recordings, picture-in-picture, tuner information and channel scan.
EOF
dpkg-deb --root-owner-group --build "$DEB" "$OUT/homerun-player_${VER}_${DEBARCH}.deb"

ls -l "$OUT"
