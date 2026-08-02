#!/usr/bin/env python3
"""trace_java.py - GROUND-TRUTH side of the tick-trace oracle.

Replay a fixed action tape through the REAL Java Minecraft 1.11.2 client via the
existing qrl bridge (java/qrl_client.py) and write, per tick:
  - a COMPACT physics CSV (`java_phys.csv`) with the legacy columns the C tracer and
    frame_oracle already consume (kept for back-compat), AND
  - the FULL per-tick STATE VECTOR as JSONL (`java_state.jsonl`) in the canonical schema
    shared with the C tracer (app/trace_main.c). See canonicalize() for the schema.

SPAWN ALIGNMENT (critical): the C tracer spawns at the magma worldgen origin column
while Java spawns at the REAL world spawn, so a raw tick-0 diff is meaningless. Pass
`--spawn "X Y Z YAW PITCH"` (or `--spawn-file trace/out/c_spawn.txt`, written by the C
tracer) and this script teleports the Java player to that EXACT pose (via a runcmds `tp`)
AFTER reset and BEFORE the first tape tick. Then both sides start from the same tick-0
state and the per-tick diff is a fair test of physics/state evolution.

REQUIRES the Java client running with the qrl bridge on 127.0.0.1:25575 (root CLAUDE.md,
Run B/C). Typical launch on anvil (headless, display :1):

    cd java && setsid nohup bash start_vnc_client.sh >/tmp/mc_launch.out 2>&1 &
    # wait until a TCP connect to 127.0.0.1:25575 succeeds, then run this script.

Usage:
    python trace_java.py --tape trace/out/tape.txt --seed 0 \
        --out trace/out/java_phys.csv --state trace/out/java_state.jsonl \
        --spawn-file trace/out/c_spawn.txt
"""
import argparse
import json
import os
import sys
from pathlib import Path

# qrl_client.py lives in java/
_JAVA = str(Path(__file__).resolve().parents[2] / "java")
if _JAVA not in sys.path:
    sys.path.insert(0, _JAVA)


def load_tape(path):
    rows = []
    with open(path) as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            v = [int(x) for x in s.split()]
            if len(v) != 11:
                raise ValueError(f"bad tape line ({len(v)} fields): {line!r}")
            rows.append(v)
    return rows


def action_dict(v):
    (forward, back, left, right, jump, sneak, sprint,
     attack, use, yaw, pitch) = v
    return {
        "forward": forward, "back": back, "left": left, "right": right,
        "jump": jump, "sneak": sneak, "sprint": sprint,
        "attack": attack, "use": use, "yaw": yaw, "pitch": pitch,
    }


def _g(ob, k, default=None):
    v = ob.get(k, default)
    return v


def canonicalize(tick, ob):
    """Map a raw qrl obs dict into the canonical per-tick state schema (shared with the C
    tracer). A value of None means the field is present but unknown; the C side uses null
    for whole categories it does not simulate. Java simulates all of them, so nothing here
    is null under normal play."""
    p = {
        "x": _g(ob, "x"), "y": _g(ob, "y"), "z": _g(ob, "z"),
        "yaw": _g(ob, "yaw"), "pitch": _g(ob, "pitch"),
        "vx": _g(ob, "vx"), "vy": _g(ob, "vy"), "vz": _g(ob, "vz"),
        "on_ground": 1 if _g(ob, "on_ground") else 0,
        "health": _g(ob, "health"), "food": _g(ob, "food"),
        "saturation": _g(ob, "saturation"),
        "air": _g(ob, "air"), "fire": _g(ob, "fire"),
        "xp_level": _g(ob, "xp"), "xp_frac": _g(ob, "xp_frac"),
        "fall_distance": _g(ob, "fall_distance"),
        "sprinting": 1 if _g(ob, "sprinting") else 0,
        "sneaking": 1 if _g(ob, "sneaking") else 0,
        "jumping": 1 if _g(ob, "jumping") else 0,
        "held_slot": _g(ob, "held_slot"),
        "held_id": _g(ob, "held_id"),
        "held_count": _g(ob, "held_count"),
        "held_meta": _g(ob, "held_meta"),
        "attack_cooldown": _g(ob, "attack_cooldown"),
        "hurt_time": _g(ob, "hurt_time"),
        "death_time": _g(ob, "death_time"),
        "dead": 1 if _g(ob, "dead") else 0,
        "deaths": _g(ob, "deaths"),
        "dim": _g(ob, "dim"),
        "potions": _g(ob, "potions", []),
    }
    inv = _g(ob, "inventory", [])
    ents = _g(ob, "entities", [])
    time = _g(ob, "time", {})
    return {"tick": tick, "player": p, "inventory": inv, "entities": ents, "time": time}


