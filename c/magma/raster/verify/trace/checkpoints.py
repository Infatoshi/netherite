#!/usr/bin/env python3
"""checkpoints.py - checkpoint pixel-parity harness.

For each scripted checkpoint (checkpoints.json: name, feet x/y/z, MC yaw/pitch,
world time), pose the REAL Java MC 1.11.2 oracle there (teleport + pose pinning),
grab its frame with the qrl "frame" command, render the IDENTICAL pose in the
magma headless game (--script set_time+set_pose), pixel-diff the pair over
whole | terrain-crop | HUD-strip regions, and write a markdown report with
side-by-side and heat-map images.

Both games start at the exact same pixels (same seed-0 worldgen, same camera);
the numbers measure what the two RENDERERS draw differently.

Usage (bridge must be up; run_trace.sh handles launch/wait):
  uv run --no-project --with numpy --with pillow python checkpoints.py \
      [--checkpoints checkpoints.json] [--out out/checkpoints] [--report report]
"""
import argparse
import json
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_lib as ol  # noqa: E402

W, H = 854, 480


def capture_oracle(env, cks, outdir):
    """Teleport + pin each checkpoint pose in the live game and grab frames."""
    # Remove worldgen passive mobs: magma renders no entities, and a wandering
    # sheep both inflates the diff and keeps frames from ever stabilizing.
    print("[ck] killentities:", env._cmd({"cmd": "killentities", "action": {}}))
    for ck in cks:
        ol.freeze_scene(env, world_time=ck.get("time", 6000), gamemode="survival")
        obs = ol.hold_pose(env, ck["x"], ck["y"], ck["z"], ck["yaw"], ck["pitch"],
                           settle_ticks=int(ck.get("settle", 40)))
        ck["oracle_obs"] = {k: obs.get(k) for k in
                            ("x", "y", "z", "yaw", "pitch", "on_ground")}
        dx = abs(obs["x"] - ck["x"]) + abs(obs["y"] - ck["y"]) + abs(obs["z"] - ck["z"])
        flag = "" if dx < 1e-6 else f"  POSE DRIFT |d|={dx:.4f} (obs pose is authoritative)"
        print(f"[ck] oracle {ck['name']}: obs=({obs['x']:.2f},{obs['y']:.2f},"
              f"{obs['z']:.2f}) on_ground={obs['on_ground']}{flag}")
        # The SETTLED oracle pose is authoritative: survival gravity/collision can
        # move the player off the requested pose (snap to real ground, leaf push).
        # magma must render from where the oracle camera ACTUALLY was.
        ck["requested"] = {k: ck[k] for k in ("x", "y", "z", "yaw", "pitch")}
        for k in ("x", "y", "z", "yaw", "pitch"):
            ck[k] = float(obs[k])
        mc_png = os.path.join(outdir, f"mc_{ck['name']}.png")
        tp = (f"tp @a {ck['x']:.6f} {ck['y']:.6f} {ck['z']:.6f} "
              f"{ck['yaw']:.4f} {ck['pitch']:.4f}")
        # wait out llvmpipe chunk streaming: grab until consecutive frames agree
        d = ol.grab_stable_frame(env, mc_png, pin_cmds=[tp])
        print(f"[ck]   frame stabilized (last delta {d:.3f}/ch)")
        ck["mc_png"] = mc_png
    # Persist the settled (authoritative) poses: --skip-oracle must render
    # magma from the pose the cached mc_<name>.png was actually grabbed at,
    # not the pinned request - survival settle drifts some scenes ~1 block and
    # a pose-mismatched diff is garbage.
    # Merge (not overwrite): a partial re-capture of one flaky scene must not
    # drop the other scenes' recorded poses.
    obs_path = os.path.join(outdir, "oracle_obs.json")
    merged = {}
    if os.path.exists(obs_path):
        with open(obs_path) as f:
            merged = json.load(f)
    merged.update({ck["name"]: {k: ck[k] for k in ("x", "y", "z", "yaw", "pitch")}
                   for ck in cks})
    with open(obs_path, "w") as f:
        json.dump(merged, f, indent=1)


