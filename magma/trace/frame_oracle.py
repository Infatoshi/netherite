#!/usr/bin/env python3
"""frame_oracle.py - POSE-FORCED FIRST-MINUTE FRAME ORACLE for the magma C game.

North-star integration test: run a FIXED action tape through BOTH the real Java
Minecraft 1.11.2 game and the magma C game and pixel-diff FRAMES over the first
~minute of scripted play, so every systematic RENDERING divergence (terrain
lighting, missing assets, rotated UVs, the hand, the HUD) shows up as a named
tick + screen region + magnitude.

THE HARD PART - isolate RENDER divergence from PHYSICS divergence. The two games do
NOT stay at the same player pose (physics differs; the tick-trace already found a
tick-0 spawn divergence), so a naive frame-vs-frame diff is meaningless. Solution:
POSE-FORCED diff. At CHECKPOINT ticks over the first minute we take magma's exact
player pose (from c_phys.csv), TELEPORT the Java player there, grab the Java frame,
render the magma frame at the SAME pose, and pixel-diff. Both cameras are then
identical, so a diff measures what the two RENDERERS draw differently, full stop.

PIPELINE (one command, --run-java to include the live Java half):
  1. run trace_game on the tape  -> c_phys.csv (magma per-tick physics + pose)
  2. pick checkpoints (every --cadence ticks) and read magma's pose at each
  3. magma frame: build + run game_candidate at each checkpoint pose  -> PNG
  4. Java frame (--run-java): write a poses file and call capture_at_poses.sh,
     which teleports the selected live game to each pose and x11grabs the window
  5. diff each pair with render-opt/wholeframe/diff_frame.py over THREE regions:
       whole | terrain-crop (isolates lighting/geometry) | HUD-region (hand/hotbar)
  6. emit a per-checkpoint table + aggregate + ranked worst offenders, and
     materialize the MC frame / magma frame / diff heat-map ONLY for checkpoints
     that exceed the noise floor (disk-efficient).

The target is the fill-rule noise floor (~0.02% of pixels; our rasterizer != GL),
NOT literally 0. Today the numbers WILL be large (the lighting model differs and
magma draws no hand/HUD-items); that is the POINT - the oracle makes each
divergence concrete and localized. This is the measuring stick to drive down.

Usage:
  # magma-side only (no live game): renders magma checkpoints + validates the
  # render+diff wiring against the existing aerial golden as checkpoint 0.
  uv run --no-project --with numpy --with pillow python trace/frame_oracle.py \
      --ticks 300 --cadence 60

  # full pose-forced oracle including a live Java game:
  uv run --no-project --with numpy --with pillow python trace/frame_oracle.py \
      --ticks 1200 --cadence 60 --run-java
"""
import argparse
import csv
import json
import os
import subprocess
import sys

MAGMA = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BLAZE_CORE = os.path.join(MAGMA, "..", "blaze", "core")
DIFF = os.path.abspath(os.path.join(MAGMA, "..", "java", "render-opt", "wholeframe", "diff_frame.py"))
TRACE_GAME = os.path.join(MAGMA, "trace_game")
GAME_CANDIDATE = "/tmp/game_candidate_oracle"
CAPTURE_SH = os.path.join(MAGMA, "../verify/mc_capture/capture_at_poses.sh")
AERIAL_GOLDEN = os.path.join(MAGMA, "../verify/mc_capture/mc_frame.png")

EYE_HEIGHT = 1.62      # PSV_EYE_HEIGHT (blaze player_survival.h); MC + magma agree
FOV = 70               # MC "Normal" default == magma fov_deg

# game_candidate object set (mirrors run_game_verify.sh; includes game/caps.o which
# world/{mesh_mc,light,populate_mc}.c now derive their pools/config from).
GC_UNITS = [
    "world/mesh_mc", "world/light", "world/populate_mc", "assets/blockmodels",
    "renderkernels/rk_31_facebakery_make_quad",
    "core/math", "core/shade", "core/config", "cpu/raster_cpu", "transform",
    "game/config", "game/caps", "game/sky",
]
GC_FLAGS = ["-O2", "-ffp-contract=off", "-Wall", "-Icore", "-I.", "-I" + BLAZE_CORE]


def sh(cmd, **kw):
    return subprocess.run(cmd, cwd=MAGMA, **kw)


