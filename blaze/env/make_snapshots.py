#!/usr/bin/env python3
"""Bake .bsnp training snapshots for the batched env (blaze).

For every seed in rl/out/coal_prefixes.json: replay the scripted prefix
(ppo_coal.make_env), then for each curriculum stage d in STAGES run
chain_probe.stage_coal(stop_dist=d) to burrow within d blocks of the nearest
coal, quiesce QUIESCE no-op ticks (drops settle / get picked up, dig delay
drains), and dump rl/out/snaps/s<seed>_d<d>.bsnp via the env's "snapshot"
action key with a CURRICULUM_R-block region radius (default T0_R=64, so
2R x 128 x 2R). Snapshots whose region contains liquid (ids 8-11) are
FLAGGED - blaze does not simulate the fluids CA, so flagged (seed,stage)
pairs must not be used for batched training.

--t0 mode bakes FRESH-SPAWN tick-0 snapshots instead (s<seed>_t0.bsnp, one
per coal_prefixes.json seed) with a --t0-r block region radius (default 64:
the s10 full chain wanders <= 5 blocks from spawn and camera rays reach 49,
so 64 covers every ray of the whole spawn-to-torch episode). Tick-0 needs no
quiesce: the state IS the clean post-init pre-first-tick boundary (verified
byte-exact by verify_cpu.py --chain).

Run (anvil):
  cd magma && uv run --no-project --with numpy,torch,matplotlib \
      python blaze/env/make_snapshots.py
  cd magma && uv run --no-project --with numpy \
      python blaze/env/make_snapshots.py --t0
  cd magma && uv run --no-project --with numpy \
      python blaze/env/make_snapshots.py --expand   # re-dump old d* fixtures
                                                    # IN PLACE at CURRICULUM_R
(torch/matplotlib are only needed because ppo_coal imports them at module
scope; the curriculum mode reuses its make_env prefix replay.)
"""
import argparse
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
# Curriculum stage dumps used the rl_mode default radius (32 -> 64x128x64)
# until 2026-08-07. blaze's world IS the .bsnp region, so a camera ray
# (obs_camera.h OC_FAR=48) leaves any radius-32 fixture the moment the agent
# has a clear sightline: magma's sliding camreg hits real blocks where blaze
# must return air, and verify_cpu could never gate cam on these fixtures
# (measured: 46/46 differing cam pixels at s14_d6.0 tick 0 were rays that had
# left the region). Physics near the edge diverges the same way. Match T0_R
# so every fixture shares one region size (blaze_load_snapshots requires
# identical dims across loaded snapshots) and the camera stays inside.
CURRICULUM_R = 64
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

def strip_snapshot_liquid(path):
    """Replace liquid cells with air for a scoped non-fluid parity fixture."""
    with open(path, "rb") as f:
        buf = bytearray(f.read())
    n_items = struct.unpack_from("<I", buf, N_ITEMS_OFF)[0]
    rnx, rny, rnz = struct.unpack_from("<6i", buf, RDIMS_OFF)[3:6]
    vol = rnx * rny * rnz
    off = HEAD_SIZE + ITEM_SIZE * n_items
    cells = np.frombuffer(buf, "<u2", vol, off)
    ids = cells >> 4
    mask = (ids >= 8) & (ids <= 11)
    count = int(np.count_nonzero(mask))
    cells[mask] = 0
    with open(path, "wb") as f:
        f.write(buf)
    return count


def _exact(proc, n):
    buf = b""
    while len(buf) < n:
        c = proc.stdout.read(n - len(buf))
        if not c:
            raise RuntimeError("real env died")
        buf += c
    return buf

def relight_snapshot(path, seed, radius):
    """Round-trip an edited snapshot through Magma so its v2 light tail
    matches the edited block cells."""
    tmp = f"{path}.relight"
    p = subprocess.Popen(
        [GAME_BIN, "--rl-bin", "--render", "off", "--pace", "unlimited",
         "--seed", str(seed), "--mobs", "off", "--snapshot-in", path],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL)
    try:
        _exact(p, BIN_SIZE)
        act = {"snapshot": tmp, "snapshot_r": radius, "cam": 0}
        p.stdin.write((json.dumps(act) + "\n").encode())
        p.stdin.flush()
        _exact(p, BIN_SIZE)
    finally:
        p.kill()
        p.wait()
    os.replace(tmp, path)


