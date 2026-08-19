#!/usr/bin/env bash
# Kernel parity gate: run on BOTH machines before touching any GPU kernel.
#
#   1. Lockstep manifest: magma CUDA/Metal raster kernels (plus device helpers)
#      and the blaze obs pair (core/obs_camera.h <-> env/blaze_metal_obs.metal)
#      must match verify/kernels/parity_manifest.json. A drift on either side
#      fails until both implementations are updated and the manifest is
#      re-recorded (kernel_pairs.py --update).
#   2. Numeric: the platform's GPU backend must reproduce the CPU reference
#      within the gate thresholds.
#
# anvil:   cpu vs cuda  (magma_game_cuda, GPU1 by default)
# macbook: cpu vs metal (magma_game_metal) + blaze obs CPU==Metal
#          (verify_metal_obs.py --chain; skipped on non-Darwin)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

UV="uv run --no-project"
command -v uv >/dev/null || UV="$HOME/.local/bin/uv run --no-project"

echo "== kernel pair manifest =="
$UV python verify/kernels/kernel_pairs.py

echo "== cross-backend frames =="
case "$(uname -s)" in
Darwin)
    make -C magma game-metal >/dev/null
    $UV --with numpy python verify/kernels/xbackend_frames.py \
        --game magma/magma_game_metal --backend metal
    echo "== blaze obs CPU==Metal =="
    make -C magma blaze_so blaze_metal_obs >/dev/null
    $UV --with numpy python blaze/env/verify_metal_obs.py --chain
    ;;
Linux)
    export CUDA_VISIBLE_DEVICES="${CUDA_VISIBLE_DEVICES:-1}"
    make -C magma game game-cuda -j >/dev/null
    $UV --with numpy python verify/kernels/xbackend_frames.py \
        --game magma/magma_game_cuda --backend cuda
    # blaze obs Metal numeric half is Darwin-only (same skip as Metal raster).
    ;;
*)
    echo "unsupported platform $(uname -s)" >&2
    exit 1
    ;;
esac
echo "kernel_parity_gate: ALL PASS"
