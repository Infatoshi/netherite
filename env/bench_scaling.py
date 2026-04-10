#!/usr/bin/env python3
"""
Benchmark throughput scaling across env counts and stepping strategies.

Strategies:
- sync: Step each env sequentially, wait for each to complete
- batched: Send all actions, then wait for each sequentially
- async: Send all actions, poll all until all ready

Sweeps: 1, 2, 4, 8 envs × 3 strategies = 12 combinations
"""

from __future__ import annotations

import argparse
import shutil
import sys
import time
from pathlib import Path

import numpy as np

from config import NetheriteConfig
from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv

ROOT = Path(__file__).resolve().parents[1]


def make_config(instance_id: int, seed: int = 424242) -> NetheriteConfig:
    """Create config for one instance."""
    return NetheriteConfig(
        instance_id=instance_id,
        seed=seed + instance_id,
        width=160,
        height=90,
        render_distance=6,
        simulation_distance=6,
        graphics="fast",
        rl=True,
        headless=True,
        uncapped=True,  # Critical for high throughput!
        java_home="/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home",
    )


def random_action() -> dict:
    """Generate a random action."""
    return {
        "forward": np.random.randint(0, 2),
        "back": 0,
        "left": np.random.randint(0, 2),
        "right": 0,
        "jump": np.random.randint(0, 2),
        "sneak": 0,
        "sprint": np.random.randint(0, 2),
        "attack": 0,
        "use": 0,
        "camera": np.array(
            [np.random.uniform(-2, 2), np.random.uniform(-1, 1)], dtype=np.float32
        ),
    }


def step_sync(envs: list[NetheriteEnv], actions: list[dict]) -> list[dict]:
    """Sync stepping: step each env sequentially."""
    results = []
    for env, action in zip(envs, actions):
        obs, _, _, _, info = env.step_sync(action)
        results.append({"obs": obs, "info": info})
    return results


def step_batched(envs: list[NetheriteEnv], actions: list[dict]) -> list[dict]:
    """Batched dispatch: send all actions first, then collect sequentially."""
    # Phase 1: Release any latches, record start ticks, send all actions
    start_ticks = []
    for env, action in zip(envs, actions):
        # Release latch if needed (mimics _step_impl behavior)
        if env._control_writer is not None:
            ctrl = env._read_control()
            if ctrl.get("start_latched") == 1:
                env._release_start_latch()
        start_ticks.append(env.get_state_tick())
        env._send_action(action)

    # Phase 2: Wait and collect sequentially
    results = []
    for i, env in enumerate(envs):
        # Wait for this env's state tick to advance
        env._wait_until_state_tick(start_ticks[i] + 1)
        # Get frame and state
        frame = env._wait_for_frame(wait_for_new=True)
        state = env._read_state()
        results.append(
            {
                "obs": {
                    "pov": frame,
                    "inventory": state["inventory"],
                    "health": state["health"],
                    "position": state["position"],
                },
                "info": {"state_tick": env.get_state_tick()},
            }
        )
    return results


def step_async(envs: list[NetheriteEnv], actions: list[dict]) -> list[dict]:
    """Async polling: send all actions, poll all until all ready."""
    n = len(envs)

    # Phase 1: Release latches, record start ticks, send all actions
    start_ticks = []
    for env, action in zip(envs, actions):
        # Release latch if needed
        if env._control_writer is not None:
            ctrl = env._read_control()
            if ctrl.get("start_latched") == 1:
                env._release_start_latch()
        start_ticks.append(env.get_state_tick())
        env._send_action(action)

    # Phase 2: Poll all envs until all have advanced
    target_ticks = [t + 1 for t in start_ticks]
    ready = [False] * n
    results: list[dict | None] = [None] * n

    timeout = 5.0
    deadline = time.monotonic() + timeout

    while not all(ready) and time.monotonic() < deadline:
        for i, env in enumerate(envs):
            if ready[i]:
                continue
            current_tick = env.get_state_tick()
            if current_tick >= target_ticks[i]:
                # This env is ready, collect its result
                frame = env._wait_for_frame(wait_for_new=True)
                state = env._read_state()
                results[i] = {
                    "obs": {
                        "pov": frame,
                        "inventory": state["inventory"],
                        "health": state["health"],
                        "position": state["position"],
                    },
                    "info": {"state_tick": current_tick},
                }
                ready[i] = True
        time.sleep(0.0001)  # Small sleep to avoid CPU spin

    # Fill any missing with empty
    for i in range(n):
        if results[i] is None:
            results[i] = {"obs": None, "info": {"state_tick": -1}}

    return results  # type: ignore


