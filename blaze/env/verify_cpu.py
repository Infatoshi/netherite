#!/usr/bin/env python3
"""M1 fidelity gate: real magma_game (--rl-bin --snapshot-in) vs batch-of-1
CPU blaze, stepped in lockstep with identical action streams; EVERY tick's
observation must match byte-for-byte on the gated fields.

Gated fields (byte ranges of the packed BOLR record): magic..inv_counts
(tick, pose, dead, hotbar, container, inv_counts) and coal..edge (coal list,
cam, depth, edge). blocks/logs are excluded BY DESIGN: their membership rides
the real env's scan-cache rebuild cadence (world_dirty countdown), which
blaze does not reproduce; the trainer never reads them.

Usage (anvil):
  uv run --no-project --with numpy python blaze/env/verify_cpu.py \
      [--seeds 14,16,...] [--ticks 1000] [--stage 6.0] [--episodes FILE.jsonl]

Default: the 8 training seeds x 1000 seeded pseudo-random actions
(attack-heavy; same xorshift32 as blaze_verify.c). --episodes replays real
trained action streams instead (one JSON list of action dicts per line).
--chain runs the FULL spawn-to-torch gate: the committed
rl/out/chain_actions_s10.json (movement + hotbar + use/place + craft:N +
interact) replayed from the fresh-spawn tick-0 snapshot s10_t0.bsnp -
craft/interact/use are simulated in blaze, nothing is stripped.
"""
import argparse
import ctypes
import json
import os
import struct
import subprocess
import sys

RL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "rl")
MAGMA = os.path.join(os.path.dirname(os.path.dirname(RL)), "magma")
BIN = os.path.join(MAGMA, "magma_game")
SO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blaze_cpu.so")
SNAPS = os.path.join(RL, "out", "snaps")

TRAIN_SEEDS = [14, 16, 20, 27, 29, 32, 44, 46]
YAWS = (-15.0, 0.0, 15.0)
PITCHES = (-10.0, 0.0, 10.0)

# BOLR record layout (packed): see game/rl_mode.c RlBinObs.
OFF = {}
o = 0
for name, sz in [("magic", 4), ("tick", 8), ("x", 8), ("y", 8), ("z", 8),
                 ("yaw", 4), ("pitch", 4), ("dead", 4),
                 ("hotbar_ids", 36), ("hotbar_counts", 36),
                 ("hotbar_sel", 4), ("container", 4), ("inv_counts", 36),
                 ("blocks", 256 * 16), ("logs", 64 * 12), ("coal", 32 * 12),
                 ("cam", 2304 * 2), ("depth", 2304), ("edge", 2304)]:
    OFF[name] = (o, o + sz)
    o += sz
BIN_SIZE = o
GATED = [(0, OFF["blocks"][0]), (OFF["coal"][0], BIN_SIZE)]


class Rng:
    """xorshift32, kept in sync with blaze_verify.c."""
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x
        return x


def rand_action(rng):
    return {"dyaw": YAWS[rng.next() % 3], "dpitch": PITCHES[rng.next() % 3],
            "forward": int(rng.next() % 4 != 0), "jump": int(rng.next() % 8 == 0),
            "attack": int(rng.next() % 4 != 3)}


class RealEnv:
    def __init__(self, seed, snap):
        self.proc = subprocess.Popen(
            [BIN, "--rl-bin", "--render", "off", "--pace", "unlimited",
             "--seed", str(seed), "--mobs", "off", "--snapshot-in", snap],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL)
        self.rec = self._read()

    def _read(self):
        win = self._exact(4)
        while win != b"BOLR":
            win = win[1:] + self._exact(1)
        return win + self._exact(BIN_SIZE - 4)

    def _exact(self, n):
        buf = b""
        while len(buf) < n:
            c = self.proc.stdout.read(n - len(buf))
            if not c:
                raise RuntimeError("real env died")
            buf += c
        return buf

    def step(self, act):
        self.proc.stdin.write((json.dumps(act) + "\n").encode())
        self.proc.stdin.flush()
        self.rec = self._read()
        return self.rec

    def close(self):
        self.proc.kill()


