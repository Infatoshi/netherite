#!/usr/bin/env python3
"""genprobe_diff.py - the worldgen RNG-cursor flywheel differ.

Inputs: two checkpoint logs with lines "TAG cx cz TYPE cursor":
  - java side:    qrl/WorldGenProbe.java (live Forge terrain-gen events, fresh world gen)
  - magma side: trace/world_dump with MAGMA_GENPROBE=path (MC_PROBE checkpoints)

For every chunk populated on both sides, walks the two checkpoint sequences in order
and reports the FIRST label/cursor divergence. The label names the exact vanilla
stage (PRE / POP LAKE|LAVA|DUNGEON / DECPRE / ORE DIRT..LAPIS / DEC SAND..LAKE_LAVA /
DECPOST) where the RNG streams split - no guessing.

Usage:
  uv run --no-project python3 trace/genprobe_diff.py \
      --java trace/out/java_genprobe.log --magma trace/out/magma_genprobe.log
"""
import argparse
from collections import defaultdict, Counter

# labels the C side emits; java-only labels (ANIMALS, ICE, POST) are filtered out
C_TAGS = {"PRE", "POP", "DECPRE", "ORE", "DEC", "DECPOST"}
JAVA_SKIP = {("POP", "ANIMALS"), ("POP", "ICE")}


def parse(path, java=False):
    """{(cx,cz): [(label, cursor), ...]} keeping only each chunk's FIRST populate.

    Biome decorate() overrides fire extra Forge events around super.decorate()
    (roofed canopy TREE/BIG_SHROOM per grid cell, forest/taiga FLOWERS, plains
    GRASS, swamp FOSSIL, hills ORE EMERALD/SILVERFISH); the C side emits matching
    MC_PROBE checkpoints at the same gate positions, so no filtering is needed.

    Events are attributed SEQUENTIALLY, not by their logged coords: roofed canopy
    events fire at getHeight(pos.add(k,0,l)) with k,l up to 23, so Forge logs them
    under the neighboring chunk when the cell crosses the chunk edge. The java log
    is single-threaded, so everything between a chunk's PRE and POST belongs to
    that populate (stack, since populates nest via cascaded chunk loads). The C
    log has no POST: a PRE simply switches the current chunk (no nesting there).
    """
    seqs = defaultdict(list)
    done = set()
    order = []
    stack = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) < 5:
                continue
            tag, cx, cz, typ, cur = parts[:5]   # optional trailing "x y z" event pos
            try:
                cx, cz, cur = int(cx), int(cz), int(cur)
            except ValueError:
                continue
            if tag == "RESUME":
                # C-side cascade emulation: nested populate done, parent resumes
                # (the C log has no POST lines to pop the nested chunk).
                if not java:
                    stack = [(cx, cz)]
                continue
            if tag == "PRE":
                key = (cx, cz)
                if key in seqs:      # re-populate of a chunk we already captured
                    done.add(key)
                else:
                    order.append(key)
                if java:
                    stack.append(key)
                else:
                    stack = [key]
            elif tag == "POST":
                if java and stack and stack[-1] == (cx, cz):
                    stack.pop()
                continue
            if not stack:
                continue
            key = stack[-1]
            if key in done:
                continue
            if tag not in C_TAGS or (tag, typ) in JAVA_SKIP:
                continue
            label = tag if typ == "-" else f"{tag} {typ}"
            seqs[key].append((label, cur))
    return seqs, order


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--java", required=True)
    ap.add_argument("--magma", required=True)
    ap.add_argument("--limit", type=int, default=25, help="max divergent chunks to detail")
    ap.add_argument("--chunk", type=int, nargs=2, metavar=("CX", "CZ"),
                    help="dump both full checkpoint sequences for one chunk and exit")
    args = ap.parse_args()

    j, jorder = parse(args.java, java=True)
    c, _ = parse(args.magma)

    if args.chunk:
        key = tuple(args.chunk)
        js, cs = j.get(key, []), c.get(key, [])
        print(f"chunk {key}: java={len(js)} magma={len(cs)} checkpoints")
        for i in range(max(len(js), len(cs))):
            jl = f"{js[i][0]:16s} {js[i][1]:>16d}" if i < len(js) else " " * 33
            cl = f"{cs[i][0]:16s} {cs[i][1]:>16d}" if i < len(cs) else ""
            mark = "  " if (i < len(js) and i < len(cs) and js[i] == cs[i]) else "<<"
            print(f"  {i:2d}  {jl} | {cl} {mark}")
        return
    both = [k for k in jorder if k in c]
    print(f"chunks: java={len(j)} magma={len(c)} common={len(both)}")

    clean = 0
    first_div = []
    for key in both:
        js, cs = j[key], c[key]
        div = None
        for i in range(min(len(js), len(cs))):
            (jl, jc), (cl, cc) = js[i], cs[i]
            if jl != cl:
                div = (i, f"LABEL {jl} vs {cl}", jc, cc)
                break
            if jc != cc:
                div = (i, jl, jc, cc)
                break
        if div is None and len(js) != len(cs):
            div = (min(len(js), len(cs)), f"LENGTH {len(js)} vs {len(cs)}", -1, -1)
        if div is None:
            clean += 1
        else:
            first_div.append((key, div))

    print(f"clean chunks (all checkpoints exact): {clean}/{len(both)}")
    hist = Counter(d[1][1] for d in first_div)
    print("\nfirst-divergent stage histogram:")
    for label, n in hist.most_common():
        print(f"  {label:24s} : {n}")
    print(f"\nfirst {args.limit} divergent chunks (java populate order):")
    for (key, (i, label, jc, cc)) in first_div[: args.limit]:
        print(f"  chunk {key}  step {i:2d}  {label:20s} java={jc} magma={cc}")


if __name__ == "__main__":
    main()
