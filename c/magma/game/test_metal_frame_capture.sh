#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
TEST_TMP="$(mktemp -d "${TMPDIR:-/tmp}/magma-metal-capture.XXXXXX")"
trap 'rm -rf "$TEST_TMP"' EXIT
export UV_CACHE_DIR="${UV_CACHE_DIR:-$TEST_TMP/uv-cache}"

COMMON=(--world superflat --view-distance 1 --width 160 --height 90
        --render off --pace unlimited --mobs off)

# The CPU-only product must continue to reject an explicit unavailable Metal
# request. This is a capability error, never a silent CPU fallback.
if ./magma_game --backend metal --frames 0 >"$TEST_TMP/unavailable.out" 2>&1; then
    echo "CPU-only magma_game unexpectedly accepted --backend metal" >&2
    exit 1
fi
rg -q 'Metal backend unavailable.*make game-metal' "$TEST_TMP/unavailable.out"

SCRIPT="$TEST_TMP/representative.jsonl"
printf '%s\n' \
    '{"tick":0,"type":"set_pose","x":8.5,"y":5,"z":8.5,"yaw":180,"pitch":18}' \
    '{"tick":0,"type":"set_inventory","slot":0,"item":1,"count":8,"meta":0}' \
    '{"tick":0,"type":"action","attack":1,"hotbar":0}' \
    '{"tick":1,"type":"set_time","value":6000}' \
    '{"tick":2,"type":"action","dyaw":12,"hotbar":0}' \
    '{"tick":3,"type":"set_time","value":13000}' >"$SCRIPT"

run_script_ppm() {
    local backend="$1" out="$2"
    mkdir "$out"
    ./magma_game_metal --backend "$backend" "${COMMON[@]}" --headless --ticks 4 \
        --script "$SCRIPT" --frames-out "$out" --state-out "$out.state" \
        >"$out.stdout" 2>"$out.stderr"
}

run_script_ppm cpu "$TEST_TMP/script-cpu"
run_script_ppm metal "$TEST_TMP/script-metal-a"
run_script_ppm metal "$TEST_TMP/script-metal-b"

uv run --no-project python - "$TEST_TMP" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])

def ppm(path):
    data = path.read_bytes()
    end = 0
    for _ in range(3):
        end = data.index(b"\n", end) + 1
    magic, dimensions, maximum = data[:end].splitlines()
    width, height = dimensions.split()
    pixels = data[end:]
    assert magic == b"P6" and maximum == b"255", path
    w, h = int(width), int(height)
    assert (w, h) == (160, 90), (path, w, h)
    assert len(pixels) == w * h * 3, (path, len(pixels))
    return w, h, pixels

expected = [f"frame_{tick:06d}.ppm" for tick in range(4)]
for dirname in ("script-cpu", "script-metal-a", "script-metal-b"):
    names = sorted(p.name for p in (root / dirname).glob("frame_*.ppm"))
    assert names == expected, (dirname, names)

