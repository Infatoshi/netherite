#!/usr/bin/env bash
# mac_metal_verify.sh - one-shot MacBook-side proof of the magma Metal backend.
#
# Run FROM THE REPO ROOT on the MacBook (M4 Max / Apple silicon):
#
#     bash scripts/mac_metal_verify.sh
#
# Steps (each gate must pass; the script stops at the first hard failure):
#   1. Environment guard: macOS (uname = Darwin) + xcrun (Xcode CLT) present.
#   2. make -C magma game-metal            -> magma_game_metal (agent-1 target).
#   3. make -C magma test-raster-parity-metal
#        CPU-vs-Metal bitwise raster parity over the shared verify scene
#        battery (make/metal.mk; mirror of the CUDA rung-1 gate).
#   4. Tape replay smoke: replay_tape.py --metal on the zombie smoke tape,
#        including the structural pixel gate (pixel_gate.py, backend-agnostic).
#
# Until ALL steps pass on the MacBook, Metal parity is UNVERIFIED
# (magma/VERIFY.md "Metal backend (macOS)").
#
# Tapes are gitignored; the smoke tape must be present locally. Sync it from
# anvil first (jsonl + sidecars, the _frames goldens dir, and the _world
# recstart snapshot dir - a missing _world makes replay skip snapshot patching
# SILENTLY, so all three are required):
#
#     rsync -av \
#       'anvil:~/dev/netherite/verify/tapes/scenario_smoke_zombie_20260722T081735Z.*' \
#       'anvil:~/dev/netherite/verify/tapes/scenario_smoke_zombie_20260722T081735Z_frames' \
#       'anvil:~/dev/netherite/verify/tapes/scenario_smoke_zombie_20260722T081735Z_world' \
#       verify/tapes/

set -u

TAPE_NAME="scenario_smoke_zombie_20260722T081735Z"

fail() { echo "FAIL: $*" >&2; echo "== METAL VERIFY: FAIL =="; exit 1; }

# --- 1. environment guards --------------------------------------------------
[ "$(uname -s)" = "Darwin" ] || fail "this script must run on macOS (uname -s = $(uname -s)); it proves the Metal backend on the MacBook"
command -v xcrun >/dev/null 2>&1 || fail "xcrun not found; install the Xcode command line tools (xcode-select --install)"
command -v uv >/dev/null 2>&1 || fail "uv not found; the replay step needs uv run"
[ -f "magma/Makefile" ] && [ -d "verify/trace" ] || fail "run from the repo root (magma/Makefile not found in cwd $(pwd))"

TAPE="verify/tapes/${TAPE_NAME}.jsonl"
[ -f "$TAPE" ] || fail "tape missing: $TAPE - rsync it from anvil (command in the header comment)"
[ -d "verify/tapes/${TAPE_NAME}_frames" ] || fail "golden frames dir missing: verify/tapes/${TAPE_NAME}_frames - rsync it from anvil"
[ -d "verify/tapes/${TAPE_NAME}_world" ] || fail "world snapshot dir missing: verify/tapes/${TAPE_NAME}_world - rsync it from anvil (without it replay skips snapshot patching silently)"

# --- 2. build the Metal game binary ----------------------------------------
echo "== [1/3] make -C magma game-metal =="
make -C magma game-metal || fail "game-metal build failed"

# --- 3. CPU vs Metal raster parity gate -------------------------------------
echo "== [2/3] make -C magma test-raster-parity-metal =="
make -C magma test-raster-parity-metal || fail "raster parity gate failed (CPU != Metal); see the per-layer lines above"

# --- 4. tape replay smoke with --metal --------------------------------------
echo "== [3/3] replay_tape.py --metal ${TAPE_NAME} =="
( cd verify/trace && \
  MAGMA_METAL_REQUIRE=1 \
  uv run --no-project --with numpy --with scipy --with pillow --with nbt \
    python replay_tape.py "../tapes/${TAPE_NAME}.jsonl" --metal --report )
replay_rc=$?
echo "replay rc=${replay_rc}"
[ "$replay_rc" -eq 0 ] || fail "tape replay with --metal returned rc=${replay_rc} (0 required; rc=3 is an unexplained pixel-gate cluster)"

echo "== METAL VERIFY: PASS =="
echo "game-metal build, CPU==Metal raster parity, and the ${TAPE_NAME} replay (rc=0) all passed."
echo "Update magma/VERIFY.md 'Metal backend (macOS)': flip UNVERIFIED to the date + git rev of this run."
exit 0
