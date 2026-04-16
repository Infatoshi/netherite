"""Tests for scaling benchmark setup and teardown."""

# ruff: noqa: E402

import io
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

import bench_scaling


def test_parse_args_accepts_java_home_override():
    args = bench_scaling.parse_args(
        [
            "--envs",
            "4",
            "--strategies",
            "sync",
            "--java-home",
            "/java21",
            "--width",
            "320",
            "--height",
            "180",
            "--render-distance",
            "2",
            "--simulation-distance",
            "3",
            "--max-fps",
            "1200",
            "--no-use-semaphore",
            "--env-timeout",
            "45",
            "--launch-stagger",
            "0.25",
            "--reset-stagger",
            "0.5",
            "--trace-startup",
        ]
    )

    assert args.envs == "4"
    assert args.strategies == "sync"
    assert args.java_home == "/java21"
    assert args.width == 320
    assert args.height == 180
    assert args.render_distance == 2
    assert args.simulation_distance == 3
    assert args.max_fps == 1200
    assert args.use_semaphore is False
    assert args.env_timeout == 45.0
    assert args.launch_stagger == 0.25
    assert args.reset_stagger == 0.5
    assert args.trace_startup is True


def test_make_config_uses_fast_throughput_defaults():
    cfg = bench_scaling.make_config(instance_id=7, java_home="/java21")

    assert cfg.instance_id == 7
    assert cfg.seed == 424249
    assert cfg.width == bench_scaling.FAST_WIDTH
    assert cfg.height == bench_scaling.FAST_HEIGHT
    assert cfg.render_distance == bench_scaling.FAST_RENDER_DISTANCE
    assert cfg.simulation_distance == bench_scaling.FAST_SIMULATION_DISTANCE
    assert cfg.max_fps == bench_scaling.FAST_MAX_FPS
    assert cfg.use_semaphore is True
    assert cfg.uncapped is True
    assert cfg.java_home == "/java21"


def test_run_benchmark_uses_launcher_prewarm_and_cleans_up(monkeypatch, tmp_path: Path):
    events: list[tuple[str, object]] = []
    closed_env_ids: list[int] = []

    class FakeLauncher:
        def __init__(self, project_dir):
            events.append(("launcher_init", project_dir))

        def cleanup_shmem(self):
            events.append(("cleanup_shmem", None))

        def cleanup_instance_run_dirs(self, instance_ids):
            events.append(("cleanup_dirs", tuple(instance_ids)))

        def launch_with_mod_cache_prewarm(
            self,
            configs,
            *,
            timeout=120.0,
            stagger_seconds=0.0,
        ):
            events.append(
                (
                    "launch",
                    (
                        tuple(cfg.instance_id for cfg in configs),
                        tuple(cfg.java_home for cfg in configs),
                        tuple(cfg.width for cfg in configs),
                        tuple(cfg.height for cfg in configs),
                        tuple(cfg.render_distance for cfg in configs),
                        tuple(cfg.simulation_distance for cfg in configs),
                        tuple(cfg.max_fps for cfg in configs),
                        tuple(cfg.use_semaphore for cfg in configs),
                        timeout,
                        stagger_seconds,
                    ),
                )
            )
            return [object() for _ in configs]

        def wait_all_ready(self, timeout=120.0):
            events.append(("wait_all_ready", timeout))
            return True

        def stop_all(self):
            events.append(("stop_all", None))

    class FakeEnv:
        def __init__(self, *, config, timeout):
            self.config = config
            self.instance_id = config.instance_id
            events.append(("env_init", (config.instance_id, config.java_home, timeout)))

        def reset(self):
            events.append(("reset", self.instance_id))
            return {"pov": None}, {}

        def wait_for_start_latch(self):
            events.append(("wait_for_start_latch", self.instance_id))

        def release_start_latch(self):
            events.append(("release_start_latch", self.instance_id))

        def get_state_tick(self):
            events.append(("get_state_tick", self.instance_id))
            return 0

        def _wait_until_state_tick(self, target_tick):
            events.append(("wait_until_state_tick", (self.instance_id, target_tick)))
            return {"tick": target_tick}

        def close(self):
            closed_env_ids.append(self.instance_id)

    step_calls: list[tuple[int, int]] = []

    def fake_step_sync(envs, actions):
        step_calls.append((len(envs), len(actions)))
        return [{} for _ in envs]

    perf_values = iter([10.0, 12.0])

    monkeypatch.setattr(bench_scaling, "ROOT", tmp_path)
    monkeypatch.setattr(bench_scaling, "Launcher", FakeLauncher)
    monkeypatch.setattr(bench_scaling, "NetheriteEnv", FakeEnv)
    monkeypatch.setattr(bench_scaling, "step_sync", fake_step_sync)
    monkeypatch.setattr(bench_scaling.time, "perf_counter", lambda: next(perf_values))
    monkeypatch.setattr(bench_scaling.time, "sleep", lambda _seconds: None)

    sps = bench_scaling.run_benchmark(
        num_envs=2,
        strategy="sync",
        steps=3,
        warmup=1,
        java_home="/java21",
        width=320,
        height=180,
        env_timeout=45.0,
        launch_stagger=0.25,
        reset_stagger=0.5,
        trace_startup=False,
    )

    assert sps == 3.0
    assert events[0] == ("launcher_init", tmp_path)
    assert events.count(("cleanup_shmem", None)) == 2
    assert events.count(("cleanup_dirs", (0, 1))) == 2
    assert (
        "launch",
        (
            (0, 1),
            ("/java21", "/java21"),
            (320, 320),
            (180, 180),
            (bench_scaling.FAST_RENDER_DISTANCE, bench_scaling.FAST_RENDER_DISTANCE),
            (
                bench_scaling.FAST_SIMULATION_DISTANCE,
                bench_scaling.FAST_SIMULATION_DISTANCE,
            ),
            (bench_scaling.FAST_MAX_FPS, bench_scaling.FAST_MAX_FPS),
            (True, True),
            120.0,
            0.25,
        ),
    ) in events
    assert ("wait_all_ready", 120.0) in events
    assert ("env_init", (0, "/java21", 45.0)) in events
    assert ("env_init", (1, "/java21", 45.0)) in events
    assert ("release_start_latch", 0) in events
    assert ("release_start_latch", 1) in events
    assert ("wait_until_state_tick", (0, 1)) in events
    assert ("wait_until_state_tick", (1, 1)) in events
    assert ("stop_all", None) in events
    assert closed_env_ids == [0, 1]
    assert step_calls == [(2, 2), (2, 2), (2, 2), (2, 2)]


def test_print_scaling_efficiency_skips_without_baseline():
    stream = io.StringIO()

    bench_scaling.print_scaling_efficiency(
        results={(8, "sync"): 146.5},
        env_counts=[8],
        strategies=["sync"],
        stream=stream,
    )

    assert "skipped: 1 env sync baseline not included in this run" in stream.getvalue()
