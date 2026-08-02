#!/usr/bin/env python3
"""Diff a tape's saved Anvil SkyLight against magma at replay tick zero.

The comparison is block-id gated: cells whose saved and replayed worlds differ
are reported separately and excluded from the light mismatch count.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys

import numpy as np
from nbt.region import InconceivedChunk, RegionFile, RegionFileFormatError

HERE = Path(__file__).resolve().parent
MAGMA = HERE.parents[1] / "magma"
SCRATCH_ROOT = Path(os.path.expanduser("~/dev/nw/.tmp/skylight_diff"))
WATER_IDS = (8, 9)
SECTION_VOLUME = 16 * 16 * 16
# gm_runtime_tick increments r.tick before script.c's diagnostic dump point.
# Runtime tick 1 is therefore the world/light state produced by tape tick 0.
DUMP_RUNTIME_TICK = 1

sys.path.insert(0, str(HERE))
import replay_tape as replay  # noqa: E402


def _nibbles(tag) -> np.ndarray:
    packed = np.frombuffer(bytes(tag.value), dtype=np.uint8)
    out = np.empty(packed.size * 2, dtype=np.uint8)
    out[0::2] = packed & 0x0F
    out[1::2] = packed >> 4
    return out


def _read_saved_chunk(region: Path, cx: int, cz: int):
    """Return saved state, skylight, and saved-section mask for one chunk."""
    region_file = region / f"r.{cx >> 5}.{cz >> 5}.mca"
    # RegionFile(filename=...) opens with r+b even though get_chunk is
    # read-only. Oracle saves may deliberately be mounted read-only, so pass
    # an rb file object instead.
    with region_file.open("rb") as stream:
        rf = RegionFile(fileobj=stream)
        chunk = rf.get_chunk(cx & 31, cz & 31)
    state = np.zeros((16, 256, 16), dtype=np.uint16)
    sky = np.zeros((16, 256, 16), dtype=np.uint8)
    present = np.zeros((16, 256, 16), dtype=bool)
    section_bases = []
    for sec in chunk["Level"]["Sections"]:
        sy = int(sec["Y"].value)
        if sy < 0 or sy >= 16 or "SkyLight" not in sec:
            continue
        y0 = sy * 16
        ids = np.frombuffer(bytes(sec["Blocks"].value), dtype=np.uint8).astype(
            np.uint16
        )
        if "Add" in sec:
            ids |= _nibbles(sec["Add"]).astype(np.uint16) << 8
        states = (ids << 4) | _nibbles(sec["Data"]).astype(np.uint16)
        # Anvil section order is y,z,x. Comparison arrays are x,y,z.
        state[:, y0 : y0 + 16, :] = np.transpose(
            states.reshape(16, 16, 16), (2, 0, 1)
        )
        sky[:, y0 : y0 + 16, :] = np.transpose(
            _nibbles(sec["SkyLight"]).reshape(16, 16, 16), (2, 0, 1)
        )
        present[:, y0 : y0 + 16, :] = True
        section_bases.append(y0)
    if not section_bases:
        raise ValueError("chunk has no saved SkyLight sections")
    return state, sky, present, max(section_bases)


def _render_distance(tape: Path) -> int:
    try:
        meta = json.loads(tape.with_suffix(".meta.json").read_text())
        return int(meta["options"]["renderDistance"])
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError):
        return 8


def _path_centers(header: dict, ticks: list[dict]) -> list[tuple[int, int]]:
    centers = []
    seen = set()
    dimension = int(header.get("dim", 0))
    x, z = float(header["x"]), float(header["z"])
    for row in [header, *ticks]:
        dimension = int(row.get("dim", dimension))
        x = float(row.get("x", x))
        z = float(row.get("z", z))
        if dimension != 0:
            continue
        center = (math.floor(x) // 16, math.floor(z) // 16)
        if center not in seen:
            centers.append(center)
            seen.add(center)
    return centers


def _load_oracle(tape: Path, header: dict, ticks: list[dict]):
    radius = _render_distance(tape)
    centers = _path_centers(header, ticks)
    wanted = {
        (cx + dx, cz + dz)
        for cx, cz in centers
        for dx in range(-radius, radius + 1)
        for dz in range(-radius, radius + 1)
    }
    region = tape.with_suffix("").with_name(tape.stem + "_world") / "region"
    if not region.is_dir():
        raise SystemExit(f"{tape}: no saved Overworld region directory")
    chunks = {}
    for cx, cz in sorted(wanted):
        try:
            chunks[(cx, cz)] = _read_saved_chunk(region, cx, cz)
        except (
            FileNotFoundError,
            InconceivedChunk,
            RegionFileFormatError,
            IndexError,
            KeyError,
            ValueError,
        ):
            continue
    if not chunks:
        raise SystemExit(f"{tape}: no saved SkyLight chunks on the recorded path")
    return radius, centers, chunks


def _opacity_table() -> np.ndarray:
    """Read magma's vanilla-derived block opacity table for diagnostics."""
    table = np.full(4096, 255, dtype=np.int16)
    source = (MAGMA.parent / "blaze" / "core" / "block_props_table.h").read_text()
    row = re.compile(
        r"case\s+(\d+):\s+d\s*=\s*\(BptProps\)"
        r"\{\s*[^,]+,\s*\d+,\s*(\d+),"
    )
    for block_id, opacity in row.findall(source):
        block_id = int(block_id)
        if block_id < len(table):
            table[block_id] = int(opacity)
    # light.c's one explicit KEEP-table correction.
    table[175] = 0
    return table


