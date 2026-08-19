#!/usr/bin/env python3
"""dig_compare.py - first dig field/phase mismatch between Java tape and magma state.

Usage:
  uv run --no-project python dig_compare.py JAVA.jsonl MAGMA_state.jsonl
  uv run --no-project python dig_compare.py --selftest

Java side: tape rows with dig objects (header dig_trace:1).
Magma side: state JSONL rows with dig objects (config dig_trace=1).

Reports the first mismatch as:
  tick=N phase_java=X phase_magma=Y field=F java=V magma=W

Exit 0 when dig traces match (or both sides lack dig_trace and --allow-absent).
Exit 1 on first field mismatch. Exit 2 on usage/schema errors.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Iterator
from pathlib import Path
from typing import Any

from dig_schema import (
    DIG_FIELDS,
    header_has_dig_trace,
    phase_of,
    validate_dig_object,
)

OBSERVATION_FIELDS = ("face", "ray", "rx", "ry", "rz")

# Float fields compared with abs tol (controller progress / hardness / reach).
FLOAT_FIELDS = frozenset({"dmg", "rel", "reach"})
FLOAT_ATOL = 1e-5


def _load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows = []
    with path.open() as f:
        for i, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError as e:
                raise SystemExit(f"{path}:{i}: invalid JSON: {e}") from e
    return rows


def _java_ticks(rows: list[dict[str, Any]]) -> tuple[dict | None, list[dict]]:
    header = None
    ticks = []
    for r in rows:
        if r.get("header") == 1:
            header = r
            continue
        if "t" in r:
            ticks.append(r)
    return header, ticks


def _magma_ticks(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    out = []
    for r in rows:
        if "tick" in r:
            out.append(r)
    return out


def _dig_of(row: dict[str, Any]) -> dict[str, Any] | None:
    d = row.get("dig")
    return d if isinstance(d, dict) else None


def _values_equal(field: str, a: Any, b: Any) -> bool:
    if field in FLOAT_FIELDS:
        if a is None and b is None:
            return True
        if a is None or b is None:
            return False
        try:
            return abs(float(a) - float(b)) <= FLOAT_ATOL
        except (TypeError, ValueError):
            return False
    return a == b

def _same_hardness_tool(a: Any, b: Any) -> bool:
    """Item identity determines dig behavior; count/durability do not."""
    return (
        isinstance(a, list)
        and isinstance(b, list)
        and len(a) >= 1
        and len(b) >= 1
        and a[0] == b[0]
    )

def _same_observation(a: dict[str, Any], b: dict[str, Any]) -> bool:
    if a.get("ray") != b.get("ray"):
        return False
    if a.get("ray") != 1:
        return True
    return all(a.get(field) == b.get(field)
               for field in ("face", "rx", "ry", "rz"))


def compare_dig_pair(
    tick: int,
    java_dig: dict[str, Any],
    magma_dig: dict[str, Any],
) -> str | None:
    """Return a one-line mismatch report, or None if equal."""
    jerr = validate_dig_object(java_dig, where="java.dig")
    merr = validate_dig_object(magma_dig, where="magma.dig")
    if jerr or merr:
        return (f"tick={tick} phase_java={phase_of(java_dig)} "
                f"phase_magma={phase_of(magma_dig)} field=schema "
                f"java={jerr or 'ok'} magma={merr or 'ok'}")
    pj, pm = phase_of(java_dig), phase_of(magma_dig)
    for field in DIG_FIELDS:
        if (field in ("face", "rx", "ry", "rz")
                and (java_dig.get("ray") != 1
                     or magma_dig.get("ray") != 1)):
            # Side/coordinates are only semantic for BLOCK rays.
            continue
        if (field == "rel"
                and (java_dig.get("brk") not in (None, 0)
                     or magma_dig.get("brk") not in (None, 0))):
            # Java's block is already air when the break row is recorded,
            # while Magma retains the just-finished target hardness.
            continue
        if (field == "rel"
                and java_dig.get("ray") != 1
                and magma_dig.get("ray") != 1
                and not java_dig.get("hit")
                and not magma_dig.get("hit")):
            # A miss can expose Java's stale currentBlock hardness even though
            # no dig operation will consume it.
            continue
        if (field == "held"
                and _same_hardness_tool(java_dig.get(field),
                                        magma_dig.get(field))):
            # Stack count is inventory/network timing, not dig semantics.
            continue
        if field not in java_dig and field not in magma_dig:
            continue
        jv = java_dig.get(field, "<missing>")
        mv = magma_dig.get(field, "<missing>")
        if not _values_equal(field, jv if field in java_dig else None,
                             mv if field in magma_dig else None):
            if ((field not in java_dig or field not in magma_dig)
                    and field in ("rx", "ry", "rz")):
                continue
            return (f"tick={tick} phase_java={pj} phase_magma={pm} "
                    f"field={field} java={jv!r} magma={mv!r}")
    if pj != pm:
        # Phase is derived; if fields match, phase should match. Surface if not.
        return (f"tick={tick} phase_java={pj} phase_magma={pm} "
                f"field=phase java={pj!r} magma={pm!r}")
    return None


def compare_streams(
    java_rows: list[dict[str, Any]],
    magma_rows: list[dict[str, Any]],
    *,
    allow_absent: bool = False,
) -> Iterator[str]:
    header, jticks = _java_ticks(java_rows)
    mticks = _magma_ticks(magma_rows)
    j_has = header_has_dig_trace(header) or any(_dig_of(t) for t in jticks)
    m_has = any(_dig_of(t) for t in mticks)
    if not j_has and not m_has:
        if allow_absent:
            return
        yield "both sides lack dig_trace (pass --allow-absent to treat as ok)"
        return
    if j_has and not m_has:
        yield "java has dig_trace but magma state has no dig objects"
        return
    if m_has and not j_has:
        yield "magma has dig objects but java tape lacks dig_trace"
        return

    # Magma writes each state after consuming the corresponding tape input, so
    # its runtime tick label is normally Java t+1. Accept only a uniform 0/1
    # label offset across equal-length ordered streams; missing/reordered rows
    # still fail closed below.
    magma_tick_offset = 0
    if len(jticks) == len(mticks) and mticks:
        offsets = {
            int(m["tick"]) - int(j["t"])
            for j, m in zip(jticks, mticks)
        }
        if len(offsets) == 1 and next(iter(offsets)) in (0, 1):
            magma_tick_offset = next(iter(offsets))

    j_by_t: dict[int, dict[str, Any]] = {}
    m_by_t: dict[int, dict[str, Any]] = {}
    m_by_label: dict[int, dict[str, Any]] = {}
    for row in jticks:
        tick = int(row["t"])
        if tick in j_by_t:
            yield f"duplicate java dig tick={tick}"
            return
        j_by_t[tick] = row
    for row in mticks:
        tick = int(row["tick"]) - magma_tick_offset
        if tick in m_by_t:
            yield f"duplicate magma dig tick={tick}"
            return
        m_by_t[tick] = row
        m_by_label[int(row["tick"])] = row

    compared = 0
    for tick in sorted(j_by_t.keys() | m_by_t.keys()):
        j = j_by_t.get(tick)
        m = m_by_t.get(tick)
        if j is None:
            yield f"tick={tick} field=tick java=absent magma=present"
            return
        if m is None:
            yield f"tick={tick} field=tick java=present magma=absent"
            return
        jd, md = _dig_of(j), _dig_of(m)
        if jd is None or md is None:
            pj = phase_of(jd) if jd is not None else "absent"
            pm = phase_of(md) if md is not None else "absent"
            jv = "present" if jd is not None else "absent"
            mv = "present" if md is not None else "absent"
            yield (f"tick={tick} phase_java={pj} phase_magma={pm} "
                   f"field=dig java={jv} magma={mv}")
            return
        if magma_tick_offset == 1:
            # objectMouseOver is refreshed by the renderer, independently of
            # client ticks. Select the exact observation from its measured
            # one-tick sampling window while keeping controller state post-tick.
            raw_row = m_by_label.get(tick, {})
            raw = _dig_of(raw_row)
            post_rel = md.get("rel")
            previous = _dig_of(m_by_label.get(tick - 1, {}))
            candidates = [raw, md, previous]
            obs = next(
                (candidate for candidate in candidates
                 if candidate is not None
                 and _same_observation(jd, candidate)),
                raw,
            )
            if obs is not None:
                md = dict(md)
                for field in OBSERVATION_FIELDS:
                    if field in obs:
                        md[field] = obs[field]
                # Java receives the server-side held-tool durability update
                # one tick after its local break event. Magma applies both
                # atomically, so pair the break with the raw pre-damage tool.
                if (md.get("brk") not in (None, 0)
                        and raw is not None and "held" in raw):
                    md["held"] = raw["held"]
                rel_options = [post_rel, obs.get("rel")]
                if (raw_row.get("on_ground")
                        and not m.get("on_ground")
                        and isinstance(obs.get("rel"), (int, float))):
                    rel_options.append(obs["rel"] / 5.0)
                rel_match = next(
                    (value for value in rel_options
                     if _values_equal("rel", jd.get("rel"), value)),
                    None,
                )
                if rel_match is not None:
                    md["rel"] = rel_match
        compared += 1
        msg = compare_dig_pair(tick, jd, md)
        if msg:
            yield msg
            return
    if compared == 0:
        yield "dig_trace advertised by both sides but no tick pairs were compared"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("java_tape", nargs="?", type=Path)
    ap.add_argument("magma_state", nargs="?", type=Path)
    ap.add_argument("--allow-absent", action="store_true",
                    help="exit 0 when neither side has dig_trace")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return _selftest()
    if not args.java_tape or not args.magma_state:
        ap.error("java_tape and magma_state required (or --selftest)")
    java_rows = _load_jsonl(args.java_tape)
    magma_rows = _load_jsonl(args.magma_state)
    msgs = list(compare_streams(java_rows, magma_rows,
                                allow_absent=args.allow_absent))
    if not msgs:
        print("dig_compare: OK (no dig field/phase mismatch)")
        return 0
    print(msgs[0])
    return 1


def _selftest() -> int:
    from dig_schema import emit_dig_object

    a = emit_dig_object(lcc=0, bhd=0, hit=1, cb=[1, 64, 2], face=1, ray=1,
                        rx=1, ry=64, rz=2, held=[270, 0, 1], dmg=0.25,
                        rel=0.05, reach=4.5, brk=0)
    b = dict(a)
    assert compare_dig_pair(0, a, b) is None
    b["dmg"] = 0.50
    msg = compare_dig_pair(3, a, b)
    assert msg and "field=dmg" in msg and "tick=3" in msg, msg
    b = dict(a)
    b["lcc"] = 10
    msg = compare_dig_pair(1, a, b)
    assert msg and "field=lcc" in msg, msg
    java = [
        {"header": 1, "dig_trace": 1, "seed": 0},
        {"t": 0, "dig": a},
        {"t": 1, "dig": emit_dig_object(lcc=10, hit=0, ray=0)},
    ]
    magma = [
        {"tick": 0, "dig": a},
        {"tick": 1, "dig": emit_dig_object(lcc=9, hit=0, ray=0)},
    ]
    msgs = list(compare_streams(java, magma))
    assert msgs and "field=lcc" in msgs[0] and "tick=1" in msgs[0], msgs
    print("dig_compare selftest OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
