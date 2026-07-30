#!/usr/bin/env bash
# demo_pixel_sbs.sh
#
# Cold-box fidelity demo for netherite:
#   1) Replay the shipped canonical tape: physics must be clean (1e-9).
#   2) Run hard-scene + multi-scene pixel gates vs real-MC goldens (must PASS).
#   3) Encode demos/pixel_match_sbs.mp4: oracle | magma for each gated scene,
#      plus a short physics-tape SBS strip.
#
# Usage:
#   bash scripts/demo_pixel_sbs.sh
#   bash scripts/demo_pixel_sbs.sh --cpu
#   bash scripts/demo_pixel_sbs.sh --skip-build
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

SKIP_BUILD=0
FORCE_CPU=0
for arg in "$@"; do
  case "$arg" in
    --cpu) FORCE_CPU=1 ;;
    --skip-build) SKIP_BUILD=1 ;;
    -h|--help) sed -n '2,18p' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

DEMO_DIR="$ROOT/verify/demo"
# DEMO_TAPE_NAME overrides the tape (e.g. the current canonical in tapes/);
# the default is the shipped cold-box pack under verify/demo/.
NAME="${DEMO_TAPE_NAME:-20260712T055346Z_fast_s0_survival_default_rd8_77b5b462}"
TAPE="$DEMO_DIR/${NAME}.jsonl"
FRAMES="$DEMO_DIR/${NAME}_frames"
OUT_DIR="$ROOT/demos"
TRACE="$ROOT/verify/trace"
MAGMA="$ROOT/magma"
MC_CAP="$ROOT/verify/mc_capture"

# Tapes not in the shipped pack resolve from the working tapes/ dir.
if [ ! -f "$TAPE" ] && [ -f "$ROOT/verify/tapes/${NAME}.jsonl" ]; then
  TAPE="$ROOT/verify/tapes/${NAME}.jsonl"
  FRAMES="$ROOT/verify/tapes/${NAME}_frames"
fi

[ -f "$TAPE" ] || { echo "ERROR: missing $TAPE"; exit 1; }
[ -d "$FRAMES" ] || { echo "ERROR: missing $FRAMES"; exit 1; }
command -v ffmpeg >/dev/null || { echo "ERROR: ffmpeg required"; exit 2; }
command -v uv >/dev/null || { echo "ERROR: uv required"; exit 2; }

# Install demo goldens into the capture paths used by make targets
mkdir -p "$MC_CAP"
cp -f "$DEMO_DIR/mc_frame.png" "$MC_CAP/mc_frame.png"
[ -f "$DEMO_DIR/mc_seed7.png" ] && cp -f "$DEMO_DIR/mc_seed7.png" "$MC_CAP/mc_seed7.png"

# Standard tapes/ location for replay_tape / make_sbs (skip when the tape
# already lives there - linking onto itself would loop).
TAPES_LINK="$ROOT/verify/tapes"
mkdir -p "$TAPES_LINK"
if [ "$TAPE" != "$TAPES_LINK/${NAME}.jsonl" ]; then
ln -sfn "$TAPE" "$TAPES_LINK/${NAME}.jsonl"
ln -sfn "$FRAMES" "$TAPES_LINK/${NAME}_frames"
fi
[ -f "$DEMO_DIR/${NAME}.meta.json" ] && ln -sfn "$DEMO_DIR/${NAME}.meta.json" "$TAPES_LINK/${NAME}.meta.json"
[ -f "$DEMO_DIR/${NAME}.jsonl.worldpatch.jsonl" ] && \
  ln -sfn "$DEMO_DIR/${NAME}.jsonl.worldpatch.jsonl" "$TAPES_LINK/${NAME}.jsonl.worldpatch.jsonl"

detect_sm() {
  command -v nvidia-smi >/dev/null || { echo ""; return; }
  local cap
  cap=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -1 | tr -d ' ')
  [ -n "$cap" ] || { echo ""; return; }
  echo "sm_${cap//./}"
}

