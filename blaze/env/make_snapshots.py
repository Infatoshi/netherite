#!/usr/bin/env python3
"""Bake .bsnp training snapshots for the batched env (blaze).

For every seed in rl/out/coal_prefixes.json: replay the scripted prefix
(ppo_coal.make_env), then for each curriculum stage d in STAGES run
chain_probe.stage_coal(stop_dist=d) to burrow within d blocks of the nearest
coal, quiesce QUIESCE no-op ticks (drops settle / get picked up, dig delay
drains), and dump rl/out/snaps/s<seed>_d<d>.bsnp via the env's "snapshot"
action key. Snapshots whose 64x128x64 region contains liquid (ids 8-11) are
FLAGGED - blaze does not simulate the fluids CA, so flagged (seed,stage)
pairs must not be used for batched training.

T0=1 mode bakes FRESH-SPAWN tick-0 snapshots instead (s<seed>_t0.bsnp, one
per coal_prefixes.json seed) with a T0_R-block region radius (default 64:
the s10 full chain wanders <= 5 blocks from spawn and camera rays reach 49,
so 64 covers every ray of the whole spawn-to-torch episode). Tick-0 needs no
quiesce: the state IS the clean post-init pre-first-tick boundary (verified
byte-exact by verify_cpu.py --chain).

Run (anvil):
  cd magma && uv run --no-project --with numpy,torch,matplotlib \
      python blaze/env/make_snapshots.py
  cd magma && T0=1 uv run --no-project --with numpy \
      python blaze/env/make_snapshots.py
(torch/matplotlib are only needed because ppo_coal imports them at module
scope; the curriculum mode reuses its make_env prefix replay.)
"""
import json
import os
import struct
import subprocess
import sys

import numpy as np

RL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "rl")  # blaze/rl
sys.path.insert(0, RL)

OUT = os.path.join(RL, "out")
SNAPS = os.path.join(OUT, "snaps")
STAGES = (6.0, 4.5, 3.0)
QUIESCE = 6
BUDGET = 3000
T0_R = 64
MAGMA = os.path.join(os.path.dirname(os.path.dirname(RL)), "magma")
GAME_BIN = os.path.join(MAGMA, "magma_game")

# BOLR record size (packed, see game/rl_mode.c RlBinObs) - t0 mode reads raw
# records from magma_game --rl-bin without importing verify_cpu.
BIN_SIZE = sum(sz for sz in (4, 8, 8, 8, 8, 4, 4, 4, 36, 36, 4, 4, 36,
                             256 * 16, 64 * 12, 32 * 12,
                             2304 * 2, 2304, 2304))

# .bsnp header layout (blaze/env/blaze_snapshot.h): packed, little-endian.
HEAD_SIZE = 752
N_ITEMS_OFF = 724          # u32
RDIMS_OFF = 728            # 6 x i32: rx0,ry0,rz0,rnx,rny,rnz
ITEM_SIZE = 76


def quiesce(env, n=QUIESCE):
    for _ in range(n):
        cp.step(env, {"cam": 0})


def snap_liquid_flag(path):
    """(has_liquid, ncoal, rnx*rny*rnz) parsed straight from the file."""
    with open(path, "rb") as f:
        buf = f.read()
    assert buf[:4] == b"BSNP", path
    n_items = struct.unpack_from("<I", buf, N_ITEMS_OFF)[0]
    rnx, rny, rnz = struct.unpack_from("<6i", buf, RDIMS_OFF)[3:6]
    vol = rnx * rny * rnz
    off = HEAD_SIZE + ITEM_SIZE * n_items
    cells = np.frombuffer(buf, "<u2", vol, off)
    ids = cells >> 4
    has_liquid = bool(np.any((ids >= 8) & (ids <= 11)))
    ncoal = int(np.count_nonzero(ids == 16))
    return has_liquid, ncoal, vol


def _exact(proc, n):
    buf = b""
    while len(buf) < n:
        c = proc.stdout.read(n - len(buf))
        if not c:
            raise RuntimeError("real env died")
        buf += c
    return buf


