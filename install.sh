#!/usr/bin/env bash
#
# Install and configure Projecteur (Logitech Spotlight support) on Ubuntu 26.04
# with the default GNOME/Wayland session.
#
# Usage: ./install.sh [options]
#   --check               Only diagnose, change nothing.
#   --autostart           Start Projecteur at login.
#   --keep-zoom           Do not disable the zoom feature (see README: it cannot work on GNOME).
#   --remove-stale-repo   Remove the obsolete jahnf Cloudsmith apt repo without asking.
#   -h, --help            Show this help.
#
# Run as your normal user. sudo is only used for apt, udev and the stale-repo cleanup.

set -euo pipefail

CHECK_ONLY=0 AUTOSTART=0 KEEP_ZOOM=0 REMOVE_STALE=0

STALE_LIST=/etc/apt/sources.list.d/jahnf-projecteur-develop.list
STALE_KEY=/etc/apt/keyrings/jahnf-projecteur-develop.gpg
UDEV_RULE=/usr/lib/udev/rules.d/55-projecteur.rules
CONF_DIR="$HOME/.config/Projecteur"
CONF="$CONF_DIR/Projecteur.conf"
LAUNCHER="$HOME/.local/bin/projecteur-gnome"
APP_DESKTOP="$HOME/.local/share/applications/projecteur.desktop"
AUTOSTART_DESKTOP="$HOME/.config/autostart/projecteur.desktop"

if [ -t 1 ]; then G=$'\e[32m' Y=$'\e[33m' R=$'\e[31m' N=$'\e[0m'; else G= Y= R= N=; fi
PROBLEMS=0
info() { printf '    %s\n' "$*"; }
ok()   { printf '%s[ ok ]%s %s\n' "$G" "$N" "$*"; }
warn() { printf '%s[warn]%s %s\n' "$Y" "$N" "$*"; }
fail() { printf '%s[FAIL]%s %s\n' "$R" "$N" "$*"; PROBLEMS=$((PROBLEMS + 1)); }
die()  { printf '%s[FAIL]%s %s\n' "$R" "$N" "$*" >&2; exit 1; }

usage() { sed -n '3,13p' "$0" | sed 's/^# \{0,1\}//'; }

confirm() {
  [ -t 0 ] || return 1
  read -r -p "    $1 [y/N] " reply
  [[ $reply == [yY]* ]]
}

# --------------------------------------------------------------------------------------------------
detect_platform() {
  . /etc/os-release
  OS_PRETTY=${PRETTY_NAME:-unknown}
  SESSION=${XDG_SESSION_TYPE:-unknown}
  DESKTOP=${XDG_CURRENT_DESKTOP:-unknown}
  GNOME_WAYLAND=0
  if [[ $SESSION == wayland && $DESKTOP == *GNOME* ]]; then GNOME_WAYLAND=1; fi

  ok "Platform: $OS_PRETTY, desktop=$DESKTOP, session=$SESSION"
  [ "${ID:-}" = ubuntu ] || die "This script targets Ubuntu (apt); found '${ID:-unknown}'."
  [ "${VERSION_ID:-}" = 26.04 ] || warn "Only tested on Ubuntu 26.04; you have ${VERSION_ID:-?}."
}

# Zoom needs a screenshot of the desktop; GNOME Shell only hands those to allow-listed apps.
# Projecteur 0.10 defaults to zoom off, so a missing key or config file means "off".
zoom_is_on() { grep -qs '^enableZoom=true' "$CONF"; }

# Write stdin to $1 only if the content differs. Returns 1 when the file was already up to date.
write_if_changed() {
  local tmp; tmp=$(mktemp)
  cat > "$tmp"
  if [ -f "$1" ] && cmp -s "$tmp" "$1"; then rm -f "$tmp"; return 1; fi
  mkdir -p "$(dirname "$1")"
  cat "$tmp" > "$1"
  rm -f "$tmp"
}

