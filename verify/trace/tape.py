#!/usr/bin/env python3
"""tape.py - record and pack human-play tapes with reproducible naming.

The mature tape flow (VERIFY.md):
  start - begin taping on the live game. The tape is named from the resolved
          launch config + UTC timestamp and gets a .meta.json sidecar holding
          EVERY hyperparameter verbatim (qrl_launch.json + the config-owned
          options.txt keys + git rev). Same name => bit-identical launch recipe.
  stop  - stop taping, then pack.
  pack  - (re)build the parquet twin for an existing tape.

Naming rule:
  <UTCstamp>_<profile>_s<seed>_<mode>_<wtype>_rd<N>_<cfg8>
  cfg8 = first 8 hex of sha256 over the full resolved config, so two tapes
  with the same visible params but ANY differing hyperparameter get different
  names. The long tail of settings lives in the sidecar, not the filename.

Formats, side by side (same row count, one row per game tick):
  <name>.jsonl      - source of truth, written live by the qrl mod recorder
                      (header line + one line per tick; ragged ents included)
  <name>.parquet    - columnar twin for fast slicing/contrast (pandas/polars);
                      ents serialized as a JSON string column
  <name>.meta.json  - full config + counts; the reproducibility record
  <name>_frames/    - sparse real-game PNGs (every frames-every ticks)

Usage (game live on :0 with the qrl bridge up):
  uv run --no-project --with pyarrow python tape.py start [--frames-every 20]
  uv run --no-project --with pyarrow python tape.py stop
  uv run --no-project --with pyarrow python tape.py pack TAPE.jsonl
"""
import argparse
import datetime
import glob
import hashlib
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
RUN = os.path.join(REPO, "java", "Minecraft", "run")
TAPES = os.path.abspath(os.path.join(HERE, "..", "tapes"))
sys.path.insert(0, os.path.join(REPO, "java"))

# options.txt keys owned by the profile yamls (mc_cli.py); key bindings and
# in-game toggles are noise and stay out of the identity hash
OPT_KEYS = (
    "fov", "maxFps", "renderDistance", "guiScale", "particles", "ao",
    "mipmapLevels", "enableVsync", "renderClouds", "gamma", "fancyGraphics",
    "entityShadows", "bobView", "anaglyph3d", "useVbo", "fboEnable",
    "chatVisibility", "soundCategory_master",
)


def resolved_config():
    with open(os.path.join(RUN, "qrl_launch.json")) as f:
        launch = json.load(f)
    opts = {}
    with open(os.path.join(RUN, "options.txt")) as f:
        for ln in f:
            k, _, v = ln.rstrip("\n").partition(":")
            if k in OPT_KEYS:
                opts[k] = v
    return launch, opts


def tape_name(launch, opts):
    ts = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    w = launch.get("world", {})
    cfg8 = hashlib.sha256(json.dumps({"launch": launch, "options": opts},
                                     sort_keys=True).encode()).hexdigest()[:8]
    return (f"{ts}_{launch.get('profile', 'unknown')}_s{w.get('seed', '?')}"
            f"_{w.get('mode', '?')}_{w.get('type', '?')}"
            f"_rd{opts.get('renderDistance', '?')}_{cfg8}")


def git_rev():
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"],
                                       cwd=REPO).decode().strip()
    except Exception:
        return "unknown"


def bridge_cmd(obj):
    from qrl_client import NetheriteEnv
    e = NetheriteEnv()
    try:
        return e._cmd(obj)
    finally:
        e.close()


def cmd_start(args):
    tapes = os.path.abspath(args.dir)
    os.makedirs(tapes, exist_ok=True)
    launch, opts = resolved_config()
    if args.seed is not None:
        # The live world was bridge-reset to a different seed than the launch
        # config's auto-launch world; the name must reflect the ACTUAL world.
        launch = dict(launch)
        launch["world"] = dict(launch.get("world", {}), seed=args.seed)
    name = tape_name(launch, opts)
    tape = os.path.join(tapes, name + ".jsonl")
    meta = {
        "name": name,
        "created_utc": name.split("_")[0],
        "git_rev": git_rev(),
        "profile": launch.get("profile", "unknown"),
        "qrl_launch": launch,
        "options": opts,
        "frames_every": args.frames_every,
        "tape_jsonl": tape,
        "frames_dir": tape[:-len(".jsonl")] + "_frames",
    }
    with open(os.path.join(tapes, name + ".meta.json"), "w") as f:
        json.dump(meta, f, indent=2)
    r = bridge_cmd({"cmd": "recstart",
                    "action": {"file": tape, "frames_every": args.frames_every}})
    print("recstart:", r)
    print("tape:", tape)
    return 0 if r.get("ok") else 1


