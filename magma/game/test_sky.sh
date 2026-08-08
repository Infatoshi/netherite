#!/usr/bin/env bash
# Standalone build+run of the sky self-consistency test (no Makefile involvement).
# Run from anywhere: bash game/test_sky.sh
set -euo pipefail
cd "$(dirname "$0")/.."
gcc -O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I../blaze/core \
    game/sky.c game/test_sky.c core/math.c -o game/test_sky -lm
./game/test_sky
rc=$?

# Convert every PPM (preview + per-pose) to PNG for eyeballing.
if command -v uv >/dev/null 2>&1; then
    uv run --no-project --with pillow python3 - <<'PY'
import glob
from PIL import Image
for p in sorted(glob.glob("game/sky_preview.ppm") + glob.glob("game/sky_pose_*.ppm")):
    out = p[:-4] + ".png"
    Image.open(p).save(out)
    print("wrote", out)
PY
elif command -v pnmtopng >/dev/null 2>&1; then
    for p in game/sky_preview.ppm game/sky_pose_*.ppm; do
        [ -f "$p" ] && pnmtopng "$p" > "${p%.ppm}.png" && echo "wrote ${p%.ppm}.png"
    done
else
    echo "no uv or pnmtopng; PNGs not generated (PPMs are in game/)"
fi
exit $rc
