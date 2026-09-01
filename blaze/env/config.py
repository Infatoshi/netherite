"""Strict flat configuration for all VecBlaze backends."""

from __future__ import annotations

from dataclasses import dataclass, replace
from pathlib import Path


@dataclass(frozen=True)
class BlazeConfig:
    backend: str = "cpu"
    device: int = 0
    n_envs: int = 1
    ktime: bool = False
    stage_time: bool = False
    legacy_recenter: bool = False
    warp_tick: int = 1
    op_trace: bool = False
    no_ore_xy: bool = False
    stack_kib: int = 128
    no_emit_all: bool = False
    natural_spawn: bool = False
    natural_spawn_passive: bool = False
    metal_max_cells: int = 2097152
    metallib: str = ""


DEFAULT_PATH = Path(__file__).resolve().parent.parent / "blaze.conf"
_BOOL_KEYS = {
    "ktime", "stage_time", "legacy_recenter", "op_trace", "no_ore_xy",
    "no_emit_all", "natural_spawn", "natural_spawn_passive",
}
_INT_KEYS = {"device", "n_envs", "warp_tick", "metal_max_cells", "stack_kib"}
_STR_KEYS = {"backend", "metallib"}
_KEYS = _BOOL_KEYS | _INT_KEYS | _STR_KEYS


def _parse_value(key: str, value: str):
    if key in _BOOL_KEYS:
        if value not in {"0", "1"}:
            raise ValueError(f"{key} must be 0 or 1")
        return value == "1"
    if key in _INT_KEYS:
        try:
            return int(value, 10)
        except ValueError as exc:
            raise ValueError(f"{key} must be an integer") from exc
    return value


def load_config(path: str | Path | None = None, **overrides) -> BlazeConfig:
    """Load defaults, one config file, then explicit keyword overrides."""
    cfg = BlazeConfig()
    source = DEFAULT_PATH if path is None else Path(path)
    values = {}
    if source.exists():
        for lineno, raw in enumerate(source.read_text().splitlines(), 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if "=" not in line:
                raise ValueError(f"{source}:{lineno}: expected key = value")
            key, value = (part.strip() for part in line.split("=", 1))
            if key not in _KEYS:
                raise ValueError(f"{source}:{lineno}: unknown key {key}")
            if key in values:
                raise ValueError(f"{source}:{lineno}: duplicate key {key}")
            values[key] = _parse_value(key, value)
    for key, value in overrides.items():
        if value is None:
            continue
        if key not in _KEYS:
            raise ValueError(f"unknown config key {key}")
        values[key] = value
    cfg = replace(cfg, **values)
    if cfg.backend not in {"cpu", "cuda", "metal"}:
        raise ValueError("backend must be cpu, cuda, or metal")
    if cfg.device < 0:
        raise ValueError("device must be non-negative")
    if cfg.n_envs <= 0:
        raise ValueError("n_envs must be positive")
    if cfg.warp_tick not in {0, 1}:
        raise ValueError("warp_tick must be 0 or 1")
    if cfg.metal_max_cells <= 0:
        raise ValueError("metal_max_cells must be positive")
    if cfg.stack_kib <= 0 or cfg.stack_kib > 1024:
        raise ValueError("stack_kib must be in 1..1024")
    return cfg
