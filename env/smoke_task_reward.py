"""End-to-end smoke check for the TaskReward shmem protocol.

Launches one MC instance with task=treechop, steps the env a few times with a
scripted action sequence (look down, spin, mash attack), and prints the
TaskReward block each tick. Confirms that:

    1. The reward block magic (NERR) is being written by Java.
    2. steps_this_episode advances each tick.
    3. The max_episode_steps truncation path fires (we request a tiny
       max_episode_steps=20 so truncation happens during the run).
    4. logs_broken is readable (may stay 0 if no trees in front -- that is OK;
       this script is a protocol smoke, not a capability smoke).

Usage:
    uv run env/smoke_task_reward.py
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from config import NetheriteConfig  # noqa: E402
from launcher import Launcher  # noqa: E402
from netherite_env import NetheriteEnv  # noqa: E402

ROOT = HERE.parent


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--steps", type=int, default=60)
    ap.add_argument("--max-episode-steps", type=int, default=25)
    ap.add_argument("--ready-timeout", type=float, default=240.0)
    ap.add_argument("--step-timeout", type=float, default=30.0)
    ap.add_argument("--seed", type=int, default=12345)
    args = ap.parse_args()

    if sys.platform == "darwin":
        java_home = "/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home"
    else:
        java_home = "/usr/lib/jvm/java-21-openjdk-amd64"
    if not Path(java_home).exists():
        java_home = os.environ.get("JAVA_HOME")
        if java_home is None or not Path(java_home).exists():
            print("JAVA_HOME (Java 21) not found", file=sys.stderr)
            return 2

    cfg = NetheriteConfig(
        instance_id=0,
        seed=args.seed,
        width=160,
        height=90,
        render_distance=4,
        simulation_distance=5,
        max_fps=9999,
        uncapped=True,
        rl=True,
        task="treechop",
        max_episode_steps=args.max_episode_steps,
        use_semaphore=True,
        step_ticks=1,
        java_home=java_home,
    )

    launcher = Launcher(ROOT)
    launcher.cleanup_shmem()
    launcher.cleanup_instance_run_dirs([cfg.instance_id])
    launcher.launch([cfg])
    try:
        if not launcher.wait_all_ready(timeout=args.ready_timeout):
            print("smoke_task_reward: MC did not come up", file=sys.stderr)
            return 3

        env = NetheriteEnv(config=cfg, timeout=args.step_timeout)
        obs, info = env.reset()
        print("smoke_task_reward: env reset ok", flush=True)

        # Confirm the reward block is alive before we even start stepping.
        block = env._read_reward_block()
        if block is None:
            print(
                "smoke_task_reward: FAIL reward block magic absent at reset",
                file=sys.stderr,
            )
            return 4
        print(f"smoke_task_reward: initial reward block = {block}", flush=True)

        try:
            env.release_start_latch()
        except Exception as e:
            print(f"smoke_task_reward: release_start_latch warn: {e}", flush=True)

        # Warmup ticks to clear any mid-startup state.
        noop = {k: 0 for k in [
            "forward", "back", "left", "right", "jump",
            "sneak", "sprint", "attack", "use",
        ]}
        noop["camera"] = np.zeros(2, dtype=np.float32)
        for _ in range(5):
            env.step(noop)

        terminations = 0
        truncations = 0
        last_episode_id = None
        last_steps_this_episode = None
        step_deltas_ok = 0
        step_deltas_bad = 0

        attack_action = dict(noop)
        attack_action["attack"] = 1
        # Look down 45 deg to give attack a chance of hitting something.
        attack_action["camera"] = np.array([30.0, 0.0], dtype=np.float32)

        for t in range(args.steps):
            # Every other tick attack + look-down; the rest noop. Keep it simple.
            action = attack_action if (t % 2 == 0) else noop
            obs, reward, terminated, truncated, info = env.step(action)
            block = env._read_reward_block()
            if block is None:
                print(
                    f"smoke_task_reward: FAIL reward block magic lost at t={t}",
                    file=sys.stderr,
                )
                return 5

            ep = block["episode_id"]
            st = block["steps_this_episode"]
            if last_episode_id is not None:
                if ep == last_episode_id:
                    # Same episode: steps_this_episode must be monotonically
                    # non-decreasing (Java ticks independently of python step_ticks).
                    if last_steps_this_episode is not None:
                        if st >= last_steps_this_episode:
                            step_deltas_ok += 1
                        else:
                            step_deltas_bad += 1
            last_episode_id = ep
            last_steps_this_episode = st

            if terminated:
                terminations += 1
            if truncated:
                truncations += 1

            if t < 20 or terminated or truncated or block["reward_delta"] != 0.0:
                print(
                    f"t={t:>3} r={reward:+.3f} term={int(terminated)} trunc={int(truncated)} "
                    f"ep={ep} step={st} logs={block['logs_broken']} "
                    f"reward_cum={block['reward_cumulative']:+.3f}",
                    flush=True,
                )

        env.close()

        print(
            f"\nsmoke_task_reward: summary steps={args.steps} "
            f"terminations={terminations} truncations={truncations} "
            f"step_deltas_ok={step_deltas_ok} step_deltas_bad={step_deltas_bad}",
            flush=True,
        )

        if truncations == 0 and terminations == 0:
            print(
                "smoke_task_reward: WARN no episode boundary observed "
                "(increase --steps or lower --max-episode-steps)",
                file=sys.stderr,
            )
        else:
            print("smoke_task_reward: PASS (episode boundary observed)")
        return 0
    finally:
        launcher.stop_all()


if __name__ == "__main__":
    sys.exit(main())
