#!/usr/bin/env python3
"""diff_trace.py - align two per-tick traces and report the FIRST divergence PER FEATURE.

TWO modes, auto-selected by file extension:

  STATE mode (*.jsonl): the FULL per-tick state-vector diff. Reads java_state.jsonl (ground
    truth) vs c_state.jsonl (magma) and reports, for EACH feature/category:
      - MATCHES throughout, or
      - DIVERGES at tick T (which field, magnitude), or
      - UNSIMULATED on the C side (C emits null for the whole category -- a critical finding:
        the magma game does not simulate that feature at all).
    Categories: player physics / look / vitals / air / fire / xp / fall_distance / flags /
    held-item / combat-timers / death, plus INVENTORY, ENTITIES, TIME-WEATHER.

  PHYS mode (*.csv): the legacy compact physics diff (java_phys.csv vs c_phys.csv) with
    optional --materialize to dump the C frames around the first divergence. Kept for
    back-compat with frame_oracle.py / world_diff.py.

Numeric compares: ints / bools / on_ground EXACT; floats atol+rtol (|a-b| <= atol+rtol|b|);
yaw/pitch as angular difference (wraps at 360). A trace diffed against a COPY of itself must
report ZERO divergence (tool self-check).

Usage:
    python diff_trace.py --java trace/out/java_state.jsonl --c trace/out/c_state.jsonl
    python diff_trace.py --java trace/out/java_phys.csv  --c trace/out/c_phys.csv --materialize
"""
import argparse
from pathlib import Path
import csv
import json
import os
import subprocess
import sys

try:
    import nbt_codec
except ModuleNotFoundError as exc:
    if exc.name != "nbt_codec":
        raise
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import nbt_codec

# ---------- shared numeric helpers ----------
ANGLE_FIELDS = {"yaw", "pitch"}


def angdiff(a, b):
    return (a - b + 180.0) % 360.0 - 180.0


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def scalar_delta(name, a, b):
    if name in ANGLE_FIELDS and is_num(a) and is_num(b):
        return abs(angdiff(float(a), float(b)))
    if is_num(a) and is_num(b):
        return abs(float(a) - float(b))
    return 0.0 if a == b else float("inf")


def scalar_ok(name, a, b, atol, rtol):
    if isinstance(a, bool) or isinstance(b, bool):
        return bool(a) == bool(b)
    if is_num(a) and is_num(b):
        # ints compared exactly; floats with tolerance
        if isinstance(a, int) and isinstance(b, int):
            return a == b
        d = scalar_delta(name, a, b)
        return d <= atol + rtol * abs(float(b))
    return a == b


# ============================ STATE mode (JSONL) ============================

# player sub-features: (label, [field names])
PLAYER_FEATURES = [
    ("player.physics",      ["x", "y", "z", "vx", "vy", "vz", "on_ground"]),
    ("player.look",         ["yaw", "pitch"]),
    ("player.vitals",       ["health", "food", "saturation",
                              "food_exhaustion", "food_timer"]),
    ("player.health_attributes", ["max_health", "absorption"]),
    ("player.air",          ["air"]),
    ("player.fire",         ["fire"]),
    ("player.xp",           ["xp_level", "xp_frac", "xp_total"]),
    ("player.fall_distance",["fall_distance"]),
    ("player.flags",        ["sprinting", "sneaking", "jumping"]),
    ("player.held_item",    ["held_slot", "held_id", "held_count", "held_meta"]),
    ("player.attack_cooldown",["attack_cooldown", "attack_ticks"]),
    ("player.hurt_time",    ["hurt_time", "hurt_resistant_time"]),
    ("player.death_time",   ["death_time"]),
    ("player.potions",      ["potions"]),
    ("player.death",        ["dead", "deaths", "dim"]),
]