def _column_baseline(
    states: np.ndarray, top_segment: int, opacity: np.ndarray
) -> np.ndarray:
    """Chunk.generateSkylightMap's straight-down ladder for classification."""
    out = np.zeros_like(states, dtype=np.uint8)
    ids = states >> 4
    top = min(top_segment + 15, 255)
    for x in range(16):
        for z in range(16):
            level = 15
            y = top
            while y > 0 and level > 0:
                op = int(opacity[min(int(ids[x, y, z]), len(opacity) - 1)])
                if op == 0 and level != 15:
                    op = 1
                level -= op
                if level > 0:
                    out[x, y, z] = level
                y -= 1
    return out


def _ensure_game() -> Path:
    game = MAGMA / "magma_game"
    subprocess.run(["make", "-C", str(MAGMA), "game"], check=True)
    if not game.is_file():
        raise SystemExit("magma_game build succeeded but binary is missing")
    return game


def _diagnostic_events(
    tape: Path, header: dict, ticks: list[dict], scratch: Path
) -> list[dict]:
    full = scratch / "magma_script.full.jsonl"
    replay.tape_to_script(header, ticks, str(full), tape_path=str(tape))
    events = [json.loads(line) for line in full.read_text().splitlines()]
    events = [event for event in events if int(event["tick"]) == 0]

    # snapshot_patch's nbt dependency opens region filenames with r+b. If the
    # oracle save is mounted read-only, replay generation cannot refresh a
    # stale cache and returns a script without any snapshot events. The cache
    # itself is the exact sparse id/meta delta we need and is read-only input,
    # so consume it directly rather than silently comparing against worldgen.
    if not any(event.get("type") == "snapshot_block" for event in events):
        cache = tape.with_suffix(tape.suffix + ".snapshot_patch.jsonl")
        if cache.is_file():
            cached = [
                json.loads(line)
                for line in cache.read_text().splitlines()
                if line.strip()
            ]
            events.extend(
                {**event, "tick": 0}
                for event in cached
                if event.get("type") in ("snapshot_region", "snapshot_block")
            )
    return events