running_pid() { pgrep -u "$(id -u)" -x projecteur | head -n1 || true; }

# Which Qt platform plugin the running instance loaded: xcb, wayland, or empty when not running.
running_platform() {
  local pid; pid=$(running_pid)
  [ -n "$pid" ] || return 0
  if grep -qs 'libqxcb' "/proc/$pid/maps"; then echo xcb
  elif grep -qs 'libqwayland' "/proc/$pid/maps"; then echo wayland
  fi
}

# Starts Projecteur the way that works on GNOME/Wayland:
#  * QT_QPA_PLATFORM=xcb (XWayland). Native Wayland leaves the invisible fullscreen overlay mapped after
#    the spotlight is switched off; it keeps keyboard and pointer focus, so the Spotlight's Next/Back
#    keys never reach the slides. Under xcb Projecteur unmaps the overlay again (see README).
#  * Projecteur 0.10 always opens its preferences window on Wayland sessions; hide it after start.
#  * A second start (app grid click) reopens the preferences of the running instance instead.
#  * `--restart` quits a running instance and starts a fresh one. Needed after changing the spot
#    shape in the preferences: 0.10 then leaves the border white until restart (see README).
launcher_script() {
  cat <<'EOF'
#!/bin/sh
if [ "${1:-}" = --restart ]; then
  projecteur -c quit >/dev/null 2>&1
  for _ in $(seq 1 20); do pgrep -u "$(id -u)" -x projecteur >/dev/null || break; sleep 0.25; done
elif pgrep -u "$(id -u)" -x projecteur >/dev/null; then
  exec projecteur -c settings=show
fi
export QT_QPA_PLATFORM=xcb
: "${DISPLAY:=:0}"
projecteur &
pid=$!
for _ in $(seq 1 60); do
  sleep 0.3
  projecteur -c settings=hide >/dev/null 2>&1 && break
done
sleep 1
projecteur -c settings=hide >/dev/null 2>&1
wait "$pid"
EOF
}

is_installed() { [ "$(dpkg-query -W -f='${db:Status-Status}' projecteur 2>/dev/null)" = installed ]; }

# --------------------------------------------------------------------------------------------------
step_stale_repo() {
  [ -e "$STALE_LIST" ] || [ -e "$STALE_KEY" ] || return 0
  warn "Obsolete jahnf Cloudsmith repo found (its newest packages are for Ubuntu 23.04)."
  info "Nothing in it installs on 26.04, yet apt still trusts its signing key and queries it on every update."
  if [ -e "$STALE_LIST" ] && ! grep -q 'dl.cloudsmith.io/public/jahnf/' "$STALE_LIST"; then
    warn "$STALE_LIST does not look like the jahnf repo; leaving it alone."
    return 0
  fi
  if [ "$REMOVE_STALE" = 1 ] || confirm "Remove $STALE_LIST and $STALE_KEY?"; then
    sudo rm -f -- "$STALE_LIST" "$STALE_KEY"
    ok "Removed stale repo"
  else
    info "Kept. Re-run with --remove-stale-repo to remove it."
  fi
}

step_install() {
  if is_installed; then
    ok "projecteur already installed ($(dpkg-query -W -f='${Version}' projecteur))"
    return
  fi
  sudo apt-get update || warn "apt-get update reported errors (often an unrelated third-party repo); continuing"
  local candidate
  candidate=$(LC_ALL=C apt-cache policy projecteur | awk '/Candidate:/ {print $2}')
  if [ -z "$candidate" ] || [ "$candidate" = "(none)" ]; then
    die "No 'projecteur' package found. Enable the 'universe' component: sudo add-apt-repository universe && sudo apt-get update"
  fi
  sudo apt-get install -y projecteur
  ok "Installed projecteur $candidate from the Ubuntu archive"
}

