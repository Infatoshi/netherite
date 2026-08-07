#!/usr/bin/env python3
"""Strict post-tick raw block-state diff for the Java-vs-Magma oracle.

Both inputs are headerless little-endian u16 arrays in QuantizedRL ``getblocks``
order: inclusive cuboid, y-major then z then x, value ``block_id << 4 | meta``.

Optional pre-tick inputs split baseline worldgen disagreement from divergent
simulation outcomes. Exit status is 0 for the selected exactness contract, 4
for a real mismatch, and 1/2 for I/O or usage errors. ``--transition-strict``
judges only cells whose pre-tick states matched. ``--allow-diff`` keeps a
diagnostic mismatch non-fatal inside the exploratory all-feature oracle.
"""

import argparse
import collections
import hashlib
import os
import struct
import sys


def cell_count(box):
    x0, y0, z0, x1, y1, z1 = box
    if x1 < x0 or y1 < y0 or z1 < z0 or y0 < 0 or y1 > 255:
        raise ValueError(f"invalid inclusive block box: {box}")
    return (x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1)


def read_states(path, cells):
    with open(path, "rb") as f:
        raw = f.read()
    expected = cells * 2
    if len(raw) != expected:
        raise ValueError(f"{path}: expected {expected} bytes ({cells} states), "
                         f"got {len(raw)}")
    values = struct.unpack(f"<{cells}H", raw)
    return raw, values


def coord_for(index, box):
    x0, y0, z0, x1, _y1, z1 = box
    nx = x1 - x0 + 1
    nz = z1 - z0 + 1
    x = x0 + index % nx
    q = index // nx
    z = z0 + q % nz
    y = y0 + q // nz
    return x, y, z


def compare(java, magma, box):
    wrong = [i for i, (j, c) in enumerate(zip(java, magma)) if j != c]
    id_wrong = sum((j >> 4) != (c >> 4) for j, c in zip(java, magma))
    meta_wrong = sum((j & 15) != (c & 15) for j, c in zip(java, magma))
    confusion = collections.Counter((j, c) for j, c in zip(java, magma) if j != c)
    return {
        "wrong": wrong,
        "id_wrong": id_wrong,
        "meta_wrong": meta_wrong,
        "confusion": confusion,
        "first_coord": coord_for(wrong[0], box) if wrong else None,
    }


def compare_transition(java_before, java_after, magma_before, magma_after, box):
    initial_equal = [i for i, (j, c) in enumerate(zip(java_before, magma_before))
                     if j == c]
    shared_wrong = [i for i in initial_equal if java_after[i] != magma_after[i]]
    java_changed = [i for i, (a, b) in enumerate(zip(java_before, java_after)) if a != b]
    magma_changed = [i for i, (a, b) in enumerate(zip(magma_before, magma_after)) if a != b]
    change_mask_wrong = [
        i for i in initial_equal
        if (java_before[i] != java_after[i]) != (magma_before[i] != magma_after[i])
    ]
    converged = [
        i for i in range(len(java_after))
        if java_before[i] != magma_before[i] and java_after[i] == magma_after[i]
    ]
    initial_equal_set = set(initial_equal)
    java_changed_shared = [i for i in java_changed if i in initial_equal_set]
    magma_changed_shared = [i for i in magma_changed if i in initial_equal_set]
    return {
        "initial_equal": initial_equal,
        "shared_wrong": shared_wrong,
        "java_changed": java_changed,
        "magma_changed": magma_changed,
        "change_mask_wrong": change_mask_wrong,
        "converged": converged,
        "java_changed_shared": java_changed_shared,
        "magma_changed_shared": magma_changed_shared,
        "first_coord": coord_for(shared_wrong[0], box) if shared_wrong else None,
    }


def state_text(v):
    return f"id={v >> 4} meta={v & 15} raw=0x{v:04x}"


def mutation_text(index, before, after, box):
    return (f"{coord_for(index, box)} "
            f"{state_text(before[index])} -> {state_text(after[index])}")


