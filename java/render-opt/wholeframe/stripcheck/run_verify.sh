#!/usr/bin/env bash
# Full stripped-instance verification: off (Java oracle) vs native (C kernels), same
# fixed action course, then whole-frame pixel diff + behavioral assertions.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
for m in off native; do
  echo "===== capture $m ====="
  bash "$HERE/capture_stripped.sh" "$m" || exit 1
done
echo "===== pixel diff (off = baseline oracle) ====="
cd "$HERE/.." && uv run --no-project --with numpy --with pillow \
    python diff_frame.py stripcheck/frame_off.png stripcheck/frame_native.png
echo "===== summary ====="
grep -h DEATH_CHECK "$HERE"/death_*.out
grep -h SOUND "$HERE"/asserts_*.out
grep -h PROOF "$HERE"/asserts_native.out 2>/dev/null
