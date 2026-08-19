"""dig_schema.py - shared dig_trace object schema (Java tape + magma state).

Backwards-compatible contract:
  - Header key dig_trace:1 (or meta dig_trace:true) opts into per-tick dig rows.
  - Legacy tapes omit dig_trace entirely; parsers must not invent dig objects.
  - When dig_trace is on, every tick carries a dig object with the fields below.

Java emission: netheritemod.Recorder.appendDigTrace (gated by recstart dig_trace).
Magma emission: game/script.c write_state when cr_cfg()->dig_trace.
Source fields (oracle):
  Minecraft.leftClickCounter
  PlayerControllerMP.{blockHitDelay,isHittingBlock,currentBlock,curBlockDamageMP}
  PlayerControllerMP.getBlockReachDistance
  Minecraft.objectMouseOver (type + sideHit + block pos)
  IBlockState.getPlayerRelativeBlockHardness (read-only)
  onPlayerDestroyBlock success -> brk
  World.notifyNeighbors* nested setBlockState -> bc_ref / bcn
"""
from __future__ import annotations

from typing import Any, Iterable, Mapping, Optional

# Canonical field order used by dig_compare for first-mismatch reports.
DIG_FIELDS = (
    "lcc",    # leftClickCounter
    "bhd",    # blockHitDelay
    "hit",    # isHittingBlock 0|1
    "cb",     # currentBlock [x,y,z] | null
    "face",   # EnumFacing index or -1
    "ray",    # RayTraceResult.Type ordinal 0=MISS 1=BLOCK 2=ENTITY
    "rx",     # ray block x (present when ray==1)
    "ry",
    "rz",
    "held",   # [id, meta, count] | 0
    "dmg",    # curBlockDamageMP / dig progress
    "rel",    # relative hardness | null
    "reach",  # getBlockReachDistance
    "brk",    # 0 | [x,y,z]
    "bcn",    # neighbor-caused final count
    "bc_ref", # ordered [[x,y,z,id,meta], ...]
)

REQUIRED_ALWAYS = (
    "lcc", "bhd", "hit", "cb", "face", "ray", "held", "dmg", "rel",
    "reach", "brk", "bcn", "bc_ref",
)


def header_has_dig_trace(header: Mapping[str, Any] | None) -> bool:
    if not header:
        return False
    v = header.get("dig_trace")
    return v == 1 or v is True or v == "1"


def meta_has_dig_trace(meta: Mapping[str, Any] | None) -> bool:
    if not meta:
        return False
    if meta.get("dig_trace") is True or meta.get("dig_trace") == 1:
        return True
    return header_has_dig_trace(meta.get("header") if isinstance(meta.get("header"), dict) else None)


