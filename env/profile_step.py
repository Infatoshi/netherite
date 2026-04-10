#!/usr/bin/env python3
"""Profile time breakdown within env.step() to identify bottlenecks."""

import sys
import time
import shutil
from pathlib import Path
from dataclasses import dataclass, field

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))

from config import NetheriteConfig
from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv


@dataclass
class TimingStats:
    samples: list[float] = field(default_factory=list)

    def add(self, t: float):
        self.samples.append(t)

    @property
    def mean_us(self) -> float:
        return np.mean(self.samples) * 1e6 if self.samples else 0

    @property
    def std_us(self) -> float:
        return np.std(self.samples) * 1e6 if self.samples else 0

    @property
    def p50_us(self) -> float:
        return np.percentile(self.samples, 50) * 1e6 if self.samples else 0

    @property
    def p99_us(self) -> float:
        return np.percentile(self.samples, 99) * 1e6 if self.samples else 0


def make_config(obs_mode: str = "both") -> NetheriteConfig:
    return NetheriteConfig(
        instance_id=0,
        seed=424242,
        width=160,
        height=90,
        render_distance=4,
        rl=True,
        headless=True,
        uncapped=True,
        max_fps=260,  # MC's "unlimited" marker
        obs_mode=obs_mode,
        voxel_forward=4,
        voxel_back=4,
        voxel_left=4,
        voxel_right=4,
        voxel_up=4,
        voxel_down=2,
        java_home="/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home",
    )


def profile_step_phases(env: NetheriteEnv, action: dict, n_steps: int = 500):
    """Profile individual phases of step() with fine-grained timing."""
    timings = {
        "total": TimingStats(),
        "latch_check": TimingStats(),
        "get_start_tick": TimingStats(),
        "send_action": TimingStats(),
        "wait_state": TimingStats(),
        "get_end_tick": TimingStats(),
        "wait_frame": TimingStats(),
        "read_state": TimingStats(),
        "build_obs": TimingStats(),
    }

    for _ in range(n_steps):
        t_total_start = time.perf_counter()

        # Phase 1: Check and release latch
        t0 = time.perf_counter()
        if env._control_writer is not None:
            ctrl = env._read_control()
            if ctrl.get("start_latched") == 1:
                env._release_start_latch()
        t1 = time.perf_counter()
        timings["latch_check"].add(t1 - t0)

        # Phase 2: Get start tick
        t0 = time.perf_counter()
        start_tick = env.get_state_tick()
        t1 = time.perf_counter()
        timings["get_start_tick"].add(t1 - t0)

        # Phase 3: Send action
        t0 = time.perf_counter()
        env._send_action(action)
        t1 = time.perf_counter()
        timings["send_action"].add(t1 - t0)

        # Phase 4: Wait for state tick to advance
        t0 = time.perf_counter()
        target_tick = start_tick + 1
        state = env._wait_until_state_tick(target_tick)
        t1 = time.perf_counter()
        timings["wait_state"].add(t1 - t0)

        # Phase 5: Get end tick
        t0 = time.perf_counter()
        end_tick = env.get_state_tick()
        t1 = time.perf_counter()
        timings["get_end_tick"].add(t1 - t0)

        # Phase 6: Wait for frame (with sync)
        t0 = time.perf_counter()
        pov = env._wait_for_frame_at_tick(end_tick)
        t1 = time.perf_counter()
        timings["wait_frame"].add(t1 - t0)

        # Phase 7: Read full state (already done in wait_until_state_tick, but measure separately)
        t0 = time.perf_counter()
        _ = env._read_state()
        t1 = time.perf_counter()
        timings["read_state"].add(t1 - t0)

        # Phase 8: Build observation dict
        t0 = time.perf_counter()
        _ = {
            "pov": pov,
            "inventory": state["inventory"],
            "health": state["health"],
            "position": state["position"],
        }
        t1 = time.perf_counter()
        timings["build_obs"].add(t1 - t0)

        t_total_end = time.perf_counter()
        timings["total"].add(t_total_end - t_total_start)

    return timings


