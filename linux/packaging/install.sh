#!/bin/sh
# Installe HomeRun Player depuis l'archive .tar.gz.
#   ./install.sh            → ~/.local (sans droits administrateur)
#   sudo ./install.sh /usr/local
# Dépendances (paquets du système) : GTK 4 (>= 4.14), libVLC 3 + plugins, libcurl.
#   Debian / Ubuntu : sudo apt install libgtk-4-1 libvlc5 vlc-plugin-base libcurl4t64
#   Fedora          : sudo dnf install gtk4 vlc-libs vlc-plugins-base libcurl   (dépôt RPM Fusion)
#   Arch            : sudo pacman -S gtk4 vlc curl
set -eu
PREFIX=${1:-"$HOME/.local"}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$PREFIX/bin" "$PREFIX/share"
cp "$HERE/bin/homerun-player" "$PREFIX/bin/"
cp -r "$HERE/share/." "$PREFIX/share/"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q "$PREFIX/share/icons/hicolor" 2>/dev/null || true
echo "HomeRun Player installé dans $PREFIX (commande : homerun-player)"
case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) echo "Ajoutez $PREFIX/bin à votre PATH." ;; esac
