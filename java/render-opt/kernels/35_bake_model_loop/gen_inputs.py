#!/usr/bin/env python3
"""Deterministic inputs for 35_bake_model_loop (MC 1.11.2 ModelBakery.bakeModel assembly).

Each line = one model: isInteger nFaces  then nFaces triples (cull rot quad).
  cull = -1 (no cullface -> general bucket) else 0-5 (D-U-N-S-W-E)
  rot  = modelRotation.rotate(cullFace) target facing 0-5 (precomputed; see README)
  quad = a monotonically increasing quad id (stands in for the baked int[28])

Coverage of the dispatch branches:
  - isInteger=1 with cull set -> face bucket rot; cull=-1 -> general.
  - isInteger=0 -> ALL faces go to general regardless of cull (the !isInteger short-circuit).
  - cube-like models (6 faces, one per facing, cull=facing), multi-element models (>6 faces, repeated
    facings to test append order within a bucket), and models with mixed -1/face culls.
"""
import random
import sys


def main():
    rnd = random.Random(35035)
    out = []
    qid = 0

    def model(isInteger, faces):
        nonlocal qid
        parts = [str(isInteger), str(len(faces))]
        for (cull, rot) in faces:
            parts += [str(cull), str(rot), str(qid)]
            qid += 1
        out.append(" ".join(parts))

    # explicit cases
    model(1, [(i, i) for i in range(6)])                       # cube, identity rotation
    model(1, [(i, (i + 1) % 6) for i in range(6)])             # cube, some rotation remap
    model(0, [(i, i) for i in range(6)])                       # !isInteger -> all general
    model(1, [(-1, 0)] * 6)                                    # all no-cull -> all general
    model(1, [(0, 0), (0, 0), (2, 2), (2, 2), (-1, 0)])        # repeated buckets + a general
    model(1, [])                                               # empty model

    for _ in range(20000):
        isInteger = rnd.randrange(2)
        nFaces = rnd.randrange(0, 25)
        faces = []
        for _ in range(nFaces):
            if rnd.random() < 0.25:
                cull = -1
            else:
                cull = rnd.randrange(6)
            rot = rnd.randrange(6)
            faces.append((cull, rot))
        model(isInteger, faces)

    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