def read_jsonl(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


class FeatureResult:
    def __init__(self, label):
        self.label = label
        self.status = "match"      # match | diverge | unsim | java_missing
        self.first_tick = None
        self.first_field = None
        self.first_delta = None
        self.max_delta = 0.0
        self.note = ""

    def line(self):
        if self.status == "match":
            return f"  {self.label:24s} MATCHES{('  (' + self.note + ')') if self.note else ''}"
        if self.status == "unsim":
            return f"  {self.label:24s} UNSIMULATED on C  ({self.note})"
        if self.status == "diverge":
            return (f"  {self.label:24s} DIVERGES @tick {self.first_tick} "
                    f"field={self.first_field} |delta|={self.first_delta:.6g} "
                    f"(max|delta| over run={self.max_delta:.6g})")
        return f"  {self.label:24s} {self.status}  ({self.note})"


def diff_scalar_feature(label, fields, ja, cc, n, atol, rtol):
    r = FeatureResult(label)
    # UNSIMULATED detection: C null for the whole feature across the run while Java has values.
    c_all_null = True
    j_has_val = False
    for i in range(n):
        for f in fields:
            cv = cc[i]["player"].get(f)
            jv = ja[i]["player"].get(f)
            if cv is not None:
                c_all_null = False
            if jv is not None:
                j_has_val = True
    if c_all_null and j_has_val:
        r.status = "unsim"
        # sample the Java value + range for the first field
        f0 = fields[0]
        vals = [ja[i]["player"].get(f0) for i in range(n) if is_num(ja[i]["player"].get(f0))]
        if vals:
            r.note = f"Java {f0} in [{min(vals):.4g}, {max(vals):.4g}]; C emits null"
        else:
            r.note = f"Java tracks {fields}; C emits null"
        return r
    # numeric per-tick compare
    for i in range(n):
        tick = ja[i]["tick"]
        for f in fields:
            a = ja[i]["player"].get(f)
            b = cc[i]["player"].get(f)
            if b is None and a is not None:
                # partially unsimulated field inside an otherwise-simulated feature
                if r.status == "match":
                    r.status, r.first_tick, r.first_field = "diverge", tick, f
                    r.first_delta = float("inf")
                    r.note = f"C null for {f}"
                continue
            d = scalar_delta(f, a, b)
            if d > r.max_delta:
                r.max_delta = d
            if not scalar_ok(f, a, b, atol, rtol) and r.status == "match":
                r.status, r.first_tick, r.first_field, r.first_delta = "diverge", tick, f, d
    return r


def inv_map(row):
    """slot -> (id,count,meta,ordered enchantments) for all player slots."""
    d = {}
    for it in (row.get("inventory") or []):
        enchants = tuple(tuple(pair) for pair in it.get("enchants", []))
        d[it["slot"]] = (
            it.get("id"), it.get("count"), it.get("meta"), enchants)
    return d


def diff_inventory(ja, cc, n):
    r = FeatureResult("inventory")
    # id NAMESPACE caveat: Java uses vanilla registry ids, C uses blaze IC_* ids. So we
    # report separately: occupancy (which slots filled) + counts (id-agnostic) vs full tuple.
    first_occ = first_cnt = first_full = None
    for i in range(n):
        tick = ja[i]["tick"]
        jm, cm = inv_map(ja[i]), inv_map(cc[i])
        if set(jm) != set(cm) and first_occ is None:
            first_occ = tick
        # counts per common slot
        for s in set(jm) & set(cm):
            if jm[s][1] != cm[s][1] and first_cnt is None:
                first_cnt = tick
        if jm != cm and first_full is None:
            first_full = tick
    if first_full is None:
        r.status = "match"
        r.note = "all 41 slots, counts, ids, metadata, and enchantments identical"
    else:
        r.status = "diverge"
        r.first_tick = first_full
        r.first_field = "slots"
        r.first_delta = float("inf")
        occ = "same" if first_occ is None else f"@{first_occ}"
        cnt = "same" if first_cnt is None else f"@{first_cnt}"
        r.note = (f"occupancy {occ}, counts {cnt}; NOTE id namespaces differ "
                  f"(Java vanilla-registry vs C blaze IC_*)")
        r.first_field = f"full-tuple (occ {occ}, cnt {cnt})"
    return r


def entities_within(row, radius):
    entities = row.get("entities")
    if entities is None:
        return None
    radius2 = radius * radius
    return [
        entity for entity in entities
        if (float(entity.get("dx", 0.0)) ** 2
            + float(entity.get("dy", 0.0)) ** 2
            + float(entity.get("dz", 0.0)) ** 2) <= radius2
    ]


def diff_entities(ja, cc, n, radius, atol=1e-6, rtol=1e-6):
    r = FeatureResult("entities")
    c_all_null = all((cc[i].get("entities") is None) for i in range(n))
    j_near = [entities_within(ja[i], radius) or [] for i in range(n)]
    c_near = [entities_within(cc[i], radius) for i in range(n)]
    j_counts = [len(entities) for entities in j_near]
    j_max = max(j_counts) if j_counts else 0
    if c_all_null and j_max > 0:
        first_nonzero = next((ja[i]["tick"] for i in range(n) if j_counts[i] > 0), None)
        types = sorted({e.get("type") for entities in j_near for e in entities})
        r.status = "unsim"
        r.note = (f"Java carried up to {j_max} entities (first nonzero @tick {first_nonzero}); "
                  f"within {radius:g} blocks; types={types[:8]}"
                  f"{'...' if len(types) > 8 else ''}; C emits null")
        return r
    if c_all_null and j_max == 0:
        r.status = "match"
        r.note = "both empty (no entities near spawn)"
        return r
    # Controlled fixtures preserve the authoritative Java eid on C. Emergent
    # falling blocks use origin+block state instead: integrated-client entity
    # construction shares Java's process-global nextEntityID and can consume
    # unrelated IDs outside the represented world slice.
    numeric_fields = (
        "x", "y", "z", "dx", "dy", "dz",
        "vx", "vy", "vz", "ax", "ay", "az", "yaw", "pitch", "health",
        "item", "count", "value", "age", "pickup_delay", "color",
        "hurt_time", "death_time", "hurt_resistant_time",
        "profession", "growing_age", "career", "career_level",
        "living_sound_time", "offers_initialized", "entity_seed48",
        "entity_have_gaussian", "entity_gaussian",
        "block", "meta", "fall_time",
        "origin_x", "origin_y", "origin_z",
        "fuse", "inner_rotation", "show_bottom",
        "has_beam", "beam_x", "beam_y", "beam_z",
    )
    for i in range(n):
        jm = {
            e.get("identity") or e.get("eid"): e
            for e in j_near[i]
        }
        cm = {
            e.get("identity") or e.get("eid"): e
            for e in (c_near[i] or [])
        }
        if set(jm) != set(cm):
            r.status, r.first_tick = "diverge", ja[i]["tick"]
            r.first_field, r.first_delta = "entity-set", float("inf")
            r.note = f"Java {len(jm)} vs C {len(cm)} entities"
            return r
        for eid in sorted(jm, key=str):
            if jm[eid].get("type") != cm[eid].get("type"):
                r.status, r.first_tick = "diverge", ja[i]["tick"]
                r.first_field = f"eid={eid}.type"
                r.first_delta = float("inf")
                return r
            if jm[eid].get("stack_payload") \
                    != cm[eid].get("stack_payload"):
                r.status, r.first_tick = "diverge", ja[i]["tick"]
                r.first_field = f"eid={eid}.stack_payload"
                r.first_delta = float("inf")
                r.note = (
                    f"Java {jm[eid].get('stack_payload')!r} vs "
                    f"C {cm[eid].get('stack_payload')!r}"
                )
                return r
            for field in numeric_fields:
                if (jm[eid].get("type") in (
                        "EntityXPOrb", "EntitySmallFireball")
                        and field in ("yaw", "pitch")):
                    # Both renderers are camera-facing; constructor/smoothed
                    # entity rotation does not participate in motion or pixels.
                    continue
                a = jm[eid].get(field)
                b = cm[eid].get(field)
                if a is None and b is None:
                    continue
                delta = scalar_delta(field, a, b)
                if delta > r.max_delta:
                    r.max_delta = delta
                if not scalar_ok(field, a, b, atol, rtol):
                    r.status, r.first_tick = "diverge", ja[i]["tick"]
                    r.first_field = f"eid={eid}.{field}"
                    r.first_delta = delta
                    return r
    r.status = "match"
    r.note = f"entity sets and state exact within {radius:g} blocks"
    return r


def diff_time(ja, cc, n):
    r = FeatureResult("time_weather")
    c_all_null = all((cc[i].get("time") in (None, {}) or
                      all(v is None for v in (cc[i].get("time") or {}).values())) for i in range(n))
    jt0 = ja[0].get("time") or {}
    jtN = ja[n - 1].get("time") or {}
    if c_all_null and jt0:
        wt0 = jt0.get("world_time")
        wtN = jtN.get("world_time")
        adv = (wt0 is not None and wtN is not None and wtN != wt0)
        rain = any((ja[i].get("time") or {}).get("raining") for i in range(n))
        thun = any((ja[i].get("time") or {}).get("thundering") for i in range(n))
        r.status = "unsim"
        r.note = (f"Java world_time {wt0}->{wtN} ({'advances' if adv else 'frozen'}), "
                  f"moon={jt0.get('moon_phase')}, raining={rain}, thundering={thun}; C emits null")
        return r
    # both present: compare
    for i in range(n):
        jt = ja[i].get("time") or {}
        ct = cc[i].get("time") or {}
        for k in (
            "world_time", "total_time", "moon_phase", "raining",
            "thundering", "rain_time", "thunder_time",
            "clean_weather_time", "do_weather_cycle",
            "do_daylight_cycle", "prev_rain_strength", "rain_strength",
            "prev_thunder_strength", "thunder_strength",
        ):
            if jt.get(k) != ct.get(k):
                r.status, r.first_tick, r.first_field = "diverge", ja[i]["tick"], k
                r.first_delta = float("inf")
                return r
    r.status = "match"
    return r


JAVA_RANDOM_MASK = (1 << 48) - 1
JAVA_RANDOM_MULTIPLIER = 0x5DEECE66D
JAVA_RANDOM_ADDEND = 0xB


def java_random_transition_count(before, after, limit=1_000_000):
    if (not isinstance(before, int) or not isinstance(after, int)
            or not 0 <= before <= JAVA_RANDOM_MASK
            or not 0 <= after <= JAVA_RANDOM_MASK):
        return None
    seed = before
    for steps in range(limit + 1):
        if seed == after:
            return steps
        seed = (seed * JAVA_RANDOM_MULTIPLIER
                + JAVA_RANDOM_ADDEND) & JAVA_RANDOM_MASK
    return None


def controlled_input_transition(bundle):
    before = bundle.get("before") if isinstance(bundle, dict) else None
    before_rng = before.get("world_rng") if isinstance(before, dict) else None
    after_rng = bundle.get("world_rng") if isinstance(bundle, dict) else None
    if not isinstance(before_rng, dict) or not isinstance(after_rng, dict):
        return None
    transitions = {}
    for key in ("java_seed48", "math_seed48", "block_seed48"):
        steps = java_random_transition_count(
            before_rng.get(key), after_rng.get(key))
        if steps is None:
            return None
        transitions[key] = steps
    before_eid = before.get("entity_id_cursor")
    after_eid = bundle.get("entity_id_cursor")
    before_lcg = before_rng.get("update_lcg")
    after_lcg = after_rng.get("update_lcg")
    if any(not isinstance(value, int) for value in (
            before_eid, after_eid, before_lcg, after_lcg)):
        return None
    transitions["entity_id_delta"] = after_eid - before_eid
    transitions["update_lcg_delta32"] = (
        (after_lcg - before_lcg) & 0xFFFFFFFF)
    return transitions


def diff_controlled_input(ja, cc, n):
    r = FeatureResult("controlled_input.causal")
    observed = 0
    for i in range(n):
        jt = ja[i].get("controlled_input")
        ct = cc[i].get("controlled_input")
        if jt is None and ct is None:
            continue
        observed += 1
        java_transition = controlled_input_transition(jt)
        c_transition = controlled_input_transition(ct)
        same_start = (
            isinstance(jt, dict) and isinstance(ct, dict)
            and jt.get("before") == ct.get("before"))
        if (java_transition is None or c_transition is None
                or java_transition != c_transition
                or (same_start and jt != ct)):
            r.status = "diverge"
            r.first_tick = ja[i]["tick"]
            r.first_field = "cursor-transition"
            r.first_delta = float("inf")
            r.note = (
                f"Java transition {java_transition!r} from {jt!r} vs "
                f"C transition {c_transition!r} from {ct!r}")
            return r
    r.note = (
        "exact uncontaminated cursor transitions, "
        f"observations={observed}")
    return r


def diff_scheduled_ticks(ja, cc, n):
    r = FeatureResult("scheduled_ticks.exact_subset")
    for i in range(n):
        jt = ja[i].get("scheduled_ticks")
        ct = cc[i].get("scheduled_ticks")
        if jt is None or ct is None:
            r.status = "unsim"
            r.first_tick = ja[i]["tick"]
            r.note = "one side omitted the pending-update subset"
            return r
        if jt != ct:
            r.status = "diverge"
            r.first_tick = ja[i]["tick"]
            r.first_field = "pending-list"
            r.first_delta = float("inf")
            r.note = f"Java {jt!r} vs C {ct!r}"
            return r
    r.status = "match"
    counts = [len(row.get("scheduled_ticks", [])) for row in ja[:n]]
    r.note = (
        f"exact ordered pending list, max={max(counts) if counts else 0}"
    )
    return r


def diff_redstone_torch_toggles(ja, cc, n):
    r = FeatureResult("redstone.torch_history")
    for i in range(n):
        java_complete = ja[i].get(
            "redstone_torch_toggles_complete") is True
        magma_complete = cc[i].get(
            "redstone_torch_toggles_complete") is True
        jt = ja[i].get("redstone_torch_toggles", [])
        ct = cc[i].get("redstone_torch_toggles", [])
        if not java_complete or not magma_complete or jt != ct:
            r.status = "diverge"
            r.first_tick = ja[i]["tick"]
            r.first_field = (
                "completeness" if not java_complete or not magma_complete
                else "toggle-list"
            )
            r.first_delta = float("inf")
            r.note = f"Java {jt!r} vs C {ct!r}"
            return r
    counts = [
        len(row.get("redstone_torch_toggles", []))
        for row in ja[:n]
    ]
    r.note = (
        f"exact chronological list, max={max(counts) if counts else 0}"
    )
    return r


def diff_comparators(ja, cc, n):
    r = FeatureResult("redstone.comparators")
    for i in range(n):
        java_complete = ja[i].get("comparators_complete") is True
        magma_complete = cc[i].get("comparators_complete") is True
        jt = ja[i].get("comparators", [])
        ct = cc[i].get("comparators", [])
        if not java_complete or not magma_complete or jt != ct:
            r.status = "diverge"
            r.first_tick = ja[i]["tick"]
            r.first_field = (
                "completeness" if not java_complete or not magma_complete
                else "tile-state-list"
            )
            r.first_delta = float("inf")
            r.note = f"Java {jt!r} vs C {ct!r}"
            return r
    counts = [len(row.get("comparators", [])) for row in ja[:n]]
    r.note = (
        f"exact comparator output tile state, max="
        f"{max(counts) if counts else 0}"
    )
    return r


def diff_moving_pistons(ja, cc, n):
    r = FeatureResult("tile_entities.moving_pistons")
    for i in range(n):
        java_complete = ja[i].get("moving_pistons_complete") is True
        magma_complete = cc[i].get("moving_pistons_complete") is True
        jt = ja[i].get("moving_pistons", [])
        ct = cc[i].get("moving_pistons", [])
        if not java_complete or not magma_complete or jt != ct:
            r.status = "diverge"
            r.first_tick = ja[i]["tick"]
            r.first_field = (
                "completeness" if not java_complete or not magma_complete
                else "moving-tile-list"
            )
            r.first_delta = float("inf")
            r.note = f"Java {jt!r} vs C {ct!r}"
            return r
    counts = [len(row.get("moving_pistons", [])) for row in ja[:n]]
    r.note = (
        "exact moving-piston tile state, max="
        f"{max(counts) if counts else 0}"
    )
    return r


def diff_containers(ja, cc, n):
    r = FeatureResult("tile_entities.containers")
    for i in range(n):
        java_complete = ja[i].get("containers_complete") is True
        magma_complete = cc[i].get("containers_complete") is True
        jt = ja[i].get("containers", [])
        ct = cc[i].get("containers", [])
        if not java_complete or not magma_complete or jt != ct:
            r.status = "diverge"
            r.first_tick = ja[i]["tick"]
            r.first_field = (
                "completeness" if not java_complete or not magma_complete
                else "inventory-list"
            )
            r.first_delta = float("inf")
            r.note = f"Java {jt!r} vs C {ct!r}"
            return r
    counts = [len(row.get("containers", [])) for row in ja[:n]]
    r.note = (
        f"exact supported container inventories, max="
        f"{max(counts) if counts else 0}"
    )
    return r


def diff_flower_pots(ja, cc, n):
    r = FeatureResult("tile_entities.flower_pots")
    for i in range(n):
        java_complete = ja[i].get("flower_pots_complete") is True
        magma_complete = cc[i].get("flower_pots_complete") is True
        jt = ja[i].get("flower_pots", [])
        ct = cc[i].get("flower_pots", [])
        if not java_complete or not magma_complete or jt != ct:
            r.status = "diverge"
            r.first_tick = ja[i]["tick"]
            r.first_field = (
                "completeness" if not java_complete or not magma_complete
                else "item-state-list"
            )
            r.first_delta = float("inf")
            r.note = f"Java {jt!r} vs C {ct!r}"
            return r
    counts = [len(row.get("flower_pots", [])) for row in ja[:n]]
    r.note = (
        "exact flower-pot item tile state, max="
        f"{max(counts) if counts else 0}"
    )
    return r


def diff_skulls(ja, cc, n):
    r = FeatureResult("tile_entities.skulls")
    for i in range(n):
        java_complete = ja[i].get("skulls_complete") is True
        magma_complete = cc[i].get("skulls_complete") is True
        try:
            jt = [
                {
                    **entry,
                    **({"owner_nbt": nbt_codec.canonical_hex(
                        entry.get("owner_nbt"))}
                       if entry.get("has_owner") is True else {}),
                }
                for entry in ja[i].get("skulls", [])
            ]
            ct = [
                {
                    **entry,
                    **({"owner_nbt": nbt_codec.canonical_hex(
                        entry.get("owner_nbt"))}
                       if entry.get("has_owner") is True else {}),
                }
                for entry in cc[i].get("skulls", [])
            ]
        except nbt_codec.NbtError as exc:
            r.status = "diverge"
            r.first_tick = ja[i]["tick"]
            r.first_field = "owner_nbt"
            r.first_delta = float("inf")
            r.note = f"invalid player-profile NBT: {exc}"
            return r
        if not java_complete or not magma_complete or jt != ct:
            r.status = "diverge"
            r.first_tick = ja[i]["tick"]
            r.first_field = (
                "completeness" if not java_complete or not magma_complete
                else "type-rotation-owner-profile-list"
            )
            r.first_delta = float("inf")
            r.note = f"Java {jt!r} vs C {ct!r}"
            return r
    counts = [len(row.get("skulls", [])) for row in ja[:n]]
    r.note = (
        "exact skull type/rotation/profile tile state, max="
        f"{max(counts) if counts else 0}"
    )
    return r


def diff_item_frames(ja, cc, n):
    r = FeatureResult("entities.item_frames")
    for i in range(n):
        java_complete = ja[i].get("item_frames_complete") is True
        magma_complete = cc[i].get("item_frames_complete") is True
        jt = ja[i].get("item_frames", [])
        ct = cc[i].get("item_frames", [])
        if not java_complete or not magma_complete or jt != ct:
            r.status = "diverge"
            r.first_tick = ja[i]["tick"]
            r.first_field = (
                "completeness"
                if not java_complete or not magma_complete
                else "source-state-list"
            )
            r.first_delta = float("inf")
            r.note = f"Java {jt!r} vs C {ct!r}"
            return r
    counts = [len(row.get("item_frames", [])) for row in ja[:n]]
    r.note = (
        "exact bounded comparator-source state, max="
        f"{max(counts) if counts else 0}"
    )
    return r


def run_state(args):
    ja = read_jsonl(args.java)
    cc = read_jsonl(args.c)
    n = min(len(ja), len(cc))
    if len(ja) != len(cc):
        print(f"WARN: tick counts differ (java={len(ja)} c={len(cc)}); comparing first {n}")

    results = []
    for label, fields in PLAYER_FEATURES:
        results.append(diff_scalar_feature(label, fields, ja, cc, n, args.atol, args.rtol))
    results.append(diff_inventory(ja, cc, n))
    results.append(
        diff_entities(
            ja, cc, n, args.entity_radius, args.atol, args.rtol))
    results.append(diff_scheduled_ticks(ja, cc, n))
    results.append(diff_moving_pistons(ja, cc, n))
    results.append(diff_comparators(ja, cc, n))
    results.append(diff_containers(ja, cc, n))
    results.append(diff_flower_pots(ja, cc, n))
    results.append(diff_skulls(ja, cc, n))
    results.append(diff_item_frames(ja, cc, n))
    results.append(diff_redstone_torch_toggles(ja, cc, n))
    results.append(diff_controlled_input(ja, cc, n))
    results.append(diff_time(ja, cc, n))

    print(f"STATE DIFF over {n} ticks  (atol={args.atol} rtol={args.rtol})")
    print(f"  java = {args.java}")
    print(f"  c    = {args.c}\n")
    print("PER-FEATURE FIRST-DIVERGENCE REPORT:")
    for r in results:
        print(r.line())

    # summary counts
    nmatch = sum(1 for r in results if r.status == "match")
    ndiv = sum(1 for r in results if r.status == "diverge")
    nun = sum(1 for r in results if r.status == "unsim")
    print(f"\nSUMMARY: {nmatch} match, {ndiv} diverge, {nun} UNSIMULATED-on-C "
          f"(of {len(results)} features)")
    # earliest physics divergence, the classic headline
    phys = next((r for r in results if r.label == "player.physics"), None)
    if phys and phys.status == "diverge":
        print(f"HEADLINE: player physics first diverges at tick {phys.first_tick} "
              f"(field {phys.first_field}, |delta|={phys.first_delta:.6g})")
    return 0


# ============================ PHYS mode (CSV, legacy) ============================
FLOAT_FIELDS = ["x", "y", "z", "vx", "vy", "vz", "health", "food"]
CSV_ANGLE = ["yaw", "pitch"]
INT_FIELDS = ["on_ground"]


def read_csv(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def run_phys(args):
    ja = read_csv(args.java)
    cc = read_csv(args.c)
    n = min(len(ja), len(cc))
    if len(ja) != len(cc):
        print(f"WARN: row counts differ (java={len(ja)} c={len(cc)}); comparing first {n}")
    check_fields = FLOAT_FIELDS + CSV_ANGLE + INT_FIELDS + (["air"] if args.include_air else [])
    first = [None, None, None]
    per_field = {f: [None, 0.0] for f in check_fields}
    for i in range(n):
        rj, rc = ja[i], cc[i]
        tick = int(rj["tick"])
        for f in check_fields:
            a, b = float(rj[f]), float(rc[f])
            d = scalar_delta(f, a, b) if f not in INT_FIELDS else abs(int(a) - int(b))
            if d > per_field[f][1]:
                per_field[f][1] = d
            ok = (int(a) == int(b)) if f in INT_FIELDS else (d <= args.atol + args.rtol * abs(b))
            if not ok:
                if per_field[f][0] is None:
                    per_field[f][0] = tick
                if first[0] is None:
                    first = [tick, f, d]
    print(f"compared {n} ticks; atol={args.atol} rtol={args.rtol}")
    print(f"fields checked: {', '.join(check_fields)}\n")
    if first[0] is None:
        print("PHYSICS: ZERO divergence (all fields within tolerance).")
    else:
        print(f"PHYSICS: FIRST divergence at tick {first[0]} in field '{first[1]}' "
              f"(|delta|={first[2]:.6g})")
    print("\nper-field summary (first-diverge tick / max |delta| over run):")
    for f in check_fields:
        ft, mx = per_field[f]
        print(f"  {f:10s} first={('never' if ft is None else str(ft)):>6s}  max|delta|={mx:.6g}")
    if args.materialize and first[0] is not None:
        t = first[0]
        lo, hi = max(0, t - args.window), t + args.window
        ddir = os.path.join(args.outdir, f"diverge_{t}")
        os.makedirs(ddir, exist_ok=True)
        if not os.path.exists(args.tracer):
            print(f"\nmaterialize: tracer not found at {args.tracer}; build it first "
                  f"(bash trace/build_c_tracer.sh)")
        else:
            cmd = [args.tracer, "--tape", args.tape, "--seed", str(args.seed),
                   "--out", os.path.join(ddir, "c_phys_replay.csv"),
                   "--render", "1", "--dump-dir", ddir,
                   "--dump-lo", str(lo), "--dump-hi", str(hi)]
            print(f"\nmaterialize: dumping C frames ticks [{lo},{hi}] -> {ddir}")
            subprocess.run(cmd, check=False)
    return 0


def selftest_nbt_diff():
    def profile(name):
        return nbt_codec.encode_hex({
            "name": "",
            "tag": {
                "type": "compound",
                "value": {
                    "Name": {"type": "string", "value": name},
                },
            },
        })

    java = [{
        "tick": 0,
        "skulls_complete": True,
        "skulls": [{
            "x": 1, "y": 2, "z": 3, "type": 3, "rotation": 7,
            "has_owner": True, "owner_nbt": profile("ParityHead"),
        }],
    }]
    same = json.loads(json.dumps(java))
    result = diff_skulls(java, same, 1)
    if result.status != "match":
        raise AssertionError(f"identical profile NBT did not match: {result.note}")
    changed = json.loads(json.dumps(java))
    changed[0]["skulls"][0]["owner_nbt"] = profile("WrongHead")
    result = diff_skulls(java, changed, 1)
    if result.status != "diverge" or result.first_field \
            != "type-rotation-owner-profile-list":
        raise AssertionError("profile-NBT negative control did not diverge")

    def item_tag(name):
        return nbt_codec.canonical_hex(nbt_codec.encode_hex({
            "name": "",
            "tag": {
                "type": "compound",
                "value": {
                    "BlockEntityTag": {
                        "type": "compound",
                        "value": {
                            "CustomName": {
                                "type": "string", "value": name,
                            },
                        },
                    },
                },
            },
        }))

    entity_java = [{
        "tick": 0,
        "entities": [{
            "eid": 7, "type": "EntityItem",
            "dx": 0.0, "dy": 0.0, "dz": 0.0,
            "stack_payload": {
                "kind": "item_tag", "nbt": item_tag("Parity Box"),
            },
        }],
    }]
    entity_same = json.loads(json.dumps(entity_java))
    result = diff_entities(entity_java, entity_same, 1, 16.0)
    if result.status != "match":
        raise AssertionError(
            f"identical shulker item NBT did not match: {result.note}")
    entity_changed = json.loads(json.dumps(entity_java))
    entity_changed[0]["entities"][0]["stack_payload"]["nbt"] = \
        item_tag("Wrong Box")
    result = diff_entities(entity_java, entity_changed, 1, 16.0)
    if result.status != "diverge" \
            or not result.first_field.endswith(".stack_payload"):
        raise AssertionError("shulker-NBT negative control did not diverge")
    print("diff_trace NBT selftest PASS "
          "(profile/item matches + semantic negative controls)")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest-nbt", action="store_true")
    ap.add_argument("--java", help="ground-truth trace (.jsonl state | .csv phys)")
    ap.add_argument("--c", help="magma trace (same kind as --java)")
    ap.add_argument("--atol", type=float, default=1e-6)
    ap.add_argument("--rtol", type=float, default=1e-6)
    ap.add_argument("--include-air", action="store_true", help="[phys mode] also check air")
    ap.add_argument("--entity-radius", type=float, default=16.0,
                    help="[state mode] compare entities within this player-relative radius")
    ap.add_argument("--materialize", action="store_true", help="[phys mode] dump frames at divergence")
    ap.add_argument("--window", type=int, default=2)
    ap.add_argument("--tape", default="trace/out/tape.txt")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--tracer",
                    default=str(Path(__file__).resolve().parent.parent / "trace_game"))
    ap.add_argument("--outdir", default="trace/out")
    args = ap.parse_args()
    if args.selftest_nbt:
        return selftest_nbt_diff()
    if not args.java or not args.c:
        ap.error("--java and --c are required unless --selftest-nbt is used")
    if args.java.endswith(".jsonl") or args.c.endswith(".jsonl"):
        return run_state(args)
    return run_phys(args)


if __name__ == "__main__":
    sys.exit(main())
