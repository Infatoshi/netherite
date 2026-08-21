#!/usr/bin/env python3
"""detmob_gate.py — magma live vs tape pose compare (det_entity_rng=1).

PASS = bit-equal pos/yaw/pitch/hyaw over the window for standing passives.
Otherwise prints the first divergent (tick, entity, field) and Entity.rand
cursor delta. Not wired into `make test`.

    uv run --no-project python verify/trace/detmob_gate.py TAPE.jsonl
"""
from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rng_cursor import draws_between

PASSIVE = {
    "EntityCow": "cow",
    "EntitySheep": "sheep",
    "EntityPig": "pig",
    "EntityChicken": "chicken",
    "cow": "cow",
    "sheep": "sheep",
    "pig": "pig",
    "chicken": "chicken",
}

REPO = Path(__file__).resolve().parents[2]


def f64_bits(v: float) -> int:
    return struct.unpack("<Q", struct.pack("<d", float(v)))[0]


def f32_bits(v: float) -> int:
    return struct.unpack("<I", struct.pack("<f", float(v)))[0]


def load_tape(path: Path):
    header = None
    rows = []
    with path.open(encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("header"):
                header = rec
            else:
                rows.append(rec)
    return header, rows


def standing_ids(header, rows):
    ents = header.get("entity_rng") or []
    ids = []
    for e in ents:
        t = e.get("type", "")
        if t not in PASSIVE:
            continue
        eid = int(e["eid"])
        x0, y0, z0 = e["x"], e["y"], e["z"]
        moved = False
        for row in rows:
            hit = None
            for er in row.get("erng") or []:
                if int(er.get("eid", -1)) == eid:
                    hit = er
                    break
            if hit is None:
                continue
            if hit["x"] != x0 or hit["y"] != y0 or hit["z"] != z0:
                moved = True
                break
        if not moved:
            ids.append(eid)
    return ids


def write_fixture(path: Path, header, rows, stand):
    by_id = {int(e["eid"]): e for e in (header.get("entity_rng") or [])}
    px, py, pz = header["x"], header["y"], header["z"]
    pyaw, ppitch = header.get("yaw", 0.0), header.get("pitch", 0.0)
    n = 0
    lines = [
        f"seed {int(header.get('seed', 0))}",
        f"ticks {len(rows)}",
        f"player {px} {py} {pz} {pyaw} {ppitch}",
    ]
    body = []
    for eid in stand:
        e = by_id[eid]
        kind = PASSIVE[e["type"]]
        body.append(
            "e {eid} {kind} {x} {y} {z} {yaw} {pitch} {hyaw} {seed48} "
            "{lst} {age} {tt} {tasks} {watch} {idle} {ix} {iz} {eat} {egg} {og}".format(
                eid=eid, kind=kind,
                x=e["x"], y=e["y"], z=e["z"],
                yaw=e.get("yaw", 0.0), pitch=e.get("pitch", 0.0),
                hyaw=e.get("hyaw", e.get("yaw", 0.0)),
                seed48=int(e["seed48"]),
                lst=int(e.get("lst", 0)), age=int(e.get("age", 0)),
                tt=int(e.get("tt", 0)), tasks=int(e.get("tasks", 0)),
                watch=int(e.get("watch", 0)), idle=int(e.get("idle", 0)),
                ix=e.get("ix", 0.0), iz=e.get("iz", 0.0),
                eat=int(e.get("eat", 0)), egg=int(e.get("egg", -1)),
                og=int(e.get("og", 1)),
            )
        )
        n += 1
    lines.append(f"n {n}")
    lines.extend(body)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return n


def magma_by_tick(path: Path):
    out = {}
    with path.open(encoding="utf-8") as fh:
        for line in fh:
            rec = json.loads(line)
            out.setdefault(int(rec["t"]), {})[int(rec["eid"])] = rec
    return out


def tape_erng_by_tick(rows):
    out = {}
    for rec in rows:
        t = int(rec["t"])
        out[t] = {int(e["eid"]): e for e in (rec.get("erng") or [])}
    return out


def first_div(stand, rows, mag, tape_erng):
    fields = (
        ("x", "x", "d"),
        ("y", "y", "d"),
        ("z", "z", "d"),
        ("yaw", "yaw", "f"),
        ("pitch", "pitch", "f"),
        ("hyaw", "hyaw", "f"),
    )
    for rec in rows:
        t = int(rec["t"])
        if t not in mag:
            return t, None, "missing_magma_tick", None, None, None
        for eid in stand:
            te = tape_erng.get(t, {}).get(eid)
            me = mag[t].get(eid)
            if te is None:
                continue
            if me is None:
                return t, eid, "missing_magma_ent", None, None, None
            for tf, mf, kind in fields:
                tv, mv = te.get(tf), me.get(mf)
                if tv is None or mv is None:
                    continue
                if kind == "d":
                    if f64_bits(tv) == f64_bits(mv):
                        continue
                else:
                    if f32_bits(tv) == f32_bits(mv):
                        continue
                ts, ms = int(te.get("seed48", -1)), int(me.get("seed48", -1))
                delta = draws_between(ts, ms) if ts >= 0 and ms >= 0 else None
                return t, eid, tf, tv, mv, (ts, ms, delta)
    return None, None, None, None, None, None


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: detmob_gate.py TAPE.jsonl", file=sys.stderr)
        return 2
    tape = Path(argv[1]).resolve()
    header, rows = load_tape(tape)
    if header is None:
        print("BLOCKED  no tape header")
        return 2
    if not header.get("det_entity_rng"):
        print("BLOCKED  header missing det_entity_rng=1  (re-record with the switch on)")
        return 2
    if not header.get("entity_rng"):
        print("BLOCKED  header entity_rng empty  (no nearby living at recstart)")
        return 2
    if not rows:
        print("BLOCKED  tape has no tick rows")
        return 2
    stand = standing_ids(header, rows)
    if not stand:
        print("BLOCKED  no standing passives (cow/sheep/pig/chicken with constant pos)")
        return 2
    outdir = REPO / "out" / "verify" / "detmob"
    outdir.mkdir(parents=True, exist_ok=True)
    fixture = outdir / "fixture.txt"
    magma_out = outdir / "magma.jsonl"
    n = write_fixture(fixture, header, rows, stand)
    print(f"tape {tape.name}: {len(rows)} ticks, {n} standing passives {stand}")
    sh = REPO / "magma" / "game" / "detmob_gate.sh"
    rc = subprocess.call(["bash", str(sh), str(fixture), str(magma_out)], cwd=str(REPO / "magma"))
    if rc != 0:
        print(f"BLOCKED  detmob_gate.sh rc={rc}")
        return 2
    mag = magma_by_tick(magma_out)
    tape_e = tape_erng_by_tick(rows)
    t, eid, field, tv, mv, cur = first_div(stand, rows, mag, tape_e)
    if t is None:
        print(f"PASS  bit-equal pos/yaw/pitch/hyaw for {n} standing entities over {len(rows)} ticks")
        return 0
    extra = ""
    if cur is not None:
        ts, ms, delta = cur
        extra = f"  entity_rand tape=0x{ts:012x} magma=0x{ms:012x} draws_between={delta}"
    print(f"FAIL  first_div tick={t} eid={eid} field={field} tape={tv} magma={mv}{extra}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
