#!/usr/bin/env python3
"""trajectory.py - temporal divergence: one fixed input trajectory through BOTH
games from the SAME starting pixels, watching position + pixel divergence grow
over time.

Trajectory (200 ticks): walk forward 60, turn right 90 deg (6 x 15deg qrl yaw
steps), walk forward 60, jump x3 (ticks 130/140/150), stand.

Oracle side: qrl bridge, tick-synced step() per action, obs recorded EVERY tick,
frame grabbed every --cadence ticks. magma side: the same actions as a
--script JSONL (qrl 15-deg yaw steps map to dyaw=15), --state-out per tick,
--frames-out every tick (subsampled to the cadence).

Alignment: both start at the checkpoint pose passed via --start (feet + MC
yaw/pitch), pinned on the oracle with a tp before tick 0 and set at magma
tick 0 with set_pose. Same seed-0 worldgen -> same terrain contact.

Outputs (--out): oracle_trace.jsonl, magma state, divergence.csv
(per-tick |dx| |dy| |dz| dyaw dpitch + euclid), divergence.png (curve),
frame diffs at each cadence tick, and report rows in --report.
"""
import argparse
import csv
import json
import math
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_lib as ol  # noqa: E402

W, H = 854, 480


def build_actions(ticks=200):
    """qrl-style action dicts, one per tick."""
    acts = []
    for t in range(ticks):
        a = {}
        if t < 60:
            a["forward"] = 1
        elif t < 66:
            a["yaw"] = 1          # 6 x 15deg = 90deg right turn
        elif t < 126:
            a["forward"] = 1
        elif t in (130, 140, 150):
            a["jump"] = 1
        acts.append(a)
    return acts


def to_magma_event(t, a):
    ev = {"tick": t, "type": "action"}
    if a.get("forward"):
        ev["forward"] = 1
    if a.get("jump"):
        ev["jump"] = 1
    if a.get("yaw"):
        ev["dyaw"] = 15.0 * a["yaw"]
    if a.get("pitch"):
        ev["dpitch"] = 15.0 * a["pitch"]
    return ev


def ang_diff(a, b):
    d = (a - b) % 360.0
    return d - 360.0 if d > 180.0 else d


def run_oracle(start, acts, cadence, outdir, seed):
    env = ol.connect()
    o = ol.ensure_world(env, seed=seed)
    print(f"[traj] world ready, spawn ~ ({o['x']:.1f},{o['y']:.1f},{o['z']:.1f})")
    ol.freeze_scene(env, world_time=6000, gamemode="survival")
    # magma renders no entities; remove worldgen animals so frame diffs are clean
    print("[traj] killentities:", env._cmd({"cmd": "killentities", "action": {}}))
    obs = ol.hold_pose(env, *start, settle_ticks=40)
    print(f"[traj] oracle pinned at ({obs['x']:.3f},{obs['y']:.3f},{obs['z']:.3f}) "
          f"yaw={obs['yaw']:.1f} pitch={obs['pitch']:.1f} on_ground={obs['on_ground']}")
    rows = []
    frames = {}
    tr = open(os.path.join(outdir, "oracle_trace.jsonl"), "w")
    for t, a in enumerate(acts):
        ob = env.step(a)
        rows.append(ob)
        tr.write(json.dumps({"tick": t, **{k: ob.get(k) for k in
                 ("x", "y", "z", "yaw", "pitch", "vx", "vy", "vz",
                  "on_ground", "health", "food")}}) + "\n")
        if t % cadence == 0 or t == len(acts) - 1:
            p = os.path.join(outdir, f"mc_t{t:04d}.png")
            time.sleep(0.15)  # let the just-stepped tick render
            ol.grab_frame(env, p)
            frames[t] = p
    tr.close()
    env.close()
    return rows, frames