def validate_dig_object(dig: Any, *, where: str = "dig") -> list[str]:
    """Return a list of schema errors (empty => ok). Does not mutate dig."""
    errs: list[str] = []
    if not isinstance(dig, dict):
        return [f"{where}: expected object, got {type(dig).__name__}"]
    for k in REQUIRED_ALWAYS:
        if k not in dig:
            errs.append(f"{where}: missing required field {k}")
    if errs:
        return errs

    def _int_like(name: str, v: Any) -> None:
        if isinstance(v, bool) or not isinstance(v, int):
            errs.append(f"{where}.{name}: expected int, got {v!r}")

    def _num(name: str, v: Any) -> None:
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            errs.append(f"{where}.{name}: expected number, got {v!r}")

    _int_like("lcc", dig["lcc"])
    _int_like("bhd", dig["bhd"])
    if dig["hit"] not in (0, 1):
        errs.append(f"{where}.hit: expected 0|1, got {dig['hit']!r}")
    cb = dig["cb"]
    if cb is not None:
        if not (isinstance(cb, list) and len(cb) == 3
                and all(isinstance(x, int) and not isinstance(x, bool) for x in cb)):
            errs.append(f"{where}.cb: expected [x,y,z] ints or null, got {cb!r}")
    _int_like("face", dig["face"])
    if dig["ray"] not in (0, 1, 2):
        errs.append(f"{where}.ray: expected 0|1|2, got {dig['ray']!r}")
    if dig["ray"] == 1:
        for k in ("rx", "ry", "rz"):
            if k not in dig:
                errs.append(f"{where}: ray==BLOCK requires {k}")
            else:
                _int_like(k, dig[k])
    held = dig["held"]
    if held != 0:
        if not (isinstance(held, list) and len(held) == 3
                and all(isinstance(x, int) and not isinstance(x, bool) for x in held)):
            errs.append(f"{where}.held: expected 0 or [id,meta,count], got {held!r}")
    _num("dmg", dig["dmg"])
    if dig["rel"] is not None:
        _num("rel", dig["rel"])
    _num("reach", dig["reach"])
    brk = dig["brk"]
    if brk != 0:
        if not (isinstance(brk, list) and len(brk) == 3
                and all(isinstance(x, int) and not isinstance(x, bool) for x in brk)):
            errs.append(f"{where}.brk: expected 0 or [x,y,z], got {brk!r}")
    _int_like("bcn", dig["bcn"])
    bc_ref = dig["bc_ref"]
    if not isinstance(bc_ref, list):
        errs.append(f"{where}.bc_ref: expected list, got {type(bc_ref).__name__}")
    else:
        if dig["bcn"] != len(bc_ref):
            errs.append(f"{where}.bcn ({dig['bcn']}) != len(bc_ref) ({len(bc_ref)})")
        for i, e in enumerate(bc_ref):
            if not (isinstance(e, list) and len(e) == 5
                    and all(isinstance(x, int) and not isinstance(x, bool) for x in e)):
                errs.append(f"{where}.bc_ref[{i}]: expected [x,y,z,id,meta], got {e!r}")
    return errs


def phase_of(dig: Mapping[str, Any]) -> str:
    """Coarse dig phase label for mismatch reports (diagnostic, not a gate)."""
    if dig.get("brk") not in (0, None):
        return "break"
    if dig.get("bhd", 0) and dig.get("bhd", 0) > 0:
        return "delay"
    if dig.get("lcc", 0) and dig.get("lcc", 0) > 0 and not dig.get("hit"):
        return "left_click_cooldown"
    if dig.get("hit"):
        return "accruing" if float(dig.get("dmg") or 0) > 0 else "acquire"
    if dig.get("ray") == 1:
        return "ray_block_idle"
    return "idle"


def emit_dig_object(
    *,
    lcc: int = 0,
    bhd: int = 0,
    hit: int = 0,
    cb: Optional[list[int]] = None,
    face: int = -1,
    ray: int = 0,
    rx: Optional[int] = None,
    ry: Optional[int] = None,
    rz: Optional[int] = None,
    held: Any = 0,
    dmg: float = 0.0,
    rel: Any = None,
    reach: float = 4.5,
    brk: Any = 0,
    bc_ref: Optional[Iterable[list[int]]] = None,
) -> dict[str, Any]:
    """Build a schema-valid dig object (Python-side emitter for tests)."""
    ref = list(bc_ref) if bc_ref is not None else []
    o: dict[str, Any] = {
        "lcc": int(lcc),
        "bhd": int(bhd),
        "hit": 1 if hit else 0,
        "cb": list(cb) if cb is not None else None,
        "face": int(face),
        "ray": int(ray),
        "held": held if held != 0 else 0,
        "dmg": float(dmg),
        "rel": None if rel is None else float(rel),
        "reach": float(reach),
        "brk": brk if brk != 0 else 0,
        "bcn": len(ref),
        "bc_ref": ref,
    }
    if ray == 1 and rx is not None and ry is not None and rz is not None:
        o["rx"], o["ry"], o["rz"] = int(rx), int(ry), int(rz)
    return o