def bake_t0(seed, radius=None, path=None, strip_liquid=False):
    """Fresh-spawn tick-0 snapshot: spawn the real env (no --snapshot-in),
    consume the initial tick-0 BOLR record, then send a single action whose
    "snapshot" key is processed PRE-tick by rl_mode - so the dump is the
    exact tick-0 state, zero settle ticks needed."""
    if radius is None:
        radius = T0_R
    path = path or os.path.join(SNAPS, f"s{seed}_t0.bsnp")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
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
    if strip_liquid:
        stripped = strip_snapshot_liquid(path)
        relight_snapshot(path, seed, radius)
        print(f"  seed {seed}: stripped {stripped} liquid cells for "
              "non-fluid parity fixture", flush=True)
    liquid, ncoal, vol = snap_liquid_flag(path)
    flag = "LIQUID" if liquid else "ok"
    print(f"  seed {seed}: {path} vol={vol} coal={ncoal} "
          f"size={os.path.getsize(path)} [{flag}]", flush=True)
    return (seed, "t0", path, flag)


def _filter_seeds(all_seeds, seeds_arg):
    if not seeds_arg:
        return list(all_seeds)
    keep = {int(s) for s in seeds_arg.split(",") if s.strip()}
    return [s for s in all_seeds if s in keep]


def main_t0(seeds_arg=None, snap_path=None, strip_liquid=False):
    os.makedirs(SNAPS, exist_ok=True)
    prefixes = json.load(open(os.path.join(OUT, "coal_prefixes.json")))
    seeds = _filter_seeds(sorted(int(s) for s in prefixes), seeds_arg)
    print(f"baking t0 snapshots (r={T0_R}) for {len(seeds)} seeds: {seeds}",
          flush=True)
    if snap_path and len(seeds) != 1:
        raise ValueError("--snap-path requires exactly one selected seed")
    rows = [bake_t0(s, path=snap_path, strip_liquid=strip_liquid)
            for s in seeds]
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
                # cp.LAST_FAIL splits the two very different skips:
                # "scan-empty" (no coal in the obs window at all) vs
                # "budget-exhausted" (the burrow ran out of ticks).
                print(f"  seed {seed} d={d}: stage_coal FAILED "
                      f"({cp.LAST_FAIL})", flush=True)
                rows.append((seed, d, None, f"stage-failed:{cp.LAST_FAIL}"))
                continue
            if cp.inv(env, cp.IX_COAL) >= 1:
                print(f"  seed {seed} d={d}: burrow already mined the coal; "
                      f"snapshot skipped", flush=True)
                rows.append((seed, d, None, "already-mined"))
                continue
            quiesce(env)
            path = os.path.join(SNAPS, f"s{seed}_d{d}.bsnp")
            cp.step(env, {"snapshot": path, "snapshot_r": CURRICULUM_R,
                          "cam": 0})
            liquid, ncoal, _ = snap_liquid_flag(path)
            flag = "LIQUID" if liquid else "ok"
            print(f"  seed {seed} d={d}: {path} coal={ncoal} [{flag}]",
                  flush=True)
            rows.append((seed, d, path, flag))
    finally:
        env.proc.kill()
    return rows


def main(seeds_arg=None):
    # Lazy: curriculum mode needs the training stack (torch etc. via
    # ppo_coal); t0 mode must stay subprocess-only.
    global make_env, cp
    import chain_probe as cp
    from ppo_coal import make_env
    os.makedirs(SNAPS, exist_ok=True)
    prefixes = json.load(open(os.path.join(OUT, "coal_prefixes.json")))
    seeds = _filter_seeds(sorted(int(s) for s in prefixes), seeds_arg)
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


