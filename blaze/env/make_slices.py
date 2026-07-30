"""Bake per-cell intermediate .bsnp snapshots along the scripted burrow.

The 3-stage curriculum (d6.0/d4.5/d3.0) has a cliff: seeds whose handoff is
already within ~3 blocks of the ore train fine, but seeds needing a real
250-480-tick multi-cell burrow (16, 20, 27, 46) never learn it - the CPU
trainer's in-episode help produced a CONTINUUM of start states along the
tunnel, the 3 stages do not. This bakes s<seed>_m<k>.bsnp after every
single scripted burrow cell (stage_coal budget=1 = one cell) between the
d6.0 handoff and the d3.0 last-mile, restoring that continuum.

Run: cd magma && SEEDS=16,20,27,46 uv run --no-project \
       --with numpy,torch,matplotlib python blaze/env/make_slices.py
"""
import json
import math
import os
import sys

RL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "rl")
sys.path.insert(0, RL)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ppo_coal import make_env, nearest_coal        # noqa: E402
import chain_probe as cp                           # noqa: E402
from make_snapshots import quiesce, snap_liquid_flag, SNAPS  # noqa: E402

OUT = os.path.join(RL, "out")
MAX_CELLS = 12


def bake(seed, prefix):
    env = make_env(seed, prefix)
    cp.step(env, {})            # refresh obs with camera + scan
    rows = []
    try:
        for k in range(1, MAX_CELLS + 1):
            nc = nearest_coal(env.obs)
            if nc is None:
                print(f"  seed {seed}: scan lost the ore, stop", flush=True)
                break
            if nc[2] <= 3.0:
                print(f"  seed {seed}: within 3.0 after {k-1} cells, done",
                      flush=True)
                break
            ok = cp.stage_coal(env, budget=1, stop_dist=3.0)
            if cp.inv(env, cp.IX_COAL) >= 1:
                print(f"  seed {seed} m{k}: burrow mined the coal, stop",
                      flush=True)
                break
            quiesce(env)
            path = os.path.join(SNAPS, f"s{seed}_m{k}.bsnp")
            cp.step(env, {"snapshot": path, "cam": 0})
            liquid, ncoal, _ = snap_liquid_flag(path)
            nc2 = nearest_coal(env.obs)
            print(f"  seed {seed} m{k}: dist {nc[2]:.1f} -> "
                  f"{nc2[2] if nc2 else -1:.1f}  coal={ncoal} "
                  f"[{'LIQUID' if liquid else 'ok'}]", flush=True)
            rows.append(path)
            if ok:
                break
    finally:
        env.proc.kill()
    return rows


def main():
    prefixes = json.load(open(os.path.join(OUT, "coal_prefixes.json")))
    seeds = [int(s) for s in os.environ.get("SEEDS", "16,20,27,46")
             .split(",")]
    for s in seeds:
        print(f"== seed {s}", flush=True)
        bake(s, prefixes[str(s)])


if __name__ == "__main__":
    main()