def bake_t0(seed, radius=T0_R):
    """Fresh-spawn tick-0 snapshot: spawn the real env (no --snapshot-in),
    consume the initial tick-0 BOLR record, then send a single action whose
    "snapshot" key is processed PRE-tick by rl_mode - so the dump is the
    exact tick-0 state, zero settle ticks needed."""
    path = os.path.join(SNAPS, f"s{seed}_t0.bsnp")
    p = subprocess.Popen(
        [GAME_BIN, "--rl-bin", "--render", "off", "--pace", "unlimited",
         "--seed", str(seed), "--mobs", "off"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL)
    try:
        _exact(p, BIN_SIZE)  # initial (tick 0) obs record
        act = {"snapshot": path, "snapshot_r": radius, "cam": 0}
        p.stdin.write((json.dumps(act) + "\n").encode())
        p.stdin.flush()
        _exact(p, BIN_SIZE)  # tick 1 record => pre-tick snapshot flushed
    finally:
        p.kill()
        p.wait()
    liquid, ncoal, vol = snap_liquid_flag(path)
    flag = "LIQUID" if liquid else "ok"
    print(f"  seed {seed}: {path} vol={vol} coal={ncoal} "
          f"size={os.path.getsize(path)} [{flag}]", flush=True)
    return (seed, "t0", path, flag)


def main_t0():
    os.makedirs(SNAPS, exist_ok=True)
    prefixes = json.load(open(os.path.join(OUT, "coal_prefixes.json")))
    seeds = sorted(int(s) for s in prefixes)
    if os.environ.get("SEEDS"):
        keep = {int(s) for s in os.environ["SEEDS"].split(",")}
        seeds = [s for s in seeds if s in keep]
    print(f"baking t0 snapshots (r={T0_R}) for {len(seeds)} seeds: {seeds}",
          flush=True)
    rows = [bake_t0(s) for s in seeds]
    nliquid = sum(r[3] == "LIQUID" for r in rows)
    print(f"\n{len(rows)} t0 snapshots written, {nliquid} liquid-flagged")


def bake_seed(seed, prefix):
    print(f"== seed {seed}: replaying prefix ({len(prefix)} ticks)",
          flush=True)
    env = make_env(seed, prefix)
    rows = []
    try:
        for d in STAGES:
            ok = cp.stage_coal(env, budget=BUDGET, stop_dist=d)
            if not ok:
                print(f"  seed {seed} d={d}: stage_coal FAILED "
                      f"(scan lost the ore or budget exhausted)", flush=True)
                rows.append((seed, d, None, "stage-failed"))
                continue
            if cp.inv(env, cp.IX_COAL) >= 1:
                print(f"  seed {seed} d={d}: burrow already mined the coal; "
                      f"snapshot skipped", flush=True)
                rows.append((seed, d, None, "already-mined"))
                continue
            quiesce(env)
            path = os.path.join(SNAPS, f"s{seed}_d{d}.bsnp")
            cp.step(env, {"snapshot": path, "cam": 0})
            liquid, ncoal, _ = snap_liquid_flag(path)
            flag = "LIQUID" if liquid else "ok"
            print(f"  seed {seed} d={d}: {path} coal={ncoal} [{flag}]",
                  flush=True)
            rows.append((seed, d, path, flag))
    finally:
        env.proc.kill()
    return rows


def main():
    # Lazy: curriculum mode needs the training stack (torch etc. via
    # ppo_coal); t0 mode must stay subprocess-only.
    global make_env, cp
    from ppo_coal import make_env
    import chain_probe as cp
    os.makedirs(SNAPS, exist_ok=True)
    prefixes = json.load(open(os.path.join(OUT, "coal_prefixes.json")))
    seeds = sorted(int(s) for s in prefixes)
    if os.environ.get("SEEDS"):
        keep = {int(s) for s in os.environ["SEEDS"].split(",")}
        seeds = [s for s in seeds if s in keep]
    print(f"baking snapshots for {len(seeds)} seeds: {seeds}", flush=True)
    all_rows = []
    for s in seeds:
        all_rows += bake_seed(s, prefixes[str(s)])
    print("\n== summary ==")
    nliquid = 0
    for seed, d, path, flag in all_rows:
        print(f"seed {seed:3d} d={d}: "
              f"{os.path.basename(path) if path else '-':<18} {flag}")
        nliquid += flag == "LIQUID"
    written = sum(1 for r in all_rows if r[2])
    print(f"{written} snapshots written, {nliquid} liquid-flagged, "
          f"{len(all_rows) - written} skipped/failed")


if __name__ == "__main__":
    if os.environ.get("T0"):
        main_t0()
    else:
        main()
