#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Install the built package on a clean Ubuntu 26.04 and check that it is what it claims to be. It installs and PURGES
# packages, so it refuses to run anywhere but in a container or in CI (the GitHub workflow runs it in an ubuntu:26.04
# container). To try it by hand:
#
#   docker run --rm -v "$PWD":/src:ro ubuntu:26.04 bash -c 'apt-get update -qq && /src/packaging/smoke-test.sh /src/<package>.deb'
set -euo pipefail

deb="$(realpath "${1:?usage: smoke-test.sh PACKAGE.deb}")"   # apt reads a bare relative path as a package name
if [ ! -f /.dockerenv ] && [ -z "${CI:-}" ]; then
    echo "smoke-test.sh: refusing to run outside a container or CI (it installs and purges packages)" >&2
    exit 2
fi
export DEBIAN_FRONTEND=noninteractive
pkg="$(dpkg-deb -f "$deb" Package)"
version="$(dpkg-deb -f "$deb" Version)"
uuid="projecteur-overlay@mamrehn.github.io"
fail=0
ok() { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; fail=1; }

echo "== $pkg $version"
echo "== can apt resolve its dependencies on this system? (simulation)"
if apt-get -s install "$deb" >/dev/null 2>&1; then ok "apt can install it with all dependencies (gnome-shell included)"; else
    apt-get -s install "$deb" 2>&1 | tail -5; bad "apt cannot resolve the dependencies"; fi

echo "== install without the desktop, then look at the result"
# The runtime libraries are what the daemon needs; gnome-shell itself is not part of this container, so the package is
# installed with its dependencies not enforced.
libs="$(dpkg-deb -f "$deb" Depends | tr ',' '\n' | sed 's/(.*//; s/ //g' | grep -v '^gnome-shell$' | grep -v '^$' | sort -u | tr '\n' ' ')"
# shellcheck disable=SC2086
apt-get install -y -qq --no-install-recommends $libs >/dev/null
dpkg --force-depends -i "$deb" >/dev/null 2>&1 || true
dpkg -s "$pkg" 2>/dev/null | grep -q '^Status: install ok installed' && ok "installed" || bad "dpkg did not install it"

for f in /usr/bin/projecteurd /usr/bin/projecteur-setup \
         /usr/lib/udev/rules.d/55-projecteurd.rules /usr/lib/systemd/user/projecteurd.service /usr/lib/modules-load.d/projecteurd.conf \
         "/usr/share/gnome-shell/extensions/$uuid/extension.js" "/usr/share/gnome-shell/extensions/$uuid/metadata.json" \
         "/usr/share/gnome-shell/extensions/$uuid/schemas/gschemas.compiled" "/usr/share/doc/$pkg/copyright"; do
    [ -e "$f" ] && ok "$f" || bad "missing: $f"
done

reported="$(projecteurd --version 2>/dev/null | head -1)"
[ "$reported" = "projecteurd $version" ] && ok "projecteurd reports its package version ($reported)" || bad "projecteurd --version says '$reported', expected 'projecteurd $version'"
sh -n /usr/bin/projecteur-setup && ok "projecteur-setup is valid shell" || bad "projecteur-setup has a syntax error"
python3 - "$uuid" <<'PY' && ok "extension metadata: uuid matches its directory, GNOME Shell 50" || bad "extension metadata is wrong"
import json, sys
uuid = sys.argv[1]
m = json.load(open(f'/usr/share/gnome-shell/extensions/{uuid}/metadata.json'))
assert m['uuid'] == uuid and '50' in m['shell-version'], m
PY
[ "$(dpkg-deb -f "$deb" Conflicts)" = "projecteur" ] && ok "conflicts with Ubuntu's Qt5 projecteur" || bad "Conflicts: field is not 'projecteur'"

echo "== purge"
dpkg --purge "$pkg" >/dev/null 2>&1 || true
if [ -e /usr/bin/projecteurd ] || [ -e "/usr/share/gnome-shell/extensions/$uuid" ] || [ -e /usr/lib/udev/rules.d/55-projecteurd.rules ]; then
    bad "files are left after purge"; else ok "purge removes everything"; fi

[ "$fail" -eq 0 ] && echo "SMOKE TEST PASSED" || { echo "SMOKE TEST FAILED"; exit 1; }