def profile_step_modes(env: NetheriteEnv, action: dict, n_steps: int = 500):
    """Compare different step modes."""
    results = {}

    # Mode 1: step() - no frame sync
    t0 = time.perf_counter()
    for _ in range(n_steps):
        env.step(action)
    elapsed = time.perf_counter() - t0
    results["step (no frame sync)"] = n_steps / elapsed

    # Mode 2: step_sync() - with frame sync
    t0 = time.perf_counter()
    for _ in range(n_steps):
        env.step_sync(action)
    elapsed = time.perf_counter() - t0
    results["step_sync (frame sync)"] = n_steps / elapsed

    # Mode 3: Just send action + wait for state (no frame at all)
    t0 = time.perf_counter()
    for _ in range(n_steps):
        start_tick = env.get_state_tick()
        env._send_action(action)
        env._wait_until_state_tick(start_tick + 1)
    elapsed = time.perf_counter() - t0
    results["state only (no frame)"] = n_steps / elapsed

    # Mode 4: Just send action (no wait)
    t0 = time.perf_counter()
    for _ in range(n_steps):
        env._send_action(action)
    elapsed = time.perf_counter() - t0
    results["action send only"] = n_steps / elapsed

    return results


def extract_java_profiling(log_path: Path) -> list[str]:
    """Extract profiling lines from MC log."""
    if not log_path.exists():
        return []
    lines = []
    with open(log_path, "r") as f:
        for line in f:
            if "profile" in line.lower():
                lines.append(line.strip())
    return lines


def benchmark_obs_mode(obs_mode: str, root: Path) -> dict:
    """Benchmark a single observation mode, return throughput results."""
    cfg = make_config(obs_mode=obs_mode)

    Launcher(root).cleanup_shmem()
    shutil.rmtree(root / "run" / "saves" / "netherite_0", ignore_errors=True)

    inst = MCInstance(cfg, root)
    inst.start()
    if not inst.wait_for_ready(120.0):
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
    for _ in range(100):
        env.step(action)

    # Benchmark
    n_steps = 500
    results = profile_step_modes(env, action, n_steps=n_steps)

    env.close()
    inst.stop()
    time.sleep(1)

    return results


def main():
    ROOT = Path("/Users/infatoshi/netherite")

    print("=" * 70)
    print("NETHERITE OBSERVATION MODE BENCHMARK")
    print("=" * 70)

    # Test all observation modes
    obs_modes = ["both", "voxels", "pixels"]
    all_results = {}

    for obs_mode in obs_modes:
        print(f"\n--- Testing obs_mode='{obs_mode}' ---")
        results = benchmark_obs_mode(obs_mode, ROOT)
        all_results[obs_mode] = results
        if "error" in results:
            print(f"  ERROR: {results['error']}")
        else:
            for mode, sps in results.items():
                print(f"  {mode:<25} {sps:>10.1f} sps")

    # Summary comparison
    print("\n" + "=" * 70)
    print("SUMMARY: THROUGHPUT BY OBSERVATION MODE")
    print("=" * 70)
    print(f"{'Step Mode':<30} {'both':>12} {'voxels':>12} {'pixels':>12}")
    print("-" * 70)

    step_modes = [
        "step (no frame sync)",
        "step_sync (frame sync)",
        "state only (no frame)",
    ]
    for step_mode in step_modes:
        row = f"{step_mode:<30}"
        for obs_mode in obs_modes:
            if obs_mode in all_results and step_mode in all_results[obs_mode]:
                row += f" {all_results[obs_mode][step_mode]:>11.1f}"
            else:
                row += f" {'N/A':>11}"
        print(row)

    # Extract Java profiling from last run
    print("\n" + "=" * 70)
    print("JAVA-SIDE PROFILING (from last run)")
    print("=" * 70)
    log_path = ROOT / "run" / "logs" / "latest.log"
    java_lines = extract_java_profiling(log_path)
    if java_lines:
        for line in java_lines[-10:]:
            print(line)

    print("\nDone.")


if __name__ == "__main__":
    main()
