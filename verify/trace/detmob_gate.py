#!/usr/bin/env python3
"""detmob_gate.py — magma live vs tape pose compare (det_entity_rng=1).

PASS = bit-equal pos/yaw/pitch/hyaw over the window for nearby passives
and zombie/skeleton/creeper.
Otherwise prints the first divergent (tick, entity, field) and Entity.rand
cursor delta. Not wired into `make test`.

    uv run --no-project python verify/trace/detmob_gate.py TAPE.jsonl
"""
from __future__ import annotations

import json
import math
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

HOSTILE = {
    "EntityZombie": "zombie",
    "EntitySkeleton": "skeleton",
    "EntityCreeper": "creeper",
    "zombie": "zombie",
    "skeleton": "skeleton",
    "creeper": "creeper",
}

DIM_MOBS = {
    "EntityBlaze": "blaze",
    "EntityPigZombie": "pigman",
    "EntityEnderman": "enderman",
    "blaze": "blaze",
    "pigman": "pigman",
    "zombie_pigman": "pigman",
    "enderman": "enderman",
}

TRACKED = {**PASSIVE, **HOSTILE, **DIM_MOBS}


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


def _erng_map(rec):
    return {int(e["eid"]): e for e in (rec.get("erng") or [])}


def _snap_key(ents, stand):
    key = []
    for eid in stand:
        e = ents.get(eid)
        if e is None:
            key.append((eid, None))
            continue
        key.append((
            eid, int(e.get("seed48", -1)),
            e.get("x"), e.get("y"), e.get("z"),
            e.get("yaw"), e.get("pitch"), e.get("hyaw"),
            int(e.get("lst", 0)), int(e.get("age", 0)), int(e.get("tt", 0)),
        ))
    return tuple(key)


def unique_server_rows(rows, stand):
    """Drop extra client ticks that reprint the same server Entity.rand snapshot."""
    out = []
    prev = None
    for rec in rows:
        ents = _erng_map(rec)
        if not ents:
            continue
        key = _snap_key(ents, stand)
        if key == prev:
            continue
        out.append(rec)
        prev = key
    return out


def tracked_ids(header, rows):
    """Passives + zombie/skeleton/creeper + blaze/pigman in the recstart snapshot.

    Populate hostiles can sit in the 64-block erng sphere. If any hostile is
    within 16 of the parked player (target tape), keep only those; ambient
    summons sit ~46 blocks out so the near set is empty and all three stay.
    DIM-1 fortress spawners can emit extra blazes into the 64-block erng
    radius; PersistenceRequired marks the summoned ambient subjects.
    DIM 1 End: header EntityEnderman (pr may be omitted); the dragon is
    EntityDragon / EntityLiving in the same sphere and is not tracked.
    """
    _ = rows
    ents = header.get("entity_rng") or []
    px = float(header.get("x", 0.0))
    py = float(header.get("y", 0.0))
    pz = float(header.get("z", 0.0))
    dim = int(header.get("dim", 0))
    passives = []
    hostiles = []
    for e in ents:
        t = e.get("type", "")
        eid = int(e["eid"])
        if t in PASSIVE:
            passives.append(eid)
        elif t in DIM_MOBS:
            kind = DIM_MOBS[t]
            # blaze/pigman: persist filter drops fortress-spawner extras.
            # enderman: recorder on this End tape omits pr; PersistenceRequired
            # still zeros entityAge. Track End (dim=1) endermen only —
            # overworld ambient T181540Z header has stray eid=3173.
            # Dragon is EntityDragon, not in DIM_MOBS.
            if kind == "enderman":
                if dim == 1:
                    passives.append(eid)
            elif int(e.get("pr") or 0) == 1:
                passives.append(eid)
        elif t in HOSTILE:
            dx = float(e["x"]) - px
            dy = float(e["y"]) - py
            dz = float(e["z"]) - pz
            dist = (dx * dx + dy * dy + dz * dz) ** 0.5
            hostiles.append((dist, eid))
    ids = list(passives)
    if hostiles:
        near = [eid for dist, eid in hostiles if dist <= 16.0]
        ids.extend(near if near else [eid for _, eid in hostiles])
    return ids


