#!/usr/bin/env python3
"""Pin DIM-04 structure/worldgen request-order permutations and census."""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import os
import pathlib
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
RECEIPT = HERE / "structure_request_order_receipt.json"
WORLD_DUMP = ROOT / "magma/trace/world_dump"
VARIABLE = ((-1, -1), (-1, 0), (0, -1), (0, 0))
FIXED = ((-1, 1), (0, 1), (1, -1), (1, 0), (1, 1))
FAMILIES = (
    "dungeon", "mineshaft", "village", "desert_temple", "jungle_temple",
    "witch_hut", "igloo", "ocean_monument", "woodland_mansion",
    "nether_fortress", "stronghold", "end_city", "end_ship",
)


def require(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)


def measure() -> list[str]:
    env = os.environ.copy()
    env.setdefault("TMPDIR", str(ROOT / ".tmp"))
    env.setdefault("UV_CACHE_DIR", str(pathlib.Path.home() / ".cache/uv"))
    if not WORLD_DUMP.is_file():
        subprocess.run(["bash", "magma/trace/build_world_dump.sh"],
                       cwd=ROOT, env=env, check=True,
                       stdout=subprocess.DEVNULL)
    hashes: list[str] = []
    with tempfile.TemporaryDirectory(
            prefix="dim04-order-", dir=ROOT / ".tmp") as directory:
        work = pathlib.Path(directory)
        for index, order in enumerate(itertools.permutations(VARIABLE)):
            prep = work / f"prep-{index:02d}.txt"
            output = work / f"world-{index:02d}.bin"
            prep.write_text("".join(
                f"{x} {z}\n" for x, z in (*order, *FIXED)),
                encoding="utf-8")
            subprocess.run([
                str(WORLD_DUMP), "--seed", "9", "--cx0", "0", "--cz0", "0",
                "--ncx", "2", "--ncz", "2", "--prep-list", str(prep),
                "--out", str(output),
            ], cwd=ROOT, env=env, check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            hashes.append(hashlib.sha256(output.read_bytes()).hexdigest())
    return hashes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--measure", action="store_true")
    args = parser.parse_args()
    hashes = measure()
    if args.measure:
        print(json.dumps(hashes, indent=2))
        return 0
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    require(receipt["schema"] == "netherite.structure_request_order"
            and receipt["version"] == 1 and receipt["todo"] == "DIM-04"
            and receipt["seed"] == 9
            and receipt["permutations"] == 24
            and receipt["families"] == list(FAMILIES),
            "DIM-04 request-order receipt identity changed")
    require(hashes == receipt["sha256_by_lexicographic_permutation"],
            "DIM-04 request-order world hashes changed")
    require(len(set(hashes)) >= 2,
            "DIM-04 request-order corpus lost its order-sensitive control")
    subprocess.run(["bash", "verify/worldgen/wrapper_gate.sh",
                    str(ROOT / ".tmp/dim04-wrapper-gate"), "0", "7", "9", "19"],
                   cwd=ROOT, env={**os.environ,
                       "TMPDIR": str(ROOT / ".tmp"),
                       "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache/uv")},
                   check=True, stdout=subprocess.DEVNULL)
    sources = "\n".join((ROOT / path).read_text(encoding="utf-8") for path in (
        "magma/game/test_village_runtime.c",
        "magma/game/test_igloo_runtime.c",
        "magma/game/test_ocean_monument_runtime.c",
        "magma/game/test_mansion_runtime.c",
        "magma/game/test_stronghold_live.c",
        "magma/game/test_end_city_runtime.c",
        "magma/game/test_structure_registry_capacity.c",
        "blaze/core/map_gen_mineshaft.h",
        "blaze/core/map_gen_fortress.h",
        "blaze/core/populate_dungeon_golden.h",
    ))
    for token in ("checkpoint", "loot", "structure", "loaded"):
        require(token in sources.lower(),
                f"DIM-04 family census lost {token!r}")
    subprocess.run(["make", "-C", "magma",
                    "game/test_structure_registry_capacity"],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    capacity = subprocess.run([
        str(ROOT / "magma/game/test_structure_registry_capacity")],
        cwd=ROOT, check=True, text=True, capture_output=True)
    require("structure_registry_capacity: PASS" in capacity.stdout,
            "DIM-04 structure registry capacity/continuation failed")
    print("PASS DIM-04 strict campaign: 13 structure families, 24 request "
          "orders with an order-sensitive control, pinned wrapper census, "
          "and checkpointed growable structure registries")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, subprocess.CalledProcessError,
            json.JSONDecodeError) as error:
        print(f"FAIL DIM-04 strict campaign: {error}")
        raise SystemExit(1)
