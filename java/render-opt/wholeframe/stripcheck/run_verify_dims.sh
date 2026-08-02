#!/usr/bin/env bash
# Nether + End oracle verification: for each dimension scene, capture off (pure-Java)
# and native (all-C kernels) frames and pixel-diff them; the End native run also does
# the dragon-in-view coverage probe. Usage: run_verify_dims.sh [nether|end ...]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SCENES=("${@:-nether}")
[ $# -eq 0 ] && SCENES=(nether end)
for sc in "${SCENES[@]}"; do
  for m in off native; do
    echo "===== capture $sc $m ====="
    bash "$HERE/capture_stripped.sh" "$m" "$sc" || exit 1
  done
done
for sc in "${SCENES[@]}"; do
  echo "===== pixel diff: $sc (off = baseline oracle) ====="
  (cd "$HERE/.." && uv run --no-project --with numpy --with pillow \
      python diff_frame.py "stripcheck/frame_${sc}_off.png" "stripcheck/frame_${sc}_native.png")
done
echo "===== summary ====="
grep -h DRAGON_COVERAGE "$HERE"/dragon_*.out 2>/dev/null
grep -h PROOF "$HERE"/asserts_native.out 2>/dev/null