class Blaze1:
    def __init__(self, snap):
        self.lib = ctypes.CDLL(SO)
        self.lib.blaze_create.restype = ctypes.c_void_p
        self.lib.blaze_create.argtypes = [ctypes.c_int, ctypes.c_int]
        self.lib.blaze_destroy.argtypes = [ctypes.c_void_p]
        self.h = self.lib.blaze_create(0, 1)
        assert self.h
        err = ctypes.create_string_buffer(256)
        paths = (ctypes.c_char_p * 1)(snap.encode())
        r = self.lib.blaze_load_snapshots(ctypes.c_void_p(self.h), paths, 1,
                                          err, 256)
        if r < 0:
            raise RuntimeError(f"load_snapshots: {err.value.decode()}")
        self.liquid = self.lib.blaze_snapshot_has_liquid(
            ctypes.c_void_p(self.h), 0)
        assign = (ctypes.c_int * 1)(0)
        assert self.lib.blaze_assign(ctypes.c_void_p(self.h), assign) == 0
        assert self.lib.blaze_reset(ctypes.c_void_p(self.h), None) == 0
        assert self.lib.blaze_obs_size() == BIN_SIZE, \
            f"CuBinObs {self.lib.blaze_obs_size()} != RlBinObs {BIN_SIZE}"
        self.buf = ctypes.create_string_buffer(BIN_SIZE)

    def emit(self, want_cam=1):
        assert self.lib.blaze_emit(ctypes.c_void_p(self.h), 0, want_cam,
                                   self.buf) == 0
        return self.buf.raw

    def step(self, act):
        a = (ctypes.c_double * 13)(
            act.get("forward", 0), act.get("strafe", 0), act.get("dyaw", 0),
            act.get("dpitch", 0), act.get("jump", 0), act.get("sneak", 0),
            act.get("sprint", 0), act.get("attack", 0), act.get("use", 0),
            act.get("hotbar", -1), act.get("craft", -1),
            act.get("interact", 0), act.get("smelt", 0))
        r = self.lib.blaze_tick_raw(ctypes.c_void_p(self.h), 0, a,
                                    act.get("cam", 1), self.buf)
        assert r == 0, "blaze_tick_raw failed"
        return self.buf.raw

    def debug(self):
        out = (ctypes.c_double * 21)()
        self.lib.blaze_debug_state(ctypes.c_void_p(self.h), 0, out, 21)
        return list(out)

    def close(self):
        self.lib.blaze_destroy(ctypes.c_void_p(self.h))


def first_diff_field(a, b):
    for name, (lo, hi) in OFF.items():
        if name in ("blocks", "logs"):
            continue
        if a[lo:hi] != b[lo:hi]:
            return name
    return None


def fmt_field(rec, name):
    lo, hi = OFF[name]
    raw = rec[lo:hi]
    if name in ("x", "y", "z", "tick"):
        return struct.unpack("<d" if name != "tick" else "<q", raw)[0]
    if name in ("yaw", "pitch"):
        return struct.unpack("<f", raw)[0]
    if name in ("dead", "hotbar_sel", "container"):
        return struct.unpack("<i", raw)[0]
    if name == "coal":
        v = struct.unpack(f"<{(hi-lo)//4}i", raw)
        return [v[i:i+3] for i in range(0, 24, 3)]
    if name in ("hotbar_ids", "hotbar_counts", "inv_counts"):
        return struct.unpack(f"<{(hi-lo)//4}i", raw)
    if name in ("cam", "depth", "edge"):
        n = sum(1 for _ in raw)
        return f"<{name}: {n} bytes, first diff elsewhere>"
    return raw.hex()


def gated_equal(a, b):
    return all(a[lo:hi] == b[lo:hi] for lo, hi in GATED)


