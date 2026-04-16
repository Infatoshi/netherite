"""Live 8x4 matplotlib grid demo for many Netherite instances."""

from __future__ import annotations

import argparse
import shutil
import time
from pathlib import Path

import numpy as np

from config import NetheriteConfig
from demo import build_demo_config
from launcher import Launcher
from netherite_env import NetheriteEnv
from run_config import DEFAULT_JAVA_HOME

ROOT = Path(__file__).resolve().parents[1]
DEMO_ACTION_LABEL = "forward+jump+attack"


def build_grid_title(
    *,
    batch_size: int,
    total_sps: float,
    display_fps: float,
    tick_min: int,
    tick_max: int,
    max_position_spread: float,
) -> str:
    return (
        f"Netherite Grid Demo | B={batch_size} | "
        f"SPS={total_sps:.1f} | display={display_fps:.1f} fps | "
        f"action={DEMO_ACTION_LABEL} | ticks={tick_min}..{tick_max} | "
        f"spread={max_position_spread:.2f}"
    )


def build_grid_configs(
    *,
    rows: int,
    cols: int,
    seed: int,
    java_home: str,
    seed_stride: int,
) -> list[NetheriteConfig]:
    configs: list[NetheriteConfig] = []
    batch_size = rows * cols
    for instance_id in range(batch_size):
        cfg = build_demo_config()
        cfg.instance_id = instance_id
        cfg.seed = seed + (instance_id * seed_stride)
        cfg.headless = True
        cfg.uncapped = True
        cfg.max_fps = 32767
        cfg.java_home = java_home
        configs.append(cfg)
    return configs


def cleanup_world_dirs(launcher: Launcher, batch_size: int):
    for instance_id in range(batch_size):
        world_dir = (
            launcher.instance_run_dir(instance_id)
            / "saves"
            / f"netherite_{instance_id}"
        )
        if world_dir.exists():
            shutil.rmtree(world_dir)


def _zero_action() -> dict[str, object]:
    return {
        "forward": 0,
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


def build_demo_action() -> dict[str, object]:
    action = _zero_action()
    action["forward"] = 1
    action["jump"] = 1
    action["attack"] = 1
    return action


def reset_demo_env(env: NetheriteEnv) -> dict[str, object]:
    obs, _ = env.reset()
    env.release_start_latch()
    return obs


def max_position_spread(observations: list[dict[str, object]]) -> float:
    if not observations:
        return 0.0
    positions = np.stack([obs["position"] for obs in observations], axis=0)
    return float(np.max(np.ptp(positions, axis=0)))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rows", type=int, default=4)
    parser.add_argument("--cols", type=int, default=8)
    parser.add_argument("--seed", type=int, default=int(time.time()))
    parser.add_argument(
        "--seed-stride",
        type=int,
        default=0,
        help="Seed delta between instances. Default 0 keeps every world identical.",
    )
    parser.add_argument("--display-fps", type=float, default=10.0)
    parser.add_argument("--java-home", default=DEFAULT_JAVA_HOME)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None):
    args = parse_args(argv)
    if args.rows <= 0 or args.cols <= 0:
        raise SystemExit("--rows and --cols must be positive")
    if args.display_fps <= 0:
        raise SystemExit("--display-fps must be positive")

    import matplotlib

    matplotlib.use("MacOSX")
    import matplotlib.pyplot as plt

    batch_size = args.rows * args.cols
    launcher = Launcher(ROOT)
    launcher.cleanup_shmem()
    cleanup_world_dirs(launcher, batch_size)

    configs = build_grid_configs(
        rows=args.rows,
        cols=args.cols,
        seed=args.seed,
        java_home=args.java_home,
        seed_stride=args.seed_stride,
    )
    instances = launcher.launch_with_mod_cache_prewarm(configs)
    for inst in instances:
        print(
            f"Launching instance {inst.config.instance_id} seed={inst.config.seed}, "
            f"pid={inst.process.pid if inst.process else 'n/a'}"
        )
    if not launcher.wait_all_ready(timeout=120.0):
        launcher.stop_all()
        raise SystemExit("Minecraft clients did not become ready")

    envs = [NetheriteEnv(config=cfg, timeout=20.0) for cfg in configs]
    first_obs: list[dict[str, object]] = []
    for env in envs:
        first_obs.append(reset_demo_env(env))

    fig, axes = plt.subplots(
        args.rows,
        args.cols,
        figsize=(args.cols * 2.0, args.rows * 1.3),
        squeeze=False,
    )
    images = []
    for instance_id, (ax, obs) in enumerate(zip(axes.flat, first_obs)):
        img = ax.imshow(obs["pov"], interpolation="nearest")
        ax.set_title(f"iid {instance_id}", fontsize=7)
        ax.axis("off")
        images.append(img)
    fig.tight_layout(pad=0.2, w_pad=0.1, h_pad=0.4)
    plt.ion()
    plt.show()

    running = True

    def on_key(event):
        nonlocal running
        if event.key == "q":
            running = False

    fig.canvas.mpl_connect("key_press_event", on_key)

    action = build_demo_action()
    total_steps = 0
    display_frames = 0
    start = time.perf_counter()
    last_display = start
    display_interval = 1.0 / args.display_fps

    try:
        while running and plt.fignum_exists(fig.number):
            latest_obs = []
            latest_infos = []
            for env in envs:
                obs, _, _, _, info = env.step_sync(action)
                latest_obs.append(obs)
                latest_infos.append(info)
            total_steps += batch_size

            now = time.perf_counter()
            if now - last_display >= display_interval:
                tick_values = [int(info["state_tick"]) for info in latest_infos]
                tick_min = min(tick_values)
                tick_max = max(tick_values)
                position_spread = max_position_spread(latest_obs)
                for instance_id, (ax, img, obs, info) in enumerate(
                    zip(axes.flat, images, latest_obs, latest_infos)
                ):
                    img.set_data(obs["pov"])
                    pos = obs["position"]
                    ax.set_title(
                        f"iid {instance_id} | t {info['state_tick']} | "
                        f"x {pos[0]:.1f} z {pos[2]:.1f}",
                        fontsize=7,
                    )
                elapsed = now - start
                display_frames += 1
                total_sps = total_steps / elapsed if elapsed > 0 else 0.0
                actual_display_fps = display_frames / elapsed if elapsed > 0 else 0.0
                title = build_grid_title(
                    batch_size=batch_size,
                    total_sps=total_sps,
                    display_fps=actual_display_fps,
                    tick_min=tick_min,
                    tick_max=tick_max,
                    max_position_spread=position_spread,
                )
                if fig.canvas.manager is not None:
                    fig.canvas.manager.set_window_title(title)
                fig.suptitle(title, fontsize=11)
                fig.canvas.draw()
                fig.canvas.flush_events()
                plt.pause(0.001)
                last_display = now
    finally:
        for env in envs:
            env.close()
        launcher.stop_all()
        plt.close("all")
        elapsed = time.perf_counter() - start
        if elapsed > 0:
            total_sps = total_steps / elapsed
            print(
                f"Done. B={batch_size} | total steps={total_steps} | "
                f"aggregate SPS={total_sps:.1f}"
            )


if __name__ == "__main__":
    main()