def render_magma(ck, outdir):
    """Render the same pose headlessly. set_pose at tick 0, 2 ticks (mesh+light
    settle), take the last frame. Pose passes through in MC convention."""
    name = ck["name"]
    scr = os.path.join(outdir, f"script_{name}.jsonl")
    frames = os.path.join(outdir, f"frames_{name}")
    os.makedirs(frames, exist_ok=True)
    with open(scr, "w") as f:
        f.write(json.dumps({"tick": 0, "type": "set_time",
                            "value": int(ck.get("time", 6000))}) + "\n")
        f.write(json.dumps({"tick": 0, "type": "set_pose", "x": ck["x"],
                            "y": ck["y"], "z": ck["z"], "yaw": ck["yaw"],
                            "pitch": ck["pitch"]}) + "\n")
        # re-assert at tick 1 so tick-0 gravity cannot sag the eye before the
        # captured frame (mirrors the oracle side's pose pinning).
        f.write(json.dumps({"tick": 1, "type": "set_pose", "x": ck["x"],
                            "y": ck["y"], "z": ck["z"], "yaw": ck["yaw"],
                            "pitch": ck["pitch"]}) + "\n")
    state = os.path.join(outdir, f"state_{name}.jsonl")
    t0 = time.time()
    ol.run_magma_script(scr, 2, frames, state, w=W, h=H)
    png = os.path.join(outdir, f"magma_{name}.png")
    ol.ppm_to_png(os.path.join(frames, "frame_000001.ppm"), png)
    shutil.rmtree(frames)
    ck["magma_png"] = png
    print(f"[ck] magma {name}: rendered in {time.time()-t0:.1f}s -> {png}")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--checkpoints", default=os.path.join(here, "checkpoints.json"))
    ap.add_argument("--out", default=os.path.join(here, "out", "checkpoints"))
    ap.add_argument("--report", default=os.path.join(here, "report"))
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--skip-oracle", action="store_true",
                    help="reuse existing mc_<name>.png frames in --out")
    args = ap.parse_args()

    with open(args.checkpoints) as f:
        cks = json.load(f)
    os.makedirs(args.out, exist_ok=True)
    os.makedirs(args.report, exist_ok=True)

    if not args.skip_oracle:
        if not ol.bridge_up():
            raise SystemExit("qrl bridge is down; run run_trace.sh (it launches "
                             "and waits) or start java/start_vnc_client.sh")
        env = ol.connect()
        o = ol.ensure_world(env, seed=args.seed)
        print(f"[ck] world ready, spawn ~ ({o['x']:.1f},{o['y']:.1f},{o['z']:.1f})")
        capture_oracle(env, cks, args.out)
        # leave the scene in the frozen noon state for future runs
        ol.freeze_scene(env, world_time=6000)
        env.close()
    else:
        obs_path = os.path.join(args.out, "oracle_obs.json")
        obs = {}
        if os.path.exists(obs_path):
            with open(obs_path) as f:
                obs = json.load(f)
        else:
            print("[ck] WARN: no oracle_obs.json - drifted scenes will diff at the "
                  "wrong pose; do one live run to refresh")
        for ck in cks:
            ck["mc_png"] = os.path.join(args.out, f"mc_{ck['name']}.png")
            if ck["name"] in obs:
                for k in ("x", "y", "z", "yaw", "pitch"):
                    ck[k] = float(obs[ck["name"]][k])

    results = []
    for ck in cks:
        render_magma(ck, args.out)
        stats = ol.diff_regions(ck["mc_png"], ck["magma_png"], W, H)
        results.append((ck, stats))
        w_, t_, h_ = stats["whole"], stats["terrain"], stats["hud"]
        print(f"[ck] {ck['name']:<16} whole mean {w_['mean_abs']:6.2f}/ch "
              f"({w_['pct_differing']:5.2f}%)  terrain {t_['mean_abs']:6.2f} "
              f"({t_['pct_differing']:5.2f}%)  hud {h_['mean_abs']:6.2f} "
              f"({h_['pct_differing']:5.2f}%)")
        ol.side_by_side(ck["mc_png"], ck["magma_png"],
                        os.path.join(args.report, f"ck_{ck['name']}_sbs.png"))
        ol.heatmap(ck["mc_png"], ck["magma_png"],
                   os.path.join(args.report, f"ck_{ck['name']}_heat.png"))

    # ---- report ----
    md = os.path.join(args.report, "checkpoints.md")
    with open(md, "w") as f:
        f.write("# Checkpoint pixel parity: REAL MC 1.11.2 vs magma\n\n")
        f.write("Same seed-0 world, same pose (feet + MC yaw/pitch), same 854x480 "
                "FOV-70 camera. Oracle = qrl in-process frame grab (llvmpipe SW GL); "
                "magma = headless `--script` render, frame after 2 ticks. Diff = "
                "abs per-channel over RGB; `%diff` = fraction of pixels with any "
                "channel delta > 0. Regions: whole frame; terrain crop (rows 14-86%, "
                "cols 9-91%: excludes sky top + HUD strip); HUD strip (bottom 10%).\n\n")
        f.write("| checkpoint | pose (feet x,y,z yaw/pitch, time) | whole mean/ch | "
                "whole %diff | terrain mean/ch | terrain %diff | hud mean/ch | hud %diff |\n")
        f.write("|---|---|---|---|---|---|---|---|\n")
        for ck, s in results:
            f.write(f"| {ck['name']} | ({ck['x']:.1f}, {ck['y']:.1f}, {ck['z']:.1f}) "
                    f"{ck['yaw']:.0f}/{ck['pitch']:.0f} t={ck.get('time',6000)} "
                    f"| {s['whole']['mean_abs']:.2f} | {s['whole']['pct_differing']:.2f}% "
                    f"| {s['terrain']['mean_abs']:.2f} | {s['terrain']['pct_differing']:.2f}% "
                    f"| {s['hud']['mean_abs']:.2f} | {s['hud']['pct_differing']:.2f}% |\n")
        n = len(results)
        if n:
            f.write(f"| **mean over {n}** | | "
                    f"{sum(s['whole']['mean_abs'] for _, s in results)/n:.2f} | "
                    f"{sum(s['whole']['pct_differing'] for _, s in results)/n:.2f}% | "
                    f"{sum(s['terrain']['mean_abs'] for _, s in results)/n:.2f} | "
                    f"{sum(s['terrain']['pct_differing'] for _, s in results)/n:.2f}% | "
                    f"{sum(s['hud']['mean_abs'] for _, s in results)/n:.2f} | "
                    f"{sum(s['hud']['pct_differing'] for _, s in results)/n:.2f}% |\n")
        f.write("\n")
        for ck, s in results:
            f.write(f"## {ck['name']}\n\n")
            if ck.get("note"):
                f.write(ck["note"] + "\n\n")
            f.write(f"Oracle readback: {json.dumps(ck.get('oracle_obs'))}\n\n")
            f.write(f"![side-by-side](ck_{ck['name']}_sbs.png)\n\n")
            f.write(f"![heatmap](ck_{ck['name']}_heat.png)\n\n")
    with open(os.path.join(args.out, "results.json"), "w") as f:
        json.dump([{**{k: v for k, v in ck.items() if k != "mc_png"},
                    "stats": s} for ck, s in results], f, indent=2)
    print(f"[ck] report -> {md}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
