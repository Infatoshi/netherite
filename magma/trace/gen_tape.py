#!/usr/bin/env python3
"""gen_tape.py - write a deterministic action tape for the tick-trace oracle.

One line per tick, whitespace-separated integer fields in the qrl action order:

    forward back left right jump sneak sprint attack use yaw pitch

forward/back/left/right/jump/sneak/sprint/attack/use in {0,1}; yaw/pitch in {-1,0,1}
(15-degree quantum steps, matching qrl_client.py / QuantizedRL.applyAction). Both the C
tracer (app/trace_main.c) and the Java tracer (trace_java.py) replay this exact tape.

Default profile: a seeded pseudo-random walk - mostly forward, occasional turns
(yaw), the odd jump and attack, rare strafes. Deterministic for a given --seed so the
Java and C runs consume identical input.

Usage:
    python gen_tape.py --ticks 300 --seed 0 --out trace/out/tape.txt
"""
import argparse
import random


def gen(ticks: int, seed: int):
    rng = random.Random(seed)
    rows = []
    # persistent turn state so turns last a few ticks (feels like a walk, not jitter)
    yaw_hold = 0        # remaining ticks of the current yaw step
    yaw_dir = 0
    for _ in range(ticks):
        forward = 1 if rng.random() < 0.85 else 0   # mostly walking forward
        back = 0
        left = right = 0
        if rng.random() < 0.05:                     # occasional strafe
            if rng.random() < 0.5:
                left = 1
            else:
                right = 1
        jump = 1 if rng.random() < 0.04 else 0
        sneak = 0
        sprint = 1 if rng.random() < 0.10 else 0
        attack = 1 if rng.random() < 0.06 else 0
        use = 0
        # turning: start a short hold of a yaw step now and then
        if yaw_hold == 0 and rng.random() < 0.12:
            yaw_dir = rng.choice((-1, 1))
            yaw_hold = rng.randint(1, 4)
        yaw = 0
        if yaw_hold > 0:
            yaw = yaw_dir
            yaw_hold -= 1
        pitch = 0
        if rng.random() < 0.03:                     # rare glance up/down
            pitch = rng.choice((-1, 1))
        rows.append((forward, back, left, right, jump, sneak, sprint,
                     attack, use, yaw, pitch))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ticks", type=int, default=300)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out", default="trace/out/tape.txt")
    args = ap.parse_args()

    import os
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    rows = gen(args.ticks, args.seed)
    with open(args.out, "w") as f:
        f.write("# forward back left right jump sneak sprint attack use yaw pitch\n")
        f.write(f"# seeded walk: ticks={args.ticks} seed={args.seed}\n")
        for r in rows:
            f.write(" ".join(str(v) for v in r) + "\n")
    print(f"wrote {len(rows)} ticks -> {args.out}")


if __name__ == "__main__":
    main()