def run_benchmark(
    num_envs: int,
    strategy: str,
    steps: int = 100,
    warmup: int = 20,
) -> float:
    """Run benchmark with given config, return steps/sec."""
    print(f"  Setting up {num_envs} env(s)...", file=sys.stderr, end=" ", flush=True)

    # Clean up
    launcher = Launcher(ROOT)
    launcher.cleanup_shmem()
    for i in range(8):  # Clean all possible instance dirs
        world_dir = ROOT / "run" / "saves" / f"netherite_{i}"
        if world_dir.exists():
            try:
                shutil.rmtree(world_dir)
            except OSError:
                pass  # May fail if still in use

    # Launch instances
    configs = [make_config(i) for i in range(num_envs)]
    instances: list[MCInstance] = []
    for cfg in configs:
        inst = MCInstance(cfg, ROOT)
        inst.start()
        instances.append(inst)

    # Wait for ready
    for inst in instances:
        if not inst.wait_for_ready(timeout=120.0):
            for i in instances:
                i.stop()
            raise RuntimeError(f"Instance {inst.config.instance_id} failed to start")

    # Create envs
    envs = [NetheriteEnv(config=cfg, timeout=10.0) for cfg in configs]
    for env in envs:
        env.reset()
        try:
            env.wait_for_start_latch()
            env.release_start_latch()
        except TimeoutError:
            pass

    print("ready", file=sys.stderr, flush=True)

    # Select step function
    step_fn = {"sync": step_sync, "batched": step_batched, "async": step_async}[
        strategy
    ]

    # Warmup
    for _ in range(warmup):
        actions = [random_action() for _ in range(num_envs)]
        step_fn(envs, actions)

    # Benchmark
    start = time.perf_counter()
    for _ in range(steps):
        actions = [random_action() for _ in range(num_envs)]
        step_fn(envs, actions)
    elapsed = time.perf_counter() - start

    # Cleanup
    for env in envs:
        env.close()
    for inst in instances:
        inst.stop()
    launcher.cleanup_shmem()

    total_steps = steps * num_envs
    steps_per_sec = total_steps / elapsed
    return steps_per_sec


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--steps", type=int, default=100, help="Steps per benchmark")
    parser.add_argument("--warmup", type=int, default=20, help="Warmup steps")
    parser.add_argument(
        "--envs",
        type=str,
        default="1,2,4,8",
        help="Comma-separated env counts to test",
    )
    parser.add_argument(
        "--strategies",
        type=str,
        default="sync,batched,async",
        help="Comma-separated strategies to test",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    env_counts = [int(x) for x in args.envs.split(",")]
    strategies = args.strategies.split(",")

    print("=" * 60, file=sys.stderr)
    print("Netherite Scaling Benchmark", file=sys.stderr)
    print(f"Steps per run: {args.steps}, Warmup: {args.warmup}", file=sys.stderr)
    print("=" * 60, file=sys.stderr)

    results: dict[tuple[int, str], float] = {}

    for num_envs in env_counts:
        for strategy in strategies:
            print(
                f"\nBenchmark: {num_envs} env(s), {strategy} stepping",
                file=sys.stderr,
            )
            try:
                sps = run_benchmark(
                    num_envs, strategy, steps=args.steps, warmup=args.warmup
                )
                results[(num_envs, strategy)] = sps
                print(f"  Result: {sps:.1f} steps/sec", file=sys.stderr)
            except Exception as e:
                print(f"  FAILED: {e}", file=sys.stderr)
                results[(num_envs, strategy)] = 0.0

    # Print summary table
    print("\n" + "=" * 60, file=sys.stderr)
    print("RESULTS (steps/sec, higher is better)", file=sys.stderr)
    print("=" * 60, file=sys.stderr)

    # Header
    header = "Envs |" + "|".join(f" {s:>8} " for s in strategies)
    print(header, file=sys.stderr)
    print("-" * len(header), file=sys.stderr)

    # Rows
    for num_envs in env_counts:
        row = f"{num_envs:>4} |"
        for strategy in strategies:
            val = results.get((num_envs, strategy), 0)
            row += f" {val:>8.1f} |"
        print(row, file=sys.stderr)

    print("=" * 60, file=sys.stderr)

    # Scaling efficiency
    print("\nScaling efficiency (vs 1 env sync baseline):", file=sys.stderr)
    baseline = results.get((1, "sync"), 1.0)
    for num_envs in env_counts:
        for strategy in strategies:
            val = results.get((num_envs, strategy), 0)
            efficiency = val / (baseline * num_envs) * 100 if baseline > 0 else 0
            print(
                f"  {num_envs} env {strategy}: {efficiency:.0f}% efficient",
                file=sys.stderr,
            )


if __name__ == "__main__":
    main()
