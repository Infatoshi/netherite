#!/usr/bin/env bash
# Pinned gate: magma product populate WRAPPER vs blaze overworld_region reference
# census under matched policy (fluid OFF, shroomlight stale).
#
# Blessed residuals live in verify/worldgen/known_divergences.json. Any drift in
# per-seed diff_cells, class breakdown, or diff content hash FAILS.
#
# Blessing rationale (maintainer, 2026-08-02): ore-family multi-window apply
# (stone -> granite/diorite/andesite) and load-order raster-vs-reverse delta are
# accepted wrapper mechanics, not bugs. See the sidecar comment field.
#
# OPEN policy (not a behavior pin): MAGMA_FLUID_CA=1 changes 279/10936/4885 cells
# at seeds 0/7/9; no gate pins magma default-off vs blaze always-on.
#
# Usage:
#   bash verify/worldgen/wrapper_gate.sh              # rc=0 exact match
#   bash verify/worldgen/wrapper_gate.sh --update     # re-bless sidecar (loud warning)
#   bash verify/worldgen/wrapper_gate.sh [OUTDIR] [--update] [seeds...]
#
# Defaults: OUTDIR=verify/worldgen/out  seeds=0 7 9 19
# CPU-only. Does not need a GPU.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIDE="$ROOT/verify/worldgen/known_divergences.json"
OUT=""
UPDATE=0
SEEDS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --update) UPDATE=1; shift ;;
    -h|--help)
      sed -n '2,25p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    *)
      if [[ -z "$OUT" && "$1" != [0-9]* ]]; then
        OUT="$1"
      else
        SEEDS+=("$1")
      fi
      shift
      ;;
  esac
done

