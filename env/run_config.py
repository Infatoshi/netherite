"""Shared YAML run config loader for recorder/replay tools."""

from __future__ import annotations

from dataclasses import dataclass, fields
from pathlib import Path

import yaml

from config import NetheriteConfig
from demo import build_demo_config


DEFAULT_JAVA_HOME = "/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home"


@dataclass
class RunConfig:
    source_path: Path
    recording_path: Path
    frame_sample_every: int
    frame_sample_dir: Path | None
    seed: int
    steps: int
    timeout: float
    display_scale: int
    display_fps: float
    netherite: NetheriteConfig


def config_to_dict(cfg: NetheriteConfig) -> dict[str, object]:
    return {field.name: getattr(cfg, field.name) for field in fields(type(cfg))}


def clone_netherite_config(cfg: NetheriteConfig) -> NetheriteConfig:
    return NetheriteConfig(**config_to_dict(cfg))


def _resolve_optional_path(
    raw_value: object,
    *,
    source_path: Path,
) -> Path | None:
    if raw_value is None:
        return None
    path = Path(str(raw_value))
    if not path.is_absolute():
        path = (source_path.parent / path).resolve()
    return path


def load_run_config(path: str | Path) -> RunConfig:
    source_path = Path(path).expanduser().resolve()
    raw = yaml.safe_load(source_path.read_text(encoding="utf-8"))
    if raw is None:
        raw = {}
    if not isinstance(raw, dict):
        raise TypeError("Run config must be a YAML mapping")

    netherite = build_demo_config()
    overrides = raw.get("netherite", {})
    if overrides is None:
        overrides = {}
    if not isinstance(overrides, dict):
        raise TypeError("'netherite' must be a YAML mapping")
    for key, value in overrides.items():
        if not hasattr(netherite, key):
            raise KeyError(f"Unknown Netherite config field: {key}")
        setattr(netherite, key, value)

    seed = int(raw.get("seed", netherite.seed))
    netherite.seed = seed
    netherite.instance_id = int(raw.get("instance_id", netherite.instance_id))
    if netherite.java_home is None:
        netherite.java_home = DEFAULT_JAVA_HOME

    recording_path_raw = raw.get("recording_path")
    if recording_path_raw is None:
        raise ValueError("Run config is missing required 'recording_path'")
    recording_path = Path(recording_path_raw)
    if not recording_path.is_absolute():
        recording_path = (source_path.parent / recording_path).resolve()

    frame_sample_every = int(raw.get("frame_sample_every", 0))
    frame_sample_dir = _resolve_optional_path(
        raw.get("frame_sample_dir"),
        source_path=source_path,
    )
    if frame_sample_every > 0 and frame_sample_dir is None:
        frame_sample_dir = recording_path.parent / f"{recording_path.stem}_samples"

    steps = int(raw.get("steps", 10_000))
    timeout = float(raw.get("timeout", 60.0))
    display_scale = int(raw.get("display_scale", 6))
    display_fps = float(raw.get("display_fps", 30.0))

    if frame_sample_every < 0:
        raise ValueError("'frame_sample_every' must be non-negative")
    if steps <= 0:
        raise ValueError("'steps' must be positive")
    if timeout <= 0:
        raise ValueError("'timeout' must be positive")
    if display_scale <= 0:
        raise ValueError("'display_scale' must be positive")
    if display_fps <= 0:
        raise ValueError("'display_fps' must be positive")

    return RunConfig(
        source_path=source_path,
        recording_path=recording_path,
        frame_sample_every=frame_sample_every,
        frame_sample_dir=frame_sample_dir,
        seed=seed,
        steps=steps,
        timeout=timeout,
        display_scale=display_scale,
        display_fps=display_fps,
        netherite=netherite,
    )