step_udev() {
  [ -e "$UDEV_RULE" ] || die "$UDEV_RULE missing; the projecteur package should ship it."
  if [ -w /dev/uinput ]; then
    ok "/dev/uinput is writable (packaged udev rule + uaccess; no 'input' group needed)"
    return
  fi
  info "Reloading udev rules so the packaged uaccess rule takes effect"
  sudo udevadm control --reload-rules
  sudo udevadm trigger --subsystem-match=misc --subsystem-match=usb \
                       --subsystem-match=hidraw --subsystem-match=input
  sudo udevadm settle
  if [ -w /dev/uinput ]; then
    ok "/dev/uinput is writable now"
  else
    warn "/dev/uinput still not writable. Log out and back in on the local desktop"
    info "(uaccess ACLs are granted to the active local session; SSH sessions never get them)."
  fi
}

ensure_config_owned() {
  [ -e "$CONF_DIR" ] || return 0
  if [ -n "$(find "$CONF_DIR" ! -user "$(id -un)" -print -quit)" ]; then
    warn "$CONF_DIR has files not owned by you (Projecteur was probably run with sudo once)."
    sudo chown -R "$(id -un):$(id -gn)" "$CONF_DIR"
    ok "Fixed ownership of $CONF_DIR"
  fi
}

step_zoom() {
  if [ "$KEEP_ZOOM" = 1 ]; then info "Zoom left as is (--keep-zoom)"; return; fi
  if [ "$GNOME_WAYLAND" != 1 ]; then
    info "Zoom left as is (only blocked on GNOME + Wayland)"; return
  fi
  ensure_config_owned
  if ! zoom_is_on; then ok "Zoom is off"; return; fi
  # A running instance would overwrite a hand-edited file, so talk to it instead.
  if [ -n "$(running_pid)" ]; then
    projecteur -c zoom=false >/dev/null 2>&1 || true
    for _ in 1 2 3 4; do zoom_is_on || break; sleep 0.5; done
  else
    sed -i 's/^enableZoom=true/enableZoom=false/' "$CONF"
  fi
  if zoom_is_on; then
    warn "Zoom is still enabled in $CONF (a preset may have it on)."
    info "Quit Projecteur (projecteur -c quit) and run this script again."
  else
    ok "Zoom disabled (GNOME Shell refuses screenshots to Projecteur; see README)"
  fi
}

desktop_entry() { # $1 = extra keys
  cat <<EOF
[Desktop Entry]
Type=Application
Name=Projecteur
Comment=Logitech Spotlight support
Exec=$LAUNCHER
Icon=projecteur
Terminal=false
Categories=Office;Presentation;
$1
EOF
}

# The launcher, plus a per-user copy of the app-grid entry so that clicking "Projecteur" uses it too.
step_launcher() {
  local changed=0
  launcher_script | write_if_changed "$LAUNCHER" && changed=1
  chmod +x "$LAUNCHER"
  desktop_entry "" | write_if_changed "$APP_DESKTOP" && changed=1
  if [ "$changed" = 1 ]; then ok "Launcher installed ($LAUNCHER, forces the xcb platform)"
  else ok "Launcher already up to date"; fi
}

step_autostart() {
  if desktop_entry "X-GNOME-Autostart-enabled=true" | write_if_changed "$AUTOSTART_DESKTOP"; then
    ok "Autostart entry installed ($AUTOSTART_DESKTOP)"
  else
    ok "Autostart entry already up to date"
  fi
}

stop_projecteur() {
  projecteur -c quit >/dev/null 2>&1 || true
  for _ in $(seq 1 20); do [ -z "$(running_pid)" ] && return 0; sleep 0.25; done
  warn "projecteur did not quit; run: pkill -u $(id -u) -x projecteur"
  return 1
}

