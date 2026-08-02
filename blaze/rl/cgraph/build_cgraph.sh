#!/usr/bin/env bash
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
torch_prefix=$(
	UV_CACHE_DIR=/home/infatoshi/.cache/uv \
		TMPDIR=/home/infatoshi/dev/nw/.tmp \
		uv run --no-project --with numpy==2.5.1 --with torch==2.13.0 \
		python -c 'import torch; print(torch.utils.cmake_prefix_path)'
)

cmake -S "$here" -B "$here/build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_CUDA_ARCHITECTURES="86;120" \
	-DCMAKE_PREFIX_PATH="$torch_prefix"
cmake --build "$here/build" --parallel 2
