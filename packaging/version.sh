#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Print the package version of this checkout. Used by setup.sh and by the GitHub workflow, so a package built by
# hand and one built by the workflow are numbered the same way.
#
#   on a release tag gnome-vX.Y.Z      X.Y.Z                                        a release
#   anywhere else                      <last release, or 0.1.0>+git<commits>.<sha>  a development build
#                                      ... and .<timestamp> added when the tree has uncommitted changes
#
# Debian sorts these correctly: a development build is newer than the release it grew from and older than the next one,
# and every commit (or edit) gives a number that apt sees as an upgrade.
set -euo pipefail
cd "$(dirname "$0")/.."

tag_re='^gnome-v[0-9]+\.[0-9]+\.[0-9]+$'
last="$(git describe --tags --match 'gnome-v[0-9]*' --abbrev=0 2>/dev/null || true)"
base="0.1.0"
if [ -n "$last" ]; then
    [[ $last =~ $tag_re ]] || { echo "version.sh: unexpected tag name '$last' (want gnome-vX.Y.Z)" >&2; exit 1; }
    base="${last#gnome-v}"
fi

dirty=""
[ -z "$(git status --porcelain 2>/dev/null)" ] || dirty=".$(date +%Y%m%d%H%M%S)"

if [ -z "$dirty" ] && [ -n "$last" ] && [ "$(git rev-parse "$last^{commit}")" = "$(git rev-parse HEAD)" ]; then
    echo "$base"
else
    count="$(git rev-list --count HEAD 2>/dev/null || echo 0)"
    sha="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "${base}+git${count}.${sha}${dirty}"
fi
