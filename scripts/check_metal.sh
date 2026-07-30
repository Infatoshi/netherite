#!/usr/bin/env bash
# Compile and execute a one-dispatch Metal runtime probe.  This deliberately
# uses runtime MSL compilation: the optional offline `metal` component is not
# required by netherite's native backends.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
  echo "ERROR: native Metal support requires macOS on Apple Silicon" >&2
  exit 2
fi

command -v xcrun >/dev/null 2>&1 || {
  echo "ERROR: xcrun is missing; install Xcode or the Command Line Tools" >&2
  exit 2
}

PROBE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/netherite-metal-probe.XXXXXX")"
trap 'rm -rf "$PROBE_DIR"' EXIT HUP INT TERM

xcrun clang++ -std=c++17 -fobjc-arc \
  "$ROOT/scripts/metal_probe.mm" \
  -framework Foundation -framework Metal \
  -o "$PROBE_DIR/metal_probe"

file "$PROBE_DIR/metal_probe"
"$PROBE_DIR/metal_probe"
