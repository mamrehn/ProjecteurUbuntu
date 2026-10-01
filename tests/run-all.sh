#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Everything that can be tested without the real remote or the real desktop session:
#   daemon unit tests, plain gjs tests, the headless GNOME Shell suite (with the daemon), and the display matrix
#   (several monitors, fractional scales, 4K).
#
#   tests/run-all.sh [BUILD_DIR]      BUILD_DIR defaults to build/daemon (configured and built if missing)
set -u
cd "$(dirname "$0")/.."
build="${1:-build/daemon}"
failed=0

step() { printf '\n######## %s\n' "$*"; }
run() { "$@" || { echo "FAILED: $*"; failed=$((failed + 1)); }; }

step "daemon: build and unit tests"
[ -f "$build/build.ninja" ] || cmake -S daemon -B "$build" -G Ninja || exit 1
cmake --build "$build" || exit 1
run ctest --test-dir "$build" --output-on-failure

step "settings model (plain gjs)"
run gjs -m tests/gjs/test_settings_model.js

step "calibration tool"
run python3 tests/tools/test_spotlight_calibrate.py

step "headless GNOME Shell: extension, settings window, daemon end to end"
run python3 tests/headless/run.py --daemon "$build/projecteurd"

for cfg in "--monitors 1280x720,1024x768" "--monitors 1920x1080 --scale 1.5" "--monitors 1920x1080,1280x1024 --scale 1.25" \
           "--monitors 3840x2160 --scale 2" "--monitors 3840x2160 --scale 1.5"; do
  step "headless GNOME Shell: $cfg"
  # shellcheck disable=SC2086
  run python3 tests/headless/run.py $cfg
done

echo
if [ "$failed" -eq 0 ]; then echo "ALL TEST GROUPS PASSED"; else echo "$failed TEST GROUP(S) FAILED"; fi
exit "$failed"