def report_transition(java_before, java, magma_before, magma, box, require_mutation=False):
    tr = compare_transition(java_before, java, magma_before, magma, box)
    cells = len(java)
    shared = len(tr["initial_equal"])
    shared_exact = shared - len(tr["shared_wrong"])
    print("TRANSITION DIFF (separates baseline worldgen from N-tick outcomes):")
    print(f"  pre-tick shared state: {shared}/{cells} = "
          f"{100.0 * shared / cells:.6f}%")
    if shared:
        print(f"  post-tick exact on shared baseline: {shared_exact}/{shared} = "
              f"{100.0 * shared_exact / shared:.6f}%")
    else:
        print("  post-tick exact on shared baseline: UNAVAILABLE (zero shared cells)")
    print(f"  changed cells: java={len(tr['java_changed'])} "
          f"magma={len(tr['magma_changed'])}")
    print(f"  changed shared-baseline cells: java={len(tr['java_changed_shared'])} "
          f"magma={len(tr['magma_changed_shared'])}")
    print(f"  change-mask disagreements on shared baseline: "
          f"{len(tr['change_mask_wrong'])}")
    print(f"  initially-different cells that converged: {len(tr['converged'])}")
    for label, indexes, before, after in (
            ("java", tr["java_changed"], java_before, java),
            ("magma", tr["magma_changed"], magma_before, magma)):
        shared_indexes = [i for i in indexes if java_before[i] == magma_before[i]]
        baseline_indexes = [i for i in indexes if java_before[i] != magma_before[i]]
        for i in shared_indexes[:6] + baseline_indexes[:3]:
            shared_tag = "shared" if i in shared_indexes else "baseline-different"
            print(f"    {label} mutation [{shared_tag}]: "
                  f"{mutation_text(i, before, after, box)}")
    if not shared:
        print("TRANSITION VERDICT: UNAVAILABLE")
        return True
    if tr["shared_wrong"]:
        i = tr["shared_wrong"][0]
        print(f"  FIRST OUTCOME DIVERGENCE: index={i} coord={tr['first_coord']} "
              f"before={state_text(java_before[i])} "
              f"java_after={state_text(java[i])} magma_after={state_text(magma[i])}")
        print(f"TRANSITION VERDICT: MISMATCH ({len(tr['shared_wrong'])} shared cells)")
        return True
    if require_mutation and not tr["java_changed_shared"] and not tr["magma_changed_shared"]:
        print("TRANSITION VERDICT: VACUOUS (required a shared-baseline mutation, observed zero)")
        return True
    suffix = " (zero mutations observed)" if not tr["java_changed"] and not tr["magma_changed"] else ""
    print(f"TRANSITION VERDICT: EXACT ON SHARED BASELINE{suffix}")
    return False


def write_mismatches(path, java, magma, wrong, box):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as f:
        f.write("x,y,z,java_id,java_meta,magma_id,magma_meta\n")
        for i in wrong:
            x, y, z = coord_for(i, box)
            j, c = java[i], magma[i]
            f.write(f"{x},{y},{z},{j >> 4},{j & 15},{c >> 4},{c & 15}\n")


