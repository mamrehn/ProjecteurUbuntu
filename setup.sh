#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Install the Spotlight support of this fork on Ubuntu 26.04 (GNOME Shell 50, Wayland): builds the package
# projecteur-gnome from this checkout and installs it with apt. It replaces Ubuntu's Qt5 "projecteur" package if that
# is installed (the two cannot run together).
#
# Usage: ./setup.sh [options]
#   --deb FILE     install this ready-made package instead of building one
#   --enable       afterwards run `projecteur-setup enable` (start the daemon at every login, enable the extension)
#   --check        only look at the system and the installation, change nothing
#   --uninstall    remove the package (apt) and switch the support off for this user
#   --force        go on even if this is not Ubuntu 26.04 with GNOME Shell 50
#   -h, --help
#
# Run as your normal user. sudo is used for apt only. The extension is NOT switched on unless you pass --enable.

set -euo pipefail

DEB="" ENABLE=0 CHECK=0 UNINSTALL=0 FORCE=0
PKG=projecteur-gnome
UUID="projecteur-overlay@mamrehn.github.io"
BUILD_DEPS=(cmake ninja-build pkg-config qt6-base-dev dpkg-dev fakeroot libglib2.0-bin)

if [ -t 1 ]; then G=$'\e[32m' Y=$'\e[33m' R=$'\e[31m' N=$'\e[0m'; else G= Y= R= N=; fi
PROBLEMS=0
info() { printf '    %s\n' "$*"; }
ok()   { printf '%s[ ok ]%s %s\n' "$G" "$N" "$*"; }
warn() { printf '%s[warn]%s %s\n' "$Y" "$N" "$*"; }
fail() { printf '%s[FAIL]%s %s\n' "$R" "$N" "$*"; PROBLEMS=$((PROBLEMS + 1)); }
die()  { printf '%s[FAIL]%s %s\n' "$R" "$N" "$*" >&2; exit 1; }
usage() { sed -n '3,16p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
  case "$1" in
    --deb) [ $# -ge 2 ] || die "--deb needs a file"; DEB="$2"; shift ;;
    --enable) ENABLE=1 ;;
    --check) CHECK=1 ;;
    --uninstall) UNINSTALL=1 ;;
    --force) FORCE=1 ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1 (see --help)" ;;
  esac
  shift
done
[ "$(id -u)" -ne 0 ] || die "run this as your normal user, not as root (sudo is used where needed)"

cd "$(dirname "$0")"

# --------------------------------------------------------------------------------------------------
check_platform() {
  . /etc/os-release
  local shell_version="" session="${XDG_SESSION_TYPE:-unknown}"
  shell_version="$(gnome-shell --version 2>/dev/null | grep -oE '[0-9]+' | head -1 || true)"
  if [ "${VERSION_ID:-}" = "26.04" ]; then ok "Ubuntu 26.04"; else warn "this is ${PRETTY_NAME:-unknown}, not Ubuntu 26.04"; fi
  if [ "$shell_version" = 50 ]; then ok "GNOME Shell 50"; else warn "GNOME Shell is ${shell_version:-not found}, the extension is written for 50"; fi
  if [ "$session" = wayland ]; then ok "Wayland session"; else warn "session type is $session (the extension targets Wayland)"; fi
  if [ "${VERSION_ID:-}" != "26.04" ] || [ "$shell_version" != 50 ]; then
    [ "$FORCE" -eq 1 ] || [ "$CHECK" -eq 1 ] || die "unsupported system; use --force to try anyway"
  fi
}

check_state() {
  echo
  echo "Installation:"
  if dpkg -s "$PKG" >/dev/null 2>&1; then ok "$PKG $(dpkg -s "$PKG" | sed -n 's/^Version: //p') is installed"; else info "$PKG is not installed"; fi
  if dpkg -s projecteur >/dev/null 2>&1; then warn "Ubuntu's Qt5 'projecteur' package is installed; installing $PKG removes it"; fi
  if pgrep -x projecteur >/dev/null 2>&1; then warn "the old 'projecteur' program is running and would fight for the remote"; fi
  if systemctl --user is-active projecteurd.service >/dev/null 2>&1; then ok "projecteurd is running"; else info "projecteurd is not running"; fi
  if [ -e /dev/uinput ] && [ -w /dev/uinput ]; then ok "/dev/uinput is writable for you"; else info "/dev/uinput is not writable for you (the package's udev rule fixes that)"; fi
  if gnome-extensions info "$UUID" >/dev/null 2>&1; then
    ok "GNOME Shell knows the extension: $(gnome-extensions info "$UUID" | grep -E 'State' | tr -s ' ')"
  else info "GNOME Shell does not know the extension yet (after an install: log out and in once)"; fi
  if [ "$(gsettings get org.gnome.desktop.a11y.applications screen-magnifier-enabled 2>/dev/null)" = true ]; then
    warn "GNOME's own screen magnifier is on. It froze one desktop in testing; this fork has its own, safer one. Turn it off in Settings > Accessibility."
  fi
  if command -v bluetoothctl >/dev/null; then
    local s; s="$(bluetoothctl devices 2>/dev/null | grep -i spotlight || true)"
    [ -n "$s" ] && ok "paired over Bluetooth: $s" || info "no Spotlight paired over Bluetooth (fine if you use the USB receiver)"
  fi
  if [ -x /usr/bin/projecteurd ]; then /usr/bin/projecteurd --list-devices | sed 's/^/    /'; fi
}

install_build_deps() {
  local missing=() p
  for p in "${BUILD_DEPS[@]}"; do dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p"); done
  [ ${#missing[@]} -eq 0 ] && { ok "build tools are installed"; return; }
  info "missing build packages: ${missing[*]}"
  [ -t 0 ] || die "install them first: sudo apt-get install ${missing[*]}"
  read -r -p "    install them with sudo apt-get now? [y/N] " reply
  [[ $reply == [yY]* ]] || die "cannot build without them"
  sudo apt-get install -y "${missing[@]}"
}

build_deb() {
  install_build_deps
  local out; out="$(mktemp -d)"
  trap 'rm -rf "$out"' EXIT
  packaging/build-deb.sh "0.1.0" "$out" >&2 || die "building the package failed"
  DEB="$(ls "$out"/${PKG}_*.deb)"
  cp "$DEB" "./" && DEB="./$(basename "$DEB")"
  ok "built $DEB"
}

# --------------------------------------------------------------------------------------------------
echo "Platform:"
check_platform

if [ "$CHECK" -eq 1 ]; then
  check_state
  echo
  [ "$PROBLEMS" -eq 0 ] && echo "No problems found." || echo "$PROBLEMS problem(s) found."
  exit "$PROBLEMS"
fi

if [ "$UNINSTALL" -eq 1 ]; then
  echo
  command -v projecteur-setup >/dev/null && projecteur-setup disable || true
  sudo apt-get remove -y "$PKG"
  ok "removed. (Ubuntu's own 'projecteur' package can be installed again with: sudo apt-get install projecteur)"
  exit 0
fi

echo
echo "Package:"
if [ -z "$DEB" ]; then build_deb; else [ -f "$DEB" ] || die "no such file: $DEB"; ok "using $DEB"; fi

echo
echo "Installing (sudo apt-get):"
sudo apt-get install -y "$DEB"
ok "$PKG installed"

if [ "$ENABLE" -eq 1 ]; then
  echo
  projecteur-setup enable
else
  echo
  echo "Next: switch it on for your user with"
  echo "    projecteur-setup enable"
  echo "(then log out and in once, so that GNOME Shell loads the new extension)."
fi
check_state