start_now() {
  local platform; platform=$(running_platform)
  if [ "$platform" = wayland ]; then
    warn "Running instance uses native Wayland (overlay would keep keyboard focus); restarting it with xcb"
    stop_projecteur || return 0
  elif [ -n "$(running_pid)" ]; then
    ok "projecteur already running"; return 0
  fi
  [ -n "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ] || { info "No graphical session; start it later with: $LAUNCHER"; return 0; }
  setsid nohup "$LAUNCHER" >/dev/null 2>&1 &
  for _ in $(seq 1 20); do [ -n "$(running_pid)" ] && break; sleep 0.25; done
  ok "Started projecteur (preferences window hidden; reopen with: projecteur -c settings=show)"
}

# --------------------------------------------------------------------------------------------------
run_checks() {
  echo; echo "== Checks =="
  if is_installed; then ok "projecteur $(dpkg-query -W -f='${Version}' projecteur) installed"
  else fail "projecteur is not installed"; fi

  if [ -e "$STALE_LIST" ]; then warn "Stale Cloudsmith repo still configured: $STALE_LIST"; fi
  if [ -e "$UDEV_RULE" ]; then ok "udev rule present"; else fail "udev rule $UDEV_RULE missing"; fi
  if [ -w /dev/uinput ]; then ok "/dev/uinput writable"; else fail "/dev/uinput not writable (see README, Troubleshooting)"; fi

  if is_installed; then
    local scan
    scan=$(projecteur -d 2>&1 | grep -E 'Found [0-9]+ supported' || true)
    if [[ $scan == *"Found 0"* || -z $scan ]]; then fail "No Spotlight receiver detected (plug it in)"
    else ok "Device scan:${scan#*Found}"; fi
  fi

  if [ -e "$CONF_DIR" ] && [ -n "$(find "$CONF_DIR" ! -user "$(id -un)" -print -quit)" ]; then
    fail "$CONF_DIR contains files not owned by you"
  fi
  if [ "${GNOME_WAYLAND:-0}" = 1 ] && zoom_is_on; then
    fail "enableZoom=true on GNOME/Wayland: every spot activation will log 'Screenshot via GNOME DBus interface failed'"
  fi
  # Read /etc/group, not `id`: a fresh usermod only shows up in `id` after the next login.
  case ",$(getent group input | cut -d: -f4 || true)," in
    *",$(id -un),"*)
      warn "You are in the 'input' group. Not needed here, and it lets your processes read every keyboard."
      info "Remove with: sudo gpasswd -d $(id -un) input   (then log out and in)" ;;
  esac
  case $(running_platform) in
    xcb) ok "projecteur is running (Qt platform: xcb)" ;;
    wayland)
      if [ "${GNOME_WAYLAND:-0}" = 1 ]; then
        fail "projecteur runs on native Wayland: its invisible overlay keeps keyboard focus, so the Spotlight's Next/Back keys miss your slides"
        info "Fix: projecteur -c quit; then start it from the app grid, or run ./install.sh"
      else ok "projecteur is running (Qt platform: wayland)"; fi ;;
    *) warn "projecteur is not running" ;;
  esac

  echo
  if [ "$PROBLEMS" -eq 0 ]; then echo "No problems found."; else echo "$PROBLEMS problem(s) found."; fi
  [ "$PROBLEMS" -eq 0 ]
}

# --------------------------------------------------------------------------------------------------
while [ $# -gt 0 ]; do
  case $1 in
    --check) CHECK_ONLY=1 ;;
    --autostart) AUTOSTART=1 ;;
    --keep-zoom) KEEP_ZOOM=1 ;;
    --remove-stale-repo) REMOVE_STALE=1 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; die "Unknown option: $1" ;;
  esac
  shift
done

[ "$(id -u)" -ne 0 ] || die "Run as your normal user, not root: root-owned files in ~/.config break Projecteur."

detect_platform
if [ "$CHECK_ONLY" = 1 ]; then run_checks; exit $?; fi

step_stale_repo
step_install
step_udev
step_zoom
step_launcher
if [ "$AUTOSTART" = 1 ]; then step_autostart; fi
start_now
run_checks