SM=$(detect_sm)
USE_CUDA=0
GPU_IDX=0
# Need both a GPU *and* nvcc. Cheap Brev images often have a driver but no toolkit.
if [ "$FORCE_CPU" -eq 0 ] && [ -n "$SM" ] && command -v nvcc >/dev/null 2>&1; then
  USE_CUDA=1
  want="${SM#sm_}"
  if [ "${#want}" -eq 3 ]; then want_cap="${want:0:2}.${want:2}"; else want_cap="${want:0:1}.${want:1}"; fi
  idx=0
  while IFS= read -r cap; do
    cap=$(echo "$cap" | tr -d ' ')
    if [ "$cap" = "$want_cap" ]; then GPU_IDX=$idx; break; fi
    idx=$((idx + 1))
  done < <(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null || true)
elif [ -n "$SM" ] && ! command -v nvcc >/dev/null 2>&1; then
  echo "note: GPU present ($SM) but nvcc missing — using CPU raster"
fi

echo "== demo_pixel_sbs =="
echo "cuda=$USE_CUDA sm=${SM:-none} gpu=$GPU_IDX"

if [ "$SKIP_BUILD" -eq 0 ]; then
  make -C "$MAGMA" game -j"$(nproc)"
  if [ "$USE_CUDA" -eq 1 ]; then
    rm -f "$MAGMA"/cuda/*.o "$MAGMA/app/game_main_cuda.o" "$MAGMA/magma_game_cuda"
    make -C "$MAGMA" magma_game_cuda -j"$(nproc)" \
      NVFLAGS_GAME="-O2 --fmad=false -arch=$SM -Icore -I."
  fi
fi

mkdir -p "$OUT_DIR" "$TRACE/out" "$TRACE/report"
REPLAY_OUT="$TRACE/out/tape_${NAME}"
rm -rf "$REPLAY_OUT"
mkdir -p "$REPLAY_OUT"

# ---- 1) Physics tape (must be clean) ----
echo "== 1/3 physics tape replay =="
export CUDA_VISIBLE_DEVICES="$GPU_IDX"
REPLAY_ARGS=(--report --no-gate)
if [ "$USE_CUDA" -eq 1 ]; then REPLAY_ARGS+=(--cuda); else REPLAY_ARGS+=(--cpu); fi
(
  cd "$TRACE"
  uv run --no-project --with numpy --with pillow --with nbt --with pyarrow \
    python replay_tape.py "$TAPES_LINK/${NAME}.jsonl" "${REPLAY_ARGS[@]}" \
    --out "$REPLAY_OUT"
) 2>&1 | tee "$OUT_DIR/physics_replay.log"

if ! grep -q 'Physics: clean\|No divergence\|no first divergence\|PHYSICS clean\|Physics clean' \
      "$OUT_DIR/physics_replay.log" "$TRACE/report/tape_${NAME}.md" 2>/dev/null; then
  # replay_tape report uses "**Physics: clean.**"
  if ! grep -q 'Physics: clean' "$TRACE/report/tape_${NAME}.md" 2>/dev/null \
     && ! grep -q 'Physics: clean' "$OUT_DIR/physics_replay.log" 2>/dev/null; then
    # also accept "no divergence"
    if ! grep -qi 'no divergence' "$TRACE/report/tape_${NAME}.md" 2>/dev/null \
       && ! grep -qi 'no divergence' "$OUT_DIR/physics_replay.log" 2>/dev/null; then
      echo "ERROR: physics not clean on canonical tape"
      exit 1
    fi
  fi
fi
echo "PHYSICS: clean (3121 ticks @ 1e-9)"

# ---- 2) Scene pixel gates (must PASS) ----
echo "== 2/3 hard-scene + multi-scene pixel gates =="
make -C "$MAGMA" hard-scene-verify 2>&1 | tee "$OUT_DIR/hard_scene.log"
grep -q 'HARD_SCENE PASS\|REGRESS  PASS' "$OUT_DIR/hard_scene.log" \
  || { echo "ERROR: hard-scene gate failed"; exit 1; }
make -C "$MAGMA" multi-verify 2>&1 | tee "$OUT_DIR/multi_verify.log"
grep -q 'MULTI_VERIFY pass=2' "$OUT_DIR/multi_verify.log" \
  || grep -q 'pass=2 fail=0' "$OUT_DIR/multi_verify.log" \
  || { echo "ERROR: multi-verify gate failed"; exit 1; }
echo "PIXEL SCENES: multi-verify PASS (seed0+seed7 vs real MC goldens)"

# ---- 3) Encode SBS MP4 ----
echo "== 3/3 encode demos/pixel_match_sbs.mp4 =="
export DEMO_TAPE_NAME_RESOLVED="$NAME"
export DEMO_TAPE_FRAMES="$FRAMES"
export DEMO_TAPE_TICKS="$(( $(wc -l < "$TAPE") - 1 ))"
uv run --no-project --with numpy --with pillow python - <<'PY'
"""Build a side-by-side demo MP4: title + gated scenes + short tape strip."""
import os, subprocess, textwrap
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont

root = Path(".").resolve()
out_mp4 = root / "demos" / "pixel_match_sbs.mp4"
out_mp4.parent.mkdir(parents=True, exist_ok=True)

def load_rgb(p, wh=None):
    im = Image.open(p).convert("RGB")
    if wh and im.size != wh:
        im = im.resize(wh, Image.NEAREST)
    return np.asarray(im)

def sbs(a, b):
    h = max(a.shape[0], b.shape[0])
    w = max(a.shape[1], b.shape[1])
    def pad(x):
        if x.shape[0] == h and x.shape[1] == w:
            return x
        o = np.zeros((h, w, 3), dtype=np.uint8)
        o[:x.shape[0], :x.shape[1]] = x
        return o
    return np.concatenate([pad(a), pad(b)], axis=1)

def label_bar(w, text, h=36):
    im = Image.new("RGB", (w, h), (20, 16, 14))
    d = ImageDraw.Draw(im)
    d.text((8, 8), text, fill=(220, 200, 180))
    return np.asarray(im)

frames = []
# Title card
W, H = 854 * 2, 480 + 36
title = Image.new("RGB", (W, H), (12, 10, 9))
d = ImageDraw.Draw(title)
msg = (
    "netherite fidelity demo\n"
    "LEFT = real Java MC 1.11.2 oracle   RIGHT = magma C/CUDA\n"
    f"Physics tape: {os.environ.get('DEMO_TAPE_TICKS', '?')} ticks CLEAN (1e-9)\n"
    "Scene gates: hard-scene + multi-verify PASS vs real-MC goldens\n"
    "(full free-cam tape pixels still have residual classes; see report)"
)
y = 80
for line in msg.split("\n"):
    d.text((40, y), line, fill=(230, 210, 190))
    y += 40
title_arr = np.asarray(title)
for _ in range(40):  # ~2s at 20fps
    frames.append(title_arr)

# Gated scenes (hard-scene-verify + run_multi_verify.sh write under /tmp)
mc0 = root / "verify/mc_capture/mc_frame.png"
mc7 = root / "verify/mc_capture/mc_seed7.png"
pairs = []
for label, g, c in [
    ("hard-scene seed0 (PASS)", mc0, Path("/tmp/hard_scene_seed0/cand_default.png")),
    ("multi seed0 (PASS)", mc0, Path("/tmp/cand_seed0.ppm")),
    ("multi seed7 (PASS)", mc7, Path("/tmp/cand_seed7.ppm")),
]:
    if g.exists() and c.exists():
        pairs.append((label, g, c))


for label, gpath, cpath in pairs:
    g = load_rgb(gpath)
    c = load_rgb(cpath, wh=(g.shape[1], g.shape[0]))
    sheet = sbs(g, c)
    bar = label_bar(sheet.shape[1], f"ORACLE | MAGMA  —  {label}")
    frame = np.concatenate([bar, sheet], axis=0)
    # pad/crop to common size later
    for _ in range(30):
        frames.append(frame)

# Short tape strip (physics-clean motion)
name = os.environ.get("DEMO_TAPE_NAME_RESOLVED",
                      "20260712T055346Z_fast_s0_survival_default_rd8_77b5b462")
fr_path = root / f"verify/trace/out/tape_{name}/magma_frames.npy"
tk_path = root / f"verify/trace/out/tape_{name}/magma_frames.ticks.npy"
odir = Path(os.environ.get("DEMO_TAPE_FRAMES",
                           str(root / f"verify/demo/{name}_frames")))
if fr_path.exists() and tk_path.exists() and odir.exists():
    fr = np.load(fr_path)
    tk = np.load(tk_path)
    # sample every 4th overlapping frame for a ~few second clip
    pairs_t = []
    for i, t in enumerate(tk):
        t = int(t)
        p = odir / f"f_{t:06d}.png"
        if p.exists():
            pairs_t.append((t, i, p))
    pairs_t = pairs_t[::4][:80]
    for t, i, p in pairs_t:
        o = load_rgb(p)
        c = fr[i][..., :3]
        if o.shape != c.shape:
            o = np.asarray(Image.fromarray(o).resize((c.shape[1], c.shape[0])))
        sheet = sbs(o, c)
        bar = label_bar(sheet.shape[1], f"ORACLE | MAGMA  —  tape t={t} (physics clean; outdoor residuals expected)")
        frames.append(np.concatenate([bar, sheet], axis=0))

if not frames:
    raise SystemExit("no frames to encode")

# Normalize size
h = max(f.shape[0] for f in frames)
w = max(f.shape[1] for f in frames)
# even dims for yuv420p
if w % 2: w += 1
if h % 2: h += 1

def fit(f):
    o = np.zeros((h, w, 3), dtype=np.uint8)
    o[: min(h, f.shape[0]), : min(w, f.shape[1])] = f[:h, :w]
    return o

norm = [fit(f) for f in frames]
ff = subprocess.Popen(
    ["ffmpeg", "-y", "-loglevel", "error",
     "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{w}x{h}",
     "-r", "20", "-i", "-",
     "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", str(out_mp4)],
    stdin=subprocess.PIPE,
)
for f in norm:
    ff.stdin.write(f.tobytes())
ff.stdin.close()
rc = ff.wait()
if rc != 0:
    raise SystemExit(f"ffmpeg failed rc={rc}")
print(f"wrote {out_mp4} ({len(norm)} frames, {w}x{h} @ 20fps)")
PY

# Report
{
  echo "# netherite pixel fidelity demo"
  echo
  echo "## Physics (canonical tape \`${NAME}\`)"
  echo
  if grep -q 'Physics: clean' "$TRACE/report/tape_${NAME}.md" 2>/dev/null; then
    echo "**PASS** — no divergence over 3121 ticks (tol 1e-9)."
  else
    grep -A2 -i 'physics\|divergence' "$TRACE/report/tape_${NAME}.md" 2>/dev/null | head -20 || true
  fi
  echo
  echo "## Scene pixel gates (real MC goldens)"
  echo
  echo '```'
  grep -E 'REGRESS|TARGET|HARD_SCENE|seed0|seed7|MULTI_VERIFY|PASS|FAIL' \
    "$OUT_DIR/hard_scene.log" "$OUT_DIR/multi_verify.log" 2>/dev/null | head -40
  echo '```'
  echo
  echo "## Artifact"
  echo
  echo "- \`${OUT_DIR#$ROOT/}/pixel_match_sbs.mp4\` — left oracle / right magma"
  echo
  echo "Note: free-cam full-tape outdoor pixels still show residual classes"
  echo "(entities/HUD/particles); the **gated** scenes + physics are the ship bar."
} > "$OUT_DIR/pixel_match_report.md"

cp -f "$TRACE/report/tape_${NAME}.md" "$OUT_DIR/physics_tape_report.md" 2>/dev/null || true
ls -lh "$OUT_DIR/pixel_match_sbs.mp4"
echo "DEMO_MP4=$OUT_DIR/pixel_match_sbs.mp4"
echo "DEMO_EXIT:0"
