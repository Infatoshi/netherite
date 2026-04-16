"""Live or recorded matplotlib grid demo for many Netherite instances."""

from __future__ import annotations

import argparse
import shutil
import time
from pathlib import Path
from typing import Callable

import numpy as np

from bench_scaling import step_async, step_batched, step_sync
from config import NetheriteConfig
from demo import build_demo_config
from launcher import Launcher
from netherite_env import NetheriteEnv
from run_config import DEFAULT_JAVA_HOME

ROOT = Path(__file__).resolve().parents[1]
DEMO_ACTION_LABEL = "forward+jump+attack"
DEFAULT_DEMO_WIDTH = 320
DEFAULT_DEMO_HEIGHT = 180
DEFAULT_YAW_CANDIDATES = tuple(float(yaw) for yaw in range(0, 360, 45))
STEP_STRATEGIES: dict[str, Callable[[list[NetheriteEnv], list[dict]], list[dict]]] = {
    "sync": step_sync,
    "batched": step_batched,
    "async": step_async,
}


def build_grid_title(
    *,
    batch_size: int,
    strategy: str,
    total_sps: float,
    display_fps: float,
    total_steps: int,
    tick_min: int,
    tick_max: int,
    max_position_spread: float,
) -> str:
    return (
        f"Netherite Grid Demo | strategy={strategy} | B={batch_size} | "
        f"SPS={total_sps:.1f} | video={display_fps:.1f} fps | steps={total_steps} | "
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
    capture_width: int = DEFAULT_DEMO_WIDTH,
    capture_height: int = DEFAULT_DEMO_HEIGHT,
    render_distance: int | None = None,
    simulation_distance: int | None = None,
    max_fps: int = 32767,
    use_semaphore: bool = False,
) -> list[NetheriteConfig]:
    configs: list[NetheriteConfig] = []
    batch_size = rows * cols
    for instance_id in range(batch_size):
        cfg = build_demo_config()
        cfg.instance_id = instance_id
        cfg.seed = seed + (instance_id * seed_stride)
        cfg.width = capture_width
        cfg.height = capture_height
        cfg.headless = True
        cfg.uncapped = True
        cfg.max_fps = max_fps
        cfg.java_home = java_home
        cfg.use_semaphore = use_semaphore
        if render_distance is not None:
            cfg.render_distance = render_distance
        if simulation_distance is not None:
            cfg.simulation_distance = simulation_distance
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


