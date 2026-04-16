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
import os
import sys
import time
from pathlib import Path

import numpy as np

from config import NetheriteConfig
from launcher import Launcher
from netherite_env import NetheriteEnv
from run_config import DEFAULT_JAVA_HOME
from startup_trace import TRACE_ENV_VAR, trace_event

ROOT = Path(__file__).resolve().parents[1]

FAST_RENDER_DISTANCE = 4
FAST_SIMULATION_DISTANCE = 5
FAST_MAX_FPS = 9999
FAST_USE_SEMAPHORE = True
FAST_ENV_TIMEOUT = 30.0
FAST_WIDTH = 160
FAST_HEIGHT = 90


def make_config(
    instance_id: int,
    seed: int = 424242,
    java_home: str | None = None,
    width: int = FAST_WIDTH,
    height: int = FAST_HEIGHT,
    render_distance: int = FAST_RENDER_DISTANCE,
    simulation_distance: int = FAST_SIMULATION_DISTANCE,
    max_fps: int = FAST_MAX_FPS,
    use_semaphore: bool = FAST_USE_SEMAPHORE,
) -> NetheriteConfig:
    """Create config for one instance."""
    return NetheriteConfig(
        instance_id=instance_id,
        seed=seed + instance_id,
        width=width,
        height=height,
        render_distance=render_distance,
        simulation_distance=simulation_distance,
        graphics="fast",
        rl=True,
        headless=True,
        uncapped=True,  # Critical for high throughput!
        max_fps=max_fps,
        use_semaphore=use_semaphore,
        java_home=java_home,
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


def wait_for_post_reset_tick(env: NetheriteEnv) -> None:
    """Wait until the env has advanced at least one live state tick."""
    target_tick = max(1, env.get_state_tick() + 1)
    trace_event(
        "env.post_reset_tick.target",
        instance_id=env.config.instance_id,
        target_tick=target_tick,
    )
    env._wait_until_state_tick(target_tick)
    trace_event(
        "env.post_reset_tick.reached",
        instance_id=env.config.instance_id,
        current_tick=env.get_state_tick(),
    )


def run_benchmark(
    num_envs: int,
    strategy: str,
    steps: int = 100,
    warmup: int = 20,
    java_home: str | None = None,
    width: int = FAST_WIDTH,
    height: int = FAST_HEIGHT,
    render_distance: int = FAST_RENDER_DISTANCE,
    simulation_distance: int = FAST_SIMULATION_DISTANCE,
    max_fps: int = FAST_MAX_FPS,
    use_semaphore: bool = FAST_USE_SEMAPHORE,
    env_timeout: float = FAST_ENV_TIMEOUT,
    launch_stagger: float = 0.0,
    reset_stagger: float = 0.0,
    trace_startup: bool = False,
) -> float:
    """Run benchmark with given config, return steps/sec."""
    print(f"  Setting up {num_envs} env(s)...", file=sys.stderr, end=" ", flush=True)

    previous_trace = os.environ.get(TRACE_ENV_VAR)
    if trace_startup:
        os.environ[TRACE_ENV_VAR] = "1"
    else:
        os.environ.pop(TRACE_ENV_VAR, None)
    trace_event(
        "bench.run.start",
        num_envs=num_envs,
        strategy=strategy,
        launch_stagger=launch_stagger,
        reset_stagger=reset_stagger,
    )

    launcher = Launcher(ROOT)
    launcher.cleanup_shmem()
    configs = [
        make_config(
            i,
            java_home=java_home,
            width=width,
            height=height,
            render_distance=render_distance,
            simulation_distance=simulation_distance,
            max_fps=max_fps,
            use_semaphore=use_semaphore,
        )
        for i in range(num_envs)
    ]
    instance_ids = [cfg.instance_id for cfg in configs]
    launcher.cleanup_instance_run_dirs(instance_ids)

    envs: list[NetheriteEnv] = []
    try:
        trace_event("bench.launch.begin", count=len(configs))
        launcher.launch_with_mod_cache_prewarm(
            configs,
            timeout=120.0,
            stagger_seconds=launch_stagger,
        )
        trace_event("bench.launch.prewarm_done", count=len(configs))
        if not launcher.wait_all_ready(timeout=120.0):
            raise RuntimeError("One or more instances failed to start")
        trace_event("bench.launch.all_ready", count=len(configs))

        envs = []
        for cfg in configs:
            trace_event("bench.env.create", instance_id=cfg.instance_id)
            envs.append(NetheriteEnv(config=cfg, timeout=env_timeout))

        for idx, env in enumerate(envs):
            trace_event("bench.env.reset.begin", instance_id=env.config.instance_id)
            env.reset()
            trace_event("bench.env.reset.done", instance_id=env.config.instance_id)
            try:
                trace_event(
                    "bench.env.start_latch.wait.begin",
                    instance_id=env.config.instance_id,
                )
                env.wait_for_start_latch()
                trace_event(
                    "bench.env.start_latch.wait.done",
                    instance_id=env.config.instance_id,
                )
                trace_event(
                    "bench.env.start_latch.release.begin",
                    instance_id=env.config.instance_id,
                )
                env.release_start_latch()
                trace_event(
                    "bench.env.start_latch.release.done",
                    instance_id=env.config.instance_id,
                )
            except TimeoutError:
                trace_event(
                    "bench.env.start_latch.timeout",
                    instance_id=env.config.instance_id,
                )
            trace_event(
                "bench.env.post_reset_tick.wait.begin",
                instance_id=env.config.instance_id,
            )
            wait_for_post_reset_tick(env)
            trace_event(
                "bench.env.post_reset_tick.wait.done",
                instance_id=env.config.instance_id,
            )
            if reset_stagger > 0 and idx < len(envs) - 1:
                time.sleep(reset_stagger)

        print("ready", file=sys.stderr, flush=True)
        trace_event("bench.ready", count=len(envs))

        step_fn = {"sync": step_sync, "batched": step_batched, "async": step_async}[
            strategy
        ]

        for _ in range(warmup):
            actions = [random_action() for _ in range(num_envs)]
            step_fn(envs, actions)

        start = time.perf_counter()
        for _ in range(steps):
            actions = [random_action() for _ in range(num_envs)]
            step_fn(envs, actions)
        elapsed = time.perf_counter() - start

        total_steps = steps * num_envs
        return total_steps / elapsed
    finally:
        for env in envs:
            env.close()
        launcher.stop_all()
        launcher.cleanup_shmem()
        launcher.cleanup_instance_run_dirs(instance_ids)
        if previous_trace is None:
            os.environ.pop(TRACE_ENV_VAR, None)
        else:
            os.environ[TRACE_ENV_VAR] = previous_trace


def print_scaling_efficiency(
    results: dict[tuple[int, str], float],
    env_counts: list[int],
    strategies: list[str],
    *,
    stream=sys.stderr,
) -> None:
    """Print efficiency only when a 1-env sync baseline is present."""
    print("\nScaling efficiency (vs 1 env sync baseline):", file=stream)
    baseline = results.get((1, "sync"))
    if baseline is None:
        print("  skipped: 1 env sync baseline not included in this run", file=stream)
        return
    if baseline <= 0:
        print("  skipped: 1 env sync baseline is zero", file=stream)
        return

    for num_envs in env_counts:
        for strategy in strategies:
            val = results.get((num_envs, strategy), 0.0)
            efficiency = val / (baseline * num_envs) * 100
            print(
                f"  {num_envs} env {strategy}: {efficiency:.0f}% efficient",
                file=stream,
            )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
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
    parser.add_argument(
        "--java-home",
        default=os.environ.get("JAVA_HOME", DEFAULT_JAVA_HOME),
        help="JAVA_HOME to use for launched Minecraft instances",
    )
    parser.add_argument(
        "--width",
        type=int,
        default=FAST_WIDTH,
        help="Minecraft framebuffer width for launched instances",
    )
    parser.add_argument(
        "--height",
        type=int,
        default=FAST_HEIGHT,
        help="Minecraft framebuffer height for launched instances",
    )
    parser.add_argument(
        "--render-distance",
        type=int,
        default=FAST_RENDER_DISTANCE,
        help="Chunk render distance for launched instances",
    )
    parser.add_argument(
        "--simulation-distance",
        type=int,
        default=FAST_SIMULATION_DISTANCE,
        help="Simulation distance for launched instances",
    )
    parser.add_argument(
        "--max-fps",
        type=int,
        default=FAST_MAX_FPS,
        help="Minecraft client FPS cap used by launched instances",
    )
    parser.add_argument(
        "--use-semaphore",
        action=argparse.BooleanOptionalAction,
        default=FAST_USE_SEMAPHORE,
        help="Use POSIX semaphores for IPC signaling",
    )
    parser.add_argument(
        "--env-timeout",
        type=float,
        default=FAST_ENV_TIMEOUT,
        help="Seconds to wait for env state/frame synchronization",
    )
    parser.add_argument(
        "--launch-stagger",
        type=float,
        default=0.0,
        help="Seconds to sleep between fan-out launches after prewarm",
    )
    parser.add_argument(
        "--reset-stagger",
        type=float,
        default=0.0,
        help="Seconds to sleep between per-instance reset/startup release",
    )
    parser.add_argument(
        "--trace-startup",
        action="store_true",
        help="Emit detailed bring-up milestones to stderr",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None):
    args = parse_args(argv)
    env_counts = [int(x) for x in args.envs.split(",")]
    strategies = args.strategies.split(",")

    print("=" * 60, file=sys.stderr)
    print("Netherite Scaling Benchmark", file=sys.stderr)
    print(f"Steps per run: {args.steps}, Warmup: {args.warmup}", file=sys.stderr)
    print(
        f"Config: {args.width}x{args.height} "
        f"RD={args.render_distance} SD={args.simulation_distance} "
        f"max_fps={args.max_fps} semaphore={args.use_semaphore} "
        f"env_timeout={args.env_timeout} launch_stagger={args.launch_stagger} "
        f"reset_stagger={args.reset_stagger} trace_startup={args.trace_startup}",
        file=sys.stderr,
    )
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
                    num_envs,
                    strategy,
                    steps=args.steps,
                    warmup=args.warmup,
                    java_home=args.java_home,
                    width=args.width,
                    height=args.height,
                    render_distance=args.render_distance,
                    simulation_distance=args.simulation_distance,
                    max_fps=args.max_fps,
                    use_semaphore=args.use_semaphore,
                    env_timeout=args.env_timeout,
                    launch_stagger=args.launch_stagger,
                    reset_stagger=args.reset_stagger,
                    trace_startup=args.trace_startup,
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

    print_scaling_efficiency(results, env_counts, strategies, stream=sys.stderr)


if __name__ == "__main__":
    main()
