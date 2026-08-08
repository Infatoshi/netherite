"""Convert a recstart Anvil snapshot into the minimal magma state patch.

Only chunks visible from recorded player positions in each dimension are
considered. For each, the real 1.11.2 Anvil id+metadata state is compared
against magma's generated canonical state. Mismatches become dimension-tagged
tick-0 snapshot events and are cached next to the tape. This keeps the script
sparse while removing populate-order and evolved-save provenance from replay.
"""

from __future__ import annotations

import copy
import io
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
MAGMA = HERE.parents[1] / "magma"
TRACE = MAGMA / "trace"
SPAWNER_EVENT_TYPES = {"set_spawner_state", "add_spawner_potential"}


def _spawner_entity_types() -> dict[str, int]:
    """Use the capsule's one canonical resource-id to native-type table."""
    sys.path.insert(0, str(TRACE))
    try:
        from state_capsule import SPAWNER_ENTITY_TYPES
    finally:
        sys.path.pop(0)
    return SPAWNER_ENTITY_TYPES


def _entity_resource_id(compound, label: str) -> str:
    try:
        entity_id = str(compound["id"])
    except (KeyError, TypeError) as exc:
        raise RuntimeError(f"snapshot_patch: {label} has no entity id") from exc
    if ":" not in entity_id:
        entity_id = "minecraft:" + entity_id.lower()
    return entity_id


def _default_entity_compound(compound, entity_id: str) -> bool:
    return ({tag.name for tag in compound.tags} == {"id"}
            and _entity_resource_id(compound, "entity compound") == entity_id)


def _compound_payload(compound) -> bytes:
    """Encode a nested TAG_Compound as an empty-name root compound."""
    from nbt.nbt import NBTFile

    document = NBTFile()
    document.tags = [copy.deepcopy(tag) for tag in compound.tags]
    buffer = io.BytesIO()
    document.write_file(buffer=buffer)
    payload = buffer.getvalue()
    if not payload or len(payload) > 1 << 20:
        raise RuntimeError("snapshot_patch: spawner entity NBT exceeds 1 MiB")
    return payload


def _tag_int(compound, name: str, default: int | None = None) -> int:
    try:
        return int(compound[name].value)
    except KeyError:
        if default is not None:
            return default
        raise RuntimeError(f"snapshot_patch: spawner is missing {name}")


def _decode_spawner_tile(tile, dimension: int) -> dict:
    """Decode the bounded exact block-spawner subset used by tape replay."""
    try:
        spawn_data = tile["SpawnData"]
    except KeyError as exc:
        raise RuntimeError(
            "snapshot_patch: mob spawner is missing SpawnData") from exc
    entity_id = _entity_resource_id(spawn_data, "SpawnData")
    entity_types = _spawner_entity_types()
    if entity_id not in entity_types:
        raise RuntimeError(
            f"snapshot_patch: unsupported spawner entity {entity_id!r}")
    if not _default_entity_compound(spawn_data, entity_id):
        raise RuntimeError(
            "snapshot_patch: legacy tape custom SpawnData is not yet exact; "
            "use the state-capsule path (WORLD-06)")
    potentials = []
    for index, row in enumerate(tile.get("SpawnPotentials", [])):
        try:
            entity = row["Entity"]
        except KeyError as exc:
            raise RuntimeError(
                f"snapshot_patch: SpawnPotentials[{index}] has no Entity") \
                from exc
        potential_id = _entity_resource_id(
            entity, f"SpawnPotentials[{index}].Entity")
        if potential_id not in entity_types:
            raise RuntimeError(
                f"snapshot_patch: unsupported potential entity "
                f"{potential_id!r}")
        if not _default_entity_compound(entity, potential_id):
            raise RuntimeError(
                "snapshot_patch: legacy tape custom SpawnPotentials entity "
                "is not yet exact; use the state-capsule path (WORLD-06)")
        weight = _tag_int(row, "Weight")
        if weight <= 0:
            raise RuntimeError(
                f"snapshot_patch: SpawnPotentials[{index}] has invalid weight")
        potentials.append({
            "entity": entity_types[potential_id],
            "weight": weight,
            "payload": _compound_payload(entity),
        })
    if len(potentials) > 16:
        raise RuntimeError(
            "snapshot_patch: spawner exceeds the exact 16-potential bound")
    return {
        "dim": dimension,
        "x": _tag_int(tile, "x"),
        "y": _tag_int(tile, "y"),
        "z": _tag_int(tile, "z"),
        "entity": entity_types[entity_id],
        "delay": _tag_int(tile, "Delay"),
        "min_delay": _tag_int(tile, "MinSpawnDelay", 200),
        "max_delay": _tag_int(tile, "MaxSpawnDelay", 800),
        "spawn_count": _tag_int(tile, "SpawnCount", 4),
        "max_nearby": _tag_int(tile, "MaxNearbyEntities", 6),
        "activate_range": _tag_int(tile, "RequiredPlayerRange", 16),
        "spawn_range": _tag_int(tile, "SpawnRange", 4),
        "payload": _compound_payload(spawn_data),
        "potentials": potentials,
    }


