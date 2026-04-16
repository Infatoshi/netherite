"""Tests for the multi-instance matplotlib grid demo helpers."""

# ruff: noqa: E402

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

import numpy as np

from grid_demo import (
    build_demo_action,
    build_grid_configs,
    build_grid_title,
    max_position_spread,
    reset_demo_env,
)


def test_build_grid_title_includes_batch_and_aggregate_sps():
    title = build_grid_title(
        batch_size=4,
        total_sps=302.4,
        display_fps=9.8,
        tick_min=25,
        tick_max=25,
        max_position_spread=0.0,
    )

    assert "B=4" in title
    assert "SPS=302.4" in title
    assert "display=9.8 fps" in title
    assert "action=forward+jump+attack" in title
    assert "ticks=25..25" in title
    assert "spread=0.00" in title


def test_build_grid_configs_creates_one_config_per_subplot_in_lockstep_mode():
    configs = build_grid_configs(
        rows=4,
        cols=8,
        seed=1000,
        java_home="/java21",
        seed_stride=0,
    )

    assert len(configs) == 32
    assert configs[0].instance_id == 0
    assert configs[-1].instance_id == 31
    assert configs[0].seed == 1000
    assert configs[-1].seed == 1000
    assert all(cfg.width == 160 for cfg in configs)
    assert all(cfg.height == 90 for cfg in configs)
    assert all(cfg.headless is True for cfg in configs)
    assert all(cfg.uncapped is True for cfg in configs)
    assert all(cfg.max_fps == 32767 for cfg in configs)
    assert all(cfg.java_home == "/java21" for cfg in configs)


def test_build_demo_action_repeats_forward_jump_and_attack():
    action = build_demo_action()

    assert action["forward"] == 1
    assert action["jump"] == 1
    assert action["attack"] == 1
    assert action["back"] == 0
    assert action["camera"].tolist() == [0.0, 0.0]


def test_max_position_spread_reports_zero_for_lockstep_positions():
    spread = max_position_spread(
        [
            {"position": np.array([1.0, 2.0, 3.0], dtype=np.float64)},
            {"position": np.array([1.0, 2.0, 3.0], dtype=np.float64)},
            {"position": np.array([1.0, 2.0, 3.0], dtype=np.float64)},
        ]
    )

    assert spread == 0.0


def test_reset_demo_env_releases_start_latch_without_waiting():
    calls: list[str] = []
    expected = {"pov": "frame"}

    class FakeEnv:
        def reset(self):
            calls.append("reset")
            return expected, {}

        def release_start_latch(self):
            calls.append("release")

    obs = reset_demo_env(FakeEnv())

    assert obs is expected
    assert calls == ["reset", "release"]
