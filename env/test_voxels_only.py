#!/usr/bin/env python3
"""Quick test of voxels-only mode throughput."""

import sys
import time
import shutil
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))

from config import NetheriteConfig
from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv


def main():
    import os

    os.environ["PYTHONUNBUFFERED"] = "1"

    ROOT = Path(__file__).resolve().parent.parent
    print("Starting test...", flush=True)

    # Voxels-only config
    cfg = NetheriteConfig(
        instance_id=0,
        seed=424242,
        width=160,
        height=90,
        render_distance=4,
        rl=True,
        headless=True,
        uncapped=True,
        max_fps=260,
        obs_mode="voxels",  # Skip pixel capture!
        voxel_forward=4,
        voxel_back=4,
        voxel_left=4,
        voxel_right=4,
        voxel_up=4,
        voxel_down=2,
        java_home="/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home",
    )

    print(f"Testing obs_mode='{cfg.obs_mode}'", flush=True)
    print(
        f"Voxel grid: ±{cfg.voxel_forward} forward/back, ±{cfg.voxel_left} left/right, +{cfg.voxel_up}/-{cfg.voxel_down} vertical",
        flush=True,
    )

    Launcher(ROOT).cleanup_shmem()
    shutil.rmtree(ROOT / "run" / "saves" / "netherite_0", ignore_errors=True)

    print("Launching Minecraft...", flush=True)
    inst = MCInstance(cfg, ROOT)
    inst.start()
    print("Waiting for MC ready...", flush=True)
    if not inst.wait_for_ready(90.0):
        print("ERROR: MC failed to start", flush=True)
        inst.stop()
        sys.exit(1)
    print("MC ready!", flush=True)

    env = NetheriteEnv(config=cfg, timeout=10.0)
    env.reset()
    print("Env reset done", flush=True)

    try:
        env.wait_for_start_latch()
        env.release_start_latch()
        print("Latch released", flush=True)
    except Exception as e:
        print(f"Latch error: {e}", flush=True)

    action = {
        "forward": 1,
        "back": 0,
        "left": 0,
        "right": 0,
        "jump": 1,
        "sneak": 0,
        "sprint": 0,
        "attack": 0,
        "use": 0,
        "camera": np.array([0.5, 0.0], dtype=np.float32),
    }

    # Warmup
    print("Warming up...", flush=True)
    for _ in range(100):
        env.step(action)
    print("Warmup done", flush=True)

    # Benchmark step() - no frame sync
    print("Benchmarking step() (no frame sync)...", flush=True)
    n_steps = 500
    t0 = time.perf_counter()
    for _ in range(n_steps):
        env.step(action)
    elapsed = time.perf_counter() - t0
    sps_step = n_steps / elapsed
    print(f"  step():      {sps_step:.1f} sps", flush=True)

    # Benchmark state-only (no frame wait at all)
    print("Benchmarking state-only...", flush=True)
    t0 = time.perf_counter()
    for _ in range(n_steps):
        start_tick = env.get_state_tick()
        env._send_action(action)
        env._wait_until_state_tick(start_tick + 1)
    elapsed = time.perf_counter() - t0
    sps_state = n_steps / elapsed
    print(f"  state-only:  {sps_state:.1f} sps", flush=True)

    env.close()
    inst.stop()
    time.sleep(1)

    print(
        f"\nResult: voxels-only mode achieves {sps_step:.0f}-{sps_state:.0f} sps",
        flush=True,
    )


if __name__ == "__main__":
    main()
