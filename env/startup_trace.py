"""Lightweight startup tracing for multi-instance benchmark bring-up."""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path


TRACE_ENV_VAR = "NETHERITE_STARTUP_TRACE"
_TRACE_T0 = time.perf_counter()


def startup_trace_enabled() -> bool:
    return os.environ.get(TRACE_ENV_VAR) == "1"


def trace_event(
    event: str,
    *,
    instance_id: int | None = None,
    **fields: object,
) -> None:
    if not startup_trace_enabled():
        return

    delta = time.perf_counter() - _TRACE_T0
    parts = [f"[startup_trace +{delta:8.3f}s]", event]
    if instance_id is not None:
        parts.append(f"instance={instance_id}")
    for key, value in fields.items():
        parts.append(f"{key}={value!r}")
    print(" ".join(parts), file=sys.stderr, flush=True)


def ensure_trace_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