def _header_g(header, eid):
    """3x3x3 block-id stencil from recstart entity_rng[].g (dx, dz, dy)."""
    for e in header.get("entity_rng") or []:
        if int(e.get("eid", -1)) != eid:
            continue
        g = e.get("g")
        if isinstance(g, list) and len(g) == 27:
            bx = int(math.floor(float(e["x"])))
            by = int(math.floor(float(e["y"])))
            bz = int(math.floor(float(e["z"])))
            return bx, by, bz, [int(v) for v in g]
    return None


def _clock0(stand, series):
    hmap = _erng_map(series[0])
    t0s = [int(hmap[eid]["tt"]) for eid in stand if eid in hmap and hmap[eid].get("tt") is not None]
    t1s = _field_vals(stand, series[-1], "tt")
    use_tt = bool(t0s) and bool(t1s) and min(t0s) >= 0 and max(t1s) - min(t0s) > 0
    clock_name = "tt" if use_tt else "age"
    clock0 = {eid: int(hmap[eid].get(clock_name, 0)) for eid in stand if eid in hmap}
    return clock_name, clock0


def _row_magma_tick(stand, rec, clock_name, clock0):
    te_map = _erng_map(rec)
    for eid in stand:
        te = te_map.get(eid)
        if te is None or eid not in clock0:
            continue
        mag = int(te.get(clock_name, 0)) - clock0[eid] - 1
        if mag >= 0:
            return mag
    return None


def _atk_magma_ticks(stand, series):
    """Map tape attack onto magma ticks (clock-hydrate-1).

    Prefer in.atk. Early mcwindow clicks can land a hit (hp drop + knockback)
    without the keybind bit making the jsonl in.atk field; those still need a
    pulse or the gate never replays the punch.
    """
    clock_name, clock0 = _clock0(stand, series)
    out = []
    seen = set()
    for rec in series[1:]:
        if not int((rec.get("in") or {}).get("atk", 0)):
            continue
        mag = _row_magma_tick(stand, rec, clock_name, clock0)
        if mag is not None and mag not in seen:
            seen.add(mag)
            out.append(mag)
    if out:
        return out
    hmap = _erng_map(series[0])
    prev = {eid: hmap[eid].get("hp") for eid in stand if eid in hmap}
    for rec in series[1:]:
        te_map = _erng_map(rec)
        dropped = False
        for eid in stand:
            te = te_map.get(eid)
            if te is None or eid not in prev:
                continue
            hp, ph = te.get("hp"), prev[eid]
            if hp is not None and ph is not None and float(hp) < float(ph) - 1e-6:
                dropped = True
            if hp is not None:
                prev[eid] = hp
        if not dropped:
            continue
        mag = _row_magma_tick(stand, rec, clock_name, clock0)
        if mag is not None and mag not in seen:
            seen.add(mag)
            out.append(mag)
    return out


def _player_pose_keyframes(stand, series):
    """Tape player pose changes mapped onto magma ticks.

    EntityAIWander.getAge() is EntityLivingBase.entityAge. despawnEntity
    zeros it only while a player is inside 32 blocks. A parked header pose
    misses later /tp or knockback, so magma keeps age=0 and extra nextInt(120).
    """
    if not series:
        return []
    clock_name, clock0 = _clock0(stand, series)
    h = series[0]
    last = (
        float(h.get("x", 0.0)), float(h.get("y", 0.0)), float(h.get("z", 0.0)),
        float(h.get("yaw", 0.0)), float(h.get("pitch", 0.0)),
    )
    out = []
    for rec in series[1:]:
        mag = _row_magma_tick(stand, rec, clock_name, clock0)
        if mag is None:
            continue
        pose = (
            float(rec.get("x", last[0])), float(rec.get("y", last[1])),
            float(rec.get("z", last[2])), float(rec.get("yaw", last[3])),
            float(rec.get("pitch", last[4])),
        )
        if pose != last:
            out.append((mag, pose))
            last = pose
    return out


