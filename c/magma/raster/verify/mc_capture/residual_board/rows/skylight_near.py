#!/usr/bin/env python3
"""PASS if cached near-cam light compare is perfect, or recompute if CSVs exist."""
import json
from pathlib import Path
from _common import parse_args, finish


def main():
    args = parse_args()
    cached = Path("/tmp/hard_scene_agents/light_compare_qrl0.json")
    if cached.is_file():
        d = json.loads(cached.read_text())
        ok = float(d.get("near_sky_match", 0)) >= 0.999
        finish(ok, source=str(cached), **d)

    jcsv = Path("/tmp/hard_scene_agents/java_light_qrl0.csv")
    ccsv = Path("/tmp/hard_scene_agents/c_light.csv")
    if not (jcsv.is_file() and ccsv.is_file()):
        finish(
            False,
            reason="no light_compare_qrl0.json and missing light CSVs; re-run dump_light+sample_light",
        )
    import numpy as np

    j = np.loadtxt(jcsv, skiprows=1, dtype=int)
    c = np.loadtxt(ccsv, skiprows=1, dtype=int)
    wx, wy, wz = j[:, 0], j[:, 1], j[:, 2]
    near = (abs(wx - 8) <= 24) & (abs(wz - 40) <= 24) & (wy >= 70) & (wy <= 105)
    match = float((j[near, 3] == c[near, 3]).mean())
    finish(match >= 0.999, near_sky_match=match, n=int(near.sum()))


if __name__ == "__main__":
    main()
