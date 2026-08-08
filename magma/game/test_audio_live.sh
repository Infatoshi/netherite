#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export TMPDIR=${TMPDIR:-"$PWD/.tmp"}
make game/test_audio_live >/dev/null
MAGMA_AUDIO_LOOPBACK=1 ./game/test_audio_live