def write_fixture(path: Path, header, hydrate, n_ticks, stand, atk_ticks=None,
                  player_poses=None, series0=None):
    src = series0 if series0 is not None else header
    px, py, pz = src["x"], src["y"], src["z"]
    pyaw, ppitch = src.get("yaw", header.get("yaw", 0.0)), src.get("pitch", header.get("pitch", 0.0))
    n = 0
    lines = [
        f"seed {int(header.get('seed', 0))}",
        f"time {int(header.get('world_time', 6000))}",
        f"ticks {n_ticks}",
        f"dim {int(header.get('dim', 0))}",
        f"player {px} {py} {pz} {pyaw} {ppitch}",
    ]
    body = []
    ground = []
    hostiles_h = []
    for eid in stand:
        e = hydrate[eid]
        kind = TRACKED[e["type"]]
        hyaw = e.get("hyaw", e.get("yaw", 0.0))
        ryaw = e.get("ryaw", hyaw)
        hp = e.get("hp", 0.0)
        init48 = 0
        for he in header.get("entity_rng") or []:
            if int(he.get("eid", -1)) == eid:
                init48 = int(he.get("seed48_init") or 0)
                break
        gv_bits = int(e.get("gv", 0) or 0)
        body.append(
            "e {eid} {kind} {x} {y} {z} {yaw} {pitch} {hyaw} {seed48} "
            "{lst} {age} {tt} {tasks} {watch} {idle} {ix} {iz} {eat} {egg} {og} "
            "{ryaw} {bhp} {bht} {hp} {init48} {hg} {gv} {hot} {hof} {pr} {anger}".format(
                eid=eid, kind=kind,
                x=e["x"], y=e["y"], z=e["z"],
                yaw=e.get("yaw", 0.0), pitch=e.get("pitch", 0.0),
                hyaw=hyaw,
                seed48=int(e["seed48"]),
                lst=int(e.get("lst", 0)), age=int(e.get("age", 0)),
                tt=int(e.get("tt", 0)), tasks=int(e.get("tasks", 0)),
                watch=int(e.get("watch", 0)), idle=int(e.get("idle", 0)),
                ix=e.get("ix", 0.0), iz=e.get("iz", 0.0),
                eat=int(e.get("eat", 0)), egg=int(e.get("egg", -1)),
                og=int(e.get("og", 1)),
                ryaw=ryaw,
                bhp=e.get("bhp", hyaw),
                bht=int(e.get("bht", 0)),
                hp=hp,
                init48=init48,
                hg=int(e.get("hg", 0) or 0),
                gv=gv_bits,
                hot=int(e.get("hot", 0) or 0),
                hof=float(e.get("hof", 0.5) if e.get("hof") is not None else 0.5),
                pr=(1 if kind == "enderman" else int(e.get("pr", 0) or 0)),
                anger=int(e.get("anger", 0) or 0),
            )
        )
        ginfo = _header_g(header, eid)
        if ginfo is not None:
            bx, by, bz, ids = ginfo
            ground.append("g {eid} {bx} {by} {bz} {ids}".format(
                eid=eid, bx=bx, by=by, bz=bz,
                ids=" ".join(str(v) for v in ids)))
        if e.get("type", "") in HOSTILE:
            hostiles_h.append(
                "h {eid} {ttt} {ttasks} {tgt} {fuse} {mdelay} "
                "{see} {stime} {atime} {scw} {sback} {cstate}".format(
                    eid=eid,
                    ttt=int(e.get("ttt", 0)),
                    ttasks=int(e.get("ttasks", 0)),
                    tgt=int(e.get("tgt", 0)),
                    fuse=int(e.get("fuse", 0)),
                    mdelay=int(e.get("mdelay", 0)),
                    see=int(e.get("see", 0)),
                    stime=int(e.get("stime", -1)),
                    atime=int(e.get("atime", -1)),
                    scw=int(e.get("scw", 0)),
                    sback=int(e.get("sback", 0)),
                    cstate=int(e.get("cstate", -1)),
                )
            )
        n += 1
    lines.append(f"n {n}")
    lines.extend(body)
    lines.extend(ground)
    lines.extend(hostiles_h)
    for mag_t in atk_ticks or []:
        lines.append(f"atk {int(mag_t)}")
    for mag_t, pose in player_poses or []:
        lines.append(
            "pl {t} {x} {y} {z} {yaw} {pitch}".format(
                t=int(mag_t), x=pose[0], y=pose[1], z=pose[2],
                yaw=pose[3], pitch=pose[4],
            )
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return n, len(ground)


def magma_by_tick(path: Path):
    out = {}
    ground = {}
    with path.open(encoding="utf-8") as fh:
        for line in fh:
            rec = json.loads(line)
            if rec.get("ground"):
                ground[int(rec["eid"])] = rec
                continue
            out.setdefault(int(rec["t"]), {})[int(rec["eid"])] = rec
    return out, ground


def walked(stand, series):
    """True if any tracked passive moved more than a look-idle jitter."""
    h = _erng_map(series[0])
    best = 0.0
    who = None
    for rec in series[1:]:
        te = _erng_map(rec)
        for eid in stand:
            a, b = h.get(eid), te.get(eid)
            if a is None or b is None:
                continue
            dx = float(b["x"]) - float(a["x"])
            dz = float(b["z"]) - float(a["z"])
            d = (dx * dx + dz * dz) ** 0.5
            if d > best:
                best = d
                who = eid
    return who, best


def tape_erng_by_tick(rows):
    out = {}
    for rec in rows:
        t = int(rec["t"])
        out[t] = {int(e["eid"]): e for e in (rec.get("erng") or [])}
    return out


def _field_vals(stand, rec, name):
    te = _erng_map(rec)
    out = []
    for eid in stand:
        e = te.get(eid)
        if e is None:
            continue
        out.append(int(e.get(name, -1)))
    return out


def magma_tick_count(stand, series):
    """Server ticks between hydrate and last snap. Client hitches skip jsonl
    rows; magma still has to step those ticks to keep Entity.rand in phase.

    Prefer EntityAITasks.tickCount: it increments every tick. entityAge
    resets inside 32 blocks, so it is not a clock on the wander tape.
    """
    t0 = _field_vals(stand, series[0], "tt")
    t1 = _field_vals(stand, series[-1], "tt")
    if t0 and t1 and min(t0) >= 0 and max(t1) - min(t0) > 0:
        return max(t1) - min(t0)
    a0 = _field_vals(stand, series[0], "age")
    a1 = _field_vals(stand, series[-1], "age")
    if a0 and a1 and min(a0) >= 0 and max(a1) - min(a0) > 0:
        return max(a1) - min(a0)
    return len(series) - 1


def first_div(stand, series, mag):
    """series[0] is hydrate; magma dump t is clock-hydrate-1 (tt, else age).

    Client frames can miss a server tick (clock jumps by 2). Magma still ran
    that tick; we only compare tape rows that exist. Per-entity clocks so a
    despawned or age-reset mob does not collapse the index.
    """
    fields = (
        ("x", "x", "d"),
        ("y", "y", "d"),
        ("z", "z", "d"),
        ("yaw", "yaw", "f"),
        ("pitch", "pitch", "f"),
        ("hyaw", "hyaw", "f"),
    )
    hmap = _erng_map(series[0])
    t0s = [int(hmap[eid]["tt"]) for eid in stand if eid in hmap and hmap[eid].get("tt") is not None]
    t1s = _field_vals(stand, series[-1], "tt")
    use_tt = bool(t0s) and bool(t1s) and min(t0s) >= 0 and max(t1s) - min(t0s) > 0
    clock_name = "tt" if use_tt else "age"
    clock0 = {eid: int(hmap[eid].get(clock_name, 0)) for eid in stand if eid in hmap}
    for rec in series[1:]:
        tape_t = int(rec["t"])
        te_map = _erng_map(rec)
        for eid in stand:
            te = te_map.get(eid)
            if te is None or eid not in clock0:
                continue
            mag_t = int(te.get(clock_name, 0)) - clock0[eid] - 1
            if mag_t not in mag:
                return tape_t, eid, "missing_magma_tick", None, None, None
            me = mag[mag_t].get(eid)
            if me is None:
                return tape_t, eid, "missing_magma_ent", None, None, None
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
                return tape_t, eid, tf, tv, mv, (ts, ms, delta)
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
    stand = tracked_ids(header, rows)
    if not stand:
        print("BLOCKED  no tracked living (passives, zombie/skeleton/creeper, blaze/pigman, or enderman) in header entity_rng")
        return 2
    series = unique_server_rows(rows, stand)
    if len(series) < 2:
        print("BLOCKED  fewer than 2 unique server Entity.rand snapshots")
        return 2
    hydrate = _erng_map(series[0])
    missing = [eid for eid in stand if eid not in hydrate]
    if missing:
        print(f"BLOCKED  standing eids missing from first erng: {missing}")
        return 2
    n_ticks = magma_tick_count(stand, series)
    atk_ticks = _atk_magma_ticks(stand, series)
    outdir = REPO / "out" / "verify" / "detmob"
    outdir.mkdir(parents=True, exist_ok=True)
    fixture = outdir / "fixture.txt"
    magma_out = outdir / "magma.jsonl"
    poses = _player_pose_keyframes(stand, series)
    n, n_ground = write_fixture(
        fixture, header, hydrate, n_ticks, stand, atk_ticks,
        player_poses=poses, series0=series[0],
    )
    print(
        f"tape {tape.name}: {len(rows)} client ticks, {len(series)} unique server "
        f"snaps, {n_ticks} magma ticks, {n} tracked {stand}, "
        f"dim={int(header.get('dim', 0))}, ground_stencil={n_ground}, atk_ticks={atk_ticks}"
    )
    sh = REPO / "magma" / "game" / "detmob_gate.sh"
    rc = subprocess.call(["bash", str(sh), str(fixture), str(magma_out)], cwd=str(REPO / "magma"))
    if rc == 3:
        print("FAIL  first_div ground_stencil  magma worldgen != tape header g")
        return 1
    if rc != 0:
        print(f"BLOCKED  detmob_gate.sh rc={rc}")
        return 2
    mag, ground = magma_by_tick(magma_out)
    if n_ground == 0:
        print("WARN  tape header has no g stencil; magma dump follows (re-record to pin)")
        for eid in stand:
            rec = ground.get(eid)
            if rec:
                print(f"  magma g eid={eid} ({rec['bx']},{rec['by']},{rec['bz']}) ids={rec['ids']}")
    t, eid, field, tv, mv, cur = first_div(stand, series, mag)
    if t is None:
        who, dist = walked(stand, series)
        walk_s = (
            f"walked eid={who} xz={dist:.6g}" if dist >= 0.05 else "standing"
        )
        print(
            f"PASS  bit-equal pos/yaw/pitch/hyaw for {n} tracked "
            f"over {n_ticks} server ticks  {walk_s}"
        )
        return 0
    extra = ""
    if cur is not None:
        ts, ms, delta = cur
        extra = f"  entity_rand tape=0x{ts:012x} magma=0x{ms:012x} draws_between={delta}"
    print(f"FAIL  first_div tick={t} eid={eid} field={field} tape={tv} magma={mv}{extra}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