def selftest():
    box = (10, 20, 30, 11, 21, 31)
    base = tuple((i << 4) | (i & 3) for i in range(8))
    assert cell_count(box) == 8
    assert compare(base, base, box)["wrong"] == []

    changed = list(base)
    changed[5] = (99 << 4) | (base[5] & 15)
    r = compare(base, changed, box)
    assert r["wrong"] == [5]
    assert r["id_wrong"] == 1 and r["meta_wrong"] == 0
    assert r["first_coord"] == (11, 21, 30)

    changed = list(base)
    changed[7] ^= 1
    r = compare(base, changed, box)
    assert r["wrong"] == [7]
    assert r["id_wrong"] == 0 and r["meta_wrong"] == 1
    assert r["first_coord"] == (11, 21, 31)

    # Different initial terrain that remains unchanged is not a transition error.
    j0 = list(base)
    c0 = list(base)
    c0[0] ^= 16
    tr = compare_transition(j0, j0, c0, c0, box)
    assert len(tr["initial_equal"]) == 7 and tr["shared_wrong"] == []

    # A post-tick mutation on only one side of a shared initial cell is detected.
    j1 = list(j0)
    j1[3] ^= 16
    tr = compare_transition(j0, j1, c0, c0, box)
    assert tr["shared_wrong"] == [3]
    assert tr["change_mask_wrong"] == [3]
    assert tr["first_coord"] == (11, 20, 31)
    print("block_diff selftest: PASS "
          "(identity, id-negative, meta-negative, baseline split, transition-negative)")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--java")
    ap.add_argument("--c")
    ap.add_argument("--java-before")
    ap.add_argument("--c-before")
    ap.add_argument("--box", type=int, nargs=6,
                    metavar=("X0", "Y0", "Z0", "X1", "Y1", "Z1"))
    ap.add_argument("--out", help="optional CSV containing every mismatching cell")
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--allow-diff", action="store_true")
    ap.add_argument("--transition-strict", action="store_true",
                    help="exit based on post-state equality only where pre-state matched")
    ap.add_argument("--require-mutation", action="store_true",
                    help="fail the transition contract if no shared-baseline cell changes")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return 0
    if not args.java or not args.c or args.box is None:
        ap.error("--java, --c, and --box are required unless --selftest is used")
    if (args.java_before is None) != (args.c_before is None):
        ap.error("--java-before and --c-before must be supplied together")
    if args.transition_strict and args.java_before is None:
        ap.error("--transition-strict requires both pre-tick inputs")
    if args.require_mutation and args.java_before is None:
        ap.error("--require-mutation requires both pre-tick inputs")

    try:
        cells = cell_count(args.box)
        jraw, java = read_states(args.java, cells)
        craw, magma = read_states(args.c, cells)
        if args.java_before:
            _j0raw, java_before = read_states(args.java_before, cells)
            _c0raw, magma_before = read_states(args.c_before, cells)
    except (OSError, ValueError) as exc:
        print(f"block_diff: {exc}", file=sys.stderr)
        return 1

    r = compare(java, magma, args.box)
    wrong = r["wrong"]
    exact = cells - len(wrong)
    ids_exact = cells - r["id_wrong"]
    metas_exact = cells - r["meta_wrong"]
    print(f"BLOCK DIFF after input tape over {cells} cells")
    print(f"  box  = {tuple(args.box)} (inclusive, y/z/x serialization)")
    print(f"  java = {args.java}  sha256={hashlib.sha256(jraw).hexdigest()[:16]}")
    print(f"  c    = {args.c}  sha256={hashlib.sha256(craw).hexdigest()[:16]}")
    print(f"  full-state exact: {exact}/{cells} = {100.0 * exact / cells:.6f}%")
    print(f"  block-id exact:   {ids_exact}/{cells} = {100.0 * ids_exact / cells:.6f}%")
    print(f"  metadata exact:   {metas_exact}/{cells} = {100.0 * metas_exact / cells:.6f}%")

    transition_failed = False
    if args.java_before:
        transition_failed = report_transition(
            java_before, java, magma_before, magma, args.box, args.require_mutation)

    if not wrong:
        print("FINAL-STATE VERDICT: EXACT MATCH")
        failed = transition_failed if args.transition_strict else False
        return 0 if args.allow_diff or not failed else 4

    first = wrong[0]
    print(f"FIRST DIVERGENCE: index={first} coord={r['first_coord']} "
          f"java({state_text(java[first])}) magma({state_text(magma[first])})")
    print("TOP STATE CONFUSIONS (java -> magma):")
    for (j, c), n in r["confusion"].most_common(max(0, args.top)):
        print(f"  {n:8d}  ({state_text(j)}) -> ({state_text(c)})")
    ys = collections.Counter(coord_for(i, args.box)[1] for i in wrong)
    coords = [coord_for(i, args.box) for i in wrong]
    bbox = (min(p[0] for p in coords), min(p[1] for p in coords),
            min(p[2] for p in coords), max(p[0] for p in coords),
            max(p[1] for p in coords), max(p[2] for p in coords))
    print(f"  mismatch bbox = {bbox}; top y-levels = {ys.most_common(8)}")
    if args.out:
        write_mismatches(args.out, java, magma, wrong, args.box)
        print(f"  every mismatch -> {args.out}")

    print(f"FINAL-STATE VERDICT: MISMATCH ({len(wrong)} cells)")
    failed = transition_failed if args.transition_strict else bool(wrong)
    return 0 if args.allow_diff or not failed else 4


if __name__ == "__main__":
    sys.exit(main())
