"""Diff two pixel-gate baselines (.gate.json): committed baseline vs tonight's.

Per-class px totals are compared; UNEXPLAINED growth beyond tolerance is a
regression (exit 1), any shrinkage is reported as progress. Accepted classes
(bossbar/hud/particles/...) get a looser band - they drift with fixes to
either side of the divergence.
"""
import argparse
import json
import os
import sys

UNEXPLAINED_GROW = 1.10   # >10% more unexplained px = regression
CLASS_GROW = 1.50         # accepted classes may drift; flag only big jumps


def load(path):
    with open(path) as fh:
        return json.load(fh)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--current", required=True)
    args = ap.parse_args()

    if not os.path.exists(args.baseline):
        print(f"no committed baseline ({args.baseline}) - treating current "
              "run as the candidate baseline; review and commit it")
        cur = load(args.current)
        for cls, s in sorted(cur["classes"].items()):
            print(f"  {cls:<12} frames {s['frames']:>5} px {s['px']:>10}")
        print(f"  failed_frames: {len(cur['failed_frames'])}")
        return 0

    base = load(args.baseline)
    cur = load(args.current)
    bad = False
    keys = sorted(set(base["classes"]) | set(cur["classes"]))
    for cls in keys:
        b = base["classes"].get(cls, {}).get("px", 0)
        c = cur["classes"].get(cls, {}).get("px", 0)
        lim = UNEXPLAINED_GROW if cls == "UNEXPLAINED" else CLASS_GROW
        mark = ""
        if b and c > b * lim:
            mark = "  <-- REGRESSION"
            bad = True
        elif not b and c:
            mark = "  <-- NEW CLASS" + ("/REGRESSION" if cls == "UNEXPLAINED"
                                        else "")
            bad = bad or cls == "UNEXPLAINED"
        elif b and c < b * 0.9:
            mark = "  (improved)"
        print(f"  {cls:<12} base {b:>10} now {c:>10}{mark}")
    bf = len(base.get("failed_frames", []))
    cf = len(cur.get("failed_frames", []))
    mark = "  <-- REGRESSION" if cf > bf else ("  (improved)" if cf < bf
                                               else "")
    if cf > bf:
        bad = True
    print(f"  failed_frames base {bf} now {cf}{mark}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