def main_expand(seeds_arg=None, stages_sel=None):
    """--expand: re-dump existing .bsnp fixtures at CURRICULUM_R in place.

    Fixtures baked before 2026-08-07 use the rl_mode default radius 32, which
    is smaller than the camera reach (OC_FAR=48), so verify_cpu can only
    BLOCK on them (fixture camera envelope). This replays each file through
    magma --snapshot-in and re-dumps it with the larger radius: the inner
    cells are byte-identical (magma restored them) and the new shell is the
    same worldgen magma's camreg reads, so the cam compare becomes exact
    again. Cheaper and attrition-free vs re-running the curriculum bake.

    --expand [--seeds 14,27] [--stages-sel 6.0,4.5] [--curriculum-r 64]
    """
    seeds = None
    if seeds_arg:
        seeds = {int(s) for s in seeds_arg.split(",") if s.strip()}
    want = ([float(s) for s in stages_sel.split(",") if s.strip()]
            if stages_sel else list(STAGES))
    rows = []
    for name in sorted(os.listdir(SNAPS)):
        if not name.startswith("s") or not name.endswith(".bsnp"):
            continue
        stem = name[1:-5]
        if "_d" not in stem:
            continue          # t0 / iron fixtures already carry T0_R
        seed_text, stage_text = stem.split("_d", 1)
        try:
            seed, stage = int(seed_text), float(stage_text)
        except ValueError:
            continue
        if (seeds is not None and seed not in seeds) or stage not in want:
            continue
        path = os.path.join(SNAPS, name)
        before = read_region_dims(path)
        if before[3] >= 2 * CURRICULUM_R and before[5] >= 2 * CURRICULUM_R:
            print(f"  {name}: already {before[3]}x{before[4]}x{before[5]}, "
                  f"skipped", flush=True)
            continue
        relight_snapshot(path, seed, CURRICULUM_R)
        after = read_region_dims(path)
        print(f"  {name}: {before[3]}x{before[4]}x{before[5]} -> "
              f"{after[3]}x{after[4]}x{after[5]}", flush=True)
        rows.append(name)
    print(f"\n{len(rows)} fixtures re-dumped at radius {CURRICULUM_R}")


def read_region_dims(path):
    """(rx0, ry0, rz0, rnx, rny, rnz) from a .bsnp head."""
    with open(path, "rb") as f:
        head = f.read(HEAD_SIZE)
    return struct.unpack_from("<6i", head, RDIMS_OFF)


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--expand", action="store_true",
                    help="re-dump existing d* fixtures at --curriculum-r")
    ap.add_argument("--t0", action="store_true",
                    help="bake fresh-spawn tick-0 snapshots (s<seed>_t0.bsnp)")
    ap.add_argument("--t0-r", type=int, default=64,
                    help="region radius for t0 snapshots (default 64)")
    ap.add_argument("--curriculum-r", type=int, default=None,
                    help="region radius for curriculum dumps "
                         "(default: same as --t0-r)")
    ap.add_argument("--seeds", default=None,
                    help="comma-separated seed filter")
    ap.add_argument("--snap-path", default=None,
                    help="explicit output path (t0 mode, single seed only)")
    ap.add_argument("--stages-sel", default=None,
                    help="comma-separated stages for --expand "
                         "(default: 6.0,4.5,3.0)")
    ap.add_argument("--strip-liquid", action="store_true",
                    help="replace liquid cells with air after t0 bake")
    return ap.parse_args(argv)


if __name__ == "__main__":
    args = parse_args()
    T0_R = args.t0_r
    CURRICULUM_R = (args.curriculum_r if args.curriculum_r is not None
                    else T0_R)
    # Module-level names used by bake_* / main_expand.
    globals()["T0_R"] = T0_R
    globals()["CURRICULUM_R"] = CURRICULUM_R
    if args.expand:
        main_expand(seeds_arg=args.seeds, stages_sel=args.stages_sel)
    elif args.t0:
        main_t0(seeds_arg=args.seeds, snap_path=args.snap_path,
                strip_liquid=args.strip_liquid)
    else:
        main(seeds_arg=args.seeds)