def _read_mca_spawners(
        region: Path, cx: int, cz: int, dimension: int) -> list[dict]:
    from nbt.region import RegionFile

    path = region / f"r.{cx >> 5}.{cz >> 5}.mca"
    with path.open("rb") as source:
        rf = RegionFile(fileobj=source)
        chunk = rf.get_chunk(cx & 31, cz & 31)
        return [
            _decode_spawner_tile(tile, dimension)
            for tile in chunk["Level"].get("TileEntities", [])
            if str(tile.get("id", "")) == "minecraft:mob_spawner"
        ]


def _cache_payloads_exist(cache: Path) -> bool:
    payload_dir = _payload_dir(cache)
    try:
        with cache.open() as source:
            for line in source:
                event = json.loads(line)
                for field in ("spawn_nbt_file", "entity_nbt_file"):
                    if field in event \
                            and not (payload_dir / event[field]).is_file():
                        return False
    except (OSError, ValueError, TypeError):
        return False
    return True


def _publish_payload(path: Path, payload: bytes) -> None:
    part = path.with_suffix(path.suffix + f".{os.getpid()}.part")
    with part.open("wb") as stream:
        stream.write(payload)
    os.replace(part, path)


def _payload_dir(cache: Path) -> Path:
    return cache.with_suffix(cache.suffix + ".payloads")


def _write_spawner_events(out, cache: Path, spawners: list[dict]) -> int:
    payload_dir = _payload_dir(cache)
    payload_dir.mkdir(exist_ok=True)
    written = 0
    for spawner_index, spawner in enumerate(spawners):
        spawn_name = f"spawner_{spawner_index:04d}_spawn.nbt"
        spawn_payload = payload_dir / spawn_name
        _publish_payload(spawn_payload, spawner["payload"])
        out.write(json.dumps({
            "tick": 0,
            "type": "set_spawner_state",
            **{field: spawner[field] for field in (
                "dim", "x", "y", "z", "entity", "delay",
                "min_delay", "max_delay", "spawn_count", "max_nearby",
                "activate_range", "spawn_range")},
            "spawn_nbt_file": spawn_name,
            "default_entity_nbt": True,
        }, separators=(",", ":")) + "\n")
        written += 1
        for potential_index, potential in enumerate(spawner["potentials"]):
            entity_name = (f"spawner_{spawner_index:04d}_potential_"
                           f"{potential_index:04d}.nbt")
            entity_payload = payload_dir / entity_name
            _publish_payload(entity_payload, potential["payload"])
            out.write(json.dumps({
                "tick": 0,
                "type": "add_spawner_potential",
                "dim": spawner["dim"],
                "x": spawner["x"],
                "y": spawner["y"],
                "z": spawner["z"],
                "entity": potential["entity"],
                "weight": potential["weight"],
                "entity_nbt_file": entity_name,
                "default_entity_nbt": True,
            }, separators=(",", ":")) + "\n")
            written += 1
    return written