def _batches(
    centers: list[tuple[int, int]],
    chunks: dict,
    radius: int,
) -> list[tuple[tuple[int, int], set[tuple[int, int]]]]:
    pending = set(chunks)
    batches = []
    while pending:
        candidates = []
        for center in centers:
            cx, cz = center
            covered = {
                chunk
                for chunk in pending
                if abs(chunk[0] - cx) <= radius and abs(chunk[1] - cz) <= radius
            }
            candidates.append((len(covered), center, covered))
        count, center, covered = max(candidates, key=lambda item: item[0])
        if count == 0:
            # This can only happen for a malformed path/chunk selection.
            center = next(iter(pending))
            covered = {center}
        batches.append((center, covered))
        pending -= covered
    return batches


def _dump_batch(
    game: Path,
    tape: Path,
    header: dict,
    base_events: list[dict],
    center: tuple[int, int],
    covered: set[tuple[int, int]],
    radius: int,
    chunks: dict,
    scratch: Path,
):
    cx, cz = center
    x0, x1 = (cx - radius) * 16, (cx + radius + 1) * 16 - 1
    z0, z1 = (cz - radius) * 16, (cz + radius + 1) * 16 - 1
    y0 = min(np.where(chunks[key][2])[1].min() for key in covered)
    y1 = max(np.where(chunks[key][2])[1].max() for key in covered)

    events = list(base_events)
    events.append(
        {
            "tick": 0,
            "type": "snapshot_region",
            "dim": 0,
            "cx": cx,
            "cz": cz,
            "radius": radius,
        }
    )
    # The original replay's filtered tick-zero patch is authoritative. Reapply
    # this batch after snapshot_region because the toroidal pool may have
    # recycled chunks while earlier path regions were loaded.
    for event in base_events:
        if (
            event.get("type") == "snapshot_block"
            and int(event.get("dim", 0)) == 0
            and x0 <= int(event["x"]) <= x1
            and z0 <= int(event["z"]) <= z1
        ):
            events.append(event)
    # Keep the runtime's normal per-tick player window on this batch. Without
    # this diagnostic-only pose, a distant recorded-path batch is immediately
    # evicted again when tick 0 recenters on the tape header position; unloaded
    # air then looks world-matched but falsely reports sky=0.
    events.append(
        {
            "tick": 0,
            "type": "set_pose",
            "x": cx * 16 + 0.5,
            "y": float(header["y"]),
            "z": cz * 16 + 0.5,
            "yaw": float(header["yaw"]),
            "pitch": float(header["pitch"]),
        }
    )

    batch_dir = scratch / f"c{cx}_{cz}"
    batch_dir.mkdir(parents=True, exist_ok=True)
    script = batch_dir / "magma_script.jsonl"
    script.write_text("".join(json.dumps(event) + "\n" for event in events))
    state = batch_dir / "magma_state.jsonl"
    frames = batch_dir / "magma_frames.npy"

    magma_state = {
        key: np.zeros((16, 256, 16), dtype=np.uint16) for key in covered
    }
    magma_sky = {key: np.zeros((16, 256, 16), dtype=np.uint8) for key in covered}
    state_seen = {key: np.zeros((16, 256, 16), dtype=bool) for key in covered}
    sky_seen = {key: np.zeros((16, 256, 16), dtype=bool) for key in covered}

    env = dict(os.environ)
    dump_spec = f"{DUMP_RUNTIME_TICK},{x0},{x1},{y0},{y1},{z0},{z1}"
    world = "superflat" if str(header.get("world", "")).endswith("_flat") else "default"
    cmd = [
        str(game),
        "--headless",
        "--world",
        world,
        "--seed",
        str(int(header["seed"])),
        "--ticks",
        "1",
        "--width",
        "1",
        "--height",
        "1",
        "--script",
        str(script),
        "--state-out",
        str(state),
        "--mobs",
        "off",
        "--daylight",
        "off",
        "--frames-out",
        str(frames),
        "--set",
        f"dump={dump_spec}",
        "--set",
        f"dump_light={dump_spec}",
    ]
    block_line = re.compile(
        rf"^\[dump t{DUMP_RUNTIME_TICK} y=(\d+)\](.*)$"
    )
    block_segment = re.compile(r" z(-?\d+):((?: \d+/\d+)+)")
    light_line = re.compile(
        r"^(-?\d+) (\d+) (-?\d+) (\d+) (\d+)$"
    )
    proc = subprocess.Popen(
        cmd,
        cwd=MAGMA,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    assert proc.stderr is not None
    for line in proc.stderr:
        match = block_line.match(line)
        if match:
            y = int(match.group(1))
            segments = list(block_segment.finditer(match.group(2)))
            if len(segments) != z1 - z0 + 1:
                proc.kill()
                raise RuntimeError(f"short MAGMA_DUMP plane: {line[:160]!r}")
            for segment in segments:
                z = int(segment.group(1))
                cells = segment.group(2).split()
                if len(cells) != x1 - x0 + 1:
                    proc.kill()
                    raise RuntimeError(f"short MAGMA_DUMP row: {line[:160]!r}")
                for offset, cell in enumerate(cells):
                    x = x0 + offset
                    key = (x >> 4, z >> 4)
                    if key not in covered:
                        continue
                    block_id, meta = (int(part) for part in cell.split("/", 1))
                    lx, lz = x & 15, z & 15
                    magma_state[key][lx, y, lz] = (block_id << 4) | meta
                    state_seen[key][lx, y, lz] = True
            continue
        match = light_line.match(line)
        if match:
            x, y, z, sky, _block = (int(part) for part in match.groups())
            key = (x >> 4, z >> 4)
            if key in covered:
                lx, lz = x & 15, z & 15
                magma_sky[key][lx, y, lz] = sky
                sky_seen[key][lx, y, lz] = True
    rc = proc.wait()
    if rc != 0:
        raise RuntimeError(f"magma diagnostic replay failed with rc={rc}")
    return magma_state, magma_sky, state_seen, sky_seen


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Compare saved Anvil SkyLight with magma at replay t=0, excluding "
            "cells whose block ids differ."
        )
    )
    parser.add_argument("tape", help="tape JSONL path")
    parser.add_argument(
        "--summary",
        action="store_true",
        help="print grouped counts without one line per mismatching cell",
    )
    parser.add_argument(
        "--scratch",
        type=Path,
        help="diagnostic output directory (default: shared disk scratch)",
    )
    args = parser.parse_args()

    tape = Path(args.tape).resolve()
    if not tape.is_file():
        parser.error(f"{tape}: tape not found")
    header, ticks = replay.load_tape(str(tape))
    radius, centers, oracle = _load_oracle(tape, header, ticks)
    opacity = _opacity_table()
    scratch = (
        args.scratch.resolve()
        if args.scratch
        else SCRATCH_ROOT / tape.stem
    )
    scratch.mkdir(parents=True, exist_ok=True)
    base_events = _diagnostic_events(tape, header, ticks, scratch)
    game = _ensure_game()

    magma_state = {}
    magma_sky = {}
    state_seen = {}
    sky_seen = {}
    batches = _batches(centers, oracle, radius)
    for index, (center, covered) in enumerate(batches, 1):
        print(
            f"[skylight] batch {index}/{len(batches)} center={center} "
            f"saved_chunks={len(covered)}",
            file=sys.stderr,
        )
        dumped = _dump_batch(
            game,
            tape,
            header,
            base_events,
            center,
            covered,
            radius,
            oracle,
            scratch,
        )
        for target, source in zip(
            (magma_state, magma_sky, state_seen, sky_seen), dumped
        ):
            target.update(source)

    signatures = defaultdict(list)
    world_mismatches = []
    compared = 0
    water_columns = 0
    water_column_cells = 0
    water_column_mismatches = 0
    for (cx, cz), (saved_state, saved_sky, present, top_segment) in oracle.items():
        seen = state_seen[(cx, cz)] & sky_seen[(cx, cz)]
        if not np.all(seen[present]):
            missing_mask = present & ~seen
            missing = int(np.count_nonzero(missing_mask))
            coords = np.where(missing_mask)
            state_missing = int(
                np.count_nonzero(present & ~state_seen[(cx, cz)])
            )
            sky_missing = int(
                np.count_nonzero(present & ~sky_seen[(cx, cz)])
            )
            raise RuntimeError(
                f"chunk {(cx, cz)}: {missing} saved cells were not dumped "
                f"(state={state_missing}, sky={sky_missing}, "
                f"x={coords[0].min()}..{coords[0].max()}, "
                f"y={coords[1].min()}..{coords[1].max()}, "
                f"z={coords[2].min()}..{coords[2].max()})"
            )
        replay_state = magma_state[(cx, cz)]
        replay_sky = magma_sky[(cx, cz)]
        saved_ids = saved_state >> 4
        replay_ids = replay_state >> 4
        id_match = saved_ids == replay_ids
        wxs, wys, wzs = np.where(present & ~id_match)
        for lx, y, lz in zip(wxs.tolist(), wys.tolist(), wzs.tolist()):
            world_mismatches.append(
                (
                    cx * 16 + lx,
                    y,
                    cz * 16 + lz,
                    int(saved_ids[lx, y, lz]),
                    int(replay_ids[lx, y, lz]),
                )
            )

        baseline = _column_baseline(saved_state, top_segment, opacity)
        water_column = np.any(
            np.isin(saved_ids, WATER_IDS) & present, axis=1
        )
        water_columns += int(np.count_nonzero(water_column))
        water_column_mask = np.broadcast_to(water_column[:, None, :], present.shape)
        comparable = present & id_match
        mismatch = comparable & (saved_sky != replay_sky)
        compared += int(np.count_nonzero(comparable))
        water_column_cells += int(np.count_nonzero(comparable & water_column_mask))
        water_column_mismatches += int(np.count_nonzero(mismatch & water_column_mask))
        xs, ys, zs = np.where(mismatch)
        for lx, y, lz in zip(xs.tolist(), ys.tolist(), zs.tolist()):
            block_id = int(saved_ids[lx, y, lz])
            medium = "water" if block_id in WATER_IDS else (
                "air" if block_id == 0 else "other"
            )
            origin = (
                "flood-reachable"
                if int(saved_sky[lx, y, lz]) > int(baseline[lx, y, lz])
                else "own-column"
            )
            signatures[(origin, medium)].append(
                (
                    cx * 16 + lx,
                    y,
                    cz * 16 + lz,
                    int(saved_sky[lx, y, lz]),
                    int(replay_sky[lx, y, lz]),
                    block_id,
                    int(saved_state[lx, y, lz] & 15),
                    int(baseline[lx, y, lz]),
                    bool(water_column[lx, lz]),
                )
            )

    mismatch_count = sum(len(cells) for cells in signatures.values())
    print(
        f"tape={tape.name} saved_chunks={len(oracle)} compared_cells={compared} "
        f"world_id_mismatches={len(world_mismatches)} "
        f"light_mismatches={mismatch_count}"
    )
    print(
        f"water_columns={water_columns} water_column_cells={water_column_cells} "
        f"water_column_light_mismatches={water_column_mismatches}"
    )
    for signature in sorted(signatures):
        cells = signatures[signature]
        print(f"signature={signature[0]}/{signature[1]} count={len(cells)}")
        if not args.summary:
            for x, y, z, oracle_sky, magma_sky_value, block_id, meta, base, water_col in cells:
                print(
                    f"  {x} {y} {z} oracle={oracle_sky} magma={magma_sky_value} "
                    f"id={block_id} meta={meta} column={base} "
                    f"water_column={int(water_col)}"
                )
    if world_mismatches and not args.summary:
        print(f"world-mismatched cells excluded ({len(world_mismatches)}):")
        for x, y, z, oracle_id, magma_id in world_mismatches:
            print(
                f"  {x} {y} {z} oracle_id={oracle_id} magma_id={magma_id}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
