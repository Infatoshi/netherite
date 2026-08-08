#!/usr/bin/env python3
"""Summarize NATIVE_TELEMETRY JSONL for HEALTH / VERDICT docs."""
import json, sys, math
from pathlib import Path

def load(path):
    rows = []
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if line:
            rows.append(json.loads(line))
    return rows

def stats(vals):
    vals = [v for v in vals if v is not None and math.isfinite(v)]
    if not vals:
        return None
    n = len(vals)
    mean = sum(vals) / n
    return {
        "n": n,
        "first": vals[0],
        "last": vals[-1],
        "min": min(vals),
        "max": max(vals),
        "mean": mean,
        "mean_first10": sum(vals[:10]) / min(10, n),
        "mean_last10": sum(vals[-10:]) / min(10, n),
        "mean_chunks_91_100": sum(vals[90:100]) / max(1, len(vals[90:100])) if n >= 100 else None,
        "mean_chunks_291_300": sum(vals[290:300]) / max(1, len(vals[290:300])) if n >= 300 else None,
    }

def main(path):
    rows = load(path)
    keys = [
        "wall_ms", "reward_mean", "policy_loss", "value_loss", "entropy",
        "approx_kl", "clip_frac", "grad_norm_pre", "grad_norm_max",
        "grad_clip_frac", "value_grad_norm", "param_norm", "update_norm",
        "v_min", "v_max", "v_mean", "ret_min", "ret_max", "available_cells",
    ]
    print(f"chunks={len(rows)} ticks_last={rows[-1].get('ticks') if rows else None}")
    # |V| trajectory at key points
    for idx in [0, 9, 49, 99, 199, 299]:
        if idx < len(rows):
            r = rows[idx]
            print(f"chunk={r['chunk']:4d} v_min={r['v_min']:.6g} v_max={r['v_max']:.6g} "
                  f"|V|max={max(abs(r['v_min']), abs(r['v_max'])):.6g} "
                  f"reward={r['reward_mean']:.6g} wall_ms={r['wall_ms']:.2f} "
                  f"cells={r['available_cells']} entropy={r['entropy']:.4f} "
                  f"kl={r['approx_kl']:.6g} clip_frac={r['clip_frac']:.4f} "
                  f"grad_pre={r['grad_norm_pre']:.4f}")
    print("--- channel stats ---")
    for k in keys:
        s = stats([r.get(k) for r in rows])
        if s is None:
            continue
        print(f"{k:18s} first={s['first']:.6g} last={s['last']:.6g} "
              f"min={s['min']:.6g} max={s['max']:.6g} "
              f"mean_f10={s['mean_first10']:.6g} mean_l10={s['mean_last10']:.6g}")
    # steady wall (skip first chunk)
    walls = [r['wall_ms'] for r in rows[1:]]
    if walls:
        walls_sorted = sorted(walls)
        med = walls_sorted[len(walls_sorted)//2]
        print(f"steady_wall_ms median={med:.3f} mean={sum(walls)/len(walls):.3f} n={len(walls)}")
    # max |V|
    vmax_abs = max(max(abs(r['v_min']), abs(r['v_max'])) for r in rows)
    print(f"max_abs_V_over_run={vmax_abs:.6g}")
    print(f"max_abs_V_at_end={max(abs(rows[-1]['v_min']), abs(rows[-1]['v_max'])):.6g}")

if __name__ == "__main__":
    main(sys.argv[1])