for name in expected:
    _, _, cpu = ppm(root / "script-cpu" / name)
    _, _, metal_a = ppm(root / "script-metal-a" / name)
    _, _, metal_b = ppm(root / "script-metal-b" / name)
    assert len(set(metal_a)) > 8 and any(metal_a), f"invalid flat frame: {name}"
    if cpu != metal_a:
        differences = [i for i, (a, b) in enumerate(zip(cpu, metal_a)) if a != b]
        changed_pixels = len({byte // 3 for byte in differences})
        max_diff = max(abs(cpu[byte] - metal_a[byte]) for byte in differences)
        byte = differences[0]
        pixel, channel = divmod(byte, 3)
        y, x = divmod(pixel, 160)
        print(
            f"CPU/Metal {name} changed_px={changed_pixels}/14400 "
            f"maxdiff={max_diff} first=({x},{y},c{channel}) "
            f"cpu={cpu[byte]} metal={metal_a[byte]}"
        )
        # The first three daylight frames are exact. At night, device sin in
        # hash21 may move isolated star dots; keep the same narrow budget as
        # the dedicated sky gate, scaled to this 160x90 frame.
        assert name == "frame_000003.ppm", name
        # Measured M4: 2/14,400 pixels (0.0139%), max channel diff 19.
        assert changed_pixels <= 3 and max_diff <= 32, (changed_pixels, max_diff)
    if metal_a != metal_b:
        byte = next(i for i, (a, b) in enumerate(zip(metal_a, metal_b)) if a != b)
        pixel, channel = divmod(byte, 3)
        y, x = divmod(pixel, 160)
        raise AssertionError(
            f"Metal repeat first difference {name} x={x} y={y} channel={channel} "
            f"a={metal_a[byte]} b={metal_b[byte]}"
        )
PY
cmp "$TEST_TMP/script-cpu.state" "$TEST_TMP/script-metal-a.state"
cmp "$TEST_TMP/script-metal-a.state" "$TEST_TMP/script-metal-b.state"

# Sparse script capture keeps the original tick numbers and pixels from the
# corresponding every-tick Metal run.
mkdir "$TEST_TMP/script-sparse"
./magma_game_metal --backend metal "${COMMON[@]}" --headless --ticks 4 \
    --script "$SCRIPT" --frames-out "$TEST_TMP/script-sparse" \
    --frame-offset 1 --frame-every 2 >"$TEST_TMP/sparse.out" 2>&1
test -f "$TEST_TMP/script-sparse/frame_000001.ppm"
test -f "$TEST_TMP/script-sparse/frame_000003.ppm"
test "$(find "$TEST_TMP/script-sparse" -name 'frame_*.ppm' | wc -l | tr -d ' ')" = 2
cmp "$TEST_TMP/script-metal-a/frame_000001.ppm" "$TEST_TMP/script-sparse/frame_000001.ppm"
cmp "$TEST_TMP/script-metal-a/frame_000003.ppm" "$TEST_TMP/script-sparse/frame_000003.ppm"

# Direct NPY uses the same composed RGB sequence and parity budget as PPM.
# numpy is invoked through uv per the repository contract.
./magma_game_metal --backend cpu "${COMMON[@]}" --headless --ticks 4 \
    --script "$SCRIPT" --frames-out "$TEST_TMP/script-cpu.npy" \
    >"$TEST_TMP/npy-cpu.out" 2>&1
./magma_game_metal --backend metal "${COMMON[@]}" --headless --ticks 4 \
    --script "$SCRIPT" --frames-out "$TEST_TMP/script-metal.npy" \
    >"$TEST_TMP/npy-metal.out" 2>&1

# Empty capture is valid for both directory and direct-NPY destinations.
mkdir "$TEST_TMP/empty"
./magma_game_metal --backend metal "${COMMON[@]}" --headless --ticks 0 \
    --frames-out "$TEST_TMP/empty" >"$TEST_TMP/empty.out" 2>&1
test "$(find "$TEST_TMP/empty" -name 'frame_*.ppm' | wc -l | tr -d ' ')" = 0
./magma_game_metal --backend metal "${COMMON[@]}" --headless --ticks 0 \
    --frames-out "$TEST_TMP/empty.npy" >"$TEST_TMP/empty-npy.out" 2>&1

# A non-directory PPM destination fails actionably and preserves the file.
printf 'sentinel\n' >"$TEST_TMP/not-a-directory"
if ./magma_game_metal --backend metal "${COMMON[@]}" --headless --ticks 1 \
    --frames-out "$TEST_TMP/not-a-directory" >"$TEST_TMP/path-error.out" 2>&1; then
    echo "Metal capture unexpectedly accepted a non-directory frames-out path" >&2
    exit 1
fi
rg -q 'frames-out path is not a directory' "$TEST_TMP/path-error.out"
rg -q '^sentinel$' "$TEST_TMP/not-a-directory"

# RL mode uses the same capture object. Check full and sparse PPM runs, then a
# sparse direct-NPY run against the same deterministic action stream.
ACTIONS="$TEST_TMP/actions.jsonl"
printf '%s\n' \
    '{"cam":0,"forward":0.25}' \
    '{"cam":0,"dyaw":4}' \
    '{"cam":0,"attack":1}' \
    '{"cam":0,"strafe":-0.25}' \
    '{"cam":0,"jump":1}' \
    '{"cam":0}' >"$ACTIONS"

mkdir "$TEST_TMP/rl-full" "$TEST_TMP/rl-sparse"
./magma_game_metal --backend metal "${COMMON[@]}" --rl --ticks 6 \
    --frames-out "$TEST_TMP/rl-full" <"$ACTIONS" \
    >"$TEST_TMP/rl-full.obs" 2>"$TEST_TMP/rl-full.err"
./magma_game_metal --backend metal "${COMMON[@]}" --rl --ticks 6 \
    --frames-out "$TEST_TMP/rl-sparse" --frame-offset 1 --frame-every 2 \
    <"$ACTIONS" >"$TEST_TMP/rl-sparse.obs" 2>"$TEST_TMP/rl-sparse.err"
for tick in 1 3 5; do
    name="frame_$(printf '%06d' "$tick").ppm"
    test -f "$TEST_TMP/rl-sparse/$name"
    cmp "$TEST_TMP/rl-full/$name" "$TEST_TMP/rl-sparse/$name"
done
test "$(find "$TEST_TMP/rl-sparse" -name 'frame_*.ppm' | wc -l | tr -d ' ')" = 3

./magma_game_metal --backend metal "${COMMON[@]}" --rl --ticks 6 \
    --frames-out "$TEST_TMP/rl-sparse.npy" --frame-offset 1 --frame-every 2 \
    <"$ACTIONS" >"$TEST_TMP/rl-npy.obs" 2>"$TEST_TMP/rl-npy.err"

uv run --no-project --with numpy python - "$TEST_TMP" <<'PY'
from pathlib import Path
import numpy as np
import sys

root = Path(sys.argv[1])
script = np.load(root / "script-metal.npy")
script_cpu = np.load(root / "script-cpu.npy")
empty = np.load(root / "empty.npy")
rl = np.load(root / "rl-sparse.npy")
assert script.shape == (4, 90, 160, 3) and script.dtype == np.uint8
assert empty.shape == (0, 90, 160, 3) and empty.dtype == np.uint8
assert rl.shape == (3, 90, 160, 3) and rl.dtype == np.uint8
assert int(script.max()) > int(script.min())
for frame in range(4):
    difference = script_cpu[frame] != script[frame]
    changed_pixels = int(np.any(difference, axis=2).sum())
    max_diff = int(np.abs(script_cpu[frame].astype(np.int16) -
                          script[frame].astype(np.int16)).max())
    if frame < 3:
        assert changed_pixels == 0 and max_diff == 0, (frame, changed_pixels, max_diff)
    else:
        assert changed_pixels <= 3 and max_diff <= 32, (frame, changed_pixels, max_diff)

def ppm_pixels(path):
    data = path.read_bytes()
    end = 0
    for _ in range(3):
        end = data.index(b"\n", end) + 1
    magic, dimensions, maximum = data[:end].splitlines()
    width, height = dimensions.split()
    pixels = data[end:]
    assert (magic, int(width), int(height), maximum) == (b"P6", 160, 90, b"255")
    return np.frombuffer(pixels, dtype=np.uint8).reshape(90, 160, 3)

for index, tick in enumerate((1, 3, 5)):
    expected = ppm_pixels(root / "rl-sparse" / f"frame_{tick:06d}.ppm")
    if not np.array_equal(expected, rl[index]):
        y, x, channel = np.argwhere(expected != rl[index])[0]
        raise AssertionError(
            f"RL PPM/NPY first difference tick={tick} x={x} y={y} "
            f"channel={channel} ppm={expected[y, x, channel]} "
            f"npy={rl[index, y, x, channel]}"
        )
PY

echo "Metal frame capture: PASS"
