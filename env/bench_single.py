#!/usr/bin/env python3
"""
Profile single-instance throughput to find bottlenecks.
"""

from __future__ import annotations

import shutil
import sys
import time
from pathlib import Path

import numpy as np

from config import NetheriteConfig
from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv

ROOT = Path(__file__).resolve().parents[1]


def make_config(
    width: int = 160,
    height: int = 90,
    render_distance: int = 6,
    headless: bool = True,
    uncapped: bool = True,
    max_fps: int = 260,
) -> NetheriteConfig:
    return NetheriteConfig(
        instance_id=0,
        seed=424242,
        width=width,
        height=height,
        render_distance=render_distance,
        simulation_distance=render_distance,
        graphics="fast",
        rl=True,
        headless=headless,
        uncapped=uncapped,
        max_fps=max_fps,
        java_home="/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home",
    )


def random_action() -> dict:
    return {
        "forward": np.random.randint(0, 2),
        "back": 0,
        "left": np.random.randint(0, 2),
        "right": 0,
        "jump": np.random.randint(0, 2),
        "sneak": 0,
        "sprint": 0,
        "attack": 0,
        "use": 0,
        "camera": np.array(
            [np.random.uniform(-2, 2), np.random.uniform(-1, 1)], dtype=np.float32
        ),
    }


def bench_step_variants(env: NetheriteEnv, steps: int = 200, warmup: int = 50):
    """Benchmark different step variants."""
    results = {}

    # Warmup
    for _ in range(warmup):
        env.step_sync(random_action())

    # step_sync (frame-synced)
    start = time.perf_counter()
    for _ in range(steps):
        env.step_sync(random_action())
    elapsed = time.perf_counter() - start
    results["step_sync"] = steps / elapsed

    # step (no frame wait)
    start = time.perf_counter()
    for _ in range(steps):
        env.step(random_action())
    elapsed = time.perf_counter() - start
    results["step"] = steps / elapsed

    # Raw: just send action + read state (no frame)
    start = time.perf_counter()
    for _ in range(steps):
        action = random_action()
        start_tick = env.get_state_tick()
        env._send_action(action)
        env._wait_until_state_tick(start_tick + 1)
        _ = env._read_state()
    elapsed = time.perf_counter() - start
    results["state_only"] = steps / elapsed

    # Raw: just tick without reading anything
    start = time.perf_counter()
    for _ in range(steps):
        action = random_action()
        start_tick = env.get_state_tick()
        env._send_action(action)
        env._wait_until_state_tick(start_tick + 1)
    elapsed = time.perf_counter() - start
    results["tick_only"] = steps / elapsed

    return results


def bench_config(
    cfg: NetheriteConfig, label: str, steps: int = 200, warmup: int = 50
) -> dict:
    """Benchmark a specific configuration."""
    print(f"\n{label}:", file=sys.stderr)
    print(
        f"  {cfg.width}x{cfg.height}, RD={cfg.render_distance}, headless={cfg.headless}, uncapped={cfg.uncapped}",
        file=sys.stderr,
    )

    # Clean and launch
    launcher = Launcher(ROOT)
    launcher.cleanup_shmem()
    world_dir = ROOT / "run" / "saves" / "netherite_0"
    if world_dir.exists():
        try:
            shutil.rmtree(world_dir)
        except OSError:
            time.sleep(1)
            try:
                shutil.rmtree(world_dir)
            except OSError:
                pass

    inst = MCInstance(cfg, ROOT)
    inst.start()
    print(f"  Launched pid={inst.process.pid}", file=sys.stderr)

    if not inst.wait_for_ready(timeout=120.0):
        inst.stop()
        return {}

    env = NetheriteEnv(config=cfg, timeout=10.0)
    env.reset()

    try:
        env.wait_for_start_latch()
        env.release_start_latch()
    except TimeoutError:
        pass

    print(f"  Running {steps} steps...", file=sys.stderr)
    results = bench_step_variants(env, steps=steps, warmup=warmup)

    env.close()
    inst.stop()
    launcher.cleanup_shmem()

    for variant, sps in results.items():
        print(f"    {variant}: {sps:.1f} steps/sec", file=sys.stderr)

    return results


def main():
    print("=" * 60, file=sys.stderr)
    print("Single Instance Throughput Benchmark", file=sys.stderr)
    print("=" * 60, file=sys.stderr)

    all_results = {}

    # Test different configurations
    configs = [
        (make_config(max_fps=260), "fps=260"),
        (make_config(max_fps=500), "fps=500"),
        (make_config(max_fps=500, render_distance=2), "fps=500 RD=2"),
    ]

    for cfg, label in configs:
        results = bench_config(cfg, label)
        all_results[label] = results

    # Summary
    print("\n" + "=" * 60, file=sys.stderr)
    print("SUMMARY (steps/sec)", file=sys.stderr)
    print("=" * 60, file=sys.stderr)

    variants = ["step_sync", "step", "state_only", "tick_only"]
    header = f"{'Config':<25} |" + "|".join(f"{v:>12}" for v in variants)
    print(header, file=sys.stderr)
    print("-" * len(header), file=sys.stderr)

    for label, results in all_results.items():
        row = f"{label:<25} |"
        for v in variants:
            val = results.get(v, 0)
            row += f"{val:>12.1f}|"
        print(row, file=sys.stderr)


if __name__ == "__main__":
    main()
