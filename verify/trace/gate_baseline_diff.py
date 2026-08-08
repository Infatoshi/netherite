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
        # Missing required baseline is a visible failure, not silent green.
        print(f"FAIL: no committed baseline ({args.baseline})")
        if os.path.exists(args.current):
            cur = load(args.current)
            for cls, s in sorted(cur.get("classes", {}).items()):
                print(f"  {cls:<12} frames {s['frames']:>5} px {s['px']:>10}")
            print(f"  failed_frames: {len(cur.get('failed_frames', []))}")
        print("commit a baseline before this tape can pass nightly")
        return 1

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

    # State block. Without this only pixel classes were diffed, so an
    # inventory divergence or a tape that stopped replaying half way through
    # could ride along under a pixel-gate FAIL and still be reported green.
    bs = base.get("state", {})
    cs = cur.get("state", {})
    for component in ("inventory", "entities", "world"):
        b_component = bs.get(component, {})
        c_component = cs.get(component, {})
        if not c_component.get("available"):
            continue
        coverage_field = ("ticks_independent" if component == "inventory"
                          else "ticks_checked")
        b_checked = b_component.get(coverage_field, 0)
        c_checked = c_component.get(coverage_field, 0)
        mark = ""
        if b_component.get("pass", True) and not c_component.get("pass", True):
            mark = "  <-- REGRESSION"
            bad = True
        elif c_checked < b_checked:
            # Losing verified ticks is a coverage regression even if it passes.
            mark = "  <-- COVERAGE REGRESSION"
            bad = True
        print(f"  {component:<12} base pass={b_component.get('pass')} "
              f"checked={b_checked} now pass={c_component.get('pass')} "
              f"checked={c_checked}{mark}")
    b_cov = bs.get("coverage", {})
    c_cov = cs.get("coverage", {})
    if c_cov:
        b_run = b_cov.get("ticks_run", 0)
        c_run = c_cov.get("ticks_run", 0)
        mark = ""
        if b_run and c_run < b_run:
            mark = "  <-- COVERAGE REGRESSION"
            bad = True
        note = " TRUNCATED" if c_cov.get("truncated") else ""
        print(f"  tape_ticks   base {b_run} now {c_run} of "
              f"{c_cov.get('ticks_total')}{note}{mark}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