def run_magma(start, acts, outdir, seed):
    scr = os.path.join(outdir, "magma_script.jsonl")
    with open(scr, "w") as f:
        f.write(json.dumps({"tick": 0, "type": "set_time", "value": 6000}) + "\n")
        f.write(json.dumps({"tick": 0, "type": "set_pose",
                            "x": start[0], "y": start[1], "z": start[2],
                            "yaw": start[3], "pitch": start[4]}) + "\n")
        for t, a in enumerate(acts):
            ev = to_magma_event(t, a)
            if len(ev) > 2:
                f.write(json.dumps(ev) + "\n")
    frames = os.path.join(outdir, "magma_frames")
    os.makedirs(frames, exist_ok=True)
    state = os.path.join(outdir, "magma_state.jsonl")
    t0 = time.time()
    ol.run_magma_script(scr, len(acts), frames, state, w=W, h=H, seed=seed,
                          timeout=3600)
    print(f"[traj] magma ran {len(acts)} ticks in {time.time()-t0:.1f}s")
    rows = []
    with open(state) as f:
        for line in f:
            rows.append(json.loads(line))
    return rows, frames


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--ticks", type=int, default=200)
    ap.add_argument("--cadence", type=int, default=20)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--start", default="44.5 68.0 176.5 0.0 5.0",
                    help='"X Y Z YAW PITCH" feet pose, MC convention')
    ap.add_argument("--out", default=os.path.join(here, "out", "trajectory"))
    ap.add_argument("--report", default=os.path.join(here, "report"))
    args = ap.parse_args()
    start = [float(v) for v in args.start.split()]
    os.makedirs(args.out, exist_ok=True)
    os.makedirs(args.report, exist_ok=True)

    acts = build_actions(args.ticks)
    if not ol.bridge_up():
        raise SystemExit("qrl bridge is down; use run_trace.sh")
    j_rows, j_frames = run_oracle(start, acts, args.cadence, args.out, args.seed)
    c_rows, c_frames_dir = run_magma(start, acts, args.out, args.seed)

    # ---- per-tick position divergence ----
    div_csv = os.path.join(args.out, "divergence.csv")
    ticks, ex, ey, ez, eyaw, epitch, euc = [], [], [], [], [], [], []
    with open(div_csv, "w", newline="") as f:
        wcsv = csv.writer(f)
        wcsv.writerow(["tick", "java_x", "java_y", "java_z", "c_x", "c_y", "c_z",
                       "adx", "ady", "adz", "euclid", "dyaw", "dpitch",
                       "java_on_ground", "c_on_ground"])
        for t in range(min(len(j_rows), len(c_rows))):
            j, c = j_rows[t], c_rows[t]
            dx, dy, dz = j["x"] - c["x"], j["y"] - c["y"], j["z"] - c["z"]
            dyw = ang_diff(j["yaw"], c["yaw"])
            dpt = j["pitch"] - c["pitch"]
            e = math.sqrt(dx * dx + dy * dy + dz * dz)
            wcsv.writerow([t, f"{j['x']:.6f}", f"{j['y']:.6f}", f"{j['z']:.6f}",
                           f"{c['x']:.6f}", f"{c['y']:.6f}", f"{c['z']:.6f}",
                           f"{abs(dx):.6f}", f"{abs(dy):.6f}", f"{abs(dz):.6f}",
                           f"{e:.6f}", f"{dyw:.4f}", f"{dpt:.4f}",
                           int(bool(j["on_ground"])), c["on_ground"]])
            ticks.append(t); ex.append(abs(dx)); ey.append(abs(dy))
            ez.append(abs(dz)); eyaw.append(abs(dyw)); epitch.append(abs(dpt))
            euc.append(e)

    # ---- plot ----
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, (a1, a2) = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
    a1.plot(ticks, ex, label="|dx|")
    a1.plot(ticks, ey, label="|dy|")
    a1.plot(ticks, ez, label="|dz|")
    a1.plot(ticks, euc, label="euclid", color="black", lw=1.6)
    a1.set_yscale("symlog", linthresh=1e-6)
    a1.set_ylabel("blocks (symlog)")
    a1.legend(loc="upper left", fontsize=8)
    a1.set_title("Oracle (Java MC 1.11.2) vs magma: position divergence, fixed 200-tick trajectory")
    for tmark, lbl in ((60, "turn"), (66, "walk2"), (130, "jumps"), (126, "stand")):
        a1.axvline(tmark, color="gray", ls=":", lw=0.7)
    a2.plot(ticks, eyaw, label="|dyaw| deg")
    a2.plot(ticks, epitch, label="|dpitch| deg")
    a2.set_xlabel("tick")
    a2.set_ylabel("degrees")
    a2.legend(loc="upper left", fontsize=8)
    curve_png = os.path.join(args.report, "trajectory_divergence.png")
    fig.tight_layout()
    fig.savefig(curve_png, dpi=110)
    print(f"[traj] divergence curve -> {curve_png}")

    # ---- pixel diff at cadence frames ----
    pix_rows = []
    for t, mc_png in sorted(j_frames.items()):
        ppm = os.path.join(c_frames_dir, f"frame_{t:06d}.ppm")
        if not os.path.exists(ppm):
            continue
        cpng = os.path.join(args.out, f"magma_t{t:04d}.png")
        ol.ppm_to_png(ppm, cpng)
        stats = ol.diff_regions(mc_png, cpng, W, H)
        pix_rows.append((t, stats))
        print(f"[traj] t={t:4d} whole mean {stats['whole']['mean_abs']:6.2f}/ch "
              f"({stats['whole']['pct_differing']:5.2f}%) terrain "
              f"{stats['terrain']['mean_abs']:6.2f} ({stats['terrain']['pct_differing']:5.2f}%)")
        if t in (0, args.ticks // 2, max(j_frames)):
            ol.side_by_side(mc_png, cpng,
                            os.path.join(args.report, f"traj_t{t:04d}_sbs.png"))
    with open(os.path.join(args.out, "pixel_divergence.csv"), "w", newline="") as f:
        wcsv = csv.writer(f)
        wcsv.writerow(["tick", "whole_mean", "whole_pct", "terrain_mean",
                       "terrain_pct", "hud_mean", "hud_pct"])
        for t, s in pix_rows:
            wcsv.writerow([t, s["whole"]["mean_abs"], s["whole"]["pct_differing"],
                           s["terrain"]["mean_abs"], s["terrain"]["pct_differing"],
                           s["hud"]["mean_abs"], s["hud"]["pct_differing"]])

    # pixel-divergence-over-time plot
    fig2, ax = plt.subplots(figsize=(9, 3.2))
    ax.plot([t for t, _ in pix_rows], [s["whole"]["mean_abs"] for _, s in pix_rows],
            "o-", label="whole mean/ch")
    ax.plot([t for t, _ in pix_rows], [s["terrain"]["mean_abs"] for _, s in pix_rows],
            "s-", label="terrain crop mean/ch")
    ax.set_xlabel("tick")
    ax.set_ylabel("mean abs diff / channel")
    ax.set_title("Pixel divergence over the trajectory (same tick, each side at its OWN pose)")
    ax.legend(fontsize=8)
    pix_png = os.path.join(args.report, "trajectory_pixel_divergence.png")
    fig2.tight_layout()
    fig2.savefig(pix_png, dpi=110)

    # ---- report ----
    md = os.path.join(args.report, "trajectory.md")
    with open(md, "w") as f:
        f.write("# Temporal divergence: fixed 200-tick trajectory\n\n")
        f.write("Both games start pinned at the same pose "
                f"({args.start}, seed {args.seed}, frozen noon) and replay the "
                "SAME inputs: forward x60, right turn 90 deg (6x15), forward x60, "
                "jump at t=130/140/150, stand. Oracle obs recorded per tick; "
                "magma --state-out per tick. Frames diffed every "
                f"{args.cadence} ticks WITHOUT pose forcing (each side renders "
                "its own simulated pose), so pixel divergence includes physics "
                "drift.\n\n")
        f.write(f"![position divergence]({os.path.basename(curve_png)})\n\n")
        f.write(f"![pixel divergence]({os.path.basename(pix_png)})\n\n")
        f.write("| tick | whole mean/ch | whole %diff | terrain mean/ch | terrain %diff |\n")
        f.write("|---|---|---|---|---|\n")
        for t, s in pix_rows:
            f.write(f"| {t} | {s['whole']['mean_abs']:.2f} | "
                    f"{s['whole']['pct_differing']:.2f}% | "
                    f"{s['terrain']['mean_abs']:.2f} | "
                    f"{s['terrain']['pct_differing']:.2f}% |\n")
        for t in sorted(p for p in (0, args.ticks // 2, max(j_frames))):
            p = os.path.join(args.report, f"traj_t{t:04d}_sbs.png")
            if os.path.exists(p):
                f.write(f"\n## tick {t}\n\n![t{t}]({os.path.basename(p)})\n")
    print(f"[traj] report -> {md}")

    # keep out/ lean: drop the full magma frame dump (cadence PNGs are kept)
    shutil.rmtree(c_frames_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
