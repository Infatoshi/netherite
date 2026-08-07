#!/usr/bin/env python3
"""gen_tape.py - write a deterministic action tape for the tick-trace oracle.

One line per tick, whitespace-separated integer fields in the qrl action order:

    forward back left right jump sneak sprint attack use yaw pitch close

forward/back/left/right/jump/sneak/sprint/attack/use/close in {0,1};
yaw/pitch in {-1,0,1}
(15-degree quantum steps, matching qrl_client.py / Recorder.applyAction). Both the C
tracer (app/trace_main.c) and the Java tracer (trace_java.py) replay this exact tape.

Default profile: a seeded pseudo-random walk - mostly forward, occasional turns
(yaw), the odd jump and attack, rare strafes. Deterministic for a given --seed so the
Java and C runs consume identical input.

``block-break`` is a controlled non-vacuous world-transition probe: stand still,
look along the aligned spawn camera, and hold attack for the full tape. Pair it
with an identical staged target via both tracers' ``--set-block`` option.

``surface`` starts idle, then holds jump long enough to swim an immersed player
above a three-block water column, exercising the vanilla air reset to 300.

``melee`` emits isolated clicks against a locked target after two aim/setup
ticks. The tick-5 click exercises partial attack strength plus hurt resistance;
ticks 14, 25, 36, and 47 are fully cooled attacks that drive a five-health
target through death.

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
                     attack, use, yaw, pitch, 0))
    return rows


def gen_block_break(ticks: int):
    rows = []
    for _ in range(ticks):
        rows.append((0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0))
    return rows


def gen_idle(ticks: int):
    return [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)] * ticks


def gen_forward(ticks: int):
    return [(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)] * ticks


def gen_single_jump(ticks: int):
    return [
        (0, 0, 0, 0, int(tick == 0), 0, 0, 0, 0, 0, 0, 0)
        for tick in range(ticks)
    ]


def gen_stair_jump(ticks: int):
    return [
        (int(tick <= 2), int(3 <= tick <= 6),
         0, 0, int(tick == 0), 0, 0, 0, 0, 0, 0, 0)
        for tick in range(ticks)
    ]


def gen_surface(ticks: int):
    rows = []
    for tick in range(ticks):
        jump = 1 if 20 <= tick < 60 else 0
        rows.append((0, 0, 0, 0, jump, 0, 0, 0, 0, 0, 0, 0))
    return rows


def gen_melee(ticks: int):
    attack_ticks = {3, 5, 14, 25, 36, 47}
    return [
        (0, 0, 0, 0, 0, 0, 0, int(tick in attack_ticks), 0, 0,
         int(tick in (1, 2)), 0)
        for tick in range(ticks)
    ]


def gen_potion_speed(ticks: int):
    return [(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)] * ticks


def gen_trapped_chest(ticks: int):
    return [
        (
            0, 0, 0, 0, 0, 0, 0, 0,
            int(tick == 2), 0, 0, int(tick == 7),
        )
        for tick in range(ticks)
    ]


def gen_use_once(ticks: int):
    return [
        (0, 0, 0, 0, 0, 0, 0, 0, int(tick == 2), 0, 0, 0)
        for tick in range(ticks)
    ]


def gen_use_twice(ticks: int):
    return [
        (0, 0, 0, 0, 0, 0, 0, 0, int(tick in (2, 7)), 0, 0, 0)
        for tick in range(ticks)
    ]


def gen_use_down_once(ticks: int):
    return [
        (0, 0, 0, 0, 0, 0, 0, 0, int(tick == 2), 0,
         int(tick <= 2), 0)
        for tick in range(ticks)
    ]


def gen_use_down_seven(ticks: int):
    return [
        (0, 0, 0, 0, 0, 0, 0, 0,
         int(tick in (2, 7, 12, 17, 22, 27, 32)), int(tick == 19),
         int(tick <= 2), 0)
        for tick in range(ticks)
    ]


def gen_use_steep_once(ticks: int):
    return [
        (0, 0, 0, 0, 0, 0, 0, 0, int(tick == 3), 0,
         int(tick <= 3), 0)
        for tick in range(ticks)
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ticks", type=int, default=300)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument(
        "--profile",
        choices=("random", "block-break", "idle", "forward", "single-jump",
                 "stair-jump",
                 "surface", "melee",
                 "potion-speed", "trapped-chest", "use-once", "use-twice",
                 "use-down-once", "use-down-seven", "use-steep-once"),
        default="random",
    )
    ap.add_argument("--out", default="trace/out/tape.txt")
    args = ap.parse_args()

    import os
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    if args.profile == "random":
        rows = gen(args.ticks, args.seed)
    elif args.profile == "block-break":
        rows = gen_block_break(args.ticks)
    elif args.profile == "forward":
        rows = gen_forward(args.ticks)
    elif args.profile == "single-jump":
        rows = gen_single_jump(args.ticks)
    elif args.profile == "stair-jump":
        rows = gen_stair_jump(args.ticks)
    elif args.profile == "surface":
        rows = gen_surface(args.ticks)
    elif args.profile == "melee":
        rows = gen_melee(args.ticks)
    elif args.profile == "potion-speed":
        rows = gen_potion_speed(args.ticks)
    elif args.profile == "trapped-chest":
        rows = gen_trapped_chest(args.ticks)
    elif args.profile == "use-once":
        rows = gen_use_once(args.ticks)
    elif args.profile == "use-twice":
        rows = gen_use_twice(args.ticks)
    elif args.profile == "use-down-once":
        rows = gen_use_down_once(args.ticks)
    elif args.profile == "use-down-seven":
        rows = gen_use_down_seven(args.ticks)
    elif args.profile == "use-steep-once":
        rows = gen_use_steep_once(args.ticks)
    else:
        rows = gen_idle(args.ticks)
    with open(args.out, "w") as f:
        f.write(
            "# forward back left right jump sneak sprint attack "
            "use yaw pitch close\n")
        f.write(f"# profile={args.profile} ticks={args.ticks} seed={args.seed}\n")
        for r in rows:
            f.write(" ".join(str(v) for v in r) + "\n")
    print(f"wrote {len(rows)} ticks -> {args.out}")


if __name__ == "__main__":
    main()
