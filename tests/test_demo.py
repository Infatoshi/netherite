"""Tests for demo configuration."""

# ruff: noqa: E402

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

from demo import build_demo_config


def test_build_demo_config_uses_fast_visible_wrapper_shape():
    cfg = build_demo_config()

    assert cfg.width == 160
    assert cfg.height == 90
    assert cfg.render_distance == 8
    assert cfg.simulation_distance == 8
    assert cfg.graphics == "fast"
    assert cfg.particles == "minimal"
    assert cfg.clouds == "off"
    assert cfg.rl is True
