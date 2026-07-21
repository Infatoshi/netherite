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
import csv
import json
import os
import subprocess
import sys

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
    ("player.vitals",       ["health", "food", "saturation"]),
    ("player.air",          ["air"]),
    ("player.fire",         ["fire"]),
    ("player.xp",           ["xp_level", "xp_frac"]),
    ("player.fall_distance",["fall_distance"]),
    ("player.flags",        ["sprinting", "sneaking", "jumping"]),
    ("player.held_item",    ["held_slot", "held_id", "held_count", "held_meta"]),
    ("player.combat_timers",["attack_cooldown", "hurt_time", "death_time"]),
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
    """slot -> (id,count,meta) dict for an inventory list."""
    d = {}
    for it in (row.get("inventory") or []):
        d[it["slot"]] = (it.get("id"), it.get("count"), it.get("meta"))
    return d


def diff_inventory(ja, cc, n):
    r = FeatureResult("inventory")
    # id NAMESPACE caveat: Java uses vanilla registry ids, C uses mc-sim IC_* ids. So we
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
        r.note = "occupancy+counts+ids identical"
    else:
        r.status = "diverge"
        r.first_tick = first_full
        r.first_field = "slots"
        r.first_delta = float("inf")
        occ = "same" if first_occ is None else f"@{first_occ}"
        cnt = "same" if first_cnt is None else f"@{first_cnt}"
        r.note = (f"occupancy {occ}, counts {cnt}; NOTE id namespaces differ "
                  f"(Java vanilla-registry vs C mc-sim IC_*)")
        r.first_field = f"full-tuple (occ {occ}, cnt {cnt})"
    return r


def diff_entities(ja, cc, n):
    r = FeatureResult("entities")
    c_all_null = all((cc[i].get("entities") is None) for i in range(n))
    j_counts = [len(ja[i].get("entities") or []) for i in range(n)]
    j_max = max(j_counts) if j_counts else 0
    if c_all_null and j_max > 0:
        first_nonzero = next((ja[i]["tick"] for i in range(n) if j_counts[i] > 0), None)
        types = sorted({e.get("type") for i in range(n) for e in (ja[i].get("entities") or [])})
        r.status = "unsim"
        r.note = (f"Java carried up to {j_max} entities (first nonzero @tick {first_nonzero}); "
                  f"types={types[:8]}{'...' if len(types) > 8 else ''}; C emits null (nents=0)")
        return r
    if c_all_null and j_max == 0:
        r.status = "match"
        r.note = "both empty (no entities near spawn)"
        return r
    # both simulate: compare entity id-sets per tick
    for i in range(n):
        js = {e.get("eid") for e in (ja[i].get("entities") or [])}
        cs = {e.get("eid") for e in (cc[i].get("entities") or [])}
        if js != cs:
            r.status, r.first_tick = "diverge", ja[i]["tick"]
            r.first_field, r.first_delta = "entity-set", float("inf")
            r.note = f"Java {len(js)} vs C {len(cs)} entities"
            return r
    r.status = "match"
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
        for k in ("world_time", "moon_phase", "raining", "thundering"):
            if jt.get(k) != ct.get(k):
                r.status, r.first_tick, r.first_field = "diverge", ja[i]["tick"], k
                r.first_delta = float("inf")
                return r
    r.status = "match"
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
    results.append(diff_entities(ja, cc, n))
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--java", required=True, help="ground-truth trace (.jsonl state | .csv phys)")
    ap.add_argument("--c", required=True, help="magma trace (same kind as --java)")
    ap.add_argument("--atol", type=float, default=1e-6)
    ap.add_argument("--rtol", type=float, default=1e-6)
    ap.add_argument("--include-air", action="store_true", help="[phys mode] also check air")
    ap.add_argument("--materialize", action="store_true", help="[phys mode] dump frames at divergence")
    ap.add_argument("--window", type=int, default=2)
    ap.add_argument("--tape", default="trace/out/tape.txt")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--tracer",
                    default="/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/magma/trace_game")
    ap.add_argument("--outdir", default="trace/out")
    args = ap.parse_args()
    if args.java.endswith(".jsonl") or args.c.endswith(".jsonl"):
        return run_state(args)
    return run_phys(args)


if __name__ == "__main__":
    sys.exit(main())