def _latest_unfinished(tapes):
    for m in sorted(glob.glob(os.path.join(tapes, "*.meta.json")), reverse=True):
        with open(m) as f:
            if "ticks" not in json.load(f):
                return m
    raise SystemExit("no unfinished tape found in " + tapes)


def cmd_stop(args):
    meta_path = _latest_unfinished(os.path.abspath(args.dir))
    with open(meta_path) as f:
        meta = json.load(f)
    r = bridge_cmd({"cmd": "recstop", "action": {}})
    print("recstop:", r)
    if not r.get("ok"):
        return 1
    meta["ticks"] = r.get("ticks")
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)
    pack(meta["tape_jsonl"], meta_path)
    print("replay next:\n  cd", HERE, "&&",
          "uv run --no-project --with numpy --with scipy --with pillow --with nbt python replay_tape.py",
          meta["tape_jsonl"], "--report")
    return 0


def pack(tape, meta_path=None):
    """JSONL -> parquet twin: one row per tick, identical row count."""
    import pyarrow as pa
    import pyarrow.parquet as pq
    with open(tape) as f:
        lines = [json.loads(ln) for ln in f if ln.strip()]
    if not lines or lines[0].get("header") != 1:
        raise SystemExit(f"{tape}: not a tape (missing header line)")
    ticks = lines[1:]
    cols = {
        "t": [r["t"] for r in ticks],
        "in_f": [r["in"]["f"] for r in ticks],
        "in_s": [r["in"]["s"] for r in ticks],
        "in_jump": [r["in"]["jump"] for r in ticks],
        "in_sneak": [r["in"]["sneak"] for r in ticks],
        "in_sprint": [r["in"]["sprint"] for r in ticks],
        "in_atk": [r["in"]["atk"] for r in ticks],
        "in_use": [r["in"]["use"] for r in ticks],
        "in_hb": [r["in"]["hb"] for r in ticks],
        "x": [r["x"] for r in ticks],
        "y": [r["y"] for r in ticks],
        "z": [r["z"] for r in ticks],
        "yaw": [r["yaw"] for r in ticks],
        "pitch": [r["pitch"] for r in ticks],
        "vx": [r["vx"] for r in ticks],
        "vy": [r["vy"] for r in ticks],
        "vz": [r["vz"] for r in ticks],
        "og": [r["og"] for r in ticks],
        "hp": [r["hp"] for r in ticks],
        "food": [r["food"] for r in ticks],
        "fall": [r.get("fall", 0.0) for r in ticks],
        "wt": [r.get("wt", 0) for r in ticks],
        "frame": [r.get("frame") for r in ticks],
        "ents_json": [json.dumps(r["ents"]) if r.get("ents") else None
                      for r in ticks],
    }
    out = tape[:-len(".jsonl")] + ".parquet"
    table = pa.table(cols, metadata={b"tape_header": json.dumps(lines[0]).encode()})
    pq.write_table(table, out)
    n_frames = sum(1 for v in cols["frame"] if v)
    print(f"packed {out}: {table.num_rows} rows ({n_frames} frames), "
          f"jsonl ticks {len(ticks)} -> row counts "
          f"{'MATCH' if table.num_rows == len(ticks) else 'MISMATCH'}")
    if meta_path is None:
        cand = tape[:-len(".jsonl")] + ".meta.json"
        meta_path = cand if os.path.exists(cand) else None
    if meta_path:
        with open(meta_path) as f:
            meta = json.load(f)
        meta["parquet"] = out
        meta["parquet_rows"] = table.num_rows
        meta["frames"] = n_frames
        with open(meta_path, "w") as f:
            json.dump(meta, f, indent=2)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("start", help="begin taping the live game")
    s.add_argument("--seed", type=int, default=None,
                   help="actual world seed if the live world was bridge-reset"
                        " to a seed other than the launch config's")
    s.add_argument("--frames-every", type=int, default=20)
    s.add_argument("--dir", default=TAPES,
                   help="artifact directory (default: canonical tapes dir)")
    s = sub.add_parser("stop", help="stop taping + pack")
    s.add_argument("--dir", default=TAPES,
                   help="artifact directory used by start")
    p = sub.add_parser("pack", help="(re)build parquet twin for a tape")
    p.add_argument("tape")
    args = ap.parse_args()
    if args.cmd == "start":
        sys.exit(cmd_start(args))
    if args.cmd == "stop":
        sys.exit(cmd_stop(args))
    pack(os.path.abspath(args.tape))


if __name__ == "__main__":
    main()
