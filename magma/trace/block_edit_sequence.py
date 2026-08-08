#!/usr/bin/env python3
"""Parse deterministic player-relative block edits for lockstep traces."""

from dataclasses import dataclass
import pathlib


@dataclass(frozen=True)
class BlockEdit:
    tick: int
    dx: int
    dy: int
    dz: int
    block: int
    meta: int


def load(path, tick_count):
    """Load `TICK DX DY DZ BLOCK META` rows, with at most one edit per tick."""
    source = pathlib.Path(path)
    edits = []
    seen_ticks = set()
    for line_no, line in enumerate(
            source.read_text(encoding="utf-8").splitlines(), 1):
        text = line.split("#", 1)[0].strip()
        if not text:
            continue
        fields = text.split()
        if len(fields) != 6:
            raise ValueError(
                f"{source}:{line_no}: expected "
                "TICK DX DY DZ BLOCK META"
            )
        try:
            tick, dx, dy, dz, block, meta = map(int, fields)
        except ValueError as exc:
            raise ValueError(
                f"{source}:{line_no}: every field must be an integer"
            ) from exc
        if not 0 <= tick < tick_count:
            raise ValueError(
                f"{source}:{line_no}: tick {tick} is outside tape "
                f"range 0..{tick_count - 1}"
            )
        if tick in seen_ticks:
            raise ValueError(
                f"{source}:{line_no}: duplicate edit for tick {tick}; "
                "the Java lock permits one block edit per tick"
            )
        if not 0 <= block <= 4095:
            raise ValueError(
                f"{source}:{line_no}: block must be in 0..4095"
            )
        if not 0 <= meta <= 15:
            raise ValueError(
                f"{source}:{line_no}: metadata must be in 0..15"
            )
        seen_ticks.add(tick)
        edits.append(BlockEdit(tick, dx, dy, dz, block, meta))
    if not edits:
        raise ValueError(f"{source}: sequence contains no block edits")
    return sorted(edits, key=lambda edit: edit.tick)
