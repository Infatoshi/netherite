#!/usr/bin/env python3
"""Nether + End terrain census: blaze CPU vs CUDA, blaze CPU vs magma.

Diagnostic only (not a frozen gate). Pattern mirrors wrapper_diff census:
sparse x,y,z,state lines, per-class breakdown, first differing cell, content sha.

Regions (documented in report):
  nether origin:  chunks (cx,cz) in [-2,1]^2  (4x4 = 64x256x64 around origin)
  nether fortress: 3x3 chunks centered on first ft_can_spawn hit in [-48,48]^2
  end main island: chunks (cx,cz) in [-4,3]^2 (8x8; covers island + fixed pillar
                   crystal xz from ender_dragon.h ed_pillar_crystal_pos)

Populate (fire/glowstone/quartz/springs/mushrooms) is OUT OF SCOPE.
End spikes (MapGenEndSpike) are NOT in cpe_provide_chunk; only island terrain
is compared. Pillar layout coordinates are fixed (not seed-dependent).

Usage (prefer the shell wrapper):
  UV_CACHE_DIR=... TMPDIR=... uv run --no-project --with numpy python \\
      verify/worldgen/nether_census.py --out verify/worldgen/out_nether \\
      [--seeds 0 2 3 7 9 10 19] [--skip-cuda] [--skip-magma] [--inject-proof]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
WG = Path(__file__).resolve().parent
DEFAULT_SEEDS = [0, 2, 3, 7, 9, 10, 19]

# Regions (chunk ranges inclusive start, exclusive end via ncx/ncz)
NETHER_ORIGIN = dict(cx0=-2, cz0=-2, ncx=4, ncz=4, tag="origin_4x4")
NETHER_FORT_HALF = 1  # 3x3 centered on fortress start
END_ISLAND = dict(cx0=-4, cz0=-4, ncx=8, ncz=8, tag="main_island_8x8")
FORT_SCAN_RADIUS = 48

# Classification for nether/end residual cells (either side's id)
LAVA = {10, 11}  # FLOWING_LAVA, LAVA
BEDROCK = {7}
GRAVEL = {13}
NETHERRACK = {87}
SOUL_SAND = {88}
FORTRESS = {52, 112, 113, 114, 115}  # spawner, brick, fence, stairs, wart
END_STONE = {121}
STONE = {1}


def load_sparse(path: Path) -> dict[tuple[int, int, int], int]:
    m: dict[tuple[int, int, int], int] = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            xs, ys, zs, ss = line.split(",")
            m[(int(xs), int(ys), int(zs))] = int(ss)
    return m


def classify_id(a: int, b: int) -> str:
    for s, name in (
        (LAVA, "lava"),
        (BEDROCK, "bedrock"),
        (GRAVEL, "gravel"),
        (NETHERRACK, "netherrack"),
        (SOUL_SAND, "soul_sand"),
        (FORTRESS, "fortress"),
        (END_STONE, "end_stone"),
        (STONE, "stone"),
    ):
        if a in s or b in s:
            return name
    return "other"


def diff_record(a: dict, b: dict, label: str) -> dict:
    keys = set(a) | set(b)
    only_a = only_b = both = 0
    cls: Counter[str] = Counter()
    pairs: Counter[tuple[int, int]] = Counter()
    lines: list[str] = []
    first: dict | None = None
    for k in sorted(keys):
        va = a.get(k, 0)
        vb = b.get(k, 0)
        if va == vb:
            continue
        if va and not vb:
            only_a += 1
        elif vb and not va:
            only_b += 1
        else:
            both += 1
        c = classify_id(va, vb)
        cls[c] += 1
        pairs[(va, vb)] += 1
        x, y, z = k
        lines.append(f"{x},{y},{z},{va},{vb}\n")
        if first is None:
            first = {"x": x, "y": y, "z": z, "a": va, "b": vb, "class": c}
    lines.sort()
    h = hashlib.sha256("".join(lines).encode("utf-8")).hexdigest()
    n = only_a + only_b + both
    return {
        "label": label,
        "a_cells": len(a),
        "b_cells": len(b),
        "diff_cells": n,
        "only_a": only_a,
        "only_b": only_b,
        "both_side": both,
        "class": dict(cls),
        "top_pairs": [
            {"a": p[0], "b": p[1], "n": n} for p, n in pairs.most_common(12)
        ],
        "first_diff": first,
        "diff_sha256": h,
    }


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def build_tools(out: Path, skip_cuda: bool) -> dict[str, Path]:
    cpu = WG / "dim_region_dump"
    cuda = WG / "dim_region_dump_cuda"
    run(
        [
            "cc",
            "-O2",
            "-ffp-contract=off",
            f"-I{ROOT / 'blaze' / 'core'}",
            str(WG / "dim_region_dump.c"),
            "-o",
            str(cpu),
            "-lm",
        ]
    )
    tools = {"cpu": cpu, "cuda": cuda}
    if not skip_cuda:
        sm = os.environ.get("MC_SM") or os.environ.get("SM") or "sm_120"
        run(
            [
                "nvcc",
                f"-arch={sm}",
                "-O3",
                "--fmad=false",
                f"-I{ROOT / 'blaze' / 'core'}",
                str(WG / "dim_region_dump.cu"),
                "-o",
                str(cuda),
            ]
        )
    # magma world_dump for blaze-vs-magma
    world_dump = ROOT / "magma" / "trace" / "world_dump"
    pop = ROOT / "magma" / "world" / "populate_mc.c"
    src = ROOT / "magma" / "trace" / "world_dump.c"
    if (
        not world_dump.is_file()
        or (pop.is_file() and pop.stat().st_mtime > world_dump.stat().st_mtime)
        or (src.is_file() and src.stat().st_mtime > world_dump.stat().st_mtime)
    ):
        run(["bash", str(ROOT / "magma" / "trace" / "build_world_dump.sh")])
    tools["world_dump"] = world_dump
    tools["crwd"] = WG / "crwd_to_sparse.py"
    return tools


def find_fortress(cpu: Path, seed: int) -> tuple[int, int] | None:
    r = subprocess.run(
        [str(cpu), "find-fortress", str(seed), str(FORT_SCAN_RADIUS)],
        capture_output=True,
        text=True,
    )
    if r.returncode != 0 or not r.stdout.strip():
        return None
    parts = r.stdout.strip().split()
    return int(parts[0]), int(parts[1])


def dump_cpu(
    tools: dict, dim: str, seed: int, cx0: int, cz0: int, ncx: int, ncz: int, path: Path
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    run(
        [
            str(tools["cpu"]),
            dim,
            str(seed),
            str(cx0),
            str(cz0),
            str(ncx),
            str(ncz),
            "-o",
            str(path),
        ]
    )


def dump_cuda(
    tools: dict, dim: str, seed: int, cx0: int, cz0: int, ncx: int, ncz: int, path: Path
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.setdefault("CUDA_VISIBLE_DEVICES", "0")
    lock = Path("/home/infatoshi/dev/nw/.tmp/gpu0.lock")
    lock.parent.mkdir(parents=True, exist_ok=True)
    # flock serializes short holds on GPU0
    cmd = [
        "flock",
        str(lock),
        str(tools["cuda"]),
        dim,
        str(seed),
        str(cx0),
        str(cz0),
        str(ncx),
        str(ncz),
        "-o",
        str(path),
    ]
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, env=env)


def magma_to_sparse_vanilla(
    tools: dict, seed: int, world_type: int, cx0: int, cz0: int, ncx: int, ncz: int, out: Path
) -> None:
    """world_dump --states --world-type WT, then unpack id = packed>>4."""
    out.parent.mkdir(parents=True, exist_ok=True)
    bin_path = out.with_suffix(".crws.bin")
    err_path = out.with_suffix(".world_dump.err")
    with open(err_path, "w") as err:
        run(
            [
                str(tools["world_dump"]),
                "--seed",
                str(seed),
                "--cx0",
                str(cx0),
                "--cz0",
                str(cz0),
                "--ncx",
                str(ncx),
                "--ncz",
                str(ncz),
                "--world-type",
                str(world_type),
                "--states",
                "--out",
                str(bin_path),
            ],
            stderr=err,
        )
    # Convert CRWS packed vanilla -> sparse vanilla id (meta discarded: terrain meta is 0)
    uv = [
        "uv",
        "run",
        "--no-project",
        "--with",
        "numpy",
        "python",
        str(tools["crwd"]),
        str(bin_path),
        "-o",
        str(out.with_suffix(".packed.txt")),
    ]
    run(uv)
    packed = load_sparse(out.with_suffix(".packed.txt"))
    with open(out, "w") as f:
        for (x, y, z), st in sorted(packed.items()):
            vid = st >> 4
            if vid:
                f.write(f"{x},{y},{z},{vid}\n")


def inject_proof(out: Path) -> dict:
    """Prove the harness detects a synthetic one-cell divergence."""
    out.mkdir(parents=True, exist_ok=True)
    a = out / "inject_a.txt"
    b = out / "inject_b.txt"
    # 3 cells match, 1 differs
    a.write_text("0,64,0,87\n1,64,0,87\n2,64,0,11\n3,64,0,112\n")
    b.write_text("0,64,0,87\n1,64,0,88\n2,64,0,11\n3,64,0,112\n")  # 87->88 at (1,64,0)
    rec = diff_record(load_sparse(a), load_sparse(b), "inject_proof")
    ok = (
        rec["diff_cells"] == 1
        and rec["first_diff"] is not None
        and rec["first_diff"]["x"] == 1
        and rec["first_diff"]["a"] == 87
        and rec["first_diff"]["b"] == 88
        and rec["class"].get("soul_sand", 0) + rec["class"].get("netherrack", 0) == 1
    )
    rec["harness_detects_injected"] = bool(ok)
    if not ok:
        raise SystemExit(f"INJECT PROOF FAILED: {json.dumps(rec, indent=2)}")
    print("INJECT_PROOF PASS: harness reports exactly 1 injected cell diff", flush=True)
    return rec


def fmt_table(rows: list[dict], keys: list[str]) -> str:
    widths = {k: max(len(k), *(len(str(r.get(k, ""))) for r in rows)) for k in keys}
    head = "  ".join(k.ljust(widths[k]) for k in keys)
    sep = "  ".join("-" * widths[k] for k in keys)
    lines = [head, sep]
    for r in rows:
        lines.append("  ".join(str(r.get(k, "")).ljust(widths[k]) for k in keys))
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--out",
        type=Path,
        default=ROOT / "verify" / "worldgen" / "out_nether",
    )
    ap.add_argument("--seeds", type=int, nargs="+", default=DEFAULT_SEEDS)
    ap.add_argument("--skip-cuda", action="store_true")
    ap.add_argument("--skip-magma", action="store_true")
    ap.add_argument("--skip-inject", action="store_true")
    args = ap.parse_args()

    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / "report.txt"
    json_path = out / "census.json"

    def log(msg: str = "") -> None:
        print(msg, flush=True)
        with open(report_path, "a") as f:
            f.write(msg + "\n")

    report_path.write_text("")
    log("=== nether_census: Nether + End terrain (blaze CPU/CUDA, blaze/magma) ===")
    log(f"date: {subprocess.check_output(['date', '-u', '+%Y-%m-%dT%H:%M:%SZ'], text=True).strip()}")
    log(f"root: {ROOT}")
    log(f"seeds: {args.seeds}")
    log("")
    log("REGIONS:")
    log(
        f"  nether origin:   chunks ({NETHER_ORIGIN['cx0']},{NETHER_ORIGIN['cz0']})"
        f"+{NETHER_ORIGIN['ncx']}x{NETHER_ORIGIN['ncz']}  "
        f"(world xz in [{-2*16},{2*16}) x [{-2*16},{2*16}))"
    )
    log(
        f"  nether fortress: 3x3 chunks centered on first ft_can_spawn in "
        f"[-{FORT_SCAN_RADIUS},{FORT_SCAN_RADIUS}]^2 per seed"
    )
    log(
        f"  end island:      chunks ({END_ISLAND['cx0']},{END_ISLAND['cz0']})"
        f"+{END_ISLAND['ncx']}x{END_ISLAND['ncz']}  "
        f"(covers main island + fixed pillar xz from ender_dragon.h)"
    )
    log("  scope: terrain+caves+fortress (nether); island terrain (end).")
    log("         populate fire/glowstone/quartz/springs/mushrooms SKIPPED.")
    log("         MapGenEndSpike not in cpe_provide_chunk (not compared as gen).")
    log("")

    tools = build_tools(out, skip_cuda=args.skip_cuda)

    inject_rec = None
    if not args.skip_inject:
        inject_rec = inject_proof(out)
        log(f"inject_proof: {json.dumps(inject_rec)}")
        log("")

    results: dict = {
        "regions": {
            "nether_origin": NETHER_ORIGIN,
            "nether_fortress_half": NETHER_FORT_HALF,
            "end_island": END_ISLAND,
            "fort_scan_radius": FORT_SCAN_RADIUS,
        },
        "seeds": {},
        "inject_proof": inject_rec,
        "live_sim": None,
    }

    # Live-sim dimension support probe (evidence-only; no GPU needed)
    blaze_core = (ROOT / "blaze" / "env" / "blaze_core.h").read_text()
    dim_always_0 = "dimension is always 0 here" in blaze_core
    results["live_sim"] = {
        "supported": False,
        "reason": (
            "blaze/env batched env is overworld-only: blaze_core.h documents "
            "'dimension is always 0 here and id 51 edits are unreachable' "
            "(nether water-vaporize / portal ignite not ported into the env). "
            "No dimension-selection API on VecBlaze; skip N=256x2000 nether tick probe."
        ),
        "evidence_file": "blaze/env/blaze_core.h",
        "evidence_snippet": "dimension is always 0 here and id 51 edits are unreachable",
        "matched": dim_always_0,
    }
    log("=== live-sim tick probe ===")
    log(f"SKIP: {results['live_sim']['reason']}")
    log(f"evidence: {results['live_sim']['evidence_file']} matched={dim_always_0}")
    log("")

    nether_cpu_cuda_rows = []
    nether_blaze_magma_rows = []
    end_cpu_cuda_rows = []
    end_blaze_magma_rows = []
    fort_table = []

    for seed in args.seeds:
        sdir = out / f"seed_{seed}"
        sdir.mkdir(parents=True, exist_ok=True)
        seed_rec: dict = {"seed": seed}
        log(f"========== seed {seed} ==========")

        # Fortress location
        fort = find_fortress(tools["cpu"], seed)
        seed_rec["fortress"] = None
        if fort:
            fcx, fcz = fort
            seed_rec["fortress"] = {"cx": fcx, "cz": fcz}
            fort_table.append({"seed": seed, "fcx": fcx, "fcz": fcz})
            log(f"fortress start: cx={fcx} cz={fcz}")
        else:
            fort_table.append({"seed": seed, "fcx": "NONE", "fcz": "NONE"})
            log(f"fortress start: NONE within +/-{FORT_SCAN_RADIUS}")

        # ---- Nether origin ----
        no = NETHER_ORIGIN
        cpu_path = sdir / "nether_origin_cpu.txt"
        dump_cpu(tools, "nether", seed, no["cx0"], no["cz0"], no["ncx"], no["ncz"], cpu_path)
        cpu_map = load_sparse(cpu_path)

        if not args.skip_cuda:
            cuda_path = sdir / "nether_origin_cuda.txt"
            dump_cuda(
                tools, "nether", seed, no["cx0"], no["cz0"], no["ncx"], no["ncz"], cuda_path
            )
            rec = diff_record(cpu_map, load_sparse(cuda_path), f"nether_origin_cpu_cuda_s{seed}")
            seed_rec["nether_origin_cpu_cuda"] = rec
            nether_cpu_cuda_rows.append(
                {
                    "seed": seed,
                    "region": "origin",
                    "diff": rec["diff_cells"],
                    "a_cells": rec["a_cells"],
                    "b_cells": rec["b_cells"],
                    "first": (
                        f"{rec['first_diff']['x']},{rec['first_diff']['y']},{rec['first_diff']['z']}"
                        if rec["first_diff"]
                        else "-"
                    ),
                    "classes": json.dumps(rec["class"], sort_keys=True),
                }
            )
            log(f"nether origin CPU vs CUDA: diff_cells={rec['diff_cells']} first={rec['first_diff']}")

        if not args.skip_magma:
            mag_path = sdir / "nether_origin_magma.txt"
            magma_to_sparse_vanilla(
                tools, seed, 2, no["cx0"], no["cz0"], no["ncx"], no["ncz"], mag_path
            )
            rec = diff_record(cpu_map, load_sparse(mag_path), f"nether_origin_blaze_magma_s{seed}")
            seed_rec["nether_origin_blaze_magma"] = rec
            nether_blaze_magma_rows.append(
                {
                    "seed": seed,
                    "region": "origin",
                    "diff": rec["diff_cells"],
                    "a_cells": rec["a_cells"],
                    "b_cells": rec["b_cells"],
                    "first": (
                        f"{rec['first_diff']['x']},{rec['first_diff']['y']},{rec['first_diff']['z']}"
                        if rec["first_diff"]
                        else "-"
                    ),
                    "classes": json.dumps(rec["class"], sort_keys=True),
                }
            )
            log(
                f"nether origin blaze vs magma: diff_cells={rec['diff_cells']} first={rec['first_diff']}"
            )

        # ---- Nether fortress region ----
        if fort:
            fcx, fcz = fort
            fcx0, fcz0 = fcx - NETHER_FORT_HALF, fcz - NETHER_FORT_HALF
            n = 2 * NETHER_FORT_HALF + 1
            fcpu = sdir / "nether_fort_cpu.txt"
            dump_cpu(tools, "nether", seed, fcx0, fcz0, n, n, fcpu)
            fcpu_map = load_sparse(fcpu)
            seed_rec["nether_fort_region"] = {
                "cx0": fcx0,
                "cz0": fcz0,
                "ncx": n,
                "ncz": n,
            }

            if not args.skip_cuda:
                fcuda = sdir / "nether_fort_cuda.txt"
                dump_cuda(tools, "nether", seed, fcx0, fcz0, n, n, fcuda)
                rec = diff_record(
                    fcpu_map, load_sparse(fcuda), f"nether_fort_cpu_cuda_s{seed}"
                )
                seed_rec["nether_fort_cpu_cuda"] = rec
                nether_cpu_cuda_rows.append(
                    {
                        "seed": seed,
                        "region": "fortress",
                        "diff": rec["diff_cells"],
                        "a_cells": rec["a_cells"],
                        "b_cells": rec["b_cells"],
                        "first": (
                            f"{rec['first_diff']['x']},{rec['first_diff']['y']},{rec['first_diff']['z']}"
                            if rec["first_diff"]
                            else "-"
                        ),
                        "classes": json.dumps(rec["class"], sort_keys=True),
                    }
                )
                log(
                    f"nether fort CPU vs CUDA: diff_cells={rec['diff_cells']} first={rec['first_diff']}"
                )

            if not args.skip_magma:
                fmag = sdir / "nether_fort_magma.txt"
                magma_to_sparse_vanilla(tools, seed, 2, fcx0, fcz0, n, n, fmag)
                rec = diff_record(
                    fcpu_map, load_sparse(fmag), f"nether_fort_blaze_magma_s{seed}"
                )
                seed_rec["nether_fort_blaze_magma"] = rec
                nether_blaze_magma_rows.append(
                    {
                        "seed": seed,
                        "region": "fortress",
                        "diff": rec["diff_cells"],
                        "a_cells": rec["a_cells"],
                        "b_cells": rec["b_cells"],
                        "first": (
                            f"{rec['first_diff']['x']},{rec['first_diff']['y']},{rec['first_diff']['z']}"
                            if rec["first_diff"]
                            else "-"
                        ),
                        "classes": json.dumps(rec["class"], sort_keys=True),
                    }
                )
                log(
                    f"nether fort blaze vs magma: diff_cells={rec['diff_cells']} first={rec['first_diff']}"
                )

        # ---- End island ----
        eo = END_ISLAND
        ecpu = sdir / "end_island_cpu.txt"
        dump_cpu(tools, "end", seed, eo["cx0"], eo["cz0"], eo["ncx"], eo["ncz"], ecpu)
        ecpu_map = load_sparse(ecpu)

        if not args.skip_cuda:
            ecuda = sdir / "end_island_cuda.txt"
            dump_cuda(
                tools, "end", seed, eo["cx0"], eo["cz0"], eo["ncx"], eo["ncz"], ecuda
            )
            rec = diff_record(ecpu_map, load_sparse(ecuda), f"end_island_cpu_cuda_s{seed}")
            seed_rec["end_island_cpu_cuda"] = rec
            end_cpu_cuda_rows.append(
                {
                    "seed": seed,
                    "diff": rec["diff_cells"],
                    "a_cells": rec["a_cells"],
                    "b_cells": rec["b_cells"],
                    "first": (
                        f"{rec['first_diff']['x']},{rec['first_diff']['y']},{rec['first_diff']['z']}"
                        if rec["first_diff"]
                        else "-"
                    ),
                    "classes": json.dumps(rec["class"], sort_keys=True),
                }
            )
            log(f"end island CPU vs CUDA: diff_cells={rec['diff_cells']} first={rec['first_diff']}")

        if not args.skip_magma:
            emag = sdir / "end_island_magma.txt"
            magma_to_sparse_vanilla(
                tools, seed, 3, eo["cx0"], eo["cz0"], eo["ncx"], eo["ncz"], emag
            )
            rec = diff_record(
                ecpu_map, load_sparse(emag), f"end_island_blaze_magma_s{seed}"
            )
            seed_rec["end_island_blaze_magma"] = rec
            end_blaze_magma_rows.append(
                {
                    "seed": seed,
                    "diff": rec["diff_cells"],
                    "a_cells": rec["a_cells"],
                    "b_cells": rec["b_cells"],
                    "first": (
                        f"{rec['first_diff']['x']},{rec['first_diff']['y']},{rec['first_diff']['z']}"
                        if rec["first_diff"]
                        else "-"
                    ),
                    "classes": json.dumps(rec["class"], sort_keys=True),
                }
            )
            log(
                f"end island blaze vs magma: diff_cells={rec['diff_cells']} first={rec['first_diff']}"
            )

        results["seeds"][str(seed)] = seed_rec
        log("")

    # Summary tables
    log("=== fortress starts (scan radius "
        f"+/-{FORT_SCAN_RADIUS}) ===")
    log(fmt_table(fort_table, ["seed", "fcx", "fcz"]))
    log("")

    if nether_cpu_cuda_rows:
        log("=== Nether blaze CPU vs CUDA ===")
        log(
            fmt_table(
                nether_cpu_cuda_rows,
                ["seed", "region", "diff", "a_cells", "b_cells", "first", "classes"],
            )
        )
        log("")

    if nether_blaze_magma_rows:
        log("=== Nether blaze CPU vs magma ===")
        log(
            fmt_table(
                nether_blaze_magma_rows,
                ["seed", "region", "diff", "a_cells", "b_cells", "first", "classes"],
            )
        )
        log("")

    if end_cpu_cuda_rows:
        log("=== End blaze CPU vs CUDA ===")
        log(
            fmt_table(
                end_cpu_cuda_rows,
                ["seed", "diff", "a_cells", "b_cells", "first", "classes"],
            )
        )
        log("")

    if end_blaze_magma_rows:
        log("=== End blaze CPU vs magma ===")
        log(
            fmt_table(
                end_blaze_magma_rows,
                ["seed", "diff", "a_cells", "b_cells", "first", "classes"],
            )
        )
        log("")

    # Ranked divergences
    ranked = []
    for seed_s, sr in results["seeds"].items():
        for key in (
            "nether_origin_cpu_cuda",
            "nether_fort_cpu_cuda",
            "nether_origin_blaze_magma",
            "nether_fort_blaze_magma",
            "end_island_cpu_cuda",
            "end_island_blaze_magma",
        ):
            rec = sr.get(key)
            if not rec or rec["diff_cells"] == 0:
                continue
            ranked.append(
                {
                    "seed": int(seed_s),
                    "cmp": key,
                    "diff_cells": rec["diff_cells"],
                    "class": rec["class"],
                    "first_diff": rec["first_diff"],
                    "diff_sha256": rec["diff_sha256"],
                }
            )
    ranked.sort(key=lambda r: (-r["diff_cells"], r["seed"], r["cmp"]))
    results["ranked"] = ranked

    log("=== ranked divergences (nonzero only) ===")
    if not ranked:
        log("(none - all compared regions byte-identical on sparse non-air cells)")
    else:
        for i, r in enumerate(ranked, 1):
            log(
                f"{i}. seed={r['seed']} {r['cmp']} diff_cells={r['diff_cells']} "
                f"class={r['class']} first={r['first_diff']}"
            )
    log("")
    log(f"json: {json_path}")
    log(f"report: {report_path}")

    json_path.write_text(json.dumps(results, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    # numpy imported to match project UV env pattern; used if needed for bulk
    _ = np
    raise SystemExit(main())