def _read_mca_states(region: Path, cx: int, cz: int) -> np.ndarray:
    """Read packed vanilla state id<<4|meta for one 1.11.2 chunk."""
    from nbt.region import RegionFile

    rf = RegionFile(str(region / f"r.{cx >> 5}.{cz >> 5}.mca"))
    ch = rf.get_chunk(cx & 31, cz & 31)
    out = np.zeros((16, 256, 16), dtype=np.uint16)
    for sec in ch["Level"]["Sections"]:
        sy = int(sec["Y"].value)
        ids = np.frombuffer(bytes(sec["Blocks"].value), dtype=np.uint8).astype(np.uint16)
        if "Add" in sec:
            add = np.frombuffer(bytes(sec["Add"].value), dtype=np.uint8)
            hi = np.zeros(4096, dtype=np.uint16)
            hi[0::2] = add & 0x0F
            hi[1::2] = add >> 4
            ids |= hi << 8
        data = np.frombuffer(bytes(sec["Data"].value), dtype=np.uint8)
        meta = np.zeros(4096, dtype=np.uint16)
        meta[0::2] = data & 0x0F
        meta[1::2] = data >> 4
        state = ((ids << 4) | meta).reshape(16, 16, 16)  # y,z,x
        out[:, sy * 16 : sy * 16 + 16, :] = np.transpose(state, (2, 0, 1))
    return out


def _read_magma_states(path: Path) -> dict[tuple[int, int], np.ndarray]:
    chunks = {}
    with path.open("rb") as f:
        if f.read(4) != b"CRWS":
            raise RuntimeError("world_dump does not support canonical --states output")
        f.read(8)
        cx0, cz0, ncx, ncz = struct.unpack("<iiii", f.read(16))
        for ix in range(ncx):
            for iz in range(ncz):
                raw = np.frombuffer(f.read(16 * 256 * 16 * 2), dtype=np.uint16).copy()
                f.read(16 * 16 * 4)  # biomes
                chunks[(cx0 + ix, cz0 + iz)] = np.transpose(
                    raw.reshape(16, 16, 256), (0, 2, 1)
                )
    return chunks


def _render_distance(tape: Path) -> int:
    meta = tape.with_suffix(".meta.json")
    try:
        data = json.loads(meta.read_text())
        return int(data["options"]["renderDistance"])
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError):
        return 8