def phys_row(tick, ob):
    """Legacy java_phys.csv row (unchanged columns)."""
    og = 1 if ob.get("on_ground") else 0
    return [
        tick,
        repr(float(ob["x"])), repr(float(ob["y"])), repr(float(ob["z"])),
        repr(float(ob["yaw"])), repr(float(ob["pitch"])),
        repr(float(ob["vx"])), repr(float(ob["vy"])), repr(float(ob["vz"])),
        og,
        repr(float(ob["health"])), repr(float(ob["food"])),
        int(ob.get("air", -1)),
        0,  # frame_hash: not grabbed per tick (disk efficiency)
    ]


def parse_spawn(args):
    if args.spawn:
        parts = args.spawn.split()
    elif args.spawn_file and os.path.exists(args.spawn_file):
        with open(args.spawn_file) as f:
            parts = f.read().split()
    else:
        return None
    if len(parts) < 5:
        raise ValueError(f"spawn needs 5 numbers (X Y Z YAW PITCH); got {parts!r}")
    return [float(x) for x in parts[:5]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tape", default="trace/out/tape.txt")
    ap.add_argument("--out", default="trace/out/java_phys.csv")
    ap.add_argument("--state", default="trace/out/java_state.jsonl")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=25575)
    ap.add_argument("--spawn", default=None,
                    help='align tick 0: teleport Java to "X Y Z YAW PITCH" before the tape')
    ap.add_argument("--spawn-file", default="trace/out/c_spawn.txt",
                    help="read the spawn pose from a file (written by the C tracer)")
    ap.add_argument("--platform", type=int, default=0,
                    help="fill an NxN solid stone platform under the spawn so the Java player is "
                         "GROUNDED at the same y as C (isolates the physics MODEL from the two "
                         "worlds' terrain mismatch). 0 = off (raw terrain).")
    args = ap.parse_args()

    import qrl_client

    tape = load_tape(args.tape)
    print(f"loaded {len(tape)} ticks from {args.tape}")

    env = qrl_client.NetheriteEnv(host=args.host, port=args.port)
    print(f"reset(seed={args.seed}) ...")
    o = env.reset({"seed": args.seed, "mode": "survival", "type": "default"})
    if not o.get("ok"):
        print("reset FAILED:", o, file=sys.stderr)
        return 1
    print(f"spawn ~ ({o.get('x'):.2f},{o.get('y'):.2f},{o.get('z'):.2f})")

    # ---- spawn alignment: teleport to the C spawn pose so tick 0 MATCHES ----
    spawn = parse_spawn(args)
    if spawn:
        sx, sy, sz, syaw, spitch = spawn
        tp = f"tp @a {sx:.6f} {sy:.6f} {sz:.6f} {syaw:.4f} {spitch:.4f}"
        # PHASE 1: teleport to the target column so its chunk LOADS (a fill into an unloaded
        # chunk silently fails). Step once so the server processes the load.
        env._cmd({"cmd": "runcmds", "action": {"cmds": [tp]}})
        env.step({})
        # PHASE 2 (optional): a flat platform so the Java player stands on solid ground at the
        # SAME y as C. The two worldgens produce different terrain at a shared column, so a bare
        # pose tp leaves Java airborne -> free-fall + death, which cascades into every downstream
        # feature. A platform isolates the physics MODEL divergence from the terrain mismatch.
        if args.platform > 0:
            fy = int(round(sy)) - 1
            h = args.platform // 2
            cx, cz = int(round(sx)), int(round(sz))
            fill = f"fill {cx-h} {fy} {cz-h} {cx+h} {fy} {cz+h} minecraft:stone"
            rr = env._cmd({"cmd": "runcmds", "action": {"cmds": [fill, tp]}})
            print(f"platform fill ran={rr.get('ran')} failed={rr.get('failed')}")
        # settle so the tp + zero motion take effect (2 ticks); these are NOT counted in the tape.
        env._cmd({"cmd": "runcmds", "action": {"cmds": [tp]}})
        env.step({}); o2 = env.step({})
        print(f"aligned spawn -> now at ({o2.get('x'):.2f},{o2.get('y'):.2f},{o2.get('z'):.2f}) "
              f"yaw={o2.get('yaw'):.1f} pitch={o2.get('pitch'):.1f} "
              f"on_ground={o2.get('on_ground')} deaths_baseline={o2.get('deaths')}")
    else:
        print("NO spawn alignment (raw world spawn) -- tick 0 will diverge on pose; "
              "pass --spawn-file trace/out/c_spawn.txt")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    cols = ["tick", "x", "y", "z", "yaw", "pitch", "vx", "vy", "vz",
            "on_ground", "health", "food", "air", "frame_hash"]
    csv_f = open(args.out, "w")
    st_f = open(args.state, "w")
    csv_f.write(",".join(cols) + "\n")
    for t, v in enumerate(tape):
        ob = env.step(action_dict(v))
        csv_f.write(",".join(str(c) for c in phys_row(t, ob)) + "\n")
        st_f.write(json.dumps(canonicalize(t, ob), separators=(",", ":")) + "\n")
    csv_f.close()
    st_f.close()
    env.close()
    print(f"wrote {len(tape)} rows -> {args.out}  (+ state {args.state})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