OUT="${OUT:-$ROOT/verify/worldgen/out}"
if [[ ${#SEEDS[@]} -eq 0 ]]; then
  SEEDS=(0 7 9 19)
fi

export UV_CACHE_DIR="${UV_CACHE_DIR:-/home/infatoshi/.cache/uv}"
export TMPDIR="${TMPDIR:-/home/infatoshi/dev/nw/.tmp}"
mkdir -p "$TMPDIR" "$OUT"

BLAZE="$ROOT/blaze"
MAGMA="$ROOT/magma"
BLAZE_DUMP="$BLAZE/build/bin/cpu/owr_policy_dump"
WORLD_DUMP="$MAGMA/trace/world_dump"
CRWD_PY="$ROOT/verify/worldgen/crwd_to_sparse.py"
CLASS_PY="$ROOT/verify/worldgen/classify_diff.py"
UV=(uv run --no-project --with numpy)

echo "=== wrapper_gate: census pin vs known_divergences.json ==="
echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "root: $ROOT"
echo "seeds: ${SEEDS[*]}"
echo "sidecar: $SIDE"
echo "outdir: $OUT"
echo "policy: fluid=OFF  shroomlight=stale"
echo ""

# ---------- build tools ----------
echo "[build] blaze owr_policy_dump"
make -C "$BLAZE" build/bin/cpu/owr_policy_dump >/dev/null

echo "[build] magma world_dump"
if [[ ! -x "$WORLD_DUMP" ]] || \
   [[ "$MAGMA/world/populate_mc.c" -nt "$WORLD_DUMP" ]] || \
   [[ "$MAGMA/trace/world_dump.c" -nt "$WORLD_DUMP" ]]; then
  bash "$MAGMA/trace/build_world_dump.sh" >/dev/null
else
  echo "  world_dump present and up to date"
fi

# ---------- fluid-on sanity (same as wrapper_diff acceptance a) ----------
echo "[sanity] fluid ON == overworld_region seed 0"
make -C "$BLAZE" build/bin/cpu/overworld_region >/dev/null
REF_FULL="$OUT/ref_overworld_region_s0.full"
POL_FULL="$OUT/policy_fluid_on_s0.full"
"$BLAZE/build/bin/cpu/overworld_region" 0 0 0 >"$REF_FULL"
"$BLAZE_DUMP" --fluid on --shroomlight ca --format full 0 0 0 >"$POL_FULL"
if ! cmp -s "$REF_FULL" "$POL_FULL"; then
  echo "FAIL: owr_policy_dump fluid-on diverges from overworld_region" >&2
  exit 1
fi
echo "  PASS: fluid-on policy dump byte-identical to overworld_region"

# ---------- per-seed census ----------
echo ""
echo "=== per-seed census ==="
MEASURED_JSON="$OUT/measured_census.json"
: >"$OUT/.measured_rows"

for S in "${SEEDS[@]}"; do
  SDIR="$OUT/seed_$S"
  mkdir -p "$SDIR"
  echo "--- seed $S ---"

  "$BLAZE_DUMP" --fluid off --shroomlight stale --format sparse "$S" 0 0 \
    >"$SDIR/blaze_sparse.txt"

  unset MAGMA_FLUID_CA || true
  unset MAGMA_SHROOMLIGHT || true
  BIN="$SDIR/magma_crwd.bin"
  if ! "$WORLD_DUMP" --seed "$S" --cx0 0 --cz0 0 --ncx 2 --ncz 2 --out "$BIN" \
      2>"$SDIR/magma_world_dump.err"; then
    echo "FAIL: world_dump seed $S" >&2
    cat "$SDIR/magma_world_dump.err" >&2
    exit 1
  fi
  "${UV[@]}" python3 "$CRWD_PY" "$BIN" -o "$SDIR/magma_sparse.txt" 2>/dev/null

  "${UV[@]}" python3 "$CLASS_PY" "$SDIR/blaze_sparse.txt" "$SDIR/magma_sparse.txt" \
    --label "seed=$S" | tee "$SDIR/classify.txt"
done

# Compute measured census + hashes (hash recipe documented in sidecar).
"${UV[@]}" python3 - "$OUT" "${SEEDS[@]}" <<'PY' >"$MEASURED_JSON"
from __future__ import annotations
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

FLUID = {2, 11, 12, 13}
MUSHROOM = {42, 43, 79, 80}

def load(path: Path) -> dict[tuple[int, int, int], int]:
    m: dict[tuple[int, int, int], int] = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            xs, ys, zs, ss = line.split(",")
            m[(int(xs), int(ys), int(zs))] = int(ss)
    return m

def classify(a: int, b: int) -> str:
    if a in FLUID or b in FLUID:
        return "fluid"
    if a in MUSHROOM or b in MUSHROOM:
        return "mushroom"
    return "other"

def seed_record(sd: Path) -> dict:
    blaze = load(sd / "blaze_sparse.txt")
    magma = load(sd / "magma_sparse.txt")
    keys = set(blaze) | set(magma)
    only_b = only_m = both = 0
    cls: Counter[str] = Counter()
    lines: list[str] = []
    for k in keys:
        ba = blaze.get(k, 0)
        ma = magma.get(k, 0)
        if ba == ma:
            continue
        if ba and not ma:
            only_b += 1
        elif ma and not ba:
            only_m += 1
        else:
            both += 1
        cls[classify(ba, ma)] += 1
        x, y, z = k
        lines.append(f"{x},{y},{z},{ba},{ma}\n")
    lines.sort()
    h = hashlib.sha256("".join(lines).encode("utf-8")).hexdigest()
    return {
        "diff_cells": only_b + only_m + both,
        "diff_sha256": h,
        "blaze_cells": len(blaze),
        "magma_cells": len(magma),
        "only_blaze": only_b,
        "only_magma": only_m,
        "both_side": both,
        "class": {
            "fluid": int(cls["fluid"]),
            "mushroom": int(cls["mushroom"]),
            "other": int(cls["other"]),
        },
    }

out = Path(sys.argv[1])
seeds = [int(s) for s in sys.argv[2:]]
doc = {str(s): seed_record(out / f"seed_{s}") for s in seeds}
json.dump(doc, sys.stdout, indent=2)
sys.stdout.write("\n")
PY

echo ""
echo "measured: $MEASURED_JSON"

# ---------- --update: re-bless ----------
if [[ "$UPDATE" -eq 1 ]]; then
  cat <<'WARN'

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  WARNING: RE-BLESSING known_divergences.json
  This is maintainer judgment. Do not re-bless to silence a real
  regression. Only re-bless when the product intentionally changes
  wrapper mechanics and the new residual is accepted as KNOWN.
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

WARN
  GIT_SHA="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
  DATE_U="$(date -u +%Y-%m-%d)"
  "${UV[@]}" python3 - "$SIDE" "$MEASURED_JSON" "$GIT_SHA" "$DATE_U" "${SEEDS[@]}" <<'PY'
from __future__ import annotations
import json
import sys
from pathlib import Path

side_path = Path(sys.argv[1])
measured = json.loads(Path(sys.argv[2]).read_text())
git_sha = sys.argv[3]
date_u = sys.argv[4]
seed_list = [int(s) for s in sys.argv[5:]]

prev = {}
if side_path.is_file():
    try:
        prev = json.loads(side_path.read_text())
    except json.JSONDecodeError:
        prev = {}

lo = prev.get("load_order_note", {
    "seed": 9,
    "raster_vs_rev_diff_cells": None,
    "comment": (
        "Informational: magma-vs-magma prep-list raster vs reverse order delta. "
        "Accepted as wrapper load-order mechanics; not a separate pin."
    ),
})

doc = {
    "version": 1,
    "comment": (
        f"Blessed {date_u} on git {git_sha}. "
        "Maintainer accepted the magma product wrapper vs blaze owr reference residuals "
        "under matched policy (fluid OFF, shroomlight stale) as KNOWN wrapper mechanics, "
        "not bugs: (1) ore-family multi-window apply (stone->granite/diorite/andesite and "
        "related 1->N transitions across the 2x2 origin window), (2) load-order "
        "raster-vs-reverse prep-list delta. "
        "Gate fails on any drift from these per-seed counts, class breakdowns, and "
        "diff content hashes. "
        "OPEN policy (not pinned here): MAGMA_FLUID_CA=1 changes 279/10936/4885 cells at "
        "seeds 0/7/9 and no gate pins magma default-off vs blaze always-on."
    ),
    "blessed_at": date_u,
    "blessed_git_sha": git_sha,
    "policy": {
        "fluid": "off",
        "shroomlight": "stale",
        "region": "origin 2x2 chunks (bcx=0, bcz=0) == owr_run window",
        "seeds": seed_list,
    },
    "hash_recipe": (
        "For each cell where blaze_state != magma_state (missing side treated as 0), "
        "emit one line 'x,y,z,blaze_state,magma_state\\n'. Sort lines lexicographically "
        "as UTF-8 text. diff_sha256 is the lowercase hex sha256 of the joined bytes."
    ),
    "open_policy_note": (
        "MAGMA_FLUID_CA=1 changes 279/10936/4885 cells at seeds 0/7/9; no gate pins "
        "magma default-off vs blaze always-on. Out of scope for this census gate."
    ),
    "load_order_note": lo,
    "seeds": {str(s): measured[str(s)] for s in seed_list},
}
side_path.write_text(json.dumps(doc, indent=2) + "\n")
print(f"updated: {side_path}")
for s in seed_list:
    r = measured[str(s)]
    print(
        f"  seed {s}: diff_cells={r['diff_cells']} "
        f"sha256={r['diff_sha256'][:16]}... "
        f"class fluid={r['class']['fluid']} mush={r['class']['mushroom']} "
        f"other={r['class']['other']}"
    )
PY
  echo ""
  echo "wrapper_gate: sidecar rewritten (--update). Re-run without --update to verify."
  exit 0
fi

# ---------- compare against sidecar ----------
if [[ ! -f "$SIDE" ]]; then
  echo "FAIL: missing sidecar $SIDE" >&2
  echo "       Run with --update after a deliberate bless, or restore the file." >&2
  exit 1
fi

echo ""
echo "=== compare measured vs blessed ==="
set +e
"${UV[@]}" python3 - "$SIDE" "$MEASURED_JSON" "${SEEDS[@]}" <<'PY'
from __future__ import annotations
import json
import sys
from pathlib import Path

side = json.loads(Path(sys.argv[1]).read_text())
meas = json.loads(Path(sys.argv[2]).read_text())
seeds = [str(s) for s in sys.argv[3:]]
blessed = side.get("seeds") or {}
failed = 0

def delta(a, b):
    return f"{b} (blessed {a}, delta {b - a:+d})"

for s in seeds:
    print(f"--- seed {s} ---")
    if s not in blessed:
        print(f"  FAIL: seed {s} not present in sidecar")
        failed += 1
        continue
    if s not in meas:
        print(f"  FAIL: seed {s} not present in measured census")
        failed += 1
        continue
    b = blessed[s]
    m = meas[s]
    ok = True

    if m["diff_cells"] != b["diff_cells"]:
        print(f"  FAIL diff_cells: {delta(b['diff_cells'], m['diff_cells'])}")
        ok = False
    else:
        print(f"  ok   diff_cells: {m['diff_cells']}")

    for k in ("fluid", "mushroom", "other"):
        bv = int(b.get("class", {}).get(k, -1))
        mv = int(m.get("class", {}).get(k, -1))
        if mv != bv:
            print(f"  FAIL class.{k}: {delta(bv, mv)}")
            ok = False
        else:
            print(f"  ok   class.{k}: {mv}")

    bh = b.get("diff_sha256", "")
    mh = m.get("diff_sha256", "")
    if mh != bh:
        print(f"  FAIL diff_sha256:")
        print(f"       blessed  {bh}")
        print(f"       measured {mh}")
        ok = False
    else:
        print(f"  ok   diff_sha256: {mh}")

    # Optional structural fields: report only if present on both and mismatch
    for k in ("blaze_cells", "magma_cells", "only_blaze", "only_magma", "both_side"):
        if k in b and k in m and m[k] != b[k]:
            print(f"  FAIL {k}: {delta(b[k], m[k])}")
            ok = False

    if ok:
        print(f"  PASS seed {s}")
    else:
        print(f"  FAIL seed {s}")
        failed += 1

# Extra seeds in sidecar not requested: ignore (gate only pins listed seeds)
if failed:
    print("")
    print(f"wrapper_gate: FAIL ({failed} seed(s) drifted from blessed census)")
    sys.exit(1)
print("")
print("wrapper_gate: ALL PASS (census matches known_divergences.json)")
sys.exit(0)
PY
rc=$?
set -e
exit "$rc"
