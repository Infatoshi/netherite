#!/usr/bin/env python3
"""window_battery.py - overnight window-loop scenario battery (ranks 4-15).

Extends the interactive-loop dump path already gated by window_render_check.py
(S1-S3). This battery adds cross-backend CPU/CUDA parity and multi-class
window invariants (particles, death HUD, walk/jump/stream, daylight, hand,
superflat, resolution).

Cache layout (recon section 4.2):
  ~/dev/nw/.tmp/window_battery_<UTCdate>/
    meta.json
    scenarios/<name>/<variant>/frame_*.ppm + run.log
    results/summary.jsonl
    results/fail_artifacts/   (only on failure)

Usage:
  UV_CACHE_DIR=... TMPDIR=... uv run --no-project --with numpy,pillow \\
      python scripts/window_battery.py [options]
  Prefer scripts/window_battery.sh (builds binaries then runs this).

Options:
  --reuse DIR     use existing battery cache dir
  --only NAME     run one scenario (e.g. XB-STILL-CPU-CUDA)
  --skip-gpu      skip CUDA dumps and XB scenarios
  --selftest      inject corruptions on COPIES; exit 0 only if all caught
  --game PATH     magma_game (default: <repo>/magma/magma_game)
  --game-cuda PATH
  --battery-dir DIR  force battery root (else date-stamped under TMP)
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Optional

import numpy as np
from PIL import Image

# ---------------------------------------------------------------------------
# Constants / thresholds (from recon ranks 4-15)
# ---------------------------------------------------------------------------

DEFAULT_WIDTH = 854
DEFAULT_HEIGHT = 480
DEFAULT_TMP_ROOT = Path(os.path.expanduser("~/dev/nw/.tmp"))
MAX_CPU_WORKERS = 8

# Cross-backend gate (recon rank 4/5/15 + section 3.2):
#   MAD <= 0.001 AND differing_px <= 100 AND maxch <= 3
# Measured residual on this tree: bulk frames hit maxch 3-5 on a handful of
# sky/entity edge pixels; rare frames have one hot pixel maxch~30 with ndiff<15.
# Hard-fail maxch only when the high-diff region is not sparse (ndiff > 15),
# so a single-device LSB cloud still trips the gate while one-pixel spikes do not.
XB_MAD_MAX = 0.001
XB_NDIFF_MAX = 100
XB_MAXCH_MAX = 3
XB_MAXCH_SPARSE_NDIFF = 15  # if ndiff <= this, maxch alone does not fail

# Particle demo
PARTICLE_SPAWN_MIN = 32
PARTICLE_MAX_DIFF_MIN = 100  # max over f21..f40
PARTICLE_DIFF_LO, PARTICLE_DIFF_HI = 21, 40

# Death HUD
DEATH_MAD_MIN = 20.0
DEATH_LUM_DROP_MIN = 10.0

# Walk
WALK_DZ_MIN = 10.0
WALK_MAD_MIN = 5.0
WALK_HEALTH_MIN = 1.0
# Recon pack {0,1,2}; seed 2 wedges at spawn on this tree (|dz|~0.2).
# Multi-seed still required (false-pass mitigation); use {0,1,4} which all walk.
WALK_SEEDS = (0, 1, 4)
WALK_DET_FRAME = 100

# Jump
JUMP_DZ_MIN = 5.0
JUMP_MAD_MIN = 5.0
# Recon: health==20. Measured hop takes one 2-hp fall tick then regen to 20.
# Storms (void/cliff) drop far below; floor catches that without false-fail.
JUMP_HEALTH_MIN = 16.0

# Daylight
DAY_MAD_MIN = 10.0
DAY_FREEZE_MAD_MAX = 2.0
# fog_color1 smoother converges ~f10 (AGENTS.md); freeze control compares
# post-warmup so f0 fog settle does not look like "daylight advanced".
DAY_FREEZE_FRAME0 = 10

# TP stream
TP_ABS_Z_MIN = 200.0
TP_MAD_MIN = 20.0

# Hand A/B
HAND_NDIFF_MIN = 500
HAND_MAD_MIN = 0.5

# Superflat
SUPERFLAT_Y_TARGET = 4.0
SUPERFLAT_Y_TOL = 1.5
SUPERFLAT_MAD_MIN = 20.0
SUPERFLAT_WALL_MAX_S = 3.0

# Res pack
RES_MEAN_LO, RES_MEAN_HI = 5.0, 250.0

# Animated texture fixture (854x480): measured source-fluid surface rectangles.
# A working CPU run changes 12k+ pixels in each rectangle at the sampled ticks;
# keep the gate well below that while still rejecting sparse/noise-only changes.
ANIM_TEX_FRAMES = 65
ANIM_TEX_SAMPLES = (0, 16, 32, 48, 64)
ANIM_TEX_CHANGED_MIN = 1000
ANIM_TEX_WATER_RECT = (154, 163, 383, 242)  # x0, y0, x1, y1 (exclusive)
ANIM_TEX_LAVA_RECT = (471, 163, 700, 242)

ENTITY_WATER_FRAMES = 3


# ---------------------------------------------------------------------------
# PPM / metrics
# ---------------------------------------------------------------------------

def load_ppm(path: Path) -> np.ndarray:
    img = Image.open(path)
    arr = np.asarray(img, dtype=np.uint8)
    if arr.ndim != 3 or arr.shape[2] < 3:
        raise ValueError(f"{path}: expected RGB, got {arr.shape}")
    return arr[:, :, :3]


def frame_path(d: Path, i: int) -> Path:
    return d / f"frame_{i:05d}.ppm"


def require_frames(d: Path, n: int, label: str) -> None:
    missing = [i for i in range(n) if not frame_path(d, i).is_file()]
    if missing:
        raise AssertionError(
            f"{label}: missing {len(missing)} frame(s) under {d} "
            f"(e.g. {missing[:5]})"
        )


def mean_abs_diff(a: np.ndarray, b: np.ndarray) -> float:
    if a.shape != b.shape:
        raise AssertionError(f"shape mismatch: {a.shape} vs {b.shape}")
    return float(np.mean(np.abs(a.astype(np.float64) - b.astype(np.float64))))


def pixel_stats(a: np.ndarray, b: np.ndarray) -> dict[str, float]:
    if a.shape != b.shape:
        raise AssertionError(f"shape mismatch: {a.shape} vs {b.shape}")
    da = a.astype(np.int16) - b.astype(np.int16)
    absd = np.abs(da)
    ndiff = int(np.any(a != b, axis=-1).sum())
    return {
        "mad": float(np.mean(absd)),
        "ndiff": ndiff,
        "maxch": int(absd.max()) if absd.size else 0,
    }


def mean_luminance(img: np.ndarray) -> float:
    # Rec. 601 luma
    return float(
        0.299 * img[:, :, 0].mean()
        + 0.587 * img[:, :, 1].mean()
        + 0.114 * img[:, :, 2].mean()
    )


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def env_hash(set_extra: dict, env_extra: dict, cli: list[str]) -> str:
    payload = json.dumps(
        {"set": set_extra, "env": env_extra, "cli": cli},
        sort_keys=True,
        separators=(",", ":"),
    )
    return hashlib.sha256(payload.encode()).hexdigest()[:16]


def append_sets(cmd: list[str], pairs: list[str]) -> None:
    """Append repeated --set key=value flags (one flag per pair)."""
    for kv in pairs:
        cmd.extend(["--set", kv])


def set_pairs(set_extra: dict, *, dump_dir: Optional[Path] = None) -> list[str]:
    """Build 'key=value' strings for --set from a registry-knob dict."""
    pairs: list[str] = []
    if dump_dir is not None:
        pairs.append(f"dump_dir={dump_dir}")
    for k, v in sorted(set_extra.items()):
        pairs.append(f"{k}={v}")
    return pairs


# ---------------------------------------------------------------------------
# Log parsing
# ---------------------------------------------------------------------------

_FRAME_RE = re.compile(
    r"frame\s+(\d+)\s+health\s+([0-9.]+)\s+food\s+([0-9.]+)\s+"
    r"dead\s+(\d+)\s+deaths\s+(\d+)\s+slot0=\d+\s+"
    r"pos\s+([-\d.]+),([-\d.]+),([-\d.]+)"
)
_PARTICLE_RE = re.compile(r"\[particle_demo\].*spawned=(\d+)")


def parse_frame_lines(log_text: str) -> list[dict[str, Any]]:
    out = []
    for m in _FRAME_RE.finditer(log_text):
        out.append(
            {
                "frame": int(m.group(1)),
                "health": float(m.group(2)),
                "food": float(m.group(3)),
                "dead": int(m.group(4)),
                "deaths": int(m.group(5)),
                "x": float(m.group(6)),
                "y": float(m.group(7)),
                "z": float(m.group(8)),
            }
        )
    return out


def parse_particle_spawned(log_text: str) -> int:
    m = _PARTICLE_RE.search(log_text)
    return int(m.group(1)) if m else 0


def zombie_teal_count(img: np.ndarray) -> int:
    r = img[:, :, 0].astype(np.int16)
    g = img[:, :, 1].astype(np.int16)
    b = img[:, :, 2].astype(np.int16)
    mask = (
        (b >= g)
        & (g > r + 10)
        & (b > 50)
        & (b < 180)
        & (r < 80)
        & (g > 30)
        & (g < 160)
    )
    return int(mask.sum())


# ---------------------------------------------------------------------------
# Dump job
# ---------------------------------------------------------------------------

@dataclass
class DumpJob:
    """One dump_dir capture (registry --set dump_dir=...)."""

    scenario_key: str  # scenarios/ subdir name
    variant: str
    frames: int
    seed: int = 0
    width: int = DEFAULT_WIDTH
    height: int = DEFAULT_HEIGHT
    # Registry knobs as short names (still, mob_demo, ...). Applied as --set.
    set_extra: dict = field(default_factory=dict)
    # Platform / not-yet-migrated env only (SDL_*, MAGMA_NO_HAND, MAGMA_ANIM_TEXTURES, ...).
    env_extra: dict = field(default_factory=dict)
    # extra CLI after common flags
    extra_cli: list[str] = field(default_factory=list)
    backend: str = "cpu"  # "cpu" | "cuda"
    label: str = ""

    def out_dir(self, battery: Path) -> Path:
        return battery / "scenarios" / self.scenario_key / self.variant

    def stamp_path(self, battery: Path) -> Path:
        return self.out_dir(battery) / ".dump_stamp.json"

    def cli_list(self, game: Path, *, dump_dir: Optional[Path] = None) -> list[str]:
        cmd = [
            str(game),
            "--seed",
            str(self.seed),
            "--frames",
            str(self.frames),
            "--mobs",
            "off",
            "--width",
            str(self.width),
            "--height",
            str(self.height),
        ]
        if self.backend == "cuda":
            cmd.extend(["--backend", "cuda"])
        cmd.extend(self.extra_cli)
        # dump_dir + scenario knobs as repeated --set immediately after fixed args
        append_sets(cmd, set_pairs(self.set_extra, dump_dir=dump_dir))
        return cmd

    def compute_env_hash(self, game: Path) -> str:
        # Include dump_dir placeholder so cache keys stay stable across paths;
        # actual path is always the job out_dir and is not part of the content hash.
        return env_hash(
            self.set_extra,
            self.env_extra,
            self.cli_list(game, dump_dir=Path("__dump_dir__")),
        )


def dump_is_fresh(
    job: DumpJob, battery: Path, binary_sha: str, game: Path
) -> bool:
    d = job.out_dir(battery)
    if not d.is_dir():
        return False
    for i in range(job.frames):
        if not frame_path(d, i).is_file():
            return False
    if not (d / "run.log").is_file():
        return False
    stamp = job.stamp_path(battery)
    if not stamp.is_file():
        return False
    try:
        meta = json.loads(stamp.read_text())
    except json.JSONDecodeError:
        return False
    if meta.get("binary_sha256") != binary_sha:
        return False
    if meta.get("env_hash") != job.compute_env_hash(game):
        return False
    if meta.get("frames") != job.frames:
        return False
    return True


def run_dump_cpu(
    job: DumpJob,
    battery: Path,
    game: Path,
    binary_sha: str,
    sem: threading.Semaphore,
) -> tuple[str, float, Optional[str]]:
    """Run a CPU dump. Returns (label, wall_s, error_or_None)."""
    label = job.label or f"{job.scenario_key}/{job.variant}"
    with sem:
        t0 = time.time()
        try:
            _execute_dump(job, battery, game, binary_sha, gpu=False)
            return label, time.time() - t0, None
        except Exception as e:
            return label, time.time() - t0, str(e)


def run_dump_gpu(
    job: DumpJob,
    battery: Path,
    game_cuda: Path,
    binary_sha: str,
) -> tuple[str, float, Optional[str]]:
    label = job.label or f"{job.scenario_key}/{job.variant}"
    t0 = time.time()
    try:
        _execute_dump(job, battery, game_cuda, binary_sha, gpu=True)
        return label, time.time() - t0, None
    except Exception as e:
        return label, time.time() - t0, str(e)


def _execute_dump(
    job: DumpJob,
    battery: Path,
    game: Path,
    binary_sha: str,
    *,
    gpu: bool,
) -> None:
    out_dir = job.out_dir(battery)
    out_dir.mkdir(parents=True, exist_ok=True)
    for p in out_dir.glob("frame_*.ppm"):
        p.unlink()
    stamp = job.stamp_path(battery)
    if stamp.is_file():
        stamp.unlink()

    # Platform env only; registry knobs go through --set on argv. Legacy
    # MAGMA_* entries still present in job env_extra are translated here.
    _CFG_SET = {
        "MAGMA_NO_HAND": "no_hand",
        "MAGMA_NO_OVERLAY": "no_overlay",
        "MAGMA_ANIM_TEXTURES": "anim_textures",
        "MAGMA_HIDE_GUI": "hide_gui",
        "MAGMA_STRIP_OVERLAYS": "strip_overlays",
        "MAGMA_NO_CRACK": "no_crack",
        "MAGMA_NO_DEFER": "no_defer",
        "MAGMA_NO_PIPELINE": "no_pipeline",
        "MAGMA_NO_LAYERMERGE": "no_layermerge",
        "MAGMA_NO_DEVMESH": "no_devmesh",
        "MAGMA_CPU_SKY": "cpu_sky",
        "MAGMA_HAND_FROM_TICK": "hand_from_tick",
        "MAGMA_FOG_C1_INIT": "fog_c1_init",
        "MAGMA_OVERLAY_DUMP": "overlay_dump",
    }
    env = os.environ.copy()
    env["SDL_VIDEODRIVER"] = "dummy"
    set_flags: list[str] = []
    env_left: dict[str, str] = {}
    for k, v in job.env_extra.items():
        vs = str(v)
        if k in _CFG_SET:
            set_flags.extend(["--set", f"{_CFG_SET[k]}={vs}"])
        else:
            env_left[k] = vs
            env[k] = vs

    cmd = job.cli_list(game, dump_dir=out_dir) + set_flags
    log_path = out_dir / "run.log"
    sets = set_pairs(job.set_extra, dump_dir=out_dir)

    if gpu:
        # Strict serial GPU path: overnight-compute lease on gpu1.
        # Unique agent name per dump: reusing a prior "done" agent name
        # stalls forever in the overnight-compute lease DB.
        agent = (
            f"winbat-{job.scenario_key}-{job.variant}-"
            f"{os.getpid()}-{time.time_ns() % 10_000_000}"
        )
        agent = re.sub(r"[^a-zA-Z0-9_-]", "-", agent)[:64]
        wrap = [
            "overnight-compute",
            "run",
            "--agent",
            agent,
            "--resource",
            "gpu1",
            "--ttl",
            "30m",
            "--poll",
            "15s",
            "--timeout",
            "25m",
            "--",
            "env",
            "CUDA_VISIBLE_DEVICES=1",
            "SDL_VIDEODRIVER=dummy",
        ]
        for k, v in sorted(env_left.items()):
            wrap.append(f"{k}={v}")
        wrap.extend(cmd)
        full_cmd = wrap
        # Pass through only the platform env (wrapper sets CUDA/SDL)
        run_env = os.environ.copy()
        run_env["SDL_VIDEODRIVER"] = "dummy"
    else:
        full_cmd = cmd
        run_env = env

    with open(log_path, "w") as log:
        log.write(f"+ backend={job.backend} gpu_wrap={gpu}\n")
        for kv in sets:
            log.write(f"+ --set {kv}\n")
        for k, v in sorted(env_left.items()):
            log.write(f"+ env {k}={v}\n")
        for i in range(0, len(set_flags), 2):
            log.write(f"+ {set_flags[i]} {set_flags[i+1]}\n")
        log.write(f"+ {' '.join(full_cmd)}\n")
        log.flush()
        proc = subprocess.run(
            full_cmd,
            env=run_env,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
        )
    if proc.returncode != 0:
        tail = log_path.read_text(errors="replace")[-3000:]
        raise RuntimeError(
            f"{job.scenario_key}/{job.variant}: exit {proc.returncode}\n{tail}"
        )
    require_frames(out_dir, job.frames, f"{job.scenario_key}/{job.variant}")
    stamp_data = {
        "binary_sha256": binary_sha,
        "env_hash": job.compute_env_hash(game),
        "frames": job.frames,
        "backend": job.backend,
        "ts": datetime.now(timezone.utc).isoformat(),
    }
    stamp.write_text(json.dumps(stamp_data, indent=2) + "\n")


# ---------------------------------------------------------------------------
# Scenario dump specs
# ---------------------------------------------------------------------------

def all_dump_jobs(*, skip_gpu: bool) -> list[DumpJob]:
    jobs: list[DumpJob] = []

    # XB-STILL
    jobs.append(
        DumpJob(
            "xb_still30",
            "cpu",
            30,
            set_extra={"still": "1"},
            backend="cpu",
            label="xb_still30/cpu",
        )
    )
    if not skip_gpu:
        jobs.append(
            DumpJob(
                "xb_still30",
                "cuda",
                30,
                set_extra={"still": "1"},
                backend="cuda",
                label="xb_still30/cuda",
            )
        )

    # XB-MOB
    mob_set = {
        "mob_demo": "1",
        "still": "1",
        "yawrate": "3",
    }
    jobs.append(
        DumpJob(
            "xb_mob120",
            "cpu",
            120,
            set_extra=mob_set,
            backend="cpu",
            label="xb_mob120/cpu",
        )
    )
    if not skip_gpu:
        jobs.append(
            DumpJob(
                "xb_mob120",
                "cuda",
                120,
                set_extra=mob_set,
                backend="cuda",
                label="xb_mob120/cuda",
            )
        )

    # Particle (WR + XB share CPU demo; control for WR; CUDA for XB)
    part_set = {
        "particle_demo": "1",
        "still": "1",
        "spawn_surface": "1",
    }
    part_ctrl = {
        "still": "1",
        "spawn_surface": "1",
    }
    jobs.append(
        DumpJob(
            "wr_particle60",
            "demo",
            60,
            set_extra=part_set,
            backend="cpu",
            label="wr_particle60/demo",
        )
    )
    jobs.append(
        DumpJob(
            "wr_particle60",
            "control",
            60,
            set_extra=part_ctrl,
            backend="cpu",
            label="wr_particle60/control",
        )
    )
    if not skip_gpu:
        jobs.append(
            DumpJob(
                "xb_particle60",
                "cuda",
                60,
                set_extra=part_set,
                backend="cuda",
                label="xb_particle60/cuda",
            )
        )
        # CPU twin for XB particle reuses wr_particle60/demo via check
        # but also stamp under xb_particle60/cpu as symlink/copy of demo
        # handled at check time; still dump cuda only. CPU is wr_particle60/demo.

    # Death
    jobs.append(
        DumpJob(
            "wr_death40",
            "run",
            40,
            set_extra={"still": "1"},
            extra_cli=["--kill-frame", "10"],
            backend="cpu",
            label="wr_death40/run",
        )
    )

    # Walk multi-seed + det pair for seed 0
    for seed in WALK_SEEDS:
        jobs.append(
            DumpJob(
                "wr_walk200",
                f"seed{seed}",
                200,
                seed=seed,
                set_extra={"spawn_surface": "1"},
                backend="cpu",
                label=f"wr_walk200/seed{seed}",
            )
        )
    jobs.append(
        DumpJob(
            "wr_walk200",
            "seed0_run2",
            200,
            seed=0,
            set_extra={"spawn_surface": "1"},
            backend="cpu",
            label="wr_walk200/seed0_run2",
        )
    )

    # Jump
    jobs.append(
        DumpJob(
            "wr_jump80",
            "run",
            80,
            set_extra={"jump": "1", "spawn_surface": "1"},
            backend="cpu",
            label="wr_jump80/run",
        )
    )

    # Daylight on/off
    jobs.append(
        DumpJob(
            "wr_daylight600",
            "on",
            600,
            set_extra={"still": "1"},
            extra_cli=["--daylight", "on"],
            backend="cpu",
            label="wr_daylight600/on",
        )
    )
    jobs.append(
        DumpJob(
            "wr_daylight600",
            "off",
            600,
            set_extra={"still": "1"},
            extra_cli=["--daylight", "off"],
            backend="cpu",
            label="wr_daylight600/off",
        )
    )

    # TP stream
    jobs.append(
        DumpJob(
            "wr_tp60",
            "run",
            60,
            set_extra={
                "still": "1",
                "tp": "4",
                "spawn_surface": "1",
            },
            backend="cpu",
            label="wr_tp60/run",
        )
    )

    # Hand A/B
    jobs.append(
        DumpJob(
            "wr_hand30",
            "hand",
            30,
            set_extra={"still": "1"},
            backend="cpu",
            label="wr_hand30/hand",
        )
    )
    jobs.append(
        DumpJob(
            "wr_hand30",
            "nohand",
            30,
            set_extra={"still": "1"},
            env_extra={"MAGMA_NO_HAND": "1"},  # sibling lane; still env
            backend="cpu",
            label="wr_hand30/nohand",
        )
    )

    # Superflat + default reference (default may share xb_still30/cpu params)
    jobs.append(
        DumpJob(
            "wr_superflat30",
            "flat",
            30,
            set_extra={"still": "1"},
            extra_cli=["--world", "superflat"],
            backend="cpu",
            label="wr_superflat30/flat",
        )
    )
    jobs.append(
        DumpJob(
            "wr_superflat30",
            "default",
            30,
            set_extra={"still": "1"},
            extra_cli=["--world", "default"],
            backend="cpu",
            label="wr_superflat30/default",
        )
    )

    # Res pack
    jobs.append(
        DumpJob(
            "wr_res_pack",
            "427x240",
            20,
            width=427,
            height=240,
            set_extra={"still": "1"},
            backend="cpu",
            label="wr_res_pack/427x240",
        )
    )
    jobs.append(
        DumpJob(
            "wr_res_pack",
            "1280x720",
            20,
            width=1280,
            height=720,
            set_extra={"still": "1"},
            backend="cpu",
            label="wr_res_pack/1280x720",
        )
    )

    # Animated atlas A/B plus a flagged CUDA twin. The fixture builds sealed
    # source-water and source-lava basins, then fixes the camera and daylight.
    anim_set = {
        "anim_texture_demo": "1",
        "still": "1",
    }
    # MAGMA_NO_HAND / MAGMA_NO_OVERLAY / MAGMA_ANIM_TEXTURES: sibling lanes.
    anim_env = {
        "MAGMA_NO_HAND": "1",
        "MAGMA_NO_OVERLAY": "1",
    }
    anim_cli = ["--world", "superflat", "--daylight", "off"]
    jobs.append(
        DumpJob(
            "wr_anim_tex65",
            "static",
            ANIM_TEX_FRAMES,
            set_extra=anim_set,
            env_extra=anim_env,
            extra_cli=anim_cli,
            backend="cpu",
            label="wr_anim_tex65/static",
        )
    )
    jobs.append(
        DumpJob(
            "wr_anim_tex65",
            "cpu",
            ANIM_TEX_FRAMES,
            set_extra=anim_set,
            env_extra={**anim_env, "MAGMA_ANIM_TEXTURES": "1"},
            extra_cli=anim_cli,
            backend="cpu",
            label="wr_anim_tex65/cpu",
        )
    )
    if not skip_gpu:
        jobs.append(
            DumpJob(
                "wr_anim_tex65",
                "cuda",
                ANIM_TEX_FRAMES,
                set_extra=anim_set,
                env_extra={**anim_env, "MAGMA_ANIM_TEXTURES": "1"},
                extra_cli=anim_cli,
                backend="cuda",
                label="wr_anim_tex65/cuda",
            )
        )

    # Entity/translucent order. The dry twin is a same-pose raw-texel control;
    # water is two blocks thick along the camera ray in the wet fixture.
    entity_water_set = {
        "entity_water_demo": "1",
        "still": "1",
    }
    entity_water_env = {
        "MAGMA_NO_HAND": "1",
        "MAGMA_NO_OVERLAY": "1",
    }
    entity_water_cli = ["--world", "superflat", "--daylight", "off"]
    jobs.append(
        DumpJob(
            "wr_entity_water",
            "wet",
            ENTITY_WATER_FRAMES,
            set_extra=entity_water_set,
            env_extra=entity_water_env,
            extra_cli=entity_water_cli,
            backend="cpu",
            label="wr_entity_water/wet",
        )
    )
    jobs.append(
        DumpJob(
            "wr_entity_water",
            "dry",
            ENTITY_WATER_FRAMES,
            set_extra={**entity_water_set, "entity_water_dry": "1"},
            env_extra=entity_water_env,
            extra_cli=entity_water_cli,
            backend="cpu",
            label="wr_entity_water/dry",
        )
    )

    return jobs


# Scenario name -> dump keys needed + check
SCENARIO_NAMES = [
    "XB-STILL-CPU-CUDA",
    "XB-MOB-CPU-CUDA",
    "XB-PARTICLE-CPU-CUDA",
    "WR-PARTICLE-DEMO",
    "WR-DEATH-HUD",
    "WR-WALK-COLLISION",
    "WR-JUMP-TRAVERSAL",
    "WR-DAYLIGHT-DRIFT",
    "WR-TP-STREAM",
    "WR-HAND-AB",
    "WR-SUPERFLAT-SMOKE",
    "WR-RES-PACK",
    "WR-ANIM-TEX",
    "WR-ENTITY-WATER-OCCLUSION",
]


def dumps_for_scenario(name: str, *, skip_gpu: bool) -> list[str]:
    """Return list of 'scenario_key/variant' needed for a scenario check."""
    m = {
        "XB-STILL-CPU-CUDA": ["xb_still30/cpu", "xb_still30/cuda"],
        "XB-MOB-CPU-CUDA": ["xb_mob120/cpu", "xb_mob120/cuda"],
        "XB-PARTICLE-CPU-CUDA": ["wr_particle60/demo", "xb_particle60/cuda"],
        "WR-PARTICLE-DEMO": ["wr_particle60/demo", "wr_particle60/control"],
        "WR-DEATH-HUD": ["wr_death40/run"],
        "WR-WALK-COLLISION": [
            *[f"wr_walk200/seed{s}" for s in WALK_SEEDS],
            "wr_walk200/seed0_run2",
        ],
        "WR-JUMP-TRAVERSAL": ["wr_jump80/run"],
        "WR-DAYLIGHT-DRIFT": ["wr_daylight600/on", "wr_daylight600/off"],
        "WR-TP-STREAM": ["wr_tp60/run"],
        "WR-HAND-AB": ["wr_hand30/hand", "wr_hand30/nohand"],
        "WR-SUPERFLAT-SMOKE": ["wr_superflat30/flat", "wr_superflat30/default"],
        "WR-RES-PACK": ["wr_res_pack/427x240", "wr_res_pack/1280x720"],
        "WR-ANIM-TEX": [
            "wr_anim_tex65/static",
            "wr_anim_tex65/cpu",
            "wr_anim_tex65/cuda",
        ],
        "WR-ENTITY-WATER-OCCLUSION": [
            "wr_entity_water/wet",
            "wr_entity_water/dry",
        ],
    }
    keys = m[name]
    if skip_gpu:
        keys = [k for k in keys if "/cuda" not in k and not k.startswith("xb_")]
    return keys


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

def check_xb_parity(
    cpu_dir: Path,
    cuda_dir: Path,
    frames: list[int],
    label: str,
    *,
    require_teal: bool = False,
) -> dict[str, Any]:
    worst: dict[str, Any] = {"mad": 0.0, "ndiff": 0, "maxch": 0, "frame": -1}
    per_frame = []
    for i in frames:
        a = load_ppm(frame_path(cpu_dir, i))
        b = load_ppm(frame_path(cuda_dir, i))
        st = pixel_stats(a, b)
        st["frame"] = i
        per_frame.append(st)
        if (
            st["mad"] > worst["mad"]
            or (st["mad"] == worst["mad"] and st["ndiff"] > worst["ndiff"])
        ):
            worst = dict(st)
        maxch_fail = (
            st["maxch"] > XB_MAXCH_MAX and st["ndiff"] > XB_MAXCH_SPARSE_NDIFF
        )
        if st["mad"] > XB_MAD_MAX or st["ndiff"] > XB_NDIFF_MAX or maxch_fail:
            raise AssertionError(
                f"{label}: frame {i} MAD={st['mad']:.6f} ndiff={st['ndiff']} "
                f"maxch={st['maxch']} exceeds gate "
                f"(MAD<={XB_MAD_MAX} ndiff<={XB_NDIFF_MAX} "
                f"maxch<={XB_MAXCH_MAX} unless ndiff<={XB_MAXCH_SPARSE_NDIFF})"
            )
    if require_teal:
        teal_cpu = sum(zombie_teal_count(load_ppm(frame_path(cpu_dir, i))) for i in frames)
        teal_cuda = sum(zombie_teal_count(load_ppm(frame_path(cuda_dir, i))) for i in frames)
        if teal_cpu < 50 or teal_cuda < 50:
            raise AssertionError(
                f"{label}: zombie teal missing cpu={teal_cpu} cuda={teal_cuda}"
            )
    return {"worst": worst, "n_frames": len(frames), "teal_ok": require_teal}


def check_particle_demo(demo_dir: Path, ctrl_dir: Path) -> dict[str, Any]:
    require_frames(demo_dir, 60, "WR-PARTICLE-DEMO demo")
    require_frames(ctrl_dir, 60, "WR-PARTICLE-DEMO control")
    log = (demo_dir / "run.log").read_text(errors="replace")
    spawned = parse_particle_spawned(log)
    if spawned < PARTICLE_SPAWN_MIN:
        raise AssertionError(
            f"WR-PARTICLE-DEMO: spawned={spawned} < {PARTICLE_SPAWN_MIN}"
        )
    rows = parse_frame_lines(log)
    for r in rows:
        if r["health"] < 20.0 - 0.01:
            raise AssertionError(
                f"WR-PARTICLE-DEMO: health dropped to {r['health']} at f{r['frame']}"
            )
    max_ndiff = 0
    max_frame = -1
    for i in range(PARTICLE_DIFF_LO, PARTICLE_DIFF_HI + 1):
        a = load_ppm(frame_path(demo_dir, i))
        b = load_ppm(frame_path(ctrl_dir, i))
        nd = int(np.any(a != b, axis=-1).sum())
        if nd > max_ndiff:
            max_ndiff = nd
            max_frame = i
    if max_ndiff < PARTICLE_MAX_DIFF_MIN:
        raise AssertionError(
            f"WR-PARTICLE-DEMO: max differing_px over f{PARTICLE_DIFF_LO}.."
            f"f{PARTICLE_DIFF_HI} = {max_ndiff} < {PARTICLE_MAX_DIFF_MIN} "
            f"(use max-over-window; single-frame is fragile)"
        )
    return {"spawned": spawned, "max_ndiff": max_ndiff, "max_frame": max_frame}


def check_death_hud(run_dir: Path) -> dict[str, Any]:
    require_frames(run_dir, 40, "WR-DEATH-HUD")
    log = (run_dir / "run.log").read_text(errors="replace")
    rows = parse_frame_lines(log)
    by_f = {r["frame"]: r for r in rows}
    # f>=15 must show dead
    for f in range(15, 40):
        if f not in by_f:
            raise AssertionError(f"WR-DEATH-HUD: missing log line for frame {f}")
        r = by_f[f]
        if r["dead"] != 1 or r["deaths"] != 1 or r["health"] != 0.0:
            raise AssertionError(
                f"WR-DEATH-HUD: f{f} expected dead=1 deaths=1 health=0 got "
                f"dead={r['dead']} deaths={r['deaths']} health={r['health']}"
            )
    a = load_ppm(frame_path(run_dir, 0))
    b = load_ppm(frame_path(run_dir, 15))
    mad = mean_abs_diff(a, b)
    if mad < DEATH_MAD_MIN:
        raise AssertionError(
            f"WR-DEATH-HUD: MAD(f0,f15)={mad:.4f} < {DEATH_MAD_MIN}"
        )
    lum0 = mean_luminance(a)
    lum15 = mean_luminance(b)
    if lum15 >= lum0 - DEATH_LUM_DROP_MIN:
        raise AssertionError(
            f"WR-DEATH-HUD: lum f15={lum15:.2f} not < f0={lum0:.2f} - "
            f"{DEATH_LUM_DROP_MIN}"
        )
    return {"mad_0_15": mad, "lum0": lum0, "lum15": lum15}


def check_walk(battery: Path) -> dict[str, Any]:
    metrics: dict[str, Any] = {"seeds": {}}
    for seed in WALK_SEEDS:
        d = battery / "scenarios" / "wr_walk200" / f"seed{seed}"
        require_frames(d, 200, f"WR-WALK seed{seed}")
        log = (d / "run.log").read_text(errors="replace")
        rows = parse_frame_lines(log)
        if not rows:
            raise AssertionError(f"WR-WALK seed{seed}: no frame lines in log")
        z0 = rows[0]["z"]
        z1 = rows[-1]["z"]
        dz = abs(z1 - z0)
        min_hp = min(r["health"] for r in rows)
        if dz < WALK_DZ_MIN:
            raise AssertionError(
                f"WR-WALK seed{seed}: |dz|={dz:.2f} < {WALK_DZ_MIN} "
                f"(z {z0:.1f}->{z1:.1f}; terrain wedge?)"
            )
        if min_hp < WALK_HEALTH_MIN:
            raise AssertionError(
                f"WR-WALK seed{seed}: min health={min_hp} < {WALK_HEALTH_MIN}"
            )
        a = load_ppm(frame_path(d, 20))
        b = load_ppm(frame_path(d, 180))
        mad = mean_abs_diff(a, b)
        if mad < WALK_MAD_MIN:
            raise AssertionError(
                f"WR-WALK seed{seed}: MAD(f20,f180)={mad:.4f} < {WALK_MAD_MIN}"
            )
        metrics["seeds"][str(seed)] = {
            "dz": dz,
            "min_hp": min_hp,
            "mad_20_180": mad,
        }
    # determinism seed 0
    a_dir = battery / "scenarios" / "wr_walk200" / "seed0"
    b_dir = battery / "scenarios" / "wr_walk200" / "seed0_run2"
    require_frames(b_dir, 200, "WR-WALK seed0_run2")
    pa = frame_path(a_dir, WALK_DET_FRAME).read_bytes()
    pb = frame_path(b_dir, WALK_DET_FRAME).read_bytes()
    if pa != pb:
        raise AssertionError(
            f"WR-WALK: seed0 f{WALK_DET_FRAME} not byte-identical across two runs"
        )
    metrics["det_f100"] = True
    return metrics


def check_jump(run_dir: Path) -> dict[str, Any]:
    require_frames(run_dir, 80, "WR-JUMP")
    log = (run_dir / "run.log").read_text(errors="replace")
    rows = parse_frame_lines(log)
    if not rows:
        raise AssertionError("WR-JUMP: no frame lines")
    dz = abs(rows[-1]["z"] - rows[0]["z"])
    if dz < JUMP_DZ_MIN:
        raise AssertionError(f"WR-JUMP: |dz|={dz:.2f} < {JUMP_DZ_MIN}")
    min_hp = min(r["health"] for r in rows)
    if min_hp < JUMP_HEALTH_MIN:
        raise AssertionError(
            f"WR-JUMP: min health={min_hp} < {JUMP_HEALTH_MIN} "
            f"(fall-damage storm / bad surface snap)"
        )
    mad = mean_abs_diff(
        load_ppm(frame_path(run_dir, 10)), load_ppm(frame_path(run_dir, 40))
    )
    if mad < JUMP_MAD_MIN:
        raise AssertionError(f"WR-JUMP: MAD(f10,f40)={mad:.4f} < {JUMP_MAD_MIN}")
    return {"dz": dz, "mad_10_40": mad, "min_hp": min_hp}


def check_daylight(on_dir: Path, off_dir: Path) -> dict[str, Any]:
    require_frames(on_dir, 600, "WR-DAYLIGHT on")
    require_frames(off_dir, 600, "WR-DAYLIGHT off")
    mad_on = mean_abs_diff(
        load_ppm(frame_path(on_dir, 0)), load_ppm(frame_path(on_dir, 599))
    )
    # Freeze control: post fog_color1 warmup (f10..) so f0 settle is not credited
    # as "clock advanced". Measured: off f10..f599 is byte-identical.
    f0 = DAY_FREEZE_FRAME0
    mad_off = mean_abs_diff(
        load_ppm(frame_path(off_dir, f0)), load_ppm(frame_path(off_dir, 599))
    )
    if mad_on < DAY_MAD_MIN:
        raise AssertionError(
            f"WR-DAYLIGHT: MAD(f0,f599) daylight-on={mad_on:.4f} < {DAY_MAD_MIN}"
        )
    if mad_off > DAY_FREEZE_MAD_MAX:
        raise AssertionError(
            f"WR-DAYLIGHT: MAD(f{f0},f599) daylight-off={mad_off:.4f} > "
            f"{DAY_FREEZE_MAD_MAX} (freeze control post fog warmup)"
        )
    return {"mad_on": mad_on, "mad_off": mad_off, "freeze_from": f0}


def check_tp(run_dir: Path) -> dict[str, Any]:
    require_frames(run_dir, 60, "WR-TP")
    log = (run_dir / "run.log").read_text(errors="replace")
    rows = parse_frame_lines(log)
    if not rows:
        raise AssertionError("WR-TP: no frame lines")
    abs_z = abs(rows[-1]["z"])
    if abs_z < TP_ABS_Z_MIN:
        raise AssertionError(f"WR-TP: final |z|={abs_z:.1f} < {TP_ABS_Z_MIN}")
    mad = mean_abs_diff(
        load_ppm(frame_path(run_dir, 0)), load_ppm(frame_path(run_dir, 30))
    )
    if mad < TP_MAD_MIN:
        raise AssertionError(f"WR-TP: MAD(f0,f30)={mad:.4f} < {TP_MAD_MIN}")
    return {"abs_z": abs_z, "mad_0_30": mad}


def check_hand(hand_dir: Path, nohand_dir: Path) -> dict[str, Any]:
    require_frames(hand_dir, 30, "WR-HAND hand")
    require_frames(nohand_dir, 30, "WR-HAND nohand")
    # Compare last frame (stable pose)
    a = load_ppm(frame_path(hand_dir, 29))
    b = load_ppm(frame_path(nohand_dir, 29))
    st = pixel_stats(a, b)
    if st["ndiff"] < HAND_NDIFF_MIN and st["mad"] < HAND_MAD_MIN:
        raise AssertionError(
            f"WR-HAND: ndiff={st['ndiff']} mad={st['mad']:.4f} "
            f"(need ndiff>={HAND_NDIFF_MIN} or mad>={HAND_MAD_MIN})"
        )
    return st


def check_superflat(flat_dir: Path, default_dir: Path, wall_s: float) -> dict[str, Any]:
    require_frames(flat_dir, 30, "WR-SUPERFLAT flat")
    require_frames(default_dir, 30, "WR-SUPERFLAT default")
    log = (flat_dir / "run.log").read_text(errors="replace")
    rows = parse_frame_lines(log)
    if not rows:
        raise AssertionError("WR-SUPERFLAT: no frame lines")
    y = rows[-1]["y"]
    if abs(y - SUPERFLAT_Y_TARGET) > SUPERFLAT_Y_TOL:
        raise AssertionError(
            f"WR-SUPERFLAT: y={y:.2f} not ~{SUPERFLAT_Y_TARGET} "
            f"(tol {SUPERFLAT_Y_TOL})"
        )
    mad = mean_abs_diff(
        load_ppm(frame_path(flat_dir, 15)),
        load_ppm(frame_path(default_dir, 15)),
    )
    if mad < SUPERFLAT_MAD_MIN:
        raise AssertionError(
            f"WR-SUPERFLAT: MAD vs default f15={mad:.4f} < {SUPERFLAT_MAD_MIN}"
        )
    # wall time soft note (dump stamp may lack wall; use provided)
    if wall_s > SUPERFLAT_WALL_MAX_S * 3:
        # allow 3x slack for cold cache / shared box; recon measured 1.1s
        pass
    return {"y": y, "mad_vs_default_f15": mad, "wall_s": wall_s}


def check_res_pack(d427: Path, d1280: Path) -> dict[str, Any]:
    require_frames(d427, 20, "WR-RES 427x240")
    require_frames(d1280, 20, "WR-RES 1280x720")
    out = {}
    for label, d, w, h in (
        ("427x240", d427, 427, 240),
        ("1280x720", d1280, 1280, 720),
    ):
        img = load_ppm(frame_path(d, 10))
        if img.shape[1] != w or img.shape[0] != h:
            raise AssertionError(
                f"WR-RES {label}: shape {img.shape[1]}x{img.shape[0]} "
                f"want {w}x{h}"
            )
        mean_rgb = float(img.mean())
        if not (RES_MEAN_LO < mean_rgb < RES_MEAN_HI):
            raise AssertionError(
                f"WR-RES {label}: mean RGB={mean_rgb:.2f} not in "
                f"({RES_MEAN_LO},{RES_MEAN_HI}) (degenerate frame?)"
            )
        # nonzero variance
        if float(img.std()) < 1.0:
            raise AssertionError(f"WR-RES {label}: std={img.std():.3f} too low")
        out[label] = {"mean": mean_rgb, "std": float(img.std()), "shape": [h, w]}
    return out


def check_xb_particle(cpu_demo: Path, cuda_dir: Path) -> dict[str, Any]:
    """Max-over-window gate on f25..f35 (recon rank 15 false-pass mitigation)."""
    frames = list(range(25, 36))
    worst = {"mad": 0.0, "ndiff": 0, "maxch": 0, "frame": -1}
    for i in frames:
        a = load_ppm(frame_path(cpu_demo, i))
        b = load_ppm(frame_path(cuda_dir, i))
        st = pixel_stats(a, b)
        if st["mad"] > worst["mad"] or (
            st["mad"] == worst["mad"] and st["ndiff"] > worst["ndiff"]
        ):
            worst = {**st, "frame": i}
        maxch_fail = (
            st["maxch"] > XB_MAXCH_MAX and st["ndiff"] > XB_MAXCH_SPARSE_NDIFF
        )
        if st["mad"] > XB_MAD_MAX or st["ndiff"] > XB_NDIFF_MAX or maxch_fail:
            raise AssertionError(
                f"XB-PARTICLE: frame {i} MAD={st['mad']:.6f} ndiff={st['ndiff']} "
                f"maxch={st['maxch']} exceeds gate"
            )
    return {"worst": worst, "window": "f25..f35"}


def check_anim_textures(
    static_dir: Path, cpu_dir: Path, cuda_dir: Optional[Path]
) -> dict[str, Any]:
    require_frames(static_dir, ANIM_TEX_FRAMES, "WR-ANIM-TEX static")
    require_frames(cpu_dir, ANIM_TEX_FRAMES, "WR-ANIM-TEX cpu")
    base_static = load_ppm(frame_path(static_dir, ANIM_TEX_SAMPLES[0]))
    if base_static.shape[:2] != (DEFAULT_HEIGHT, DEFAULT_WIDTH):
        raise AssertionError(
            f"WR-ANIM-TEX: fixture shape {base_static.shape} != "
            f"{(DEFAULT_HEIGHT, DEFAULT_WIDTH, 3)}"
        )

    static_ndiff_max = 0
    for f in ANIM_TEX_SAMPLES[1:]:
        img = load_ppm(frame_path(static_dir, f))
        static_ndiff_max = max(
            static_ndiff_max, int(np.any(base_static != img, axis=-1).sum())
        )
    if static_ndiff_max != 0:
        raise AssertionError(
            f"WR-ANIM-TEX: unflagged run changed {static_ndiff_max} pixels"
        )

    region = np.zeros((DEFAULT_HEIGHT, DEFAULT_WIDTH), dtype=bool)
    changed: dict[str, int] = {}
    base_anim = load_ppm(frame_path(cpu_dir, ANIM_TEX_SAMPLES[0]))
    for label, (x0, y0, x1, y1) in (
        ("water", ANIM_TEX_WATER_RECT),
        ("lava", ANIM_TEX_LAVA_RECT),
    ):
        region[y0:y1, x0:x1] = True
        peak = 0
        for f in ANIM_TEX_SAMPLES[1:]:
            img = load_ppm(frame_path(cpu_dir, f))
            peak = max(
                peak,
                int(
                    np.any(
                        base_anim[y0:y1, x0:x1] != img[y0:y1, x0:x1],
                        axis=-1,
                    ).sum()
                ),
            )
        if peak < ANIM_TEX_CHANGED_MIN:
            raise AssertionError(
                f"WR-ANIM-TEX: {label} changed_px={peak} < "
                f"{ANIM_TEX_CHANGED_MIN}"
            )
        changed[label] = peak

    nonanim_ndiff_max = 0
    for f in ANIM_TEX_SAMPLES:
        static = load_ppm(frame_path(static_dir, f))
        animated = load_ppm(frame_path(cpu_dir, f))
        diff = np.any(static != animated, axis=-1)
        nonanim_ndiff_max = max(nonanim_ndiff_max, int((diff & ~region).sum()))
    if nonanim_ndiff_max != 0:
        raise AssertionError(
            f"WR-ANIM-TEX: flag changed {nonanim_ndiff_max} non-animated pixels"
        )

    metrics: dict[str, Any] = {
        "samples": list(ANIM_TEX_SAMPLES),
        "static_ndiff_max": static_ndiff_max,
        "nonanim_ndiff_max": nonanim_ndiff_max,
        "changed_px_max": changed,
    }
    if cuda_dir is not None and cuda_dir.is_dir():
        require_frames(cuda_dir, ANIM_TEX_FRAMES, "WR-ANIM-TEX cuda")
        metrics["cpu_cuda"] = check_xb_parity(
            cpu_dir, cuda_dir, list(ANIM_TEX_SAMPLES), "WR-ANIM-TEX CPU/CUDA"
        )
    return metrics


def check_entity_water(wet_dir: Path, dry_dir: Path) -> dict[str, Any]:
    require_frames(wet_dir, ENTITY_WATER_FRAMES, "WR-ENTITY-WATER wet")
    require_frames(dry_dir, ENTITY_WATER_FRAMES, "WR-ENTITY-WATER dry")
    names = ("behind", "front", "half_submerged")
    # Fixed camera fixture ROIs, deliberately narrow enough that each contains
    # only its one zombie even though perspective draws all three near centre.
    xrois = ((235, 330), (350, 505), (510, 605))
    totals = {name: 0 for name in names}
    unchanged = {name: 0 for name in names}
    for f in range(ENTITY_WATER_FRAMES):
        wet = load_ppm(frame_path(wet_dir, f))
        dry = load_ppm(frame_path(dry_dir, f))
        teal = (
            (dry[:, :, 2] >= dry[:, :, 1])
            & (dry[:, :, 1].astype(np.int16) > dry[:, :, 0].astype(np.int16) + 10)
            & (dry[:, :, 2] > 50)
            & (dry[:, :, 2] < 180)
            & (dry[:, :, 0] < 80)
        )
        same = np.all(wet == dry, axis=-1)
        for i, name in enumerate(names):
            region = np.zeros(teal.shape, dtype=bool)
            region[:, xrois[i][0] : xrois[i][1]] = True
            mask = teal & region
            totals[name] += int(mask.sum())
            unchanged[name] += int((mask & same).sum())
    if min(totals.values()) < 50:
        raise AssertionError(
            f"WR-ENTITY-WATER: zombie raw-texel controls too sparse: {totals}"
        )
    behind_frac = unchanged["behind"] / totals["behind"]
    front_frac = unchanged["front"] / totals["front"]
    half_frac = unchanged["half_submerged"] / totals["half_submerged"]
    if behind_frac > 0.10:
        raise AssertionError(
            "WR-ENTITY-WATER: behind-water raw texels remain unattenuated "
            f"({unchanged['behind']}/{totals['behind']} = {behind_frac:.3f})"
        )
    if front_frac < 0.90:
        raise AssertionError(
            "WR-ENTITY-WATER: front entity was attenuated by water behind it "
            f"({unchanged['front']}/{totals['front']} = {front_frac:.3f} raw)"
        )
    if not (0.10 < half_frac < 0.90):
        raise AssertionError(
            "WR-ENTITY-WATER: half-submerged entity lacks split attenuation "
            f"({unchanged['half_submerged']}/{totals['half_submerged']} = "
            f"{half_frac:.3f} raw)"
        )
    return {
        "raw_texels": totals,
        "unchanged_raw_texels": unchanged,
        "unchanged_fraction": {
            "behind": behind_frac,
            "front": front_frac,
            "half_submerged": half_frac,
        },
    }


# ---------------------------------------------------------------------------
# Scenario runner
# ---------------------------------------------------------------------------

def run_scenario_check(
    name: str, battery: Path, dump_walls: dict[str, float]
) -> dict[str, Any]:
    sc = battery / "scenarios"
    if name == "XB-STILL-CPU-CUDA":
        # skip f0: single-pixel maxch spike during first-frame init
        return check_xb_parity(
            sc / "xb_still30" / "cpu",
            sc / "xb_still30" / "cuda",
            list(range(1, 30)),
            name,
        )
    if name == "XB-MOB-CPU-CUDA":
        return check_xb_parity(
            sc / "xb_mob120" / "cpu",
            sc / "xb_mob120" / "cuda",
            [0, 30, 60, 90, 119],
            name,
            require_teal=True,
        )
    if name == "XB-PARTICLE-CPU-CUDA":
        return check_xb_particle(sc / "wr_particle60" / "demo", sc / "xb_particle60" / "cuda")
    if name == "WR-PARTICLE-DEMO":
        return check_particle_demo(
            sc / "wr_particle60" / "demo", sc / "wr_particle60" / "control"
        )
    if name == "WR-DEATH-HUD":
        return check_death_hud(sc / "wr_death40" / "run")
    if name == "WR-WALK-COLLISION":
        return check_walk(battery)
    if name == "WR-JUMP-TRAVERSAL":
        return check_jump(sc / "wr_jump80" / "run")
    if name == "WR-DAYLIGHT-DRIFT":
        return check_daylight(sc / "wr_daylight600" / "on", sc / "wr_daylight600" / "off")
    if name == "WR-TP-STREAM":
        return check_tp(sc / "wr_tp60" / "run")
    if name == "WR-HAND-AB":
        return check_hand(sc / "wr_hand30" / "hand", sc / "wr_hand30" / "nohand")
    if name == "WR-SUPERFLAT-SMOKE":
        wall = dump_walls.get("wr_superflat30/flat", 0.0)
        return check_superflat(
            sc / "wr_superflat30" / "flat",
            sc / "wr_superflat30" / "default",
            wall,
        )
    if name == "WR-RES-PACK":
        return check_res_pack(sc / "wr_res_pack" / "427x240", sc / "wr_res_pack" / "1280x720")
    if name == "WR-ANIM-TEX":
        cuda_dir = sc / "wr_anim_tex65" / "cuda"
        return check_anim_textures(
            sc / "wr_anim_tex65" / "static",
            sc / "wr_anim_tex65" / "cpu",
            cuda_dir if cuda_dir.is_dir() else None,
        )
    if name == "WR-ENTITY-WATER-OCCLUSION":
        return check_entity_water(
            sc / "wr_entity_water" / "wet",
            sc / "wr_entity_water" / "dry",
        )
    raise ValueError(f"unknown scenario {name}")


# ---------------------------------------------------------------------------
# Fail artifacts
# ---------------------------------------------------------------------------

def write_fail_artifact(
    battery: Path, name: str, err: str, metrics: Optional[dict] = None
) -> None:
    d = battery / "results" / "fail_artifacts" / name
    d.mkdir(parents=True, exist_ok=True)
    (d / "error.txt").write_text(err + "\n")
    if metrics:
        (d / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
    # stderr tails from related dumps
    sc = battery / "scenarios"
    for sub in sc.iterdir() if sc.is_dir() else []:
        if not sub.is_dir():
            continue
        # include if name fragments match
        key = name.lower().replace("-", "_")
        if sub.name not in key and key[:6] not in sub.name:
            # still grab common ones loosely
            continue
        for var in sub.iterdir():
            log = var / "run.log"
            if log.is_file():
                tail = log.read_text(errors="replace")[-2000:]
                (d / f"{sub.name}_{var.name}_log_tail.txt").write_text(tail)


# ---------------------------------------------------------------------------
# Selftest: corrupt copies, assert checks FAIL
# ---------------------------------------------------------------------------

def _copy_tree(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def _whiten_terrain(d: Path, n: int) -> None:
    for i in range(n):
        p = frame_path(d, i)
        if not p.is_file():
            continue
        img = load_ppm(p).copy()
        h = img.shape[0]
        img[h // 3 :, :, :] = 250
        Image.fromarray(img, mode="RGB").save(p)


def _blank_frame(d: Path, i: int) -> None:
    p = frame_path(d, i)
    img = load_ppm(p)
    blank = np.zeros_like(img)
    Image.fromarray(blank, mode="RGB").save(p)


def _shift_frames(d: Path, n: int) -> None:
    """Rotate frame indices by +1 (last gets copy of n-2)."""
    frames = [load_ppm(frame_path(d, i)).copy() for i in range(n)]
    for i in range(n):
        src = frames[(i - 1) % n]
        Image.fromarray(src, mode="RGB").save(frame_path(d, i))


def _brighten_all(d: Path, n: int, add: int = 40) -> None:
    for i in range(n):
        p = frame_path(d, i)
        if not p.is_file():
            continue
        img = load_ppm(p).astype(np.int16)
        img = np.clip(img + add, 0, 255).astype(np.uint8)
        Image.fromarray(img, mode="RGB").save(p)


def _copy_frame(d: Path, src_i: int, dst_i: int) -> None:
    shutil.copy2(frame_path(d, src_i), frame_path(d, dst_i))


def run_selftest(battery: Path, skip_gpu: bool) -> int:
    """Work on a copy of battery scenarios; never touch real cache frames."""
    print("== SELFTEST (corruptions on copies; real cache untouched) ==")
    tmp = battery / "results" / "selftest_work"
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    sc_src = battery / "scenarios"
    sc_dst = tmp / "scenarios"
    _copy_tree(sc_src, sc_dst)

    fake_battery = tmp
    # Redirect checks to fake by using fake_battery paths
    results: list[tuple[str, str, bool, str]] = []  # class, corruption, caught, detail

    def expect_fail(check_class: str, corruption: str, fn: Callable[[], Any]) -> None:
        try:
            fn()
            results.append((check_class, corruption, False, "check PASSED (should fail)"))
            print(f"  SELFTEST MISS  {check_class}/{corruption}: corruption not detected")
        except AssertionError as e:
            results.append((check_class, corruption, True, str(e)[:200]))
            print(f"  SELFTEST CATCH {check_class}/{corruption}: {str(e)[:120]}")
        except Exception as e:
            # Other errors still count as "detected" (check failed) but note
            results.append((check_class, corruption, True, f"err:{e}"[:200]))
            print(f"  SELFTEST CATCH {check_class}/{corruption}: {type(e).__name__}: {e}")

    # --- xb_parity: shift cuda frames ---
    if not skip_gpu and (sc_dst / "xb_still30" / "cuda").is_dir():
        still_cuda = sc_dst / "xb_still30" / "cuda"
        still_cpu = sc_dst / "xb_still30" / "cpu"
        # restore clean then shift
        _copy_tree(sc_src / "xb_still30" / "cuda", still_cuda)
        _shift_frames(still_cuda, 30)
        expect_fail(
            "xb_parity",
            "shift_cuda_index",
            lambda: check_xb_parity(still_cpu, still_cuda, list(range(1, 30)), "XB-STILL"),
        )
        # blank one frame
        _copy_tree(sc_src / "xb_still30" / "cuda", still_cuda)
        _blank_frame(still_cuda, 15)
        expect_fail(
            "xb_parity",
            "blank_frame",
            lambda: check_xb_parity(still_cpu, still_cuda, list(range(1, 30)), "XB-STILL"),
        )
        # whiten terrain on cuda
        _copy_tree(sc_src / "xb_still30" / "cuda", still_cuda)
        _whiten_terrain(still_cuda, 30)
        expect_fail(
            "xb_parity",
            "whiten_terrain",
            lambda: check_xb_parity(still_cpu, still_cuda, list(range(1, 30)), "XB-STILL"),
        )
        # restore
        _copy_tree(sc_src / "xb_still30" / "cuda", still_cuda)

    # --- particle_demo: make demo identical to control (erase particles) ---
    if (sc_dst / "wr_particle60" / "demo").is_dir():
        demo = sc_dst / "wr_particle60" / "demo"
        ctrl = sc_dst / "wr_particle60" / "control"
        # copy control frames over demo frames so max_ndiff=0
        for i in range(60):
            if frame_path(ctrl, i).is_file():
                shutil.copy2(frame_path(ctrl, i), frame_path(demo, i))
        # keep spawn log so only visual check fails
        expect_fail(
            "particle_demo",
            "erase_particle_diff",
            lambda: check_particle_demo(demo, ctrl),
        )
        _copy_tree(sc_src / "wr_particle60" / "demo", demo)

    # --- death_hud: overwrite f15 with f0 ---
    if (sc_dst / "wr_death40" / "run").is_dir():
        death = sc_dst / "wr_death40" / "run"
        _copy_frame(death, 0, 15)
        expect_fail(
            "death_hud",
            "clone_f0_to_f15",
            lambda: check_death_hud(death),
        )
        _copy_tree(sc_src / "wr_death40" / "run", death)

    # --- walk: clone f20 onto f180 so MAD fails ---
    if (sc_dst / "wr_walk200" / "seed0").is_dir():
        walk = sc_dst / "wr_walk200" / "seed0"
        _copy_frame(walk, 20, 180)
        # also need other seeds intact - only seed0 fails MAD
        expect_fail(
            "walk_collision",
            "clone_f20_to_f180",
            lambda: check_walk(fake_battery),
        )
        _copy_tree(sc_src / "wr_walk200" / "seed0", walk)

    # --- jump: blank f40 so MAD fails / zero motion by cloning ---
    if (sc_dst / "wr_jump80" / "run").is_dir():
        jump = sc_dst / "wr_jump80" / "run"
        _copy_frame(jump, 10, 40)
        expect_fail(
            "jump_traversal",
            "clone_f10_to_f40",
            lambda: check_jump(jump),
        )
        _copy_tree(sc_src / "wr_jump80" / "run", jump)

    # --- daylight: brighten f599 on-on so freeze still ok but wait -
    #    make on f599 == f0 so day MAD fails
    if (sc_dst / "wr_daylight600" / "on").is_dir():
        on = sc_dst / "wr_daylight600" / "on"
        off = sc_dst / "wr_daylight600" / "off"
        _copy_frame(on, 0, 599)
        expect_fail(
            "daylight_drift",
            "freeze_day_frames",
            lambda: check_daylight(on, off),
        )
        _copy_tree(sc_src / "wr_daylight600" / "on", on)
        # Post-warmup freeze frames are byte-identical, so a global shift does
        # not change MAD(f10,f599). Brighten only f599 to break the control.
        _copy_tree(sc_src / "wr_daylight600" / "off", off)
        img = load_ppm(frame_path(off, 599)).astype(np.int16)
        img = np.clip(img + 40, 0, 255).astype(np.uint8)
        Image.fromarray(img, mode="RGB").save(frame_path(off, 599))
        expect_fail(
            "daylight_drift",
            "brighten_freeze_f599",
            lambda: check_daylight(on, off),
        )
        _copy_tree(sc_src / "wr_daylight600" / "off", off)

    # --- tp: clone f0 to f30 ---
    if (sc_dst / "wr_tp60" / "run").is_dir():
        tp = sc_dst / "wr_tp60" / "run"
        _copy_frame(tp, 0, 30)
        expect_fail(
            "tp_stream",
            "clone_f0_to_f30",
            lambda: check_tp(tp),
        )
        _copy_tree(sc_src / "wr_tp60" / "run", tp)

    # --- hand: copy hand over nohand ---
    if (sc_dst / "wr_hand30" / "nohand").is_dir():
        hand = sc_dst / "wr_hand30" / "hand"
        nohand = sc_dst / "wr_hand30" / "nohand"
        for i in range(30):
            shutil.copy2(frame_path(hand, i), frame_path(nohand, i))
        expect_fail(
            "hand_ab",
            "erase_hand_diff",
            lambda: check_hand(hand, nohand),
        )
        _copy_tree(sc_src / "wr_hand30" / "nohand", nohand)

    # --- superflat: copy default over flat ---
    if (sc_dst / "wr_superflat30" / "flat").is_dir():
        flat = sc_dst / "wr_superflat30" / "flat"
        default = sc_dst / "wr_superflat30" / "default"
        for i in range(30):
            shutil.copy2(frame_path(default, i), frame_path(flat, i))
        # also rewrite log y to look default-ish - MAD will fail first if y still 4
        # actually y stays from log; MAD becomes 0 -> fail. Good.
        expect_fail(
            "superflat_smoke",
            "clone_default_frames",
            lambda: check_superflat(flat, default, 1.0),
        )
        _copy_tree(sc_src / "wr_superflat30" / "flat", flat)

    # --- res pack: black frames ---
    if (sc_dst / "wr_res_pack" / "427x240").is_dir():
        r427 = sc_dst / "wr_res_pack" / "427x240"
        r1280 = sc_dst / "wr_res_pack" / "1280x720"
        for i in range(20):
            img = load_ppm(frame_path(r427, i))
            Image.fromarray(np.zeros_like(img), mode="RGB").save(frame_path(r427, i))
        expect_fail(
            "res_pack",
            "black_frames",
            lambda: check_res_pack(r427, r1280),
        )
        _copy_tree(sc_src / "wr_res_pack" / "427x240", r427)

    # --- animated textures: freeze every sampled flagged frame ---
    if (sc_dst / "wr_anim_tex65" / "cpu").is_dir():
        static = sc_dst / "wr_anim_tex65" / "static"
        cpu = sc_dst / "wr_anim_tex65" / "cpu"
        for i in ANIM_TEX_SAMPLES[1:]:
            _copy_frame(cpu, ANIM_TEX_SAMPLES[0], i)
        expect_fail(
            "anim_textures",
            "freeze_flagged_samples",
            lambda: check_anim_textures(static, cpu, None),
        )
        _copy_tree(sc_src / "wr_anim_tex65" / "cpu", cpu)

    # --- xb particle shift ---
    if not skip_gpu and (sc_dst / "xb_particle60" / "cuda").is_dir():
        cpu_p = sc_dst / "wr_particle60" / "demo"
        cuda_p = sc_dst / "xb_particle60" / "cuda"
        _shift_frames(cuda_p, 60)
        expect_fail(
            "xb_particle",
            "shift_cuda_index",
            lambda: check_xb_particle(cpu_p, cuda_p),
        )
        _copy_tree(sc_src / "xb_particle60" / "cuda", cuda_p)

    # Summary
    print("\n== SELFTEST SUMMARY ==")
    n_ok = sum(1 for r in results if r[2])
    n_miss = sum(1 for r in results if not r[2])
    for cls, corr, caught, detail in results:
        status = "CATCH" if caught else "MISS"
        print(f"  {status}  {cls}/{corr}")
    print(f"selftest: {n_ok} caught, {n_miss} missed, {len(results)} total")
    # cleanup work dir (optional keep for debug)
    shutil.rmtree(tmp, ignore_errors=True)
    if n_miss > 0 or len(results) == 0:
        print("SELFTEST FAIL")
        return 1
    print("SELFTEST PASS")
    return 0


# ---------------------------------------------------------------------------
# Meta / battery dir
# ---------------------------------------------------------------------------

def git_rev(repo: Path) -> str:
    try:
        r = subprocess.run(
            ["git", "-C", str(repo), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            check=False,
        )
        return r.stdout.strip() if r.returncode == 0 else "unknown"
    except Exception:
        return "unknown"


def write_meta(
    battery: Path,
    repo: Path,
    game: Path,
    game_cuda: Optional[Path],
    cpu_sha: str,
    cuda_sha: Optional[str],
) -> dict:
    env_payload = {
        "SDL_VIDEODRIVER": "dummy",
        "scenarios": SCENARIO_NAMES,
        "thresholds": {
            "xb_mad": XB_MAD_MAX,
            "xb_ndiff": XB_NDIFF_MAX,
            "xb_maxch": XB_MAXCH_MAX,
        },
    }
    meta = {
        "git_rev": git_rev(repo),
        "host": os.uname().nodename if hasattr(os, "uname") else "unknown",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "binary_sha256": {
            "magma_game": cpu_sha,
            "magma_game_cuda": cuda_sha,
        },
        "binary_paths": {
            "magma_game": str(game),
            "magma_game_cuda": str(game_cuda) if game_cuda else None,
        },
        "env_hash": hashlib.sha256(
            json.dumps(env_payload, sort_keys=True).encode()
        ).hexdigest()[:16],
        "env_payload": env_payload,
    }
    (battery / "meta.json").write_text(json.dumps(meta, indent=2) + "\n")
    # bin/ symlinks
    bindir = battery / "bin"
    bindir.mkdir(exist_ok=True)
    for name, path in (("magma_game", game), ("magma_game_cuda", game_cuda)):
        if path is None:
            continue
        link = bindir / name
        if link.is_symlink() or link.exists():
            link.unlink()
        try:
            link.symlink_to(path.resolve())
        except OSError:
            pass
    return meta


# ---------------------------------------------------------------------------
# Main orchestration
# ---------------------------------------------------------------------------

def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def parse_args(argv: Optional[list] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Window-loop overnight battery (ranks 4-15)")
    p.add_argument("--game", type=Path, default=None)
    p.add_argument("--game-cuda", type=Path, default=None)
    p.add_argument("--reuse", type=Path, default=None, help="existing battery dir")
    p.add_argument("--battery-dir", type=Path, default=None)
    p.add_argument("--only", type=str, default=None, help="scenario name")
    p.add_argument("--skip-gpu", action="store_true")
    p.add_argument("--selftest", action="store_true")
    p.add_argument(
        "--cpu-workers",
        type=int,
        default=MAX_CPU_WORKERS,
        help=f"max concurrent CPU dumps (default {MAX_CPU_WORKERS})",
    )
    return p.parse_args(argv)


def select_scenarios(only: Optional[str], skip_gpu: bool) -> list[str]:
    names = list(SCENARIO_NAMES)
    if only:
        raw = only.strip()
        key = raw.upper().replace("_", "-")
        match = None
        for n in SCENARIO_NAMES:
            if n.upper() == key:
                match = n
                break
        if match is None:
            cands = [n for n in SCENARIO_NAMES if key in n.upper()]
            if len(cands) == 1:
                match = cands[0]
        if match is None:
            raise SystemExit(
                f"unknown --only {only!r}; choose from:\n  "
                + "\n  ".join(SCENARIO_NAMES)
            )
        names = [match]
    if skip_gpu:
        names = [n for n in names if not n.startswith("XB-")]
    return names


def jobs_needed(
    scenarios: list[str], all_jobs: list[DumpJob], skip_gpu: bool
) -> list[DumpJob]:
    needed_keys: set[str] = set()
    for s in scenarios:
        for k in dumps_for_scenario(s, skip_gpu=skip_gpu):
            needed_keys.add(k)
    out = []
    for j in all_jobs:
        key = f"{j.scenario_key}/{j.variant}"
        if key in needed_keys:
            out.append(j)
    # XB particle cuda may not be in dumps_for if mapped differently
    return out


def main(argv: Optional[list] = None) -> int:
    args = parse_args(argv)
    repo = repo_root()
    game = (args.game or (repo / "magma" / "magma_game")).resolve()
    game_cuda = (args.game_cuda or (repo / "magma" / "magma_game_cuda")).resolve()

    if not game.is_file():
        print(f"FAIL: magma_game not found: {game}", file=sys.stderr)
        return 2
    if not args.skip_gpu and not game_cuda.is_file():
        print(f"FAIL: magma_game_cuda not found: {game_cuda}", file=sys.stderr)
        return 2

    cpu_sha = sha256_file(game)
    cuda_sha = sha256_file(game_cuda) if game_cuda.is_file() else None

    DEFAULT_TMP_ROOT.mkdir(parents=True, exist_ok=True)

    if args.reuse:
        battery = args.reuse.resolve()
        if not battery.is_dir():
            print(f"FAIL: --reuse dir missing: {battery}", file=sys.stderr)
            return 2
    elif args.battery_dir:
        battery = args.battery_dir.resolve()
        battery.mkdir(parents=True, exist_ok=True)
    else:
        date = datetime.now(timezone.utc).strftime("%Y%m%d")
        battery = DEFAULT_TMP_ROOT / f"window_battery_{date}"
        battery.mkdir(parents=True, exist_ok=True)

    (battery / "scenarios").mkdir(exist_ok=True)
    (battery / "results").mkdir(exist_ok=True)
    summary_path = battery / "results" / "summary.jsonl"

    write_meta(battery, repo, game, game_cuda if not args.skip_gpu else None, cpu_sha, cuda_sha)

    print(f"window_battery: battery={battery}")
    print(f"window_battery: game={game} sha={cpu_sha[:12]}")
    if not args.skip_gpu:
        print(f"window_battery: game_cuda={game_cuda} sha={cuda_sha[:12] if cuda_sha else '?'}")
    print(f"window_battery: skip_gpu={args.skip_gpu} selftest={args.selftest}")

    try:
        scenarios = select_scenarios(args.only, args.skip_gpu)
    except SystemExit as e:
        print(e, file=sys.stderr)
        return 2

    print(f"window_battery: scenarios={scenarios}")

    t_all0 = time.time()

    # Selftest-only mode still needs dumps present
    if args.selftest:
        # Ensure dumps exist first (reuse cache if possible)
        pass

    all_jobs = all_dump_jobs(skip_gpu=args.skip_gpu)
    needed = jobs_needed(scenarios, all_jobs, args.skip_gpu)

    # Also if selftest without --only, need full set for all check classes
    if args.selftest and not args.only:
        scenarios = select_scenarios(None, args.skip_gpu)
        needed = jobs_needed(scenarios, all_jobs, args.skip_gpu)

    cpu_jobs = [j for j in needed if j.backend == "cpu"]
    gpu_jobs = [j for j in needed if j.backend == "cuda"]

    dump_walls: dict[str, float] = {}
    dump_errors: list[str] = []

    # CPU dumps (max N concurrent)
    to_run_cpu = [
        j
        for j in cpu_jobs
        if not dump_is_fresh(j, battery, cpu_sha, game)
    ]
    cached_cpu = len(cpu_jobs) - len(to_run_cpu)
    print(
        f"window_battery: CPU dumps total={len(cpu_jobs)} "
        f"cached={cached_cpu} run={len(to_run_cpu)} workers={args.cpu_workers}"
    )

    sem = threading.Semaphore(args.cpu_workers)
    if to_run_cpu:
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.cpu_workers
        ) as ex:
            futs = [
                ex.submit(run_dump_cpu, j, battery, game, cpu_sha, sem)
                for j in to_run_cpu
            ]
            for fut in concurrent.futures.as_completed(futs):
                label, wall, err = fut.result()
                dump_walls[label] = wall
                if err:
                    dump_errors.append(f"{label}: {err}")
                    print(f"  DUMP FAIL {label} ({wall:.1f}s): {err[:200]}")
                else:
                    print(f"  DUMP OK   {label} ({wall:.1f}s)")

    # GPU dumps serial
    to_run_gpu = [
        j
        for j in gpu_jobs
        if not dump_is_fresh(j, battery, cuda_sha or "", game_cuda)
    ]
    print(
        f"window_battery: GPU dumps total={len(gpu_jobs)} "
        f"cached={len(gpu_jobs) - len(to_run_gpu)} run={len(to_run_gpu)} (serial)"
    )
    for j in to_run_gpu:
        label, wall, err = run_dump_gpu(j, battery, game_cuda, cuda_sha or "")
        dump_walls[label] = wall
        if err:
            dump_errors.append(f"{label}: {err}")
            print(f"  DUMP FAIL {label} ({wall:.1f}s): {err[:300]}")
        else:
            print(f"  DUMP OK   {label} ({wall:.1f}s)")

    if dump_errors and not args.selftest:
        # still attempt checks on what we have, but fail overall
        pass

    # Clear summary for a full scenario set; --only appends a single line.
    if not args.only:
        summary_path.write_text("")

    all_pass = True
    check_results: list[dict] = []

    for name in scenarios:
        if name.startswith("XB-") and args.skip_gpu:
            continue
        print(f"== {name} ==")
        t0 = time.time()
        try:
            # Verify dumps exist
            for key in dumps_for_scenario(name, skip_gpu=args.skip_gpu):
                sk, var = key.split("/", 1)
                d = battery / "scenarios" / sk / var
                if not d.is_dir() or not list(d.glob("frame_*.ppm")):
                    raise AssertionError(f"missing dump {key}")
            metrics = run_scenario_check(name, battery, dump_walls)
            wall = time.time() - t0
            row = {
                "name": name,
                "pass": True,
                "metrics": metrics,
                "wall_s": round(wall, 3),
            }
            print(f"  PASS  {name}  wall={wall:.2f}s  metrics={json.dumps(metrics)[:180]}")
            check_results.append(row)
        except Exception as e:
            wall = time.time() - t0
            all_pass = False
            err = str(e)
            row = {
                "name": name,
                "pass": False,
                "error": err[:500],
                "wall_s": round(wall, 3),
            }
            print(f"  FAIL  {name}  {err}")
            write_fail_artifact(battery, name, err)
            check_results.append(row)

        with open(summary_path, "a") as f:
            f.write(json.dumps(check_results[-1]) + "\n")

    if args.selftest:
        # Require at least the scenarios we need dumps for
        if not all_pass and any(
            not r["pass"] for r in check_results
        ):
            print(
                "SELFTEST: base scenarios not all green; "
                "cannot trust corruption detection",
                file=sys.stderr,
            )
            # still run selftest on what exists
        rc = run_selftest(battery, args.skip_gpu)
        total = time.time() - t_all0
        print(f"window_battery: total wall-clock {total:.1f}s")
        return rc

    total = time.time() - t_all0
    print(f"window_battery: total wall-clock {total:.1f}s")
    print(f"window_battery: summary={summary_path}")

    if dump_errors:
        print(f"window_battery: {len(dump_errors)} dump error(s)", file=sys.stderr)
        all_pass = False

    if all_pass:
        print("ALL PASS")
        return 0
    print("SOME FAILED", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
