#!/usr/bin/env python3
"""Benchmark different step_ticks values to measure throughput scaling."""

import sys
import time
import shutil
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))

from config import NetheriteConfig
from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv


def benchmark_step_ticks(step_ticks: int, root: Path) -> dict:
    """Benchmark a specific step_ticks value."""
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
        obs_mode="both",
        step_ticks=step_ticks,
        java_home="/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home",
    )

    Launcher(root).cleanup_shmem()
    shutil.rmtree(root / "run" / "saves" / "netherite_0", ignore_errors=True)

    inst = MCInstance(cfg, root)
    inst.start()
    if not inst.wait_for_ready(90.0):
        inst.stop()
        return {"error": "MC failed to start"}

    env = NetheriteEnv(config=cfg, timeout=10.0)
    env.reset()

    try:
        env.wait_for_start_latch()
        env.release_start_latch()
    except Exception:
        pass

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
    for _ in range(50):
        env.step(action)

    # Benchmark
    n_steps = 300
    start_tick = env.get_state_tick()
    t0 = time.perf_counter()
    for _ in range(n_steps):
        env.step(action)
    elapsed = time.perf_counter() - t0
    end_tick = env.get_state_tick()

    sps = n_steps / elapsed
    actual_ticks = end_tick - start_tick
    tps = actual_ticks / elapsed

    env.close()
    inst.stop()
    time.sleep(1)

    return {
        "step_ticks": step_ticks,
        "sps": sps,
        "tps": tps,
        "actual_ticks_per_step": actual_ticks / n_steps,
    }


def main():
    ROOT = Path(__file__).resolve().parent.parent

    print("=" * 70, flush=True)
    print("STEP_TICKS BENCHMARK", flush=True)
    print("=" * 70, flush=True)
    print(
        "Testing how step_ticks affects throughput (higher = more game ticks per Python step)",
        flush=True,
    )
    print()

    step_ticks_values = [1, 2, 4, 8]
    results = []

    for st in step_ticks_values:
        print(f"Testing step_ticks={st}...", flush=True)
        result = benchmark_step_ticks(st, ROOT)
        results.append(result)
        if "error" in result:
            print(f"  ERROR: {result['error']}", flush=True)
        else:
            print(
                f"  SPS: {result['sps']:.0f} | "
                f"TPS: {result['tps']:.0f} | "
                f"Actual ticks/step: {result['actual_ticks_per_step']:.1f}",
                flush=True,
            )

    print()
    print("=" * 70, flush=True)
    print("SUMMARY", flush=True)
    print("=" * 70, flush=True)
    print(f"{'step_ticks':<12} {'SPS':>10} {'TPS':>10} {'Ticks/Step':>12}", flush=True)
    print("-" * 70, flush=True)

    for r in results:
        if "error" not in r:
            print(
                f"{r['step_ticks']:<12} {r['sps']:>10.0f} {r['tps']:>10.0f} {r['actual_ticks_per_step']:>12.1f}",
                flush=True,
            )

    print()
    print(
        "Note: Higher step_ticks = more game ticks per Python call = higher throughput",
        flush=True,
    )
    print(
        "      but less granular control (action held for multiple ticks)", flush=True
    )


if __name__ == "__main__":
    main()