def build_magma_tools(force=False):
    """Build trace_game (via its own script) and game_candidate (arbitrary-pose)."""
    if force or not os.path.exists(TRACE_GAME):
        print("[oracle] building trace_game ...")
        r = sh(["bash", "trace/build_c_tracer.sh"],
               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if r.returncode != 0:
            sys.stderr.write(r.stderr.decode(errors="replace"))
            raise SystemExit("[oracle] trace_game build FAILED")
    print("[oracle] building game_candidate (arbitrary-pose renderer) ...")
    for u in GC_UNITS:
        r = sh(["gcc", *GC_FLAGS, "-c", u + ".c", "-o", u + ".o"],
               stderr=subprocess.PIPE)
        if r.returncode != 0:
            sys.stderr.write(r.stderr.decode(errors="replace"))
            raise SystemExit(f"[oracle] compile {u}.c FAILED")
    objs = [u + ".o" for u in GC_UNITS]
    r = sh(["gcc", *GC_FLAGS, "../verify/mc_capture/game_candidate.c",
            *objs, "-o", GAME_CANDIDATE, "-lm"], stderr=subprocess.PIPE)
    if r.returncode != 0:
        sys.stderr.write(r.stderr.decode(errors="replace"))
        raise SystemExit("[oracle] game_candidate link FAILED")


def gen_tape(ticks, seed, path):
    print(f"[oracle] generating {ticks}-tick tape (seed {seed}) -> {path}")
    sh(["python3", "trace/gen_tape.py", "--ticks", str(ticks), "--seed", str(seed),
        "--out", path], stdout=subprocess.DEVNULL)


def run_trace_game(tape, seed, out_csv):
    print(f"[oracle] running trace_game -> {out_csv}")
    # --render 0: we only need the physics/pose CSV here (frames come from
    # game_candidate, not trace_game's small internal framebuffer).
    r = sh([TRACE_GAME, "--tape", tape, "--seed", str(seed), "--out", out_csv,
            "--render", "0"], stderr=subprocess.PIPE)
    if r.returncode != 0:
        sys.stderr.write(r.stderr.decode(errors="replace"))
        raise SystemExit("[oracle] trace_game run FAILED")


def read_checkpoints(csv_path, cadence):
    """Return list of dicts {tick,x,y,z,mc_yaw,mc_pitch} at every `cadence` ticks."""
    cks = []
    with open(csv_path) as f:
        for row in csv.DictReader(f):
            t = int(row["tick"])
            if t % cadence != 0:
                continue
            cks.append({
                "tick": t,
                "x": float(row["x"]), "y": float(row["y"]), "z": float(row["z"]),
                "mc_yaw": float(row["yaw"]), "mc_pitch": float(row["pitch"]),
                "on_ground": int(float(row["on_ground"])),
                "health": float(row["health"]),
            })
    return cks


def render_magma(ck, w, h, out_ppm):
    """Render magma at a checkpoint pose. eye = feet + EYE_HEIGHT; MC->magma
    convention: magma_yaw = 180 - mc_yaw, magma_pitch = -mc_pitch."""
    cyaw = 180.0 - ck["mc_yaw"]
    cpitch = -ck["mc_pitch"]
    r = sh([GAME_CANDIDATE,
            "--eye", f"{ck['x']:.6f}", f"{ck['y'] + EYE_HEIGHT:.6f}", f"{ck['z']:.6f}",
            "--yaw", f"{cyaw:.6f}", "--pitch", f"{cpitch:.6f}",
            "--fov", str(FOV), "--w", str(w), "--h", str(h), "--ppm", out_ppm],
           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0 or not os.path.exists(out_ppm):
        sys.stderr.write(r.stderr.decode(errors="replace"))
        raise SystemExit(f"[oracle] game_candidate render FAILED (tick {ck['tick']})")


def ppm_to_png(ppm, png):
    from PIL import Image
    Image.open(ppm).convert("RGB").save(png)


def diff_regions(golden, cand, w, h, crops):
    """Run diff_frame.py once per named region, return {name: stats}."""
    out = {}
    for name, crop in crops.items():
        r = subprocess.run(["python3", DIFF, golden, cand, "--crop", crop, "--json"],
                           capture_output=True)
        if r.returncode != 0:
            sys.stderr.write(r.stderr.decode(errors="replace"))
            raise SystemExit(f"[oracle] diff_frame FAILED ({name})")
        j = json.loads(r.stdout)
        e = j["comparisons"][0]
        out[name] = e["whole"] if crop == "none" else e["crop"]
    return out


def heatmap(golden, cand, out_png):
    import numpy as np
    from PIL import Image
    a = np.asarray(Image.open(golden).convert("RGB")).astype(np.int16)
    b = np.asarray(Image.open(cand).convert("RGB")).astype(np.int16)
    d = np.abs(a - b).max(axis=2).astype(np.float64)
    mx = d.max()
    scaled = (d / mx * 255.0).astype(np.uint8) if mx > 0 else d.astype(np.uint8)
    Image.fromarray(scaled).save(out_png)


def region_verdict(name, region_pct):
    """Where does this checkpoint diverge? A crude localizer from per-region %."""
    hud, terr = region_pct.get("hud", 0.0), region_pct.get("terrain", 0.0)
    tags = []
    if terr > 5.0:
        tags.append("TERRAIN")
    if hud > 5.0:
        tags.append("HUD/hand")
    return "+".join(tags) if tags else "near-floor"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ticks", type=int, default=300,
                    help="tape length (1200 ~= 60s at 20 tps)")
    ap.add_argument("--cadence", type=int, default=60,
                    help="checkpoint every N ticks (60 = ~20 checkpoints over 60s)")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--w", type=int, default=854, help="frame width (MC window)")
    ap.add_argument("--h", type=int, default=480, help="frame height (MC window)")
    ap.add_argument("--tape", default=os.path.join(MAGMA, "trace/out/tape.txt"))
    ap.add_argument("--csv", default=os.path.join(MAGMA, "trace/out/c_phys.csv"))
    ap.add_argument("--outdir", default=os.path.join(MAGMA, "trace/out/frame_oracle"))
    ap.add_argument("--run-java", action="store_true",
                    help="also drive a live Java game (teleport + x11grab)")
    ap.add_argument("--java-host", default="127.0.0.1")
    ap.add_argument("--java-port", type=int, default=25575)
    ap.add_argument("--java-display", default=":1")
    ap.add_argument("--fresh-java", action="store_true",
                    help="delete/recreate the Java qrl_<seed> save before capture")
    ap.add_argument("--reuse-java", action="store_true",
                    help="reuse existing mc_ck_<tick>.png files instead of recapturing")
    ap.add_argument("--gamemode", default="survival",
                    choices=["survival", "spectator"],
                    help="survival draws hand+HUD (the point); spectator = clean")
    ap.add_argument("--no-launch", action="store_true",
                    help="assume the qrl bridge is already up (skip launch/kill)")
    ap.add_argument("--noise-pct", type=float, default=0.5,
                    help="materialize a checkpoint's frames only if whole %%-diff "
                         "exceeds this (fill-rule floor ~0.02%%; default coarse 0.5%%)")
    ap.add_argument("--reuse-csv", action="store_true",
                    help="reuse an existing c_phys.csv instead of re-running trace_game")
    ap.add_argument("--no-build", action="store_true", help="skip the C builds")
    args = ap.parse_args()

    args.tape = os.path.abspath(args.tape)
    args.csv = os.path.abspath(args.csv)
    args.outdir = os.path.abspath(args.outdir)
    os.makedirs(args.outdir, exist_ok=True)
    magma_dir = os.path.join(args.outdir, "magma")
    os.makedirs(magma_dir, exist_ok=True)

    # crop regions on the WxH frame. terrain = central band excluding the very top
    # (sky) and the bottom HUD strip; hud = bottom strip (hotbar/vitals/hand base).
    hud_top = int(args.h * 0.90)                 # bottom ~10% = HUD strip
    terr = f"{int(args.h*0.14)}:{int(args.h*0.86)},{int(args.w*0.09)}:{int(args.w*0.91)}"
    crops = {"whole": "none", "terrain": terr, "hud": f"{hud_top}:{args.h},0:{args.w}"}

    # ---- 1. tape + trace_game (magma physics/pose) ----
    if not args.no_build:
        build_magma_tools()
    if not os.path.exists(args.tape):
        gen_tape(args.ticks, args.seed, args.tape)
    if not (args.reuse_csv and os.path.exists(args.csv)):
        run_trace_game(args.tape, args.seed, args.csv)

    cks = read_checkpoints(args.csv, args.cadence)
    print(f"[oracle] {len(cks)} checkpoints (every {args.cadence} ticks) from {args.csv}")

    # ---- 2. render magma frame at each checkpoint pose ----
    for ck in cks:
        ppm = os.path.join(magma_dir, f"magma_ck_{ck['tick']}.ppm")
        png = os.path.join(magma_dir, f"magma_ck_{ck['tick']}.png")
        render_magma(ck, args.w, args.h, ppm)
        ppm_to_png(ppm, png)
        ck["magma_png"] = png
    print(f"[oracle] rendered {len(cks)} magma checkpoint frames -> {magma_dir}")

    # ---- 3. Java frames (optional): teleport the live game to each pose + grab ----
    have_java = False
    if args.run_java:
        poses_file = os.path.join(args.outdir, "poses.txt")
        with open(poses_file, "w") as f:
            f.write("# IDX FEET_X FEET_Y FEET_Z MC_YAW MC_PITCH\n")
            for ck in cks:
                f.write(f"{ck['tick']} {ck['x']:.6f} {ck['y']:.6f} {ck['z']:.6f} "
                        f"{ck['mc_yaw']:.6f} {ck['mc_pitch']:.6f}\n")
        expected_mc = [os.path.join(args.outdir, f"mc_ck_{ck['tick']}.png")
                       for ck in cks]
        if args.reuse_java and all(os.path.exists(path) for path in expected_mc):
            print(f"[oracle] reusing {len(expected_mc)} existing Java checkpoint frames")
        else:
            cmd = ["bash", CAPTURE_SH, "--poses", poses_file, "--out", args.outdir,
                   "--seed", str(args.seed), "--fov", str(FOV),
                   "--gamemode", args.gamemode,
                   "--host", args.java_host, "--port", str(args.java_port),
                   "--display", args.java_display]
            if args.fresh_java:
                cmd.append("--fresh")
            if args.no_launch:
                cmd.append("--no-launch")
            print("[oracle] capturing Java frames via capture_at_poses.sh ...")
            print("  " + " ".join(cmd))
            r = subprocess.run(cmd)
            if r.returncode != 0:
                print("[oracle] WARN: capture_at_poses.sh returned nonzero; "
                      "some MC frames may be missing.", file=sys.stderr)
        have_java = True
        for ck in cks:
            mc = os.path.join(args.outdir, f"mc_ck_{ck['tick']}.png")
            ck["mc_png"] = mc if os.path.exists(mc) else None

    # ---- 4. diff each pair (or the aerial golden as ck-0 wiring proof) ----
    results = []
    if have_java:
        for ck in cks:
            if not ck.get("mc_png"):
                results.append((ck, None))
                continue
            stats = diff_regions(ck["mc_png"], ck["magma_png"], args.w, args.h, crops)
            results.append((ck, stats))
    else:
        # No live game: prove the render+diff wiring end-to-end by rendering magma
        # at the KNOWN aerial golden pose and diffing vs the existing mc_frame.png.
        # This is checkpoint 0's stand-in (reproduces the documented rung-4 numbers).
        print("[oracle] no --run-java: validating render+diff wiring vs the aerial "
              "golden (mc_frame.png) as the checkpoint-0 proxy.")
        aerial = {"tick": 0, "x": 8.2994, "y": 95.0 - EYE_HEIGHT, "z": 40.0,
                  "mc_yaw": 180.0, "mc_pitch": 35.0, "on_ground": 0, "health": 20.0}
        ppm = os.path.join(magma_dir, "magma_aerial.ppm")
        png = os.path.join(magma_dir, "magma_aerial.png")
        render_magma(aerial, args.w, args.h, ppm)
        ppm_to_png(ppm, png)
        aerial["magma_png"] = png
        aerial["mc_png"] = AERIAL_GOLDEN if os.path.exists(AERIAL_GOLDEN) else None
        if aerial["mc_png"]:
            stats = diff_regions(aerial["mc_png"], png, args.w, args.h, crops)
            results.append((aerial, stats))
        else:
            print("[oracle] aerial golden not found; magma frames rendered only.")

    # ---- 5. table + aggregate + ranked worst + selective materialization ----
    print()
    print("=" * 112)
    mode = f"POSE-FORCED FRAME ORACLE  ({'live Java' if have_java else 'wiring-validation (aerial golden)'}, "
    print(mode + f"{args.w}x{args.h}, fov {FOV}, gamemode {args.gamemode})")
    print("=" * 112)
    print(f"  terrain crop = rows/cols {crops['terrain']}   hud region = {crops['hud']}")
    print(f"  noise floor target ~0.02% (fill-rule); materialize threshold whole>{args.noise_pct}%")
    print("-" * 112)
    hdr = ("%-6s %-22s %8s %8s | %8s %8s | %8s %8s | %-14s" %
           ("tick", "pose(x,y,z yaw/pit)", "whol_mn", "whol_%",
            "terr_mn", "terr_%", "hud_mn", "hud_%", "where"))
    print(hdr)
    print("-" * 112)

    scored = []
    for ck, stats in results:
        posestr = (f"{ck['x']:.0f},{ck['y']:.0f},{ck['z']:.0f} "
                   f"{ck['mc_yaw']:.0f}/{ck['mc_pitch']:.0f}")
        if stats is None:
            print("%-6d %-22s %8s %8s | %8s %8s | %8s %8s | %-14s" %
                  (ck["tick"], posestr, "-", "-", "-", "-", "-", "-", "NO-MC-FRAME"))
            continue
        w_ = stats["whole"]; t_ = stats["terrain"]; h_ = stats["hud"]
        region_pct = {"terrain": t_["pct_differing"], "hud": h_["pct_differing"]}
        where = region_verdict(ck["tick"], region_pct)
        print("%-6d %-22s %8.2f %7.2f%% | %8.2f %7.2f%% | %8.2f %7.2f%% | %-14s" %
              (ck["tick"], posestr, w_["mean_abs"], w_["pct_differing"],
               t_["mean_abs"], t_["pct_differing"], h_["mean_abs"],
               h_["pct_differing"], where))
        scored.append((ck, stats, where))

    print("-" * 112)
    aggregate = None
    if scored:
        n = len(scored)
        agg_w = sum(s["whole"]["mean_abs"] for _, s, _ in scored) / n
        agg_t = sum(s["terrain"]["mean_abs"] for _, s, _ in scored) / n
        agg_h = sum(s["hud"]["mean_abs"] for _, s, _ in scored) / n
        aggregate = {"whole_mean_abs": agg_w, "terrain_mean_abs": agg_t,
                     "hud_mean_abs": agg_h, "checkpoints": n}
        print("%-6s %-22s %8.2f %8s | %8.2f %8s | %8.2f %8s | (mean over %d)" %
              ("AGG", "", agg_w, "", agg_t, "", agg_h, "", n))
    print("=" * 112)

    summary_path = os.path.join(args.outdir, "frame_summary.json")
    summary = {
        "mode": "live-java" if have_java else "aerial-golden",
        "width": args.w, "height": args.h, "fov": FOV,
        "gamemode": args.gamemode,
        "java": ({"host": args.java_host, "port": args.java_port,
                  "display": args.java_display, "fresh": args.fresh_java}
                 if have_java else None),
        "crops": crops,
        "aggregate": aggregate,
        "results": [{
            "tick": ck["tick"],
            "pose": {key: ck[key] for key in ("x", "y", "z", "mc_yaw", "mc_pitch")},
            "regions": stats,
            "where": where,
        } for ck, stats, where in scored],
    }
    with open(summary_path, "w") as f:
        json.dump(summary, f, indent=2, sort_keys=True)
        f.write("\n")
    print(f"[oracle] machine-readable summary -> {summary_path}")

    # ranked worst offenders (by whole mean_abs), materialize their frames + heatmap
    if scored:
        worst = sorted(scored, key=lambda x: -x[1]["whole"]["mean_abs"])
        print("\nWORST CHECKPOINTS (by whole mean/ch) - materialized frames + heat-map:")
        mat_dir = os.path.join(args.outdir, "worst")
        os.makedirs(mat_dir, exist_ok=True)
        for ck, stats, where in worst:
            if stats["whole"]["pct_differing"] < args.noise_pct:
                continue
            hp = os.path.join(mat_dir, f"diff_ck_{ck['tick']}.png")
            heatmap(ck["mc_png"], ck["magma_png"], hp)
            print(f"  tick {ck['tick']:>5}: whole mean {stats['whole']['mean_abs']:6.2f}/ch "
                  f"({stats['whole']['pct_differing']:.2f}%)  where={where}")
            print(f"           mc={ck['mc_png']}")
            print(f"           magma={ck['magma_png']}")
            print(f"           heatmap={hp}")
    if not have_java:
        print("\n[oracle] LIVE JAVA MULTI-CHECKPOINT RUN PENDING A GAME LAUNCH. To run it:")
        print("  uv run --no-project --with numpy --with pillow python trace/frame_oracle.py \\")
        print(f"      --ticks {args.ticks} --cadence {args.cadence} --run-java")
        print("  (frame_oracle launches the headless game on :1 via capture_at_poses.sh;")
        print("   on a fresh checkout export MC_GRADLE_ONLINE=1 first - see root CLAUDE.md.)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
