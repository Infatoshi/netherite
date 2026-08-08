#!/usr/bin/env bash
# Run after full train writes native_1p92b.nckpt. GPU0 lease required for sim_sanity.
set -euo pipefail
cd /home/infatoshi/dev/nw/ppo-native-bf16
export UV_CACHE_DIR=/home/infatoshi/.cache/uv
export TMPDIR=/home/infatoshi/dev/nw/.tmp
export CUDA_VISIBLE_DEVICES=0
UV=(uv run --no-project --with torch==2.13.0 --with numpy)
FINAL=optloop_runs/ppo-native-bf16-d55-v4/final
NCKPT=$FINAL/native_1p92b.nckpt
PT=$FINAL/native_1p92b.pt
test -f "$NCKPT"
"${UV[@]}" python blaze/rl/native/convert_checkpoint.py \
  "$NCKPT" "$PT" --receipt "$FINAL/conversion.json" | tee "$FINAL/conversion.log"
set +e
"${UV[@]}" python blaze/rl/native/sim_sanity.py \
  "$PT" --receipt "$FINAL/sim_sanity.json" 2>&1 | tee "$FINAL/sim_sanity.log"
echo "sim_sanity_exit=${PIPESTATUS[0]}" | tee -a "$FINAL/sim_sanity.log"
set -e
python3 - <<'PY'
import csv, json, statistics
from pathlib import Path
final = Path("optloop_runs/ppo-native-bf16-d55-v4/final")
rows = list(csv.DictReader((final/"train_curve.csv").open()))
wall = [float(r["wall_s"]) for r in rows]
deltas = [wall[i]-wall[i-1] for i in range(1,len(wall))]
steady = deltas[5:] if len(deltas)>10 else deltas
steady_ms = [d*1000 for d in steady]
sim = json.loads((final/"sim_sanity.json").read_text()) if (final/"sim_sanity.json").exists() else {}
conv = json.loads((final/"conversion.json").read_text()) if (final/"conversion.json").exists() else {}
last = rows[-1]
receipt = {
  "schema": "netherite.native-full-train.v1",
  "ticks": int(float(last["ticks"])),
  "chunks": int(last["chunk"]),
  "wall_s": float(last["wall_s"]),
  "wall_min": float(last["wall_s"])/60,
  "reward_first": float(rows[0]["reward_mean"]),
  "reward_last": float(last["reward_mean"]),
  "loss_last": float(last["loss_mean"]),
  "episodes": int(last["episodes"]),
  "available_cells_last": int(float(last["available_cells"])),
  "alloc_gib_last": float(last["allocated_gib"]),
  "peak_gib": float(last["peak_allocated_gib"]),
  "all_finite": all(r["finite"]=="1" for r in rows),
  "steady_chunk_wall_ms_median": statistics.median(steady_ms) if steady_ms else None,
  "steady_chunk_wall_ms_mean": statistics.mean(steady_ms) if steady_ms else None,
  "env_ticks_per_s": float(last["ticks"])/float(last["wall_s"]),
  "conversion": conv,
  "sim_sanity": {
    "value_min": sim.get("value_min"),
    "value_max": sim.get("value_max"),
    "values_within_sane_range": sim.get("values_within_sane_range"),
    "multi_category_heads_diverse": sim.get("multi_category_heads_diverse"),
    "deepest_stage_histogram": sim.get("deepest_stage_histogram"),
    "finite_and_non_degenerate": sim.get("finite_and_non_degenerate"),
    "torch_successes": sim.get("torch_successes"),
    "episodes_ended": sim.get("episodes_ended"),
  },
  "checkpoint_nckpt": str(final/"native_1p92b.nckpt"),
  "checkpoint_pt": str(final/"native_1p92b.pt"),
}
(final/"full_train_receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True)+"\n")
print(json.dumps(receipt, indent=2, sort_keys=True))
PY
echo "post_train_eval done"
