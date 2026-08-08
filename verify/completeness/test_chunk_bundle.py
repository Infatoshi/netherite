#!/usr/bin/env python3
"""End-to-end mutation test for the compact Anvil active-chunk loader."""

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


class ChunkBundleTestError(RuntimeError):
    pass


def run_game(root: pathlib.Path, bundle: pathlib.Path, suffix: str) -> subprocess.CompletedProcess:
    script = root / f"events_{suffix}.jsonl"
    state = root / f"state_{suffix}.jsonl"
    blocks = root / f"blocks_{suffix}.u16le"
    sky = root / f"sky_{suffix}.u8"
    block_light = root / f"block_light_{suffix}.u8"
    script.write_text(json.dumps({
        "tick": 0,
        "type": "snapshot_chunk_bundle",
        "file": bundle.name,
        "dim": 0,
        "cx": 0,
        "cz": 0,
        "radius": 0,
    }, separators=(",", ":")) + "\n")
    environment = dict(os.environ)
    environment.update({
        "MAGMA_CAPSULE_DIR": str(root),
        "MAGMA_BLOCKS_OUT": str(blocks),
        "MAGMA_SKY_LIGHT_OUT": str(sky),
        "MAGMA_BLOCK_LIGHT_OUT": str(block_light),
        "MAGMA_BLOCKS_BOX": "4,2,3,4,2,3",
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
        expected = ((35 << 4) | 14, bytes([12]), bytes([7]))
        if actual != expected:
            raise ChunkBundleTestError(
                f"bulk bundle restore mismatch: {actual!r} != {expected!r}")
        state_row = json.loads(state.read_text().splitlines()[0])
        if state_row.get("loaded_chunk_order") != [
                [2, 0], [-1, 4], [0, 0]]:
            raise ChunkBundleTestError(
                "loaded chunk order was reconstructed from coordinates: "
                f"{state_row.get('loaded_chunk_order')!r}")
    return result


def main() -> int:
    semantic = {"load_inputs": {"chunks": {
        importer._chunk_key(0, 0, 0):
            importer._section_document(35, 14, 12, 7),
        importer._chunk_key(0, 2, 0):
            importer._section_document(1, 0, 15, 0),
        importer._chunk_key(0, -1, 4):
            importer._section_document(1, 0, 15, 0),
    }}}
    normalized = {"ok": True, "worlds": [{
        "dim": 0,
        "watched_entries": [{
            "order": 0, "x": 0, "z": 0, "ticked": True,
            "terrain": True, "light": True,
        }],
        "ticking_chunks": [{
            "x": 0, "z": 0, "ticked": True,
            "terrain": True, "light": True,
            "sections": 1, "random_tick_mask": 1,
        }],
        "loaded_chunks": [
            {"order": 0, "x": 2, "z": 0},
            {"order": 1, "x": -1, "z": 4},
            {"order": 2, "x": 0, "z": 0},
        ],
        "pending_chunk_unloads": [],
    }]}
    with tempfile.TemporaryDirectory(
            prefix="netherite-chunk-loader-", dir=ROOT / ".tmp") as raw:
        root = pathlib.Path(raw)
        bundle = root / importer.CHUNK_BUNDLE_FILE
        importer.write_active_chunk_bundle(
            semantic, normalized, 0, 0, 0, bundle)
        result = run_game(root, bundle, "exact")
        if result.returncode != 0:
            raise ChunkBundleTestError(
                f"valid chunk bundle failed:\n{result.stdout}")
        truncated = root / "truncated.bin"
        truncated.write_bytes(bundle.read_bytes()[:-1])
        result = run_game(root, truncated, "truncated")
        if result.returncode == 0 or "invalid snapshot_chunk_bundle" not in result.stdout:
            raise ChunkBundleTestError(
                "truncated chunk bundle did not fail closed")
    print("PASS native active-chunk bundle: block/meta/sky/block-light, "
          "non-coordinate loaded order, truncation")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ChunkBundleTestError, OSError, ValueError) as exc:
        print(f"FAIL chunk bundle: {exc}", file=sys.stderr)
        raise SystemExit(1)
