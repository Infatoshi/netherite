#!/usr/bin/env bash
# Kernel parity gate: run on BOTH machines before touching any GPU kernel.
#
#   1. Lockstep manifest: the six CUDA kernels and their Metal twins (plus the
#      device helpers around them) must match verify/kernels/parity_manifest.json.
#      A drift on either side fails until both implementations are updated and
#      the manifest is re-recorded (kernel_pairs.py --update).
#   2. Numeric: the platform's GPU backend must reproduce the CPU rasterizer's
#      window-path frames within the XB thresholds (identical thresholds on
#      both platforms; CPU is the shared reference, so anvil green + macbook
#      green => CUDA and Metal kernels agree).
#
# anvil:   cpu vs cuda  (magma_game_cuda, GPU1 by default)
# macbook: cpu vs metal (magma_game_metal)
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
    ;;
Linux)
    export CUDA_VISIBLE_DEVICES="${CUDA_VISIBLE_DEVICES:-1}"
    make -C magma game game-cuda -j >/dev/null
    $UV --with numpy python verify/kernels/xbackend_frames.py \
        --game magma/magma_game_cuda --backend cuda
    ;;
*)
    echo "unsupported platform $(uname -s)" >&2
    exit 1
    ;;
esac
echo "kernel_parity_gate: ALL PASS"
