#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build projecteur-gnome_<version>_<arch>.deb from this checkout. Needs: cmake ninja-build pkg-config qt6-base-dev
# dpkg-dev fakeroot libglib2.0-bin.
#
#   packaging/build-deb.sh [VERSION] [OUTPUT_DIR]       defaults: 0.1.0 and the current directory
set -euo pipefail
cd "$(dirname "$0")/.."
root="$(pwd)"
version="${1:-0.1.0}"
out="$(realpath -m "${2:-.}")"
uuid="projecteur-overlay@mamrehn.github.io"
arch="$(dpkg --print-architecture)"
pkg="projecteur-gnome"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
stage="$work/root"

echo "== build"
cmake -S daemon -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_CXX_FLAGS="-O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2" -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,relro,-z,now" \
      -DPROJECTEURD_VERSION="$version" >/dev/null
cmake --build "$work/build" --target projecteurd
DESTDIR="$stage" cmake --install "$work/build" >/dev/null
strip --strip-unneeded "$stage/usr/bin/projecteurd"

echo "== extension schema"
glib-compile-schemas --strict "$stage/usr/share/gnome-shell/extensions/$uuid/schemas"

echo "== documentation"
doc="$stage/usr/share/doc/$pkg"
install -D -m 644 packaging/copyright "$doc/copyright"
install -D -m 644 README.md "$doc/README.md"
install -D -m 644 doc/ubuntu/INPUT-MODEL.md "$doc/INPUT-MODEL.md"
{
    echo "$pkg ($version) unstable; urgency=medium"
    echo
    echo "  * Build of $(git rev-parse --short HEAD 2>/dev/null || echo unknown) from https://github.com/mamrehn/ProjecteurUbuntu"
    echo
    echo " -- mamrehn <mamrehn@users.noreply.github.com>  $(date -R -d "@${SOURCE_DATE_EPOCH:-$(date +%s)}")"
} | gzip -9n > "$doc/changelog.Debian.gz"

echo "== dependencies"
mkdir -p "$work/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$pkg" "$pkg" > "$work/debian/control"
shlibs="$(cd "$work" && dpkg-shlibdeps -O -e"$stage/usr/bin/projecteurd" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')"
echo "   shared libraries: $shlibs"

echo "== package"
mkdir -p "$stage/DEBIAN"
size="$(du -sk --exclude=DEBIAN "$stage" | cut -f1)"
cat > "$stage/DEBIAN/control" <<CONTROL
Package: $pkg
Version: $version
Section: utils
Priority: optional
Architecture: $arch
Maintainer: mamrehn <mamrehn@users.noreply.github.com>
Installed-Size: $size
Depends: $shlibs, gnome-shell (>= 50~)
Conflicts: projecteur
Replaces: projecteur
Homepage: https://github.com/mamrehn/ProjecteurUbuntu
Description: Logitech Spotlight support for GNOME on Wayland
 A background daemon and a GNOME Shell extension for the Logitech Spotlight
 (original version, USB receiver or Bluetooth): highlight, live magnifier and
 laser pointer effects, held Next/Back actions, presentation timer with
 vibration alerts, battery warning, per-application profiles and a settings
 window. Replaces the Qt5 "projecteur" package, which cannot zoom on GNOME.
CONTROL
install -m 755 packaging/postinst packaging/postrm "$stage/DEBIAN/"
(cd "$stage" && find usr -type f -print0 | sort -z | xargs -0 md5sum) > "$stage/DEBIAN/md5sums"
find "$stage" -type d -exec chmod 755 {} +
find "$stage" -type f ! -perm /111 -exec chmod 644 {} +   # no group-writable files whatever the umask was
mkdir -p "$out"
fakeroot dpkg-deb --root-owner-group -Zxz --build "$stage" "$out/${pkg}_${version}_${arch}.deb" >/dev/null
echo "built $out/${pkg}_${version}_${arch}.deb"
