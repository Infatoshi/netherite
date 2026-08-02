#!/usr/bin/env python3
"""window_render_check.py - regression check for magma's interactive window loop.

The window path (magma/app/game_main.c) is not covered by automated pixel gates.
Garbage-terrain (missing lightmap LUT bind) and unlit-white entities (missing
per-entity lightmap fill) shipped invisibly. This script exercises the same
loop via headless --frames + --set dump_dir=... and asserts image-level invariants.

Scenarios
---------
  LIGHTMAP-SANITY
    30 frames default lightmap mode + 30 frames --set legacy_lightmap=1, same
    seed, --mobs off. Frame 29 mean abs pixel difference below threshold (noon-
    fold legacy and lightmap nearly agree early in the day), AND lightmap-mode
    frame has < 2% near-white pixels (r,g,b all > 245) in the bottom two-thirds
    (terrain region). The broken build produced ~40%+.

  MOB-LIT
    120 frames, --set mob_demo=1 still=1 yawrate=3 --mobs off.
    Assert zombie palette pixels exist (teal shirt / olive skin RGB windows,
    lighting-tolerant) and are NOT near-white. Assert at least one frame differs
    from the corresponding no-demo run (entities actually drew).

  TICK-RATE
    Run 40 frames twice; assert frame_00029.ppm is byte-identical (headless
    determinism guard).

Thresholds were calibrated against the post-fix tree (e49286e / 12c865a).

Detection proof (pre-fix binary)
---------------------------------
  e49286e~1 builds with generated assets copied from a fixed tree and fails
  LIGHTMAP-SANITY: MAD ~70 and near-white ~39% in the terrain region (vs
  MAD ~7.3 / ~0.02% post-fix). mob_demo exists only from 12c865a;
  pre-entity-light fix is covered by the entity near-white / teal asserts
  when run on a binary that has the demo hook but lacks entity light fill.

  Alternate: --reuse-lm on a whitened terrain dump also fails the 2% check.

Usage
-----
  UV_CACHE_DIR=... TMPDIR=... uv run --no-project --with numpy,pillow \\
      python scripts/window_render_check.py [--game PATH] [options]

  Prefer scripts/window_render_check.sh (builds magma_game then runs this).

Options of note
  --game PATH           magma_game binary (default: <repo>/magma/magma_game)
  --only NAME           run one scenario: lightmap-sanity | mob-lit | tick-rate
  --reuse-lm DIR        skip lightmap capture; use existing frame_*.ppm dir
  --reuse-legacy DIR    skip legacy capture; use existing frame_*.ppm dir
  --tmpdir DIR          base temp dir (default under ~/dev/nw/.tmp)
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional

import numpy as np
from PIL import Image

# ---------------------------------------------------------------------------
# Calibrated thresholds (post-fix tree; MAD measured ~7.3 at 854x480 seed 0)
# ---------------------------------------------------------------------------
LM_FRAMES = 30
LM_COMPARE_FRAME = 29
# Fixed build MAD ~7.3 (dawn worldTime vs noon-fold legacy). Broken lightmap
# mode washes terrain toward white and drives MAD into the tens/hundreds.
LM_MAD_MAX = 20.0
# Broken build produced ~40%+; fixed is ~0.02%.
LM_NEAR_WHITE_FRAC_MAX = 0.02
NEAR_WHITE_MIN = 245  # r,g,b all strictly greater

MOB_FRAMES = 120
# Teal zombie-shirt pixels across 120 frames at default res are thousands;
# require a modest floor so a partial view still passes.
MOB_ZOMBIE_TEAL_MIN = 200
MOB_ZOMBIE_SKIN_MIN = 50  # among entity-diff pixels only (terrain is green too)
MOB_ENTITY_NEAR_WHITE_FRAC_MAX = 0.05  # of entity-diff pixels
MOB_MIN_DIFF_FRAMES = 1

TICK_FRAMES = 40
TICK_COMPARE_FRAME = 29

DEFAULT_SEED = 0
DEFAULT_WIDTH = 854
DEFAULT_HEIGHT = 480
DEFAULT_TMP_ROOT = Path(os.path.expanduser("~/dev/nw/.tmp"))


# ---------------------------------------------------------------------------
# PPM I/O
# ---------------------------------------------------------------------------

def load_ppm(path: Path) -> np.ndarray:
    """Load a P6 PPM (or anything Pillow accepts) as uint8 HxWx3."""
    img = Image.open(path)
    arr = np.asarray(img, dtype=np.uint8)
    if arr.ndim != 3 or arr.shape[2] < 3:
        raise ValueError(f"{path}: expected RGB image, got shape {arr.shape}")
    return arr[:, :, :3]


def frame_path(d: Path, i: int) -> Path:
    return d / f"frame_{i:05d}.ppm"


def require_frames(d: Path, n: int, label: str) -> None:
    missing = [i for i in range(n) if not frame_path(d, i).is_file()]
    if missing:
        sample = missing[:5]
        raise AssertionError(
            f"{label}: missing {len(missing)} frame(s) under {d} "
            f"(e.g. {sample})"
        )


# ---------------------------------------------------------------------------
# Game runner
# ---------------------------------------------------------------------------

def append_sets(cmd: list[str], pairs: list[str]) -> None:
    """Append repeated --set key=value flags (one flag per pair)."""
    for kv in pairs:
        cmd.extend(["--set", kv])


def run_game(
    game: Path,
    out_dir: Path,
    frames: int,
    seed: int,
    width: int,
    height: int,
    *,
    set_extra: Optional[dict] = None,
    env_extra: Optional[dict] = None,
    mobs: str = "off",
    label: str = "game",
) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    # Clear previous dumps so a partial re-run cannot pass on stale frames.
    for p in out_dir.glob("frame_*.ppm"):
        p.unlink()

    # Platform env only; drive/scenario knobs go through --set on argv.
    env = os.environ.copy()
    env["SDL_VIDEODRIVER"] = "dummy"
    if env_extra:
        env.update(env_extra)

    cmd = [
        str(game),
        "--seed", str(seed),
        "--frames", str(frames),
        "--mobs", mobs,
        "--width", str(width),
        "--height", str(height),
    ]
    sets = [f"dump_dir={out_dir}"]
    for k, v in sorted((set_extra or {}).items()):
        sets.append(f"{k}={v}")
    append_sets(cmd, sets)
    log_path = out_dir / "run.log"
    with open(log_path, "w") as log:
        for kv in sets:
            log.write(f"+ --set {kv}\n")
        for k, v in sorted((env_extra or {}).items()):
            log.write(f"+ env {k}={v}\n")
        log.write(f"+ {' '.join(cmd)}\n")
        log.flush()
        proc = subprocess.run(
            cmd, env=env, stdout=log, stderr=subprocess.STDOUT, check=False
        )
    if proc.returncode != 0:
        tail = log_path.read_text(errors="replace")[-2000:]
        raise AssertionError(
            f"{label}: {game} exited {proc.returncode}\n--- log tail ---\n{tail}"
        )
    require_frames(out_dir, frames, label)


# ---------------------------------------------------------------------------
# Assertions
# ---------------------------------------------------------------------------

def near_white_mask(img: np.ndarray, thr: int = NEAR_WHITE_MIN) -> np.ndarray:
    return np.all(img > thr, axis=-1)


def mean_abs_diff(a: np.ndarray, b: np.ndarray) -> float:
    if a.shape != b.shape:
        raise AssertionError(f"shape mismatch: {a.shape} vs {b.shape}")
    return float(np.mean(np.abs(a.astype(np.float64) - b.astype(np.float64))))


def zombie_teal_mask(img: np.ndarray) -> np.ndarray:
    """Zombie teal/cyan shirt: B >= G > R, muted, not sky-white."""
    r = img[:, :, 0].astype(np.int16)
    g = img[:, :, 1].astype(np.int16)
    b = img[:, :, 2].astype(np.int16)
    return (
        (b >= g)
        & (g > r + 10)
        & (b > 50)
        & (b < 180)
        & (r < 80)
        & (g > 30)
        & (g < 160)
    )


def zombie_skin_mask(img: np.ndarray) -> np.ndarray:
    """Olive/green zombie skin (used on entity-diff pixels only)."""
    r = img[:, :, 0].astype(np.int16)
    g = img[:, :, 1].astype(np.int16)
    b = img[:, :, 2].astype(np.int16)
    return (
        (g > r)
        & (g >= b)
        & (g > 25)
        & (g < 120)
        & (r < 90)
        & (b < 90)
        & ((g - r) >= 5)
        & ((g - b) <= 50)
    )


def check_lightmap_sanity(
    lm_dir: Path,
    leg_dir: Path,
    *,
    mad_max: float = LM_MAD_MAX,
    white_frac_max: float = LM_NEAR_WHITE_FRAC_MAX,
) -> None:
    require_frames(lm_dir, LM_FRAMES, "LIGHTMAP-SANITY lightmap")
    require_frames(leg_dir, LM_FRAMES, "LIGHTMAP-SANITY legacy")
    a = load_ppm(frame_path(lm_dir, LM_COMPARE_FRAME))
    b = load_ppm(frame_path(leg_dir, LM_COMPARE_FRAME))
    mad = mean_abs_diff(a, b)
    if mad > mad_max:
        raise AssertionError(
            f"LIGHTMAP-SANITY: frame {LM_COMPARE_FRAME} MAD lightmap vs legacy "
            f"= {mad:.4f} exceeds max {mad_max:.4f} "
            f"(modes should nearly agree; garbage lightmap diverges hard)"
        )

    h = a.shape[0]
    terrain = a[h // 3 :, :, :]  # bottom two-thirds
    white_frac = float(near_white_mask(terrain).mean())
    if white_frac > white_frac_max:
        raise AssertionError(
            f"LIGHTMAP-SANITY: near-white (r,g,b>{NEAR_WHITE_MIN}) fraction in "
            f"bottom 2/3 of lightmap frame {LM_COMPARE_FRAME} = "
            f"{white_frac * 100:.2f}% exceeds max {white_frac_max * 100:.2f}% "
            f"(broken lightmap bind washed terrain ~40%+)"
        )
    print(
        f"  LIGHTMAP-SANITY OK  MAD={mad:.4f} (max {mad_max})  "
        f"near-white={white_frac * 100:.3f}% (max {white_frac_max * 100:.1f}%)"
    )


def check_mob_lit(mob_dir: Path, nomob_dir: Path) -> None:
    require_frames(mob_dir, MOB_FRAMES, "MOB-LIT demo")
    require_frames(nomob_dir, MOB_FRAMES, "MOB-LIT no-demo")

    total_teal = 0
    total_skin_on_diff = 0
    total_diff_px = 0
    total_diff_near_white = 0
    frames_with_diff = 0

    for i in range(MOB_FRAMES):
        mob = load_ppm(frame_path(mob_dir, i))
        nom = load_ppm(frame_path(nomob_dir, i))
        if mob.shape != nom.shape:
            raise AssertionError(
                f"MOB-LIT: shape mismatch at frame {i}: {mob.shape} vs {nom.shape}"
            )
        total_teal += int(zombie_teal_mask(mob).sum())

        diff = np.any(mob != nom, axis=-1)
        n_diff = int(diff.sum())
        if n_diff > 0:
            frames_with_diff += 1
            ent = mob[diff]
            total_diff_px += n_diff
            total_diff_near_white += int(near_white_mask(ent).sum())
            # skin among entity pixels only
            r = ent[:, 0].astype(np.int16)
            g = ent[:, 1].astype(np.int16)
            b = ent[:, 2].astype(np.int16)
            skin = (
                (g > r)
                & (g >= b)
                & (g > 25)
                & (g < 120)
                & (r < 90)
                & (b < 90)
                & ((g - r) >= 5)
                & ((g - b) <= 50)
            )
            total_skin_on_diff += int(skin.sum())

    if total_teal < MOB_ZOMBIE_TEAL_MIN:
        raise AssertionError(
            f"MOB-LIT: zombie teal-shirt pixels across {MOB_FRAMES} frames = "
            f"{total_teal} < min {MOB_ZOMBIE_TEAL_MIN} "
            f"(mob_demo zombie not visible or unlit-white)"
        )
    if total_skin_on_diff < MOB_ZOMBIE_SKIN_MIN:
        raise AssertionError(
            f"MOB-LIT: zombie green-skin pixels on entity-diff mask = "
            f"{total_skin_on_diff} < min {MOB_ZOMBIE_SKIN_MIN}"
        )
    if frames_with_diff < MOB_MIN_DIFF_FRAMES:
        raise AssertionError(
            f"MOB-LIT: demo vs no-demo identical on all {MOB_FRAMES} frames "
            f"(entities did not draw)"
        )
    if total_diff_px > 0:
        nw_frac = total_diff_near_white / total_diff_px
        if nw_frac > MOB_ENTITY_NEAR_WHITE_FRAC_MAX:
            raise AssertionError(
                f"MOB-LIT: entity-diff near-white fraction = {nw_frac * 100:.2f}% "
                f"exceeds max {MOB_ENTITY_NEAR_WHITE_FRAC_MAX * 100:.1f}% "
                f"({total_diff_near_white}/{total_diff_px} px; unlit entities "
                f"render white)"
            )
    print(
        f"  MOB-LIT OK  teal_px={total_teal}  skin_on_diff={total_skin_on_diff}  "
        f"diff_frames={frames_with_diff}/{MOB_FRAMES}  "
        f"entity_near_white="
        f"{(total_diff_near_white / max(total_diff_px, 1)) * 100:.3f}%"
    )


def check_tick_rate(run_a: Path, run_b: Path) -> None:
    require_frames(run_a, TICK_FRAMES, "TICK-RATE run A")
    require_frames(run_b, TICK_FRAMES, "TICK-RATE run B")
    pa = frame_path(run_a, TICK_COMPARE_FRAME)
    pb = frame_path(run_b, TICK_COMPARE_FRAME)
    ba = pa.read_bytes()
    bb = pb.read_bytes()
    if ba != bb:
        # Helpful diagnostic without dumping whole files.
        if len(ba) != len(bb):
            detail = f"size {len(ba)} vs {len(bb)}"
        else:
            # find first differing byte
            for i, (x, y) in enumerate(zip(ba, bb)):
                if x != y:
                    detail = f"first byte diff at offset {i}: {x} vs {y}"
                    break
            else:
                detail = "unknown"
        raise AssertionError(
            f"TICK-RATE: frame_{TICK_COMPARE_FRAME:05d}.ppm not byte-identical "
            f"across two runs ({detail})"
        )
    print(
        f"  TICK-RATE OK  frame_{TICK_COMPARE_FRAME:05d}.ppm "
        f"byte-identical ({len(ba)} bytes) across two runs"
    )


# ---------------------------------------------------------------------------
# Scenario drivers
# ---------------------------------------------------------------------------

def scenario_lightmap(
    game: Path,
    work: Path,
    seed: int,
    width: int,
    height: int,
    reuse_lm: Optional[Path],
    reuse_legacy: Optional[Path],
) -> None:
    print("== LIGHTMAP-SANITY ==")
    lm_dir = reuse_lm if reuse_lm else work / "lightmap"
    leg_dir = reuse_legacy if reuse_legacy else work / "legacy"
    if not reuse_lm:
        run_game(
            game, lm_dir, LM_FRAMES, seed, width, height,
            set_extra={"still": "1"},
            label="LIGHTMAP-SANITY lightmap",
        )
    if not reuse_legacy:
        run_game(
            game, leg_dir, LM_FRAMES, seed, width, height,
            set_extra={"still": "1", "legacy_lightmap": "1"},
            label="LIGHTMAP-SANITY legacy",
        )
    check_lightmap_sanity(lm_dir, leg_dir)


def scenario_mob(
    game: Path,
    work: Path,
    seed: int,
    width: int,
    height: int,
) -> None:
    print("== MOB-LIT ==")
    mob_dir = work / "mob_demo"
    nomob_dir = work / "mob_nodemo"
    run_game(
        game, mob_dir, MOB_FRAMES, seed, width, height,
        set_extra={
            "mob_demo": "1",
            "still": "1",
            "yawrate": "3",
        },
        label="MOB-LIT demo",
    )
    run_game(
        game, nomob_dir, MOB_FRAMES, seed, width, height,
        set_extra={
            "still": "1",
            "yawrate": "3",
        },
        label="MOB-LIT no-demo",
    )
    check_mob_lit(mob_dir, nomob_dir)


def scenario_tick(
    game: Path,
    work: Path,
    seed: int,
    width: int,
    height: int,
) -> None:
    print("== TICK-RATE ==")
    a = work / "tick_a"
    b = work / "tick_b"
    run_game(
        game, a, TICK_FRAMES, seed, width, height,
        set_extra={"still": "1"},
        label="TICK-RATE A",
    )
    run_game(
        game, b, TICK_FRAMES, seed, width, height,
        set_extra={"still": "1"},
        label="TICK-RATE B",
    )
    check_tick_rate(a, b)


# ---------------------------------------------------------------------------
# Synthetic whitened frames for detection proof
# ---------------------------------------------------------------------------

def write_whitened_terrain(src_dir: Path, dst_dir: Path, n: int) -> None:
    """Copy frames; force bottom 2/3 to near-white (simulates lightmap bug)."""
    dst_dir.mkdir(parents=True, exist_ok=True)
    for i in range(n):
        src = frame_path(src_dir, i)
        img = load_ppm(src).copy()
        h = img.shape[0]
        img[h // 3 :, :, :] = 250
        Image.fromarray(img, mode="RGB").save(frame_path(dst_dir, i))


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def parse_args(argv: Optional[list] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Headless regression check for magma interactive window loop"
    )
    p.add_argument(
        "--game",
        type=Path,
        default=None,
        help="path to magma_game (default: <repo>/magma/magma_game)",
    )
    p.add_argument(
        "--tmpdir",
        type=Path,
        default=None,
        help=f"work directory root (default: under {DEFAULT_TMP_ROOT})",
    )
    p.add_argument(
        "--only",
        choices=("lightmap-sanity", "mob-lit", "tick-rate"),
        default=None,
        help="run a single scenario",
    )
    p.add_argument("--seed", type=int, default=DEFAULT_SEED)
    p.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    p.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    p.add_argument(
        "--reuse-lm",
        type=Path,
        default=None,
        help="use existing lightmap frame dir (skip capture)",
    )
    p.add_argument(
        "--reuse-legacy",
        type=Path,
        default=None,
        help="use existing legacy frame dir (skip capture)",
    )
    p.add_argument(
        "--keep",
        action="store_true",
        help="do not delete the work directory on success",
    )
    return p.parse_args(argv)


def main(argv: Optional[list] = None) -> int:
    args = parse_args(argv)
    game = args.game or (repo_root() / "magma" / "magma_game")
    game = game.resolve()
    if not game.is_file():
        print(f"FAIL: magma_game not found at {game}", file=sys.stderr)
        return 2
    if not os.access(game, os.X_OK):
        print(f"FAIL: {game} is not executable", file=sys.stderr)
        return 2

    DEFAULT_TMP_ROOT.mkdir(parents=True, exist_ok=True)
    if args.tmpdir:
        work = args.tmpdir
        work.mkdir(parents=True, exist_ok=True)
        cleanup = False
    else:
        work = Path(
            tempfile.mkdtemp(prefix="window_render_check_", dir=str(DEFAULT_TMP_ROOT))
        )
        cleanup = not args.keep

    print(f"window_render_check: game={game}")
    print(f"window_render_check: work={work}")
    print(f"window_render_check: seed={args.seed} size={args.width}x{args.height}")

    try:
        only = args.only
        if only in (None, "lightmap-sanity"):
            scenario_lightmap(
                game, work, args.seed, args.width, args.height,
                args.reuse_lm, args.reuse_legacy,
            )
        if only in (None, "mob-lit"):
            scenario_mob(game, work, args.seed, args.width, args.height)
        if only in (None, "tick-rate"):
            scenario_tick(game, work, args.seed, args.width, args.height)
        print("ALL PASS")
        return 0
    except AssertionError as e:
        print(f"FAIL: {e}", file=sys.stderr)
        print(f"(work dir preserved for inspection: {work})", file=sys.stderr)
        cleanup = False
        return 1
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        print(f"(work dir preserved for inspection: {work})", file=sys.stderr)
        cleanup = False
        return 2
    finally:
        if cleanup and work.exists():
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
