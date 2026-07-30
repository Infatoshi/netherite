#!/usr/bin/env python3
"""Vigorous nether + end worldgen tests (same process as overworld world_diff).

1) blaze Java golden == C provideChunk for multiple seeds (bitwise hex).
2) REAL Anvil DIM-1 / DIM1 (qrl_0 seed 0) vs C multi-chunk dump after free-pass
   of known populate/structures only.

Run:
  uv run --no-project --with numpy --with nbt pytest magma/tests/test_dim_worldgen.py -q
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
BLAZE = REPO / "blaze"
MAGMA = REPO / "magma"
TRACE = MAGMA / "trace"
SAVE = REPO / "java" / "Minecraft" / "run" / "saves" / "qrl_0"
NETHER_REGION = SAVE / "DIM-1" / "region"
END_REGION = SAVE / "DIM1" / "region"

# Multi-seed matrix for provideChunk Java golden (blaze oracle runner).
ORACLE_SEEDS = ["0", "7", "12345", "1337"]
ORACLE_NAMES = ["chunk_provider_nether", "chunk_provider_end"]


def _run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


@pytest.mark.parametrize("name", ORACLE_NAMES)
@pytest.mark.parametrize("seed", ORACLE_SEEDS)
def test_mc_sim_java_golden_cpu(name, seed):
    """Verbatim-Java golden == C CPU provideChunk (bitwise line dump)."""
    r = _run(
        [sys.executable, str(BLAZE / "oracle" / "runner.py"), "--cpu-only", name, seed],
        cwd=str(BLAZE),
        timeout=180,
    )
    assert r.returncode == 0, f"{name} seed={seed}\n{r.stdout}\n{r.stderr}"
    assert "PASS" in r.stdout
    assert "java == cpu" in r.stdout or "ok  java == cpu" in r.stdout


def _dim_diff(dim: str, region: Path, cx0=-2, cz0=-2, cx1=2, cz1=2):
    assert region.is_dir(), f"missing Anvil region {region} (need play/dim once on qrl_0)"
    r = _run(
        [
            sys.executable,
            str(TRACE / "dim_world_diff.py"),
            "--dim", dim,
            "--region", str(region),
            "--seed", "0",
            "--cx0", str(cx0), "--cz0", str(cz0),
            "--cx1", str(cx1), "--cz1", str(cz1),
            "--gate",
        ],
        cwd=str(TRACE),
        timeout=300,
    )
    return r


def test_nether_anvil_providechunk_parity():
    """5x5 chunk window: C ChunkProviderHell vs REAL DIM-1 Anvil after free-pass."""
    r = _dim_diff("nether", NETHER_REGION)
    assert r.returncode == 0, r.stdout + "\n" + r.stderr
    assert "PASS" in r.stdout
    # 100% preferred; allow tiny live-save residue (teleporter pad / E2E edits)
    assert "exact-match 99.99" in r.stdout or "exact-match 100." in r.stdout


def test_end_anvil_providechunk_parity():
    """5x5 chunk window: C ChunkProviderEnd vs REAL DIM1 Anvil after free-pass.

    Free-pass covers end spikes (obsidian), crystal cages, exit portal bedrock —
    the structures provideChunk deliberately omits. Residual without free-pass is
    the obvious End miss (pillars / platform), not terrain shape.
    """
    r = _dim_diff("end", END_REGION)
    assert r.returncode == 0, r.stdout + "\n" + r.stderr
    assert "PASS" in r.stdout


def test_end_stone_id_mapping_not_stone_confusion():
    """CE_END_STONE maps to vanilla 121 in dumps — never leave as small-int 1."""
    sys.path.insert(0, str(TRACE))
    # build dump of single chunk 0,0 and assert only {0,121} appear
    with tempfile.TemporaryDirectory() as td:
        dumper = Path(td) / "dump"
        subprocess.run(
            [
                "cc", "-O2", "-ffp-contract=off",
                f"-I{BLAZE / 'core'}",
                str(TRACE / "dim_worldgen_dump.c"),
                "-o", str(dumper), "-lm",
            ],
            check=True,
        )
        out = Path(td) / "e.mcbd"
        with open(out, "wb") as f:
            subprocess.run(
                [str(dumper), "end", "0", "0", "0", "0", "0"],
                stdout=f, check=True,
            )
        import numpy as np
        ids = (np.fromfile(out, dtype="<u2") >> 4).astype(np.int32)
        uniq = set(ids.tolist())
        assert uniq <= {0, 121}, f"end dump ids {uniq} — end_stone must be 121 not 1"
        assert 121 in uniq, "central island chunk must contain end_stone"


def test_nether_ids_are_vanilla_numeric():
    """Nether primer uses vanilla block ids (87 netherrack) not CB_* remaps."""
    with tempfile.TemporaryDirectory() as td:
        dumper = Path(td) / "dump"
        subprocess.run(
            [
                "cc", "-O2", "-ffp-contract=off",
                f"-I{BLAZE / 'core'}",
                str(TRACE / "dim_worldgen_dump.c"),
                "-o", str(dumper), "-lm",
            ],
            check=True,
        )
        out = Path(td) / "n.mcbd"
        with open(out, "wb") as f:
            subprocess.run(
                [str(dumper), "nether", "0", "0", "0", "0", "0"],
                stdout=f, check=True,
            )
        import numpy as np
        ids = (np.fromfile(out, dtype="<u2") >> 4).astype(np.int32)
        uniq = set(ids.tolist())
        assert 87 in uniq, f"expected netherrack 87 in {uniq}"
        # CB_STONE=1 must not dominate; netherrack is 87
        assert ids[ids != 0].size > 0
        mode = int(np.bincount(ids[ids != 0]).argmax()) if (ids != 0).any() else -1
        assert mode == 87, f"dominant solid should be netherrack 87, got {mode}"


def test_cross_seed_anvil_gates_only_seed0():
    """Document: Anvil save is seed 0 only; multi-seed covered by java golden tests."""
    assert (SAVE / "level.dat").is_file() or NETHER_REGION.is_dir()


def test_end_raw_mismatch_is_obsidian_pillars_not_terrain():
    """Negative control: without free-pass, End Anvil vs C is dominated by obsidian (49).

    That is the obvious End miss (WorldGenSpikes / exit portal), not island density.
    """
    import numpy as np

    sys.path.insert(0, str(TRACE))
    from dim_world_diff import build_dumper, dump_c, load_c_mcbd
    from world_diff import read_mca_chunk

    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        dumper = build_dumper(td / "dump")
        mcbd = td / "e.mcbd"
        dump_c(dumper, "end", 0, -1, -1, 1, 1, mcbd)
        chunks = load_c_mcbd(mcbd, -1, -1, 1, 1)
        conf = {}
        mism = 0
        cells = 0
        for cz in range(-1, 2):
            for cx in range(-1, 2):
                j, _ = read_mca_chunk(str(END_REGION), cx, cz)
                c = chunks[(cx, cz)]
                bad = j != c
                mism += int(bad.sum())
                cells += j.size
                xs, ys, zs = np.where(bad)
                for x, y, z in zip(xs.tolist(), ys.tolist(), zs.tolist()):
                    pair = (int(j[x, y, z]), int(c[x, y, z]))
                    conf[pair] = conf.get(pair, 0) + 1
        assert mism > 0, "expected raw End mismatches (pillars/platform)"
        top = max(conf.items(), key=lambda p: p[1])
        # Dominant residual must involve obsidian (49) or bedrock platform (7)
        assert top[0][0] in (49, 7), f"unexpected dominant residual {top}"
        # Terrain is still excellent: raw exact >> 95%
        assert (cells - mism) / cells > 0.95


if __name__ == "__main__":
    # allow direct run without pytest collection noise
    raise SystemExit(pytest.main([__file__, "-v", "--tb=short"]))
