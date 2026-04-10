#!/usr/bin/env python3
"""Benchmark POSIX semaphore vs polling for state synchronization."""

import os
import sys
import time
import subprocess
from pathlib import Path

import numpy as np

os.environ["PYTHONUNBUFFERED"] = "1"
sys.path.insert(0, str(Path(__file__).parent))

from config import NetheriteConfig
from netherite_env import NetheriteEnv
from launcher import MCInstance, Launcher
from sync import SemaphoreSync


def cleanup_resources(instance_id: int = 0):
    """Clean up shmem and semaphore files."""
    subprocess.run(
        f"sh -c 'rm -f /tmp/netherite_*{instance_id}* 2>/dev/null || true'",
        shell=True,
        capture_output=True,
    )
    SemaphoreSync.cleanup(instance_id)


def run_benchmark(
    use_semaphore: bool, n_steps: int = 500, warmup: int = 100, root: Path = None
) -> dict:
    """Run a single benchmark with or without semaphore."""
    if root is None:
        root = Path(__file__).parent.parent

    instance_id = 0
    cleanup_resources(instance_id)
    Launcher(root).cleanup_shmem()

    cfg = NetheriteConfig(
        instance_id=instance_id,
        width=160,
        height=90,
        render_distance=4,
        max_fps=260,
        uncapped=True,
        rl=True,
        headless=True,
        obs_mode="both",
        step_ticks=1,  # Standard granularity
        use_semaphore=use_semaphore,
        java_home="/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home",
    )

    # Use the launcher to start MC
    mc = MCInstance(cfg, root)
    print(f"  Launching MC (use_semaphore={use_semaphore})...", flush=True)
    mc.start()

    try:
        print("  Waiting for MC to initialize...", flush=True)
        if not mc.wait_for_ready(timeout=120):
            raise TimeoutError("MC failed to start within timeout")

        env = NetheriteEnv(cfg)
        print("  MC initialized. Resetting env...", flush=True)
        env.reset()

        # Warmup
        action = {
            "forward": 1,
            "back": 0,
            "left": 0,
            "right": 0,
            "jump": 0,
            "sneak": 0,
            "sprint": 0,
            "attack": 0,
            "use": 0,
            "camera": np.array([0.0, 0.0], dtype=np.float32),
        }
        print(f"  Warmup ({warmup} steps)...", flush=True)
        for _ in range(warmup):
            env.step(action)

        # Benchmark
        print(f"  Benchmarking ({n_steps} steps)...", flush=True)
        start_tick = env.get_state_tick()
        t0 = time.perf_counter()

        for _ in range(n_steps):
            env.step(action)

        elapsed = time.perf_counter() - t0
        end_tick = env.get_state_tick()

        sps = n_steps / elapsed
        actual_ticks = end_tick - start_tick
        tps = actual_ticks / elapsed
        avg_step_ms = elapsed / n_steps * 1000

        result = {
            "use_semaphore": use_semaphore,
            "sps": sps,
            "tps": tps,
            "avg_step_ms": avg_step_ms,
            "elapsed": elapsed,
        }

        env.close()
        return result

    finally:
        mc.stop()
        time.sleep(1)
        cleanup_resources(instance_id)


def main():
    root = Path(__file__).parent.parent
    print("=" * 60, flush=True)
    print("Semaphore vs Polling Benchmark", flush=True)
    print("=" * 60, flush=True)
    print(flush=True)

    results = []

    # Run polling baseline first
    print("[1/2] Testing POLLING (baseline)...", flush=True)
    try:
        r = run_benchmark(use_semaphore=False, root=root)
        results.append(r)
        print(
            f"  Result: {r['sps']:.0f} SPS, {r['avg_step_ms']:.2f} ms/step", flush=True
        )
    except Exception as e:
        print(f"  FAILED: {e}", flush=True)

    print(flush=True)
    time.sleep(2)  # Let resources settle

    # Run semaphore test
    print("[2/2] Testing SEMAPHORE...", flush=True)
    try:
        r = run_benchmark(use_semaphore=True, root=root)
        results.append(r)
        print(
            f"  Result: {r['sps']:.0f} SPS, {r['avg_step_ms']:.2f} ms/step", flush=True
        )
    except Exception as e:
        print(f"  FAILED: {e}", flush=True)

    print(flush=True)
    print("=" * 60, flush=True)
    print("RESULTS SUMMARY", flush=True)
    print("=" * 60, flush=True)
    print(f"{'Method':<15} {'SPS':>10} {'ms/step':>10} {'vs baseline':>12}", flush=True)
    print("-" * 47, flush=True)

    baseline_sps = results[0]["sps"] if results else 0
    for r in results:
        method = "SEMAPHORE" if r["use_semaphore"] else "POLLING"
        improvement = ((r["sps"] / baseline_sps) - 1) * 100 if baseline_sps > 0 else 0
        sign = "+" if improvement >= 0 else ""
        print(
            f"{method:<15} {r['sps']:>10.0f} {r['avg_step_ms']:>10.2f} {sign}{improvement:>11.1f}%",
            flush=True,
        )

    print(flush=True)
    print("Expected baseline: ~290-400 SPS (from prior benchmarks)", flush=True)


if __name__ == "__main__":
    main()