def _visible_chunks(tape: Path, header: dict,
                    ticks: list[dict]) -> dict[int, set[tuple[int, int]]]:
    radius = _render_distance(tape)
    centers: dict[int, set[tuple[int, int]]] = {}
    samples = [header, *ticks]
    for row in samples:
        dimension = int(row.get("dim", header.get("dim", 0)))
        centers.setdefault(dimension, set()).add(
            (math.floor(row["x"]) // 16, math.floor(row["z"]) // 16)
        )
    return {
        dimension: {
            (cx + dx, cz + dz)
            for cx, cz in dim_centers
            for dx in range(-radius, radius + 1)
            for dz in range(-radius, radius + 1)
        }
        for dimension, dim_centers in centers.items()
    }


def _ensure_world_dump() -> Path:
    binary = TRACE / "world_dump"
    source = TRACE / "world_dump.c"
    if not binary.exists() or binary.stat().st_mtime < source.stat().st_mtime:
        subprocess.run(["bash", str(TRACE / "build_world_dump.sh")], check=True, cwd=MAGMA)
    return binary


# Blocks a vanilla BiomeDecorator run can stand vegetation on, i.e. the ids that
# can be the top of a column under the decoration pass. Used to place the
# DECORATION BAND (below) - not a claim about collision or opacity.
_GROUND_IDS = np.array([
    1, 2, 3, 4, 8, 9, 12, 13, 24, 48, 60, 78, 79, 80, 82,
    87, 88, 110, 121, 159, 172, 179,
], dtype=np.uint16)

# Height of the band above each column's ground top that the patch makes
# AUTHORITATIVE. BiomeDecorator's vegetation lands within 3 blocks of the
# surface (tall grass 1, double plants 2, reeds/cactus 3); 4 leaves a margin.
_BAND = 4


def _decoration_band(jstate: np.ndarray) -> set[tuple[int, int, int]]:
    """Cells the patch must state EXPLICITLY, not just where magma disagrees.

    Only used for chunks the probe pass could NOT observe (see `_game_states`);
    those still fall back to diffing against `world_dump`, and `world_dump`'s
    generation is not the replay's.

    magma's populate windows seed each other with their neighbours' out-of-bounds
    spill (`world/populate_mc.c` build_window donor seeding), so a window's cell
    list depends on which windows were already resident when it was built.
    `world_dump` builds them in one fixed sweep; the game builds them around a
    walking player. Measured on scenario_scenic_walk_20260729T063050Z chunk
    (-11,15), seed 3: world_dump generates 24 tallgrass cells, the game 42,
    agreeing on only 12. Cells where the save and `world_dump` HAPPEN to agree
    emit no event and keep whatever the game grew there - phantom plants,
    including one 0.8 blocks from the t=80 camera that filled a third of the
    frame with a single magnified texel.

    Stating the save's value for the whole vegetation band removes magma's
    generation from the answer for the cells that drift MOST often. It is a
    band, so it cannot cover trees: a census over the 169 chunks around the
    scenic_walk start measured 4664 cells where the game's generation differs
    from the save while world_dump agrees with it, and 4488 of those (96%) are
    logs and leaves, up to 30 blocks above the ground the band is anchored to.
    That is what `_game_states` exists to fix.
    """
    ground = np.isin(jstate >> 4, _GROUND_IDS)
    ys = np.arange(256, dtype=np.int32)[None, :, None]
    top = np.where(ground, ys, -1).max(axis=1)          # (16, 16) per column
    out = set()
    for lx in range(16):
        for lz in range(16):
            t = int(top[lx, lz])
            if t < 0:
                continue
            for y in range(t + 1, min(t + 1 + _BAND, 256)):
                out.add((lx, y, lz))
    return out


# The live chunk store is a 19x19 toroidal pool (world/light.c: light_D =
# 2*view_radius+3), and streaming ensures radius view_radius+1 = 9 around the
# player, which exactly fills it. So every chunk within +-8 of the player's
# chunk is resident, and nothing outside +-9 can be relied on.
_POOL_R = 8


def _probe_plan(header: dict, ticks: list[dict],
                wanted: dict[int, set[tuple[int, int]]]):
    """Player-centred rectangles that observe every wanted chunk while resident.

    Greedy set cover over the tape's own chunk positions: each candidate dumps
    the (2*_POOL_R+1)^2 square around where the player stands at that tick, and
    the dimension is implicit - it is whichever one the replay is in then.
    """
    side = 2 * _POOL_R + 1
    centres: dict[tuple[int, int, int], int] = {}
    for row in ticks:
        if "x" not in row or "z" not in row:
            continue
        dimension = int(row.get("dim", header.get("dim", 0)))
        key = (dimension, math.floor(float(row["x"])) // 16,
               math.floor(float(row["z"])) // 16)
        centres[key] = int(row["t"])   # latest tick standing there

    def covered(key, left):
        dimension, ccx, ccz = key
        return {c for c in left
                if c[0] == dimension and abs(c[1] - ccx) <= _POOL_R
                and abs(c[2] - ccz) <= _POOL_R}

    remaining = {(dimension, cx, cz)
                 for dimension, chunks in wanted.items() for cx, cz in chunks}
    plan = []
    while remaining and centres:
        best = max(centres,
                   key=lambda k: (len(covered(k, remaining)), centres[k]))
        hit = covered(best, remaining)
        if not hit:
            break
        dimension, ccx, ccz = best
        plan.append((centres.pop(best), dimension,
                     ccx - _POOL_R, ccz - _POOL_R, side))
        remaining -= hit
    return plan, remaining


def _game_states(tape: Path, header: dict, ticks: list[dict],
                 wanted: dict[int, set[tuple[int, int]]],
                 region_lines: list[str], scratch: Path):
    """The world THE REPLAY ITSELF generates, keyed (dim, cx, cz).

    Diffing the save against `world_dump` is diffing against the wrong world:
    decoration is populate-order dependent and world_dump sweeps while the game
    walks. So run the replay's own script once with the patch reduced to its
    `snapshot_region` ensures and no `snapshot_block`, and read the world back
    out of the running game (`--set worlddump=...` / game/script.c).

    That probe pass generates the same world the real replay will, because the
    build order is fixed by the ensure sequence and the simulated walk, and
    neither depends on the patch's block contents: `snapshot_region` is a plain
    `gm_world_ensure` (game/runtime.c gm_runtime_snapshot_region_dim) and the
    published patch's regions are exactly `region_lines`, one per 8x8 tile of
    saved chunks - a function of the tape, not of the diff. Hence the caller
    hands the identical lines in the identical order down here. Verified on
    scenario_scenic_walk_20260729T063050Z: the per-tick player chunk is
    identical across all 274 ticks with and without the block events applied.

    Chunks the plan could not observe come back missing and the caller falls
    back to world_dump plus the vegetation band for those.
    """
    import oracle_lib
    import replay_tape

    plan, _ = _probe_plan(header, ticks, wanted)
    if not plan:
        return {}, wanted

    regions = scratch / "probe_regions.jsonl"
    regions.write_text("".join(region_lines))

    script = scratch / "probe_script.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script), str(tape),
                               snapshot_override=str(regions))

    specs, outs = [], []
    for i, (tick, dimension, cx0, cz0, side) in enumerate(plan):
        path = scratch / f"probe_{i}.bin"
        outs.append((dimension, path))
        specs.append(f"{tick},{cx0},{cz0},{side},{side},{path}")

    try:
        oracle_lib.run_magma_script(
            str(script), len(ticks), None, str(scratch / "probe_state.jsonl"),
            seed=int(header["seed"]), mobs=False, daylight=False,
            world=replay_tape.magma_world(header),
            set_kv=[f"worlddump={';'.join(specs)}"], timeout=3600)
    except RuntimeError:
        # rc=2 is "event lies beyond --ticks" / an early death; the dumps that
        # already fired are still the replay's own generation. Any that did not
        # simply stay missing and fall back to world_dump below.
        pass

    states: dict[tuple[int, int, int], np.ndarray] = {}
    for dimension, path in outs:
        if not path.exists():
            continue
        for (cx, cz), block in _read_magma_states(path).items():
            # An all-zero chunk was not resident: the game never generated it
            # here. Treating that as "the game made air" would patch a whole
            # real chunk away, one snapshot_block per cell.
            if block.any():
                states[(dimension, cx, cz)] = block

    missing = {dimension: {(cx, cz) for cx, cz in chunks
                           if (dimension, cx, cz) not in states}
               for dimension, chunks in wanted.items()}
    return states, {d: c for d, c in missing.items() if c}


def ensure_snapshot_patch(tape_path: str, header: dict, ticks: list[dict]) -> Path | None:
    tape = Path(tape_path).resolve()
    snapshot = tape.with_suffix("").with_name(tape.stem + "_world")
    regions = {
        0: snapshot / "region",
        -1: snapshot / "DIM-1" / "region",
        1: snapshot / "DIM1" / "region",
    }
    if not regions[0].is_dir():
        return None
    cache = tape.with_suffix(tape.suffix + ".snapshot_patch.jsonl")
    sources = [path for region in regions.values() if region.is_dir()
               for path in region.glob("r.*.*.mca")]
    newest = max((p.stat().st_mtime for p in sources), default=0)
    # Every input that can move a generated cell: the save, the tape, this
    # module, world_dump, and - since the patch is now diffed against the
    # world the GAME generates - the generator and the probe hook themselves.
    newest = max(newest, tape.stat().st_mtime if tape.exists() else 0,
                 Path(__file__).stat().st_mtime,
                 (TRACE / "world_dump.c").stat().st_mtime,
                 (TRACE / "build_world_dump.sh").stat().st_mtime,
                 (MAGMA / "game" / "script.c").stat().st_mtime,
                 (MAGMA / "world" / "light.c").stat().st_mtime,
                 (MAGMA / "world" / "populate_mc.c").stat().st_mtime)
    if (cache.exists() and cache.stat().st_mtime >= newest
            and _cache_payloads_exist(cache)):
        return cache

    wanted = _visible_chunks(tape, header, ticks)
    java: dict[tuple[int, int, int], np.ndarray] = {}
    spawners: list[dict] = []
    for dimension, chunks in wanted.items():
        region = regions.get(dimension)
        if region is None or not region.is_dir():
            continue
        for cx, cz in sorted(chunks):
            try:
                java[(dimension, cx, cz)] = _read_mca_states(region, cx, cz)
            except ImportError:
                raise SystemExit("snapshot_patch: python 'nbt' package missing "
                                 "(run with --with nbt); refusing to silently "
                                 "skip the world snapshot")
            except Exception:
                continue
            try:
                spawners.extend(
                    _read_mca_spawners(region, cx, cz, dimension))
            except ImportError:
                raise SystemExit("snapshot_patch: python 'nbt' package missing "
                                 "(run with --with nbt); refusing to silently "
                                 "skip saved spawners")
            except Exception as exc:
                raise RuntimeError(
                    f"snapshot_patch: could not decode TileEntities in "
                    f"dimension {dimension} chunk ({cx},{cz})") from exc
    if not java:
        return None
    if len(spawners) > 64:
        raise RuntimeError(
            "snapshot_patch: visible save exceeds the exact 64-spawner bound")

    binary = _ensure_world_dump()
    tiles: dict[tuple[int, int, int], list[tuple[int, int]]] = {}
    for dimension, cx, cz in java:
        tiles.setdefault((dimension, cx // 8, cz // 8), []).append((cx, cz))

    # The published patch's snapshot_region lines, one per tile, in the order
    # they are written below. The probe pass replays the tape with EXACTLY
    # these ensures and no snapshot_block, so it builds populate windows in the
    # real replay's order and the world it reports back is the world the real
    # replay generates. Compute them before the probe runs; nothing here
    # depends on the diff.
    region_lines = [
        json.dumps({"tick": 0, "type": "snapshot_region", "dim": dimension,
                    "cx": tx * 8 + 3, "cz": tz * 8 + 3, "radius": 4},
                   separators=(",", ":")) + "\n"
        for dimension, tx, tz in sorted(tiles)
    ]
    saved = {dimension: {(cx, cz) for d, cx, cz in java if d == dimension}
             for dimension in {d for d, _, _ in java}}
    with tempfile.TemporaryDirectory(prefix="snapshot_probe.") as td:
        game, fallback = _game_states(tape, header, ticks, saved,
                                      region_lines, Path(td))
    nfall = sum(len(c) for c in fallback.values())
    print(f"[tape] snapshot patch: probed {len(game)} chunks from the replay's "
          f"own generation, {nfall} fall back to world_dump")

    events = 0
    # Both scratch names carry the pid, and the cache is published with an
    # atomic rename. Agent worktrees symlink tapes/ to one shared directory,
    # and the staleness check above keys off this file's mtime, which differs
    # per worktree - so N concurrent replays all decide to regenerate the same
    # cache at once. With a fixed temp name they overwrite each other's
    # world_dump tiles (wrong patch, silently), and with an in-place cache
    # write a reader sees a truncated patch (unpatched cells, silently). Both
    # showed up as a phantom 3.5x terrain residual on a tape that was fine.
    part = cache.with_suffix(cache.suffix + f".{os.getpid()}.part")
    temp = cache.with_suffix(cache.suffix + f".{os.getpid()}.tmp.bin")
    with part.open("w") as out:
        for line, (dimension, tx, tz) in zip(region_lines, sorted(tiles)):
            chunks = tiles[(dimension, tx, tz)]
            magma = {}
            if any((dimension, cx, cz) not in game for cx, cz in chunks):
                overworld_type = (1 if str(header.get("world", "")).endswith("_flat")
                                  else 0)
                world_type = {-1: 2, 0: overworld_type, 1: 3}[dimension]
                subprocess.run([
                    str(binary), "--seed", str(int(header["seed"])),
                    "--cx0", str(tx * 8), "--cz0", str(tz * 8),
                    "--ncx", "8", "--ncz", "8", "--states",
                    "--world-type", str(world_type), "--out", str(temp),
                ], check=True, cwd=MAGMA, stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL)
                magma = _read_magma_states(temp)
            out.write(line)
            for cx, cz in chunks:
                jstate = java[(dimension, cx, cz)]
                probed = game.get((dimension, cx, cz))
                # Probed chunks are diffed against the world the replay itself
                # generates, so the mismatch set is exact and the vegetation
                # band is dead weight. Unprobed ones keep the old world_dump
                # comparison, which is a different world, hence the band.
                cstate = probed if probed is not None else magma[(cx, cz)]
                cells = set(zip(*(a.tolist()
                                  for a in np.where(jstate != cstate))))
                if probed is None:
                    cells |= _decoration_band(jstate) - cells
                for lx, y, lz in sorted(cells):
                    state = int(jstate[lx, y, lz])
                    out.write(json.dumps({
                        "tick": 0, "type": "snapshot_block",
                        "dim": dimension,
                        "x": cx * 16 + lx, "y": y, "z": cz * 16 + lz,
                        "id": state >> 4, "meta": state & 15,
                    }, separators=(",", ":")) + "\n")
                    events += 1
        _write_spawner_events(out, cache, spawners)
    temp.unlink(missing_ok=True)
    os.replace(part, cache)
    print(f"[tape] snapshot patch: {len(java)} visible saved chunks, {events} "
          f"cells, {len(spawners)} spawners -> {cache}")
    return cache