def score_frame_openness(frame: np.ndarray) -> float:
    rgb = frame[..., :3].astype(np.float32) / 255.0
    upper = rgb[: max(1, (rgb.shape[0] * 2) // 3)]
    top = rgb[: max(1, rgb.shape[0] // 3)]
    brightness = float(np.mean(upper))
    contrast = float(np.std(upper))
    blue_margin = float(
        np.mean(np.clip(top[..., 2] - np.maximum(top[..., 0], top[..., 1]), 0.0, 1.0))
    )
    dark_fraction = float(np.mean(np.all(upper < 0.12, axis=-1)))
    return brightness + (0.5 * contrast) + (2.0 * blue_margin) - dark_fraction


def orient_demo_env(
    env: NetheriteEnv,
    *,
    start_pitch: float,
    yaw_candidates: tuple[float, ...] = DEFAULT_YAW_CANDIDATES,
) -> dict[str, object]:
    base_pose = env.get_player_pose()
    best_obs = None
    best_pose = None
    best_score = float("-inf")

    for yaw in yaw_candidates:
        target_pose = {
            **base_pose,
            "yaw": float(yaw),
            "pitch": float(start_pitch),
        }
        obs = env.align_to_pose(target_pose)
        score = score_frame_openness(obs["pov"])
        if score > best_score:
            best_score = score
            best_pose = target_pose
            best_obs = obs

    if best_pose is None or best_obs is None:
        raise RuntimeError("Failed to choose a demo orientation")

    return env.align_to_pose(best_pose)


def reset_demo_env(
    env: NetheriteEnv,
    *,
    auto_orient: bool,
    start_pitch: float,
) -> dict[str, object]:
    env.reset()
    env.wait_for_start_latch()
    env.release_start_latch()
    if auto_orient:
        return orient_demo_env(env, start_pitch=start_pitch)
    pose = env.get_player_pose()
    pose["pitch"] = float(start_pitch)
    return env.align_to_pose(pose)


def max_position_spread(observations: list[dict[str, object]]) -> float:
    if not observations:
        return 0.0
    positions = np.stack([obs["position"] for obs in observations], axis=0)
    return float(np.max(np.ptp(positions, axis=0)))


def parse_benchmark_results(text: str) -> dict[tuple[int, str], float]:
    lines = text.splitlines()
    for idx, line in enumerate(lines):
        if not line.strip().startswith("Envs |"):  # benchmark summary header
            continue
        headers = [part.strip() for part in line.split("|") if part.strip()]
        if len(headers) < 2 or headers[0] != "Envs":
            continue
        strategies = headers[1:]
        results: dict[tuple[int, str], float] = {}
        for row in lines[idx + 2 :]:
            stripped = row.strip()
            if not stripped or stripped.startswith("="):
                break
            parts = [part.strip() for part in row.split("|") if part.strip()]
            if len(parts) != len(strategies) + 1:
                continue
            env_count = int(parts[0])
            for strategy, value in zip(strategies, parts[1:]):
                results[(env_count, strategy)] = float(value)
        if results:
            return results
    return {}


def load_benchmark_results(path: Path | None) -> dict[tuple[int, str], float]:
    if path is None:
        return {}
    return parse_benchmark_results(path.read_text(encoding="utf-8"))


def format_results_panel(
    results: dict[tuple[int, str], float],
    *,
    highlight: tuple[int, str] | None = None,
) -> str:
    if not results:
        return "Sweep SPS\nunavailable"

    env_counts = sorted({env_count for env_count, _ in results})
    strategies = [
        strategy
        for strategy in ("sync", "batched", "async")
        if any(key_strategy == strategy for _, key_strategy in results)
    ]
    header = "env " + " ".join(f"{strategy:>8}" for strategy in strategies)
    lines = ["Sweep SPS", header]
    for env_count in env_counts:
        cells = []
        for strategy in strategies:
            value = results.get((env_count, strategy))
            if value is None:
                cell = f"{'--':>8}"
            elif highlight == (env_count, strategy):
                cell = f">{value:7.1f}"
            else:
                cell = f"{value:8.1f}"
            cells.append(cell)
        lines.append(f"{env_count:>3} " + " ".join(cells))
    return "\n".join(lines)


def build_stats_panel(
    *,
    strategy: str,
    batch_size: int,
    total_steps: int,
    total_sps: float,
    elapsed: float,
    display_fps: float,
    steps_per_frame: int,
    tick_min: int,
    tick_max: int,
    max_position_spread: float,
) -> str:
    per_env_sps = total_sps / batch_size if batch_size > 0 else 0.0
    return "\n".join(
        [
            "Live Demo",
            f"strategy:        {strategy}",
            f"batch size:      {batch_size}",
            f"aggregate SPS:   {total_sps:8.1f}",
            f"per-env SPS:     {per_env_sps:8.1f}",
            f"elapsed sec:     {elapsed:8.1f}",
            f"video fps:       {display_fps:8.1f}",
            f"steps/frame:     {steps_per_frame:8d}",
            f"total env steps: {total_steps:8d}",
            f"tick range:      {tick_min:8d}..{tick_max:<8d}",
            f"xyz spread:      {max_position_spread:8.2f}",
        ]
    )


def create_figure(*, rows: int, cols: int, first_obs: list[dict[str, object]]):
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(
        rows,
        cols,
        figsize=(cols * 2.2 + 4.5, rows * 1.8 + 0.4),
        squeeze=False,
    )
    fig.subplots_adjust(left=0.03, right=0.76, bottom=0.05, top=0.90, wspace=0.05, hspace=0.20)

    images = []
    for instance_id, (ax, obs) in enumerate(zip(axes.flat, first_obs)):
        img = ax.imshow(obs["pov"], interpolation="nearest")
        ax.set_title(f"iid {instance_id}", fontsize=8)
        ax.axis("off")
        images.append(img)

    stats_artist = fig.text(
        0.79,
        0.89,
        "",
        va="top",
        ha="left",
        fontsize=10,
        family="monospace",
    )
    results_artist = fig.text(
        0.79,
        0.46,
        "",
        va="top",
        ha="left",
        fontsize=9,
        family="monospace",
    )
    return fig, axes, images, stats_artist, results_artist


def advance_envs(
    envs: list[NetheriteEnv],
    *,
    action: dict[str, object],
    stepper: Callable[[list[NetheriteEnv], list[dict]], list[dict]],
    steps_per_frame: int,
) -> tuple[list[dict[str, object]], list[dict[str, object]], int]:
    actions = [action for _ in envs]
    latest_results: list[dict] = []
    for _ in range(steps_per_frame):
        latest_results = stepper(envs, actions)
    observations = [result["obs"] for result in latest_results]
    infos = [result["info"] for result in latest_results]
    return observations, infos, len(envs) * steps_per_frame


def redraw_frame(
    *,
    fig,
    axes,
    images,
    stats_artist,
    results_artist,
    observations: list[dict[str, object]],
    infos: list[dict[str, object]],
    batch_size: int,
    strategy: str,
    total_steps: int,
    start_time: float,
    display_fps: float,
    steps_per_frame: int,
    benchmark_results: dict[tuple[int, str], float],
):
    tick_values = [int(info["state_tick"]) for info in infos]
    tick_min = min(tick_values)
    tick_max = max(tick_values)
    position_spread = max_position_spread(observations)

    for instance_id, (ax, img, obs, info) in enumerate(
        zip(axes.flat, images, observations, infos)
    ):
        img.set_data(obs["pov"])
        pos = obs["position"]
        ax.set_title(
            f"iid {instance_id} | t {info['state_tick']} | x {pos[0]:.1f} z {pos[2]:.1f}",
            fontsize=8,
        )

    elapsed = max(1e-9, time.perf_counter() - start_time)
    total_sps = total_steps / elapsed
    title = build_grid_title(
        batch_size=batch_size,
        strategy=strategy,
        total_sps=total_sps,
        display_fps=display_fps,
        total_steps=total_steps,
        tick_min=tick_min,
        tick_max=tick_max,
        max_position_spread=position_spread,
    )
    if fig.canvas.manager is not None:
        fig.canvas.manager.set_window_title(title)
    fig.suptitle(title, fontsize=12)
    stats_artist.set_text(
        build_stats_panel(
            strategy=strategy,
            batch_size=batch_size,
            total_steps=total_steps,
            total_sps=total_sps,
            elapsed=elapsed,
            display_fps=display_fps,
            steps_per_frame=steps_per_frame,
            tick_min=tick_min,
            tick_max=tick_max,
            max_position_spread=position_spread,
        )
    )
    results_artist.set_text(
        format_results_panel(benchmark_results, highlight=(batch_size, strategy))
    )
    fig.canvas.draw()
    return total_sps


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rows", type=int, default=4)
    parser.add_argument("--cols", type=int, default=8)
    parser.add_argument("--seed", type=int, default=424242)
    parser.add_argument("--capture-width", type=int, default=DEFAULT_DEMO_WIDTH)
    parser.add_argument("--capture-height", type=int, default=DEFAULT_DEMO_HEIGHT)
    parser.add_argument(
        "--seed-stride",
        type=int,
        default=1,
        help="Seed delta between instances. Default 1 matches the benchmark sweep.",
    )
    parser.add_argument(
        "--display-fps",
        type=float,
        default=12.0,
        help="Viewer refresh or encoded video FPS.",
    )
    parser.add_argument(
        "--strategy",
        choices=sorted(STEP_STRATEGIES),
        default="batched",
        help="Stepping strategy to visualize.",
    )
    parser.add_argument(
        "--steps-per-frame",
        type=int,
        default=8,
        help="Environment steps to advance between rendered frames.",
    )
    parser.add_argument(
        "--duration-sec",
        type=float,
        default=12.0,
        help="Output video duration when --output is set.",
    )
    parser.add_argument("--output", type=Path, default=None, help="Write an MP4 instead of opening a live window.")
    parser.add_argument(
        "--benchmark-log",
        type=Path,
        default=None,
        help="Optional bench_scaling log to display as a result table.",
    )
    parser.add_argument("--dpi", type=int, default=140, help="Video DPI for --output mode.")
    parser.add_argument("--java-home", default=DEFAULT_JAVA_HOME)
    parser.add_argument("--render-distance", type=int, default=4)
    parser.add_argument("--simulation-distance", type=int, default=5)
    parser.add_argument("--max-fps", type=int, default=9999)
    parser.add_argument(
        "--use-semaphore",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Use POSIX semaphores for IPC signaling.",
    )
    parser.add_argument(
        "--auto-orient",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Rotate each instance to the most open-looking starting view before capture.",
    )
    parser.add_argument(
        "--start-pitch",
        type=float,
        default=-10.0,
        help="Pitch angle used for the initial demo framing.",
    )
    parser.add_argument("--env-timeout", type=float, default=30.0)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None):
    args = parse_args(argv)
    if args.rows <= 0 or args.cols <= 0:
        raise SystemExit("--rows and --cols must be positive")
    if args.capture_width <= 0 or args.capture_height <= 0:
        raise SystemExit("--capture-width and --capture-height must be positive")
    if args.display_fps <= 0:
        raise SystemExit("--display-fps must be positive")
    if args.steps_per_frame <= 0:
        raise SystemExit("--steps-per-frame must be positive")
    if args.output is not None and args.duration_sec <= 0:
        raise SystemExit("--duration-sec must be positive when --output is set")

    import matplotlib

    matplotlib.use("Agg" if args.output is not None else "MacOSX")
    import matplotlib.pyplot as plt

    batch_size = args.rows * args.cols
    benchmark_results = load_benchmark_results(args.benchmark_log)
    launcher = Launcher(ROOT)
    launcher.cleanup_shmem()
    cleanup_world_dirs(launcher, batch_size)

    configs = build_grid_configs(
        rows=args.rows,
        cols=args.cols,
        seed=args.seed,
        java_home=args.java_home,
        seed_stride=args.seed_stride,
        capture_width=args.capture_width,
        capture_height=args.capture_height,
        render_distance=args.render_distance,
        simulation_distance=args.simulation_distance,
        max_fps=args.max_fps,
        use_semaphore=args.use_semaphore,
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

    envs = [NetheriteEnv(config=cfg, timeout=args.env_timeout) for cfg in configs]
    first_obs = [
        reset_demo_env(
            env,
            auto_orient=args.auto_orient,
            start_pitch=args.start_pitch,
        )
        for env in envs
    ]
    fig, axes, images, stats_artist, results_artist = create_figure(
        rows=args.rows,
        cols=args.cols,
        first_obs=first_obs,
    )
    action = build_demo_action()
    stepper = STEP_STRATEGIES[args.strategy]
    total_steps = 0
    start = time.perf_counter()
    running = True

    try:
        if args.output is not None:
            from matplotlib.animation import FFMpegWriter

            args.output.parent.mkdir(parents=True, exist_ok=True)
            writer = FFMpegWriter(
                fps=args.display_fps,
                metadata={
                    "title": f"Netherite B={batch_size} {args.strategy} demo",
                    "artist": "Codex",
                },
            )
            frame_count = max(1, int(round(args.duration_sec * args.display_fps)))
            with writer.saving(fig, str(args.output), dpi=args.dpi):
                observations = first_obs
                infos = [{"state_tick": 0} for _ in envs]
                redraw_frame(
                    fig=fig,
                    axes=axes,
                    images=images,
                    stats_artist=stats_artist,
                    results_artist=results_artist,
                    observations=observations,
                    infos=infos,
                    batch_size=batch_size,
                    strategy=args.strategy,
                    total_steps=total_steps,
                    start_time=start,
                    display_fps=args.display_fps,
                    steps_per_frame=args.steps_per_frame,
                    benchmark_results=benchmark_results,
                )
                writer.grab_frame()
                for _ in range(frame_count - 1):
                    observations, infos, stepped = advance_envs(
                        envs,
                        action=action,
                        stepper=stepper,
                        steps_per_frame=args.steps_per_frame,
                    )
                    total_steps += stepped
                    redraw_frame(
                        fig=fig,
                        axes=axes,
                        images=images,
                        stats_artist=stats_artist,
                        results_artist=results_artist,
                        observations=observations,
                        infos=infos,
                        batch_size=batch_size,
                        strategy=args.strategy,
                        total_steps=total_steps,
                        start_time=start,
                        display_fps=args.display_fps,
                        steps_per_frame=args.steps_per_frame,
                        benchmark_results=benchmark_results,
                    )
                    writer.grab_frame()
            print(f"Wrote video to {args.output}")
            return

        plt.ion()
        plt.show()

        def on_key(event):
            nonlocal running
            if event.key == "q":
                running = False

        fig.canvas.mpl_connect("key_press_event", on_key)

        display_interval = 1.0 / args.display_fps
        last_display = start
        observations = first_obs
        infos = [{"state_tick": 0} for _ in envs]

        while running and plt.fignum_exists(fig.number):
            observations, infos, stepped = advance_envs(
                envs,
                action=action,
                stepper=stepper,
                steps_per_frame=args.steps_per_frame,
            )
            total_steps += stepped
            now = time.perf_counter()
            if now - last_display >= display_interval:
                redraw_frame(
                    fig=fig,
                    axes=axes,
                    images=images,
                    stats_artist=stats_artist,
                    results_artist=results_artist,
                    observations=observations,
                    infos=infos,
                    batch_size=batch_size,
                    strategy=args.strategy,
                    total_steps=total_steps,
                    start_time=start,
                    display_fps=args.display_fps,
                    steps_per_frame=args.steps_per_frame,
                    benchmark_results=benchmark_results,
                )
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
                f"Done. strategy={args.strategy} | B={batch_size} | "
                f"total steps={total_steps} | aggregate SPS={total_sps:.1f}"
            )


if __name__ == "__main__":
    main()