def run_seed(seed, snap, actions, label, show_final_inv=False):
    real = RealEnv(seed, snap)
    cu = Blaze1(snap)
    if cu.liquid:
        print(f"  note: seed {seed} snapshot region contains liquid "
              f"(flagged; fluids CA not simulated)")
    ok = True
    try:
        a_rec, b_rec = real.rec, cu.emit(1)
        if not gated_equal(a_rec, b_rec):
            f = first_diff_field(a_rec, b_rec)
            print(f"  seed {seed} [{label}] INITIAL obs differs: field {f}")
            print(f"    real:  {fmt_field(a_rec, f)}")
            print(f"    blaze: {fmt_field(b_rec, f)}")
            ok = False
        for t, act in enumerate(actions):
            if not ok:
                break
            a_rec = real.step(act)
            b_rec = cu.step(act)
            if not gated_equal(a_rec, b_rec):
                f = first_diff_field(a_rec, b_rec)
                print(f"  seed {seed} [{label}] FIRST DIVERGENCE tick {t} "
                      f"(env tick {struct.unpack('<q', a_rec[4:12])[0]}): "
                      f"field {f}")
                print(f"    action: {act}")
                print(f"    real:  {fmt_field(a_rec, f)}")
                print(f"    blaze: {fmt_field(b_rec, f)}")
                d = cu.debug()
                print(f"    blaze state: pos=({d[0]:.17g},{d[1]:.17g},"
                      f"{d[2]:.17g}) mot=({d[3]:.17g},{d[4]:.17g},{d[5]:.17g})"
                      f" og={d[8]:.0f} dig=({d[12]:.4f},{d[13]:.0f},"
                      f"{d[14]:.0f})")
                ok = False
    finally:
        if ok and show_final_inv:
            names = ["log", "planks", "stick", "cobble", "table", "wpick",
                     "spick", "coal", "torch"]
            inv = fmt_field(a_rec, "inv_counts")
            print("  final inv_counts: "
                  + " ".join(f"{n}={c}" for n, c in zip(names, inv) if c))
        real.close()
        cu.close()
    if ok:
        print(f"  seed {seed} [{label}]: {len(actions)} ticks, ZERO diffs")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", default=",".join(map(str, TRAIN_SEEDS)))
    ap.add_argument("--ticks", type=int, default=1000)
    ap.add_argument("--stage", default="6.0")
    ap.add_argument("--episodes", default=None,
                    help="jsonl of real trained action lists (one per line)")
    ap.add_argument("--chain", action="store_true",
                    help="full spawn-to-torch chain gate: replay "
                         "rl/out/chain_actions_s10.json (craft/interact/use "
                         "INCLUDED) from the fresh-spawn tick-0 snapshot "
                         "rl/out/snaps/s10_t0.bsnp")
    ap.add_argument("--chain-seed", type=int, default=10)
    ap.add_argument("--iron", action="store_true",
                    help="iron-stage gate: replay "
                         "rl/out/iron_actions_s10.json (craft:6/7 + smelt:1 "
                         "+ furnace place/tick INCLUDED) from the inventory-"
                         "injected snapshot rl/out/snaps/s10_t0_iron.bsnp "
                         "(regenerate both with make_iron_actions.py)")
    args = ap.parse_args()
    seeds = [int(s) for s in args.seeds.split(",")]

    npass = 0
    total = 0

    if args.iron:
        seed = args.chain_seed
        snap = os.path.join(SNAPS, f"s{seed}_t0_iron.bsnp")
        acts_path = os.path.join(RL, "out", f"iron_actions_s{seed}.json")
        if not os.path.exists(snap) or not os.path.exists(acts_path):
            print(f"iron gate: missing {snap} or {acts_path} "
                  f"(run make_iron_actions.py)")
            sys.exit(1)
        acts = json.load(open(acts_path))
        total += 1
        npass += run_seed(seed, snap, acts, f"iron stage x{len(acts)}",
                          show_final_inv=True)
        print(f"\n{'PASS' if npass == total else 'FAIL'}: "
              f"{npass}/{total} iron stream zero-diff")
        sys.exit(0 if npass == total else 1)
    if args.chain:
        seed = args.chain_seed
        snap = os.path.join(SNAPS, f"s{seed}_t0.bsnp")
        acts_path = os.path.join(RL, "out", f"chain_actions_s{seed}.json")
        if not os.path.exists(snap) or not os.path.exists(acts_path):
            print(f"chain gate: missing {snap} or {acts_path}")
            sys.exit(1)
        acts = json.load(open(acts_path))
        # replay the chain EXACTLY as recorded - craft/interact/use are now
        # simulated in blaze; only a stray "snapshot" key would be protocol-
        # level (none in the committed chains).
        acts = [{k: v for k, v in a.items() if k != "snapshot"} for a in acts]
        total += 1
        npass += run_seed(seed, snap, acts, f"full chain x{len(acts)}",
                          show_final_inv=True)
        print(f"\n{'PASS' if npass == total else 'FAIL'}: "
              f"{npass}/{total} chain stream zero-diff")
        sys.exit(0 if npass == total else 1)
    for seed in seeds:
        snap = os.path.join(SNAPS, f"s{seed}_d{args.stage}.bsnp")
        if not os.path.exists(snap):
            print(f"  seed {seed}: MISSING {snap} (run make_snapshots.py)")
            continue
        rng = Rng(0xC0A1 ^ (seed * 2654435761 & 0xFFFFFFFF))
        actions = [rand_action(rng) for _ in range(args.ticks)]
        total += 1
        npass += run_seed(seed, snap, actions, f"random x{args.ticks}")

    if args.episodes and os.path.exists(args.episodes):
        with open(args.episodes) as f:
            for li, line in enumerate(f):
                line = line.strip()
                if not line:
                    continue
                acts = json.loads(line)
                if isinstance(acts, dict):
                    acts = acts.get("actions", [])
                # strip protocol primitives outside the learned action space
                # (craft/interact/snapshot): blaze does not implement them, so
                # BOTH envs replay the identical filtered stream.
                acts = [{k: v for k, v in a.items()
                         if k not in ("craft", "interact", "snapshot", "use")}
                        for a in acts][:2000]
                # trained streams may carry cam:0 repeats; keep them - the
                # real env and blaze share the want_cam semantics.
                seed = seeds[0]
                snap = os.path.join(SNAPS, f"s{seed}_d{args.stage}.bsnp")
                total += 1
                npass += run_seed(seed, snap, acts, f"episode {li}")
                if li >= 2:
                    break

    print(f"\n{'PASS' if npass == total and total else 'FAIL'}: "
          f"{npass}/{total} streams zero-diff")
    sys.exit(0 if npass == total and total else 1)


if __name__ == "__main__":
    main()
