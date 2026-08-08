"""Geometry oracle differ: vanilla ModelRenderer part poses vs magma's.

Java side (qrl recorder, recstart): <tape>.geom.jsonl - one JSON line per
entity per recorded frame, parts dumped by reflection AFTER the golden
re-render, i.e. post-setRotationAngles:
  {"t":123,"eid":45,"cls":"ModelDragon",
   "parts":{"head":[rpX,rpY,rpZ,raX,raY,raZ], ...}}

Magma side (--set geom_dump=<path> on the replay binary): text lines
  D <tick> <label> rpx rpy rpz rx ry rz
with the exact args magma passes to er_dragon_part (same units: rotation
points in texels, angles in radians).

Vanilla's ModelDragon renders ONE 'spine' part 5x for the neck and 12x for
the tail; the reflected field holds the LAST assignment = tail segment 11,
so vanilla 'spine' compares against magma 'tail11'. Magma's neck0-4 /
tail0-10 intermediates have no reflectable vanilla counterpart.

The two dumps may disagree on tick origin by a constant; --offset sets it,
otherwise the differ scans [-4,4] for the offset minimizing mismatch rate.

Warmup: magma seeds the 64-entry trail ring flat at tape start while the
oracle dragon carries its lifetime ring, so ring-fed parts (head, spine)
diverge hard for the first ~90 ticks - skipped by default (--warmup).
Validated residual after warmup (staged seed-77 tape): <=0.7 texel head,
<=2.5 texel / 0.035 rad tail11 - the ring is fed recorded SERVER yaw/y while
the client dragon the oracle renders steps every ~3 ticks; every other part
is exact. Tolerances sit just above that floor so structural regressions
(wrong lookback, wrong assignment order) still fail loudly.
"""
import argparse
import json
import math
import sys
from collections import defaultdict

PART_MAP = {  # vanilla field -> magma label
    "head": "head", "jaw": "jaw", "body": "body", "spine": "tail11",
    "wing": "wing", "wingTip": "wingTip",
    "frontLeg": "frontLeg", "frontLegTip": "frontLegTip",
    "frontFoot": "frontFoot",
    "rearLeg": "rearLeg", "rearLegTip": "rearLegTip", "rearFoot": "rearFoot",
}
POINT_TOL = 3.5    # texels; ring-feed residual floor is ~2.5 (see docstring)
ANGLE_TOL = 5e-2   # rad; residual floor ~0.035
WARMUP = 90        # ticks; cold trail ring at tape start


def load_java(path, cls):
    out = defaultdict(dict)          # tick -> {magma_label: [6 floats]}
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            r = json.loads(line)
            if r.get("cls") != cls:
                continue
            for fld, vals in r["parts"].items():
                lbl = PART_MAP.get(fld)
                if lbl:
                    out[r["t"]][lbl] = vals
    return out


def load_magma(path):
    out = defaultdict(dict)
    with open(path) as fh:
        for line in fh:
            p = line.split()
            if len(p) != 9 or p[0] != "D":
                continue
            out[int(p[1])][p[2]] = [float(v) for v in p[3:]]
    return out


def ang_err(a, b):
    d = math.fmod(a - b, 2.0 * math.pi)
    if d > math.pi:
        d -= 2.0 * math.pi
    if d < -math.pi:
        d += 2.0 * math.pi
    return abs(d)


def compare(jv, cr, offset, warmup=WARMUP, verbose=False, max_report=40):
    ticks = [t for t in sorted(set(jv) & set(t - offset for t in cr))
             if t >= warmup]
    if not ticks:
        return None
    n_cmp = n_bad = 0
    worst = []
    for t in ticks:
        cparts = cr[t + offset]
        for lbl, jvals in jv[t].items():
            cvals = cparts.get(lbl)
            if cvals is None:
                continue
            pe = max(abs(jvals[i] - cvals[i]) for i in range(3))
            ae = max(ang_err(jvals[i], cvals[i]) for i in range(3, 6))
            n_cmp += 1
            if pe > POINT_TOL or ae > ANGLE_TOL:
                n_bad += 1
                worst.append((max(pe, ae), t, lbl, pe, ae, jvals, cvals))
    worst.sort(reverse=True)
    if verbose:
        for _, t, lbl, pe, ae, jvals, cvals in worst[:max_report]:
            print(f"  t={t} {lbl}: point_err={pe:.5f} angle_err={ae:.6f}")
            print(f"    java    {['%.5f' % v for v in jvals]}")
            print(f"    magma {['%.5f' % v for v in cvals]}")
    return {"ticks": len(ticks), "compared": n_cmp, "mismatched": n_bad,
            "worst": worst[0][0] if worst else 0.0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--java", required=True, help="<tape>.geom.jsonl")
    ap.add_argument("--magma", required=True, help="geom_dump file from magma")
    ap.add_argument("--cls", default="EntityDragon")
    ap.add_argument("--offset", type=int, default=None)
    ap.add_argument("--warmup", type=int, default=WARMUP)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    jv = load_java(args.java, args.cls)
    cr = load_magma(args.magma)
    print(f"[geom] java ticks={len(jv)} magma ticks={len(cr)} cls={args.cls}")
    if not jv or not cr:
        sys.exit("[geom] empty dump on one side")

    if args.offset is None:
        best = None
        for off in range(-4, 5):
            r = compare(jv, cr, off, warmup=args.warmup)
            if r and r["compared"]:
                score = r["mismatched"] / r["compared"]
                if best is None or score < best[0]:
                    best = (score, off, r)
        if best is None:
            sys.exit("[geom] no overlapping ticks at any offset in [-4,4]")
        off = best[1]
        print(f"[geom] auto offset={off} (mismatch rate {best[0]:.4f})")
    else:
        off = args.offset

    r = compare(jv, cr, off, warmup=args.warmup, verbose=args.verbose)
    print(f"[geom] ticks={r['ticks']} compared={r['compared']} "
          f"mismatched={r['mismatched']} worst={r['worst']:.6f} "
          f"(tol point {POINT_TOL}, angle {ANGLE_TOL})")
    if r["mismatched"]:
        print("[geom] FAIL")
        return 3
    print("[geom] PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
