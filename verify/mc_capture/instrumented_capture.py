#!/usr/bin/env python3
"""Instrumented hard-scene capture: PNG + camera.json + options snapshot.

Uses live qrl bridge. Requires game running (start_vnc_client.sh) with rebuilt
mod that supports cmd "camera".

Atomic artifact set written to --out-dir (default: mc_capture/):
  mc_frame.png
  camera.json          # JVM dump (eye, effective FOV, projection params, world)
  options_snapshot.txt # copy of run/options.txt if found
  pose.json            # backward-compat thin pose derived from camera.json

Usage:
  uv run --no-project python verify/mc_capture/instrumented_capture.py
  uv run --no-project python .../instrumented_capture.py --no-spiral --out-dir /tmp/cap
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]  # mc_capture -> verify -> repo
JAVA = REPO / "java"
sys.path.insert(0, str(JAVA))
import qrl_client  # noqa: E402

OUT_DEFAULT = Path(__file__).resolve().parent


def grab_window_png(path: Path) -> tuple[int, int]:
    env = os.environ.copy()
    env["DISPLAY"] = env.get("DISPLAY", ":1")
    geom = subprocess.check_output(
        ["bash", "-c", 'xwininfo -root -tree 2>/dev/null | grep -i "Minecraft 1.11.2" | head -1'],
        env=env,
        text=True,
    )
    import re

    m = re.search(r"(\d+)x(\d+)\+(\d+)\+(\d+)", geom)
    abs_m = re.search(r"\+(\d+)\+(\d+)\s*$", geom.strip())
    if not m or not abs_m:
        raise RuntimeError(f"MC window not found: {geom!r}")
    w, h = int(m.group(1)), int(m.group(2))
    ax, ay = int(abs_m.group(1)), int(abs_m.group(2))
    subprocess.check_call(
        [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-f",
            "x11grab",
            "-draw_mouse",
            "0",
            "-video_size",
            f"{w}x{h}",
            "-i",
            f":1.0+{ax},{ay}",
            "-frames:v",
            "1",
            str(path),
        ],
        env=env,
    )
    return w, h


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, default=OUT_DEFAULT)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument(
        "--fresh",
        action="store_true",
        help="delete qrl_<seed>, create that seed, and verify the attached world before capture",
    )
    ap.add_argument("--eye-x", type=float, default=8.3)
    ap.add_argument("--eye-y", type=float, default=95.0)
    ap.add_argument("--eye-z", type=float, default=40.5)
    ap.add_argument("--yaw", type=float, default=180.0)
    ap.add_argument("--pitch", type=float, default=35.0)
    ap.add_argument("--eye-height", type=float, default=1.62)
    ap.add_argument("--spiral-rd", type=int, default=10)
    ap.add_argument("--no-spiral", action="store_true")
    ap.add_argument("--settle-ticks", type=int, default=80)
    args = ap.parse_args()
    out: Path = args.out_dir
    out.mkdir(parents=True, exist_ok=True)

    feet_y = args.eye_y - args.eye_height
    e = qrl_client.NetheriteEnv()
    if hasattr(e, "s"):
        e.s.settimeout(180)

    # Attach by default.  --fresh is deliberately explicit because it deletes the
    # qrl_<seed> save.  A capture must never silently label whichever world happened
    # to be open as args.seed.
    try:
        o = e.obs()
        print("[icap] attached", {k: o.get(k) for k in ("x", "y", "z", "ok")}, file=sys.stderr)
    except Exception as ex:
        print("[icap] obs failed, reset", ex, file=sys.stderr)
        o = e.reset({"seed": args.seed, "mode": "survival", "type": "default"})
        if not o.get("ok"):
            print("reset failed", o, file=sys.stderr)
            return 1

    if args.fresh:
        print(f"[icap] fresh reset seed={args.seed}", file=sys.stderr)
        o = e.reset(
            {
                "seed": args.seed,
                "mode": "survival",
                "type": "default",
                "fresh": True,
            }
        )
        deadline = time.monotonic() + 180.0
        while not o.get("ok"):
            if time.monotonic() >= deadline:
                print("fresh reset timed out", o, file=sys.stderr)
                return 1
            time.sleep(0.25)
            o = e.reset(
                {"seed": args.seed, "mode": "survival", "type": "default"}
            )

    setup = [
        "gamerule sendCommandFeedback false",
        "gamerule logAdminCommands false",
        "gamerule doDaylightCycle false",
        "gamerule doWeatherCycle false",
        "gamerule doMobSpawning false",
        "gamerule doFireTick false",
        "gamerule randomTickSpeed 0",
        "time set 6000",
        "weather clear 1000000",
        "gamemode spectator @a",
        "tp @a %g %g %g %g %g" % (args.eye_x, feet_y, args.eye_z, args.yaw, args.pitch),
    ]
    print("[icap] setup", e._cmd({"cmd": "runcmds", "action": {"cmds": setup}}), file=sys.stderr)

    if not args.no_spiral:
        ccx, ccz = int(args.eye_x) // 16, int(args.eye_z) // 16
        coords = [(0, 0)]
        for r in range(1, args.spiral_rd + 1):
            for x in range(-r, r + 1):
                coords.append((x, -r))
                coords.append((x, r))
            for z in range(-r + 1, r):
                coords.append((-r, z))
                coords.append((r, z))
        seen, spiral = set(), []
        for c in coords:
            if c not in seen:
                seen.add(c)
                spiral.append(c)
        print(f"[icap] spiral {len(spiral)} around ({ccx},{ccz})", file=sys.stderr)
        for i, (dx, dz) in enumerate(spiral):
            wx = (ccx + dx) * 16 + 8
            wz = (ccz + dz) * 16 + 8
            e._cmd(
                {
                    "cmd": "runcmds",
                    "action": {
                        "cmds": ["tp @a %g 200 %g %g %g" % (wx, wz, args.yaw, args.pitch)]
                    },
                }
            )
            e.step({})
            if i % 40 == 0:
                print(f"[icap] spiral {i}/{len(spiral)}", file=sys.stderr)

    # Pin pose and settle
    tp = "tp @a %g %g %g %g %g" % (args.eye_x, feet_y, args.eye_z, args.yaw, args.pitch)
    for _ in range(args.settle_ticks):
        e._cmd({"cmd": "runcmds", "action": {"cmds": [tp]}})
        e.step({})
        time.sleep(0.03)
    time.sleep(1.5)

    cam_path = out / "camera.json"
    # Prefer JVM camera dump; fall back to thin pose if mod not rebuilt yet
    cam = e._cmd({"cmd": "camera", "action": {"file": str(cam_path)}})
    if not cam.get("ok"):
        print("[icap] camera cmd failed (rebuild qrl?):", cam, file=sys.stderr)
        # still write what we can from obs
        obs = e.obs()
        cam = {
            "ok": False,
            "schema": "qrl.camera.fallback",
            "feet_x": obs.get("x"),
            "feet_y": obs.get("y"),
            "feet_z": obs.get("z"),
            "eye_y": (obs.get("y") or 0) + 1.62,
            "yaw": obs.get("yaw"),
            "pitch": obs.get("pitch"),
            "fov_effective": 77.0,
            "note": "fallback — rebuild Recorder for full camera dump",
        }
        cam_path.write_text(json.dumps(cam, indent=2))
    else:
        # re-read file if wrote
        if cam_path.is_file():
            cam = json.loads(cam_path.read_text())
        else:
            cam_path.write_text(json.dumps(cam, indent=2))

    actual_seed = (cam.get("world") or {}).get("seed")
    if actual_seed is not None and actual_seed != args.seed:
        print(
            f"capture seed mismatch: requested {args.seed}, attached {actual_seed}",
            file=sys.stderr,
        )
        return 1

    png = out / "mc_frame.png"
    w, h = grab_window_png(png)
    print(f"[icap] frame {w}x{h} -> {png}", file=sys.stderr)

    # options snapshot
    opt_src = JAVA / "Minecraft" / "run" / "options.txt"
    if opt_src.is_file():
        shutil.copy2(opt_src, out / "options_snapshot.txt")

    # backward-compat pose.json from camera
    pose = {
        "seed": args.seed,
        "x": cam.get("feet_x", args.eye_x),
        "y": cam.get("feet_y", feet_y),
        "z": cam.get("feet_z", args.eye_z),
        "yaw": cam.get("yaw", args.yaw),
        "pitch": cam.get("pitch", args.pitch),
        "eye_x": cam.get("eye_x", args.eye_x),
        "eye_y": cam.get("eye_y", args.eye_y),
        "eye_z": cam.get("eye_z", args.eye_z),
        "fov_setting": cam.get("fov_setting"),
        "fov_effective": cam.get("fov_effective"),
        "fov_modifier": cam.get("fov_modifier"),
        "magma_yaw_deg": cam.get("magma_yaw_deg"),
        "magma_pitch_deg": cam.get("magma_pitch_deg"),
        "width": w,
        "height": h,
        "camera_json": str(cam_path),
        "notes": "instrumented capture; use camera.json as source of truth for verify",
    }
    (out / "pose.json").write_text(json.dumps(pose, indent=2))

    # Manifest
    manifest = {
        "frame": str(png),
        "camera": str(cam_path),
        "pose": str(out / "pose.json"),
        "options": str(out / "options_snapshot.txt") if opt_src.is_file() else None,
        "fov_effective": cam.get("fov_effective"),
        "eye": [cam.get("eye_x"), cam.get("eye_y"), cam.get("eye_z")],
        "world_fingerprint": (cam.get("world") or {}).get("fingerprint"),
    }
    (out / "capture_manifest.json").write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest, indent=2))
    e.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
