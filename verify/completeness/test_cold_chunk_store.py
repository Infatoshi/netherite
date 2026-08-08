#!/usr/bin/env python3
"""End-to-end paging test for complete persisted Anvil chunk stores."""

from __future__ import annotations

import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MAGMA = ROOT / "magma"
sys.path.insert(0, str(HERE))
try:
    import anvil_to_capsule as importer
finally:
    sys.path.pop(0)


class ColdStoreTestError(RuntimeError):
    pass


def _run_game(
    root: pathlib.Path, store: pathlib.Path, suffix: str,
    regions: list[int], output_chunk: int,
    mutation: tuple[int, int] | None = None,
    write_name: str | None = None,
    expected_override: tuple[int, int, int, int] | None = None,
) -> subprocess.CompletedProcess[str]:
    script = root / f"events_{suffix}.jsonl"
    state = root / f"state_{suffix}.jsonl"
    blocks = root / f"blocks_{suffix}.u16le"
    sky = root / f"sky_{suffix}.u8"
    block_light = root / f"block_light_{suffix}.u8"
    biomes = root / f"biomes_{suffix}.u8"
    heights = root / f"heights_{suffix}.u16le"
    events = [{
        "tick": 0, "type": "attach_chunk_store",
        "file": store.name, "dim": 0,
    }]
    for region_index, chunk_x in enumerate(regions):
        events.append({
            "tick": 0, "type": "snapshot_region",
            "dim": 0, "cx": chunk_x, "cz": 0, "radius": 0,
        })
        if mutation is not None and region_index == 0:
            events.append({
                "tick": 0, "type": "set_block",
                "x": 4, "y": 2, "z": 3,
                "id": mutation[0], "meta": mutation[1],
            })
    if write_name is not None:
        events.append({
            "tick": 0, "type": "write_chunk_store",
            "file": write_name, "dim": 0,
        })
    script.write_text("".join(
        json.dumps(event, separators=(",", ":")) + "\n"
        for event in events))
    wx = output_chunk * 16 + 4
    environment = dict(os.environ)
    environment.update({
        "MAGMA_CAPSULE_DIR": str(root),
        "MAGMA_NATIVE_SAVE_DIR": str(root),
        "MAGMA_BLOCKS_OUT": str(blocks),
        "MAGMA_SKY_LIGHT_OUT": str(sky),
        "MAGMA_BLOCK_LIGHT_OUT": str(block_light),
        "MAGMA_BIOMES_OUT": str(biomes),
        "MAGMA_HEIGHTS_OUT": str(heights),
        "MAGMA_BLOCKS_BOX": f"{wx},2,3,{wx},2,3",
        "MAGMA_RESTORE_ONLY": "1",
    })
    result = subprocess.run([
        str(MAGMA / "magma_game"),
        "--world", "superflat", "--headless", "--ticks", "1",
        "--view-distance", "1", "--mobs", "off", "--script", str(script),
        "--state-out", str(state), "--render", "off", "--pace", "unlimited",
    ], cwd=MAGMA, env=environment, stdout=subprocess.PIPE,
       stderr=subprocess.STDOUT, text=True, check=False)
    if result.returncode == 0:
        actual = (
            struct.unpack("<H", blocks.read_bytes())[0],
            sky.read_bytes(), block_light.read_bytes())
        if expected_override is not None:
            expected_id, expected_meta, expected_sky, expected_light = \
                expected_override
        else:
            expected_id = mutation[0] if mutation is not None else (
                35 if output_chunk == 0 else 41)
            expected_meta = mutation[1] if mutation is not None else (
                14 if output_chunk == 0 else 3)
            expected_sky = sky.read_bytes()[0] if mutation is not None else (
                12 if output_chunk == 0 else 9)
            expected_light = block_light.read_bytes()[0] if mutation is not None else (
                7 if output_chunk == 0 else 5)
        expected = (
            (expected_id << 4) | expected_meta,
            bytes([expected_sky]), bytes([expected_light]))
        if actual != expected:
            raise ColdStoreTestError(
                f"cold page mismatch: {actual!r} != {expected!r}")
        if biomes.read_bytes() != bytes([1]):
            raise ColdStoreTestError(
                f"cold biome mismatch: {biomes.read_bytes()!r} != b'\\x01'")
        expected_height = 3 if mutation is not None else 4
        if expected_override is not None:
            expected_height = 3
        if struct.unpack("<H", heights.read_bytes())[0] != expected_height:
            raise ColdStoreTestError(
                "cold height-map mismatch: "
                f"{struct.unpack('<H', heights.read_bytes())[0]} "
                f"!= {expected_height}")
    return result


def main() -> int:
    far_chunk = 19
    semantic = {"load_inputs": {"chunks": {
        importer._chunk_key(0, 0, 0):
            importer._section_document(35, 14, 12, 7),
        importer._chunk_key(0, far_chunk, 0):
            importer._section_document(41, 3, 9, 5),
    }}}
    with tempfile.TemporaryDirectory(
            prefix="netherite-cold-chunks-", dir=ROOT / ".tmp") as raw:
        root = pathlib.Path(raw)
        store = root / importer._cold_store_file(0)
        first = importer.write_cold_chunk_store(semantic, 0, store)
        second = importer.write_cold_chunk_store(
            semantic, 0, root / "repeat.bin")
        if first["sha256"] != second["sha256"] \
                or store.read_bytes() != (root / "repeat.bin").read_bytes():
            raise ColdStoreTestError("cold chunk store is not deterministic")
        result = _run_game(root, store, "far", [far_chunk], far_chunk)
        if result.returncode != 0:
            raise ColdStoreTestError(
                f"valid far page failed:\n{result.stdout}")
        # 0 and 19 collide in the default 19-wide toroidal light pool. Returning
        # to zero therefore proves epoch-based reapplication after eviction.
        result = _run_game(
            root, store, "evict_return", [0, far_chunk, 0], 0)
        if result.returncode != 0:
            raise ColdStoreTestError(
                f"evict/return page failed:\n{result.stdout}")
        result = _run_game(
            root, store, "mutate_evict_return", [0, far_chunk, 0], 0,
            mutation=(57, 1), write_name="saved.bin")
        if result.returncode != 0:
            raise ColdStoreTestError(
                f"mutate/evict/return page failed:\n{result.stdout}")
        saved_sky = (root / "sky_mutate_evict_return.u8").read_bytes()[0]
        saved_light = (root / "block_light_mutate_evict_return.u8").read_bytes()[0]
        result = _run_game(
            root, root / "saved.bin", "saved_reload", [0], 0,
            expected_override=(57, 1, saved_sky, saved_light))
        if result.returncode != 0:
            raise ColdStoreTestError(
                f"written-store reload failed:\n{result.stdout}")
        truncated = root / "truncated.bin"
        truncated.write_bytes(store.read_bytes()[:-1])
        result = _run_game(root, truncated, "truncated", [0], 0)
        if result.returncode == 0 or "invalid attach_chunk_store" not in result.stdout:
            raise ColdStoreTestError("truncated cold store did not fail closed")
    print("PASS persisted cold chunks: deterministic/page/evict/mutate/write/reload/light/truncation")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ColdStoreTestError, OSError, ValueError) as exc:
        print(f"FAIL cold chunk store: {exc}", file=sys.stderr)
        raise SystemExit(1)
