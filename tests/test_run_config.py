"""Tests for YAML run config loading."""

# ruff: noqa: E402

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

from run_config import DEFAULT_JAVA_HOME, load_run_config


def test_load_run_config_applies_yaml_overrides_and_resolves_paths(tmp_path: Path):
    cfg_dir = tmp_path / "config"
    cfg_dir.mkdir()
    config_path = cfg_dir / "v1.yaml"
    config_path.write_text(
        """
seed: 999
recording_path: ../recordings/test.jsonl
frame_sample_every: 25
frame_sample_dir: ../recordings/test_samples
steps: 321
timeout: 12.5
display_scale: 7
display_fps: 24.0
netherite:
  width: 200
  height: 120
  render_distance: 10
  simulation_distance: 9
  java_home: /custom/java
""".strip()
        + "\n",
        encoding="utf-8",
    )

    run = load_run_config(config_path)

    assert run.source_path == config_path.resolve()
    assert run.recording_path == (tmp_path / "recordings" / "test.jsonl").resolve()
    assert run.frame_sample_every == 25
    assert run.frame_sample_dir == (tmp_path / "recordings" / "test_samples").resolve()
    assert run.seed == 999
    assert run.steps == 321
    assert run.timeout == 12.5
    assert run.display_scale == 7
    assert run.display_fps == 24.0
    assert run.netherite.width == 200
    assert run.netherite.height == 120
    assert run.netherite.render_distance == 10
    assert run.netherite.simulation_distance == 9
    assert run.netherite.java_home == "/custom/java"


def test_load_run_config_fills_default_java_home_when_missing(tmp_path: Path):
    path = tmp_path / "v1.yaml"
    path.write_text("recording_path: run.jsonl\n", encoding="utf-8")

    run = load_run_config(path)

    assert run.netherite.java_home == DEFAULT_JAVA_HOME
    assert run.frame_sample_every == 0
    assert run.frame_sample_dir is None


def test_load_run_config_defaults_sample_dir_from_recording_path(tmp_path: Path):
    path = tmp_path / "v1.yaml"
    path.write_text(
        "recording_path: recordings/run.jsonl\nframe_sample_every: 100\n",
        encoding="utf-8",
    )

    run = load_run_config(path)

    assert run.frame_sample_dir == (tmp_path / "recordings" / "run_samples").resolve()
