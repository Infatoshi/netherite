#!/usr/bin/env python3
"""Import a real Java 1.11.2 save-fork snapshot into native capsule inputs.

This bridge is intentionally fail-closed.  It decodes blocks and saved light
directly from Anvil NBT, builds the canonical pre-tick state already consumed
by ``state_capsule.py``, and records every remaining whole-save limitation.
The strict command exits nonzero while any limitation remains; ``--bounded``
is available for focused fixtures that explicitly accept the reported scope.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import pathlib
import re
import shutil
import struct
import sys
import tempfile
import uuid
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
TRACE_DIR = ROOT / "magma" / "trace"
SAVE_FORK_SOURCE = HERE / "save_fork.py"
ANVIL_SOURCE = HERE / "anvil_semantic.py"
TRACE_JAVA_SOURCE = TRACE_DIR / "trace_java.py"
CAPSULE_SOURCE = TRACE_DIR / "state_capsule.py"
REGISTRY_MANIFEST = HERE / "registry_manifest.json"
SURFACE_REGISTRY_MANIFEST = HERE / "surface_registry_manifest.json"
ITEM_NAME_MANIFEST = ROOT / "magma" / "assets" / "item_name_manifest.h"
REPORT_FILE = "anvil_import_report.json"
STATE_FILE = "java_reload_state.json"
BLOCK_FILE = "anvil_blocks.u16le"
SKY_FILE = "anvil_sky_light.u8"
BLOCK_LIGHT_FILE = "anvil_block_light.u8"
HEIGHT_FILE = "anvil_height.u16le"
CHUNK_BUNDLE_FILE = "active_chunks.bin"
STATISTICS_FILE = "player_statistics.json"
CHUNK_BUNDLE_MAGIC = b"NTHCHN01"
CHUNK_BUNDLE_VERSION = 4
COLD_STORE_MAGIC = b"NTHCLD01"
COLD_STORE_VERSION = 1
COLD_STORE_PAYLOAD_BYTES = 263424


class AnvilImportError(RuntimeError):
    pass


def _load(name: str, path: pathlib.Path, sibling: pathlib.Path | None = None):
    if sibling is not None:
        sys.path.insert(0, str(sibling))
    try:
        spec = importlib.util.spec_from_file_location(name, path)
        if spec is None or spec.loader is None:
            raise AnvilImportError(f"could not load {path}")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    finally:
        if sibling is not None:
            sys.path.pop(0)


SAVE_FORK = _load("netherite_save_fork_import", SAVE_FORK_SOURCE, HERE)
ANVIL = _load("netherite_anvil_import", ANVIL_SOURCE)
TRACE_JAVA = _load("netherite_trace_java_import", TRACE_JAVA_SOURCE, TRACE_DIR)
CAPSULE = _load("netherite_capsule_import", CAPSULE_SOURCE, TRACE_DIR)

with SURFACE_REGISTRY_MANIFEST.open() as _surface_registry_stream:
    _surface_registry = json.load(_surface_registry_stream)
BLOCK_RESOURCE_IDS = {
    row["name"]: row["id"] for row in _surface_registry["blocks"]
}
ITEM_RESOURCE_IDS = {
    name: int(item_id) for name, item_id in re.findall(
        r'\{"([^"]+)",\s*(\d+)\}', ITEM_NAME_MANIFEST.read_text())
}
if len(ITEM_RESOURCE_IDS) != 392:
    raise AnvilImportError(
        "HAR-01: item name manifest must contain 392 vanilla rows")


def _compound(node: Any, label: str) -> dict[str, Any]:
    if not isinstance(node, dict) or node.get("type") != "compound" \
            or not isinstance(node.get("value"), dict):
        raise AnvilImportError(f"{label} must be TAG_Compound")
    return node["value"]


def _list(node: Any, label: str, element_type: str | None = None) -> list[Any]:
    if not isinstance(node, dict) or node.get("type") != "list" \
            or not isinstance(node.get("value"), list):
        raise AnvilImportError(f"{label} must be TAG_List")
    # NBT permits an empty TAG_List to declare element type TAG_End (0).
    # There are no elements whose type could violate the requested schema.
    if element_type is not None and node.get("element_type") != element_type \
            and not (not node["value"] and node.get("element_type") == "end"):
        raise AnvilImportError(
            f"{label} must contain TAG_{element_type}, got "
            f"{node.get('element_type')!r}")
    return node["value"]


def _integer(node: Any, label: str) -> int:
    if not isinstance(node, dict) or node.get("type") not in {
        "byte", "short", "int", "long"
    } or isinstance(node.get("value"), bool) \
            or not isinstance(node.get("value"), int):
        raise AnvilImportError(f"{label} must be an integer NBT tag")
    return node["value"]


def _number(node: Any, label: str) -> float:
    if not isinstance(node, dict):
        raise AnvilImportError(f"{label} must be a numeric NBT tag")
    kind, value = node.get("type"), node.get("value")
    if kind in {"byte", "short", "int", "long"} \
            and isinstance(value, int) and not isinstance(value, bool):
        return float(value)
    if kind in {"float", "double"} and isinstance(value, str):
        expected = 8 if kind == "float" else 16
        if len(value) != expected:
            raise AnvilImportError(f"{label} has invalid raw {kind} bits")
        return struct.unpack(">f" if kind == "float" else ">d",
                             bytes.fromhex(value))[0]
    raise AnvilImportError(f"{label} must be a numeric NBT tag")


def _scheduled_block_id(node: Any, label: str) -> int:
    if isinstance(node, dict) and node.get("type") in {
            "byte", "short", "int", "long"}:
        return _integer(node, label)
    resource = _string(node, label)
    block_id = BLOCK_RESOURCE_IDS.get(resource)
    if block_id is None:
        raise AnvilImportError(
            f"{label} names unknown block resource {resource!r}")
    return block_id


def _string(node: Any, label: str) -> str:
    if not isinstance(node, dict) or node.get("type") != "string" \
            or not isinstance(node.get("value"), str) or not node["value"]:
        raise AnvilImportError(f"{label} must be a nonempty TAG_String")
    return node["value"]


def _byte_array(node: Any, label: str, size: int) -> bytes:
    if not isinstance(node, dict) or node.get("type") != "byte_array" \
            or node.get("count") != size \
            or not isinstance(node.get("value_hex"), str):
        raise AnvilImportError(
            f"{label} must be a compact TAG_Byte_Array[{size}]")
    try:
        raw = bytes.fromhex(node["value_hex"])
    except ValueError as exc:
        raise AnvilImportError(f"{label} has invalid hexadecimal data") from exc
    if len(raw) != size:
        raise AnvilImportError(
            f"{label} declared {size} bytes but decoded {len(raw)}")
    return raw


def _int_array(node: Any, label: str, size: int) -> tuple[int, ...]:
    if not isinstance(node, dict) or node.get("type") != "int_array" \
            or node.get("count") != size \
            or not isinstance(node.get("value_hex"), str):
        raise AnvilImportError(
            f"{label} must be a compact TAG_Int_Array[{size}]")
    try:
        raw = bytes.fromhex(node["value_hex"])
    except ValueError as exc:
        raise AnvilImportError(f"{label} has invalid hexadecimal data") from exc
    if len(raw) != size * 4:
        raise AnvilImportError(
            f"{label} declared {size} integers but decoded {len(raw)} bytes")
    return struct.unpack(f">{size}i", raw)


def _nibble(raw: bytes, index: int) -> int:
    value = raw[index >> 1]
    return (value >> 4) & 15 if index & 1 else value & 15


def _chunk_level(document: dict[str, Any], label: str) -> dict[str, Any]:
    root = _compound(document.get("tag"), f"{label}/root")
    return _compound(root.get("Level"), f"{label}/Level")


def _chunk_key(dimension: int, chunk_x: int, chunk_z: int) -> str:
    return f"dim={dimension},x={chunk_x},z={chunk_z}"


def _section_map(level: dict[str, Any], label: str) -> dict[int, dict[str, Any]]:
    result: dict[int, dict[str, Any]] = {}
    sections = _list(level.get("Sections"), f"{label}/Sections", "compound")
    for index, node in enumerate(sections):
        fields = _compound(node, f"{label}/Sections[{index}]")
        section_y = _integer(fields.get("Y"), f"{label}/Sections[{index}]/Y")
        if not 0 <= section_y <= 15 or section_y in result:
            raise AnvilImportError(
                f"{label}/Sections[{index}]/Y is invalid or duplicated")
        result[section_y] = fields
    return result


def _java_loaded_block_state(block_id: int, meta: int) -> tuple[int, int]:
    """Apply 1.11.2 Block.getStateFromMeta at an Anvil load boundary."""
    # BlockObserver.getStateFromMeta(meta) restores only FACING and leaves its
    # default POWERED=false. Both live observer states serialize as metadata
    # 12 in 1.11.2's duplicate block-state registry, so the Anvil nibble is
    # not itself a live powered-state bit after a cold reload.
    if block_id == 218:
        meta &= 7
    return block_id, meta


def _chunk_arrays(
    document: dict[str, Any], label: str, dimension: int
) -> tuple[bytes, bytes, bytes]:
    """Decode one complete saved Chunk storage column in vanilla index order."""
    level = _chunk_level(document, label)
    sections = _section_map(level, label)
    height_map = _int_array(level.get("HeightMap"), f"{label}/HeightMap", 256)
    if any(value < 0 or value > 256 for value in height_map):
        raise AnvilImportError(
            f"{label}/HeightMap contains a value outside 0..256")
    decoded: dict[int, tuple[bytes, bytes, bytes | None, bytes | None, bytes]] = {}
    for section_y, section in sections.items():
        prefix = f"{label}/Sections[{section_y}]"
        ids = _byte_array(section.get("Blocks"), f"{prefix}/Blocks", 4096)
        data = _byte_array(section.get("Data"), f"{prefix}/Data", 2048)
        high_node = section.get("Add")
        high = None if high_node is None else _byte_array(
            high_node, f"{prefix}/Add", 2048)
        block_node = section.get("BlockLight")
        if block_node is None:
            raise AnvilImportError(
                f"SAVE-01: {prefix} has no saved BlockLight array")
        saved_block = _byte_array(
            block_node, f"{prefix}/BlockLight", 2048)
        sky_node = section.get("SkyLight")
        if sky_node is None and dimension == 0:
            raise AnvilImportError(
                f"SAVE-01: {prefix} has no saved SkyLight array in a sky dimension")
        saved_sky = None if sky_node is None else _byte_array(
            sky_node, f"{prefix}/SkyLight", 2048)
        decoded[section_y] = (ids, data, high, saved_sky, saved_block)
    blocks = bytearray(256 * 256 * 2)
    sky = bytearray(256 * 256)
    block_light = bytearray(256 * 256)
    for y in range(256):
        section = decoded.get(y >> 4)
        for z in range(16):
            for x in range(16):
                index = (y << 8) | (z << 4) | x
                if section is None:
                    packed = 0
                    saved_sky = (
                        15 if dimension == 0
                        and y >= height_map[(z << 4) | x] else 0)
                    saved_block = 0
                else:
                    local_index = ((y & 15) << 8) | (z << 4) | x
                    ids, data, high, saved_sky_raw, saved_block_raw = section
                    high_id = 0 if high is None else _nibble(high, local_index)
                    block_id, meta = _java_loaded_block_state(
                        ids[local_index] | (high_id << 8),
                        _nibble(data, local_index))
                    packed = block_id << 4 | meta
                    saved_block = _nibble(saved_block_raw, local_index)
                    saved_sky = 0 if saved_sky_raw is None else _nibble(
                        saved_sky_raw, local_index)
                struct.pack_into("<H", blocks, index * 2, packed)
                sky[index] = saved_sky
                block_light[index] = saved_block
    return bytes(blocks), bytes(sky), bytes(block_light)


def _chunk_cold_payload(
    document: dict[str, Any], label: str, dimension: int
) -> bytes:
    """Return the complete persisted column payload used by the native pager.

    Blocks and saved light use the same y,z,x cell order as the active bundle.
    Biomes retain vanilla z,x byte order. HeightMap is retained in its original
    big-endian NBT integer representation for a future native save writer; the
    live world currently derives its collision/light height caches from blocks.
    """
    level = _chunk_level(document, label)
    blocks, sky, block_light = _chunk_arrays(document, label, dimension)
    biomes = _byte_array(level.get("Biomes"), f"{label}/Biomes", 256)
    height_map = _int_array(
        level.get("HeightMap"), f"{label}/HeightMap", 256)
    payload = blocks + sky + block_light + biomes \
        + struct.pack(">256i", *height_map)
    if len(payload) != COLD_STORE_PAYLOAD_BYTES:
        raise AnvilImportError(
            f"internal cold chunk payload size mismatch: {len(payload)}")
    return payload


def _chunk_coordinates(key: str) -> tuple[int, int, int]:
    try:
        dimension_text, x_text, z_text = key.split(",")
        if not dimension_text.startswith("dim=") \
                or not x_text.startswith("x=") \
                or not z_text.startswith("z="):
            raise ValueError
        return (int(dimension_text[4:]), int(x_text[2:]), int(z_text[2:]))
    except (ValueError, TypeError) as exc:
        raise AnvilImportError(f"invalid semantic chunk key: {key!r}") from exc


def _cold_store_file(dimension: int) -> str:
    label = "neg1" if dimension == -1 else str(dimension)
    return f"cold_chunks_dim_{label}.bin"


def write_cold_chunk_store(
    semantic: dict[str, Any], dimension: int, path: pathlib.Path,
) -> dict[str, Any]:
    chunks = semantic["load_inputs"].get("chunks")
    if not isinstance(chunks, dict):
        raise AnvilImportError("semantic save has no chunk map")
    ordered = sorted(
        ((chunk_x, chunk_z, key) for key in chunks
         for saved_dimension, chunk_x, chunk_z in [_chunk_coordinates(key)]
         if saved_dimension == dimension),
        key=lambda row: (row[0], row[1]),
    )
    if not ordered:
        raise AnvilImportError(
            f"SAVE-01: dimension {dimension} has no persisted chunks")
    header_bytes = 24
    index_bytes = len(ordered) * 16
    offset = header_bytes + index_bytes
    digest = hashlib.sha256()
    total = 0
    with path.open("wb") as stream:
        header = struct.pack(
            "<8sIiII", COLD_STORE_MAGIC, COLD_STORE_VERSION,
            dimension, len(ordered), COLD_STORE_PAYLOAD_BYTES)
        stream.write(header)
        digest.update(header)
        total += len(header)
        for chunk_x, chunk_z, _key in ordered:
            record = struct.pack("<iiQ", chunk_x, chunk_z, offset)
            stream.write(record)
            digest.update(record)
            total += len(record)
            offset += COLD_STORE_PAYLOAD_BYTES
        for _chunk_x, _chunk_z, key in ordered:
            payload = _chunk_cold_payload(
                chunks[key], key, dimension)
            stream.write(payload)
            digest.update(payload)
            total += len(payload)
    if total != offset:
        raise AnvilImportError(
            f"internal cold chunk store size mismatch: {total} != {offset}")
    return {
        "file": path.name,
        "version": COLD_STORE_VERSION,
        "dimension": dimension,
        "chunks": len(ordered),
        "payload_bytes": COLD_STORE_PAYLOAD_BYTES,
        "bytes": total,
        "sha256": digest.hexdigest(),
    }


def write_cold_chunk_stores(
    semantic: dict[str, Any], directory: pathlib.Path,
) -> list[dict[str, Any]]:
    chunks = semantic["load_inputs"].get("chunks")
    if not isinstance(chunks, dict):
        raise AnvilImportError("semantic save has no chunk map")
    dimensions = sorted({_chunk_coordinates(key)[0] for key in chunks})
    if not dimensions:
        raise AnvilImportError("SAVE-01: save contains no persisted chunks")
    return [write_cold_chunk_store(
        semantic, dimension, directory / _cold_store_file(dimension))
        for dimension in dimensions]


def extract_cuboid(
    semantic: dict[str, Any], dimension: int, box: list[int]
) -> tuple[bytes, bytes, bytes, dict[str, Any]]:
    """Return packed blocks, sky light, block light, and source coverage."""
    cells = CAPSULE.cell_count(box)
    x0, y0, z0, x1, y1, z1 = box
    chunks = semantic["load_inputs"].get("chunks")
    if not isinstance(chunks, dict):
        raise AnvilImportError("semantic save has no chunk map")
    chunk_cache: dict[tuple[int, int], tuple[bytes, bytes, bytes]] = {}
    used_chunks: set[str] = set()
    blocks = bytearray(cells * 2)
    sky = bytearray(cells)
    block_light = bytearray(cells)
    out_index = 0
    for y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            chunk_z, local_z = z >> 4, z & 15
            for x in range(x0, x1 + 1):
                chunk_x, local_x = x >> 4, x & 15
                key = _chunk_key(dimension, chunk_x, chunk_z)
                document = chunks.get(key)
                if document is None:
                    raise AnvilImportError(
                        f"SAVE-01: selected cuboid needs absent saved chunk {key}")
                used_chunks.add(key)
                cache_key = (chunk_x, chunk_z)
                arrays = chunk_cache.get(cache_key)
                if arrays is None:
                    arrays = _chunk_arrays(document, key, dimension)
                    chunk_cache[cache_key] = arrays
                chunk_blocks, chunk_sky, chunk_block_light = arrays
                index = (y << 8) | (local_z << 4) | local_x
                packed = struct.unpack_from("<H", chunk_blocks, index * 2)[0]
                saved_sky = chunk_sky[index]
                saved_block = chunk_block_light[index]
                struct.pack_into("<H", blocks, out_index * 2, packed)
                sky[out_index] = saved_sky
                block_light[out_index] = saved_block
                out_index += 1
    if out_index != cells:
        raise AnvilImportError("internal cuboid traversal length mismatch")
    dimension_chunks = sorted(
        key for key in chunks if key.startswith(f"dim={dimension},"))
    return bytes(blocks), bytes(sky), bytes(block_light), {
        "selected_chunks": sorted(used_chunks),
        "dimension_chunks": dimension_chunks,
        "unselected_chunks": sorted(set(dimension_chunks) - used_chunks),
    }


def extract_height_cuboid(
    semantic: dict[str, Any], dimension: int, box: list[int],
) -> bytes:
    """Return persisted Chunk.heightMap in the common y,z,x cuboid order."""
    x0, y0, z0, x1, y1, z1 = box
    chunks = semantic["load_inputs"].get("chunks")
    if not isinstance(chunks, dict):
        raise AnvilImportError("semantic save has no chunk map")
    cache: dict[tuple[int, int], tuple[int, ...]] = {}
    output = bytearray(CAPSULE.cell_count(box) * 2)
    out_index = 0
    for _y in range(y0, y1 + 1):
        for z in range(z0, z1 + 1):
            chunk_z, local_z = z >> 4, z & 15
            for x in range(x0, x1 + 1):
                chunk_x, local_x = x >> 4, x & 15
                key = _chunk_key(dimension, chunk_x, chunk_z)
                document = chunks.get(key)
                if document is None:
                    raise AnvilImportError(
                        f"SAVE-01: height cuboid needs absent saved chunk {key}")
                coordinate = (chunk_x, chunk_z)
                height_map = cache.get(coordinate)
                if height_map is None:
                    level = _chunk_level(document, key)
                    height_map = _int_array(
                        level.get("HeightMap"), f"{key}/HeightMap", 256)
                    if any(value < 0 or value > 256 for value in height_map):
                        raise AnvilImportError(
                            f"{key}/HeightMap contains a value outside 0..256")
                    cache[coordinate] = height_map
                struct.pack_into(
                    "<H", output, out_index * 2,
                    height_map[(local_z << 4) | local_x])
                out_index += 1
    return bytes(output)


def _player_document(semantic: dict[str, Any], authoritative: dict[str, Any]):
    most = int(authoritative["player_uuid_most_hex"], 16)
    least = int(authoritative["player_uuid_least_hex"], 16)
    wanted = f"{most:016x}{least:016x}"
    candidates = []
    for key, document in semantic["load_inputs"].items():
        if not key.startswith("playerdata/") or not key.endswith(".dat"):
            continue
        root = _compound(document.get("tag"), f"{key}/root")
        saved_most = _integer(root.get("UUIDMost"), f"{key}/UUIDMost") \
            & 0xFFFFFFFFFFFFFFFF
        saved_least = _integer(root.get("UUIDLeast"), f"{key}/UUIDLeast") \
            & 0xFFFFFFFFFFFFFFFF
        identity = f"{saved_most:016x}{saved_least:016x}"
        candidates.append(identity)
        if identity == wanted:
            return key, root
    raise AnvilImportError(
        "SAVE-01: authoritative player UUID has no exact playerdata file; "
        f"wanted {wanted}, found {sorted(candidates)}")


def _saved_player_values(root: dict[str, Any], label: str) -> dict[str, Any]:
    def numbers(name: str, count: int) -> list[float]:
        values = _list(root.get(name), f"{label}/{name}")
        if len(values) != count:
            raise AnvilImportError(
                f"{label}/{name} must contain {count} values")
        return [_number(value, f"{label}/{name}[{index}]")
                for index, value in enumerate(values)]

    position = numbers("Pos", 3)
    motion = numbers("Motion", 3)
    rotation = numbers("Rotation", 2)
    return {
        "x": position[0], "y": position[1], "z": position[2],
        "vx": motion[0], "vy": motion[1], "vz": motion[2],
        "yaw": rotation[0], "pitch": rotation[1],
        "on_ground": bool(_integer(root.get("OnGround"), f"{label}/OnGround")),
        "fall_distance": _number(root.get("FallDistance"),
                                   f"{label}/FallDistance"),
        "sneaking": False,
        "jumping": False,
        "dead": _number(root.get("Health"), f"{label}/Health") <= 0.0,
        # Death count lives in stats JSON and is not a player NBT scalar.  The
        # current native capsule has no stats restore path; capability reporting
        # below rejects that surface instead of pretending this zero is exact.
        "deaths": 0,
    }


def _saved_ender_inventory(
    root: dict[str, Any], label: str,
) -> list[dict[str, Any]]:
    node = root.get("EnderItems")
    if node is None:
        return []
    entries = _list(node, f"{label}/EnderItems", "compound")
    if len(entries) > 27:
        raise AnvilImportError(f"{label}/EnderItems exceeds 27 slots")
    output = []
    seen_slots = set()
    for index, entry_node in enumerate(entries):
        stack_label = f"{label}/EnderItems[{index}]"
        stack = _compound(entry_node, stack_label)
        slot = _integer(stack.get("Slot"), f"{stack_label}/Slot")
        count = _integer(stack.get("Count"), f"{stack_label}/Count")
        meta = _integer(stack.get("Damage"), f"{stack_label}/Damage")
        item_node = stack.get("id")
        if isinstance(item_node, dict) and item_node.get("type") == "string":
            resource = _string(item_node, f"{stack_label}/id")
            item_id = ITEM_RESOURCE_IDS.get(resource)
            if item_id is None:
                raise AnvilImportError(
                    f"{stack_label}/id names unknown item {resource!r}")
        else:
            item_id = _integer(item_node, f"{stack_label}/id")
        if slot in seen_slots or not 0 <= slot < 27:
            raise AnvilImportError(f"{stack_label}/Slot is invalid or duplicate")
        if not 1 <= item_id <= 4095 or not 1 <= count <= 64 \
                or not 0 <= meta <= 32767:
            raise AnvilImportError(f"{stack_label} is not a valid ItemStack")
        seen_slots.add(slot)
        tag_node = stack.get("tag")
        tag = _compound(tag_node, f"{stack_label}/tag") \
            if tag_node is not None else {}
        enchantments = []
        enchant_node = tag.get("ench") or tag.get("StoredEnchantments")
        if enchant_node is not None:
            for enchant_index, enchant_node_value in enumerate(_list(
                    enchant_node, f"{stack_label}/tag/ench", "compound")):
                enchant = _compound(
                    enchant_node_value,
                    f"{stack_label}/tag/ench[{enchant_index}]")
                enchantments.append([
                    _integer(enchant.get("id"),
                             f"{stack_label}/tag/ench[{enchant_index}]/id"),
                    _integer(enchant.get("lvl"),
                             f"{stack_label}/tag/ench[{enchant_index}]/lvl"),
                ])
        if len(enchantments) > 8:
            raise AnvilImportError(
                f"{stack_label} exceeds native eight-enchantment stack cap")
        repair_cost = _integer(
            tag["RepairCost"], f"{stack_label}/tag/RepairCost") \
            if "RepairCost" in tag else 0
        custom_name = ""
        if "display" in tag:
            display = _compound(tag["display"], f"{stack_label}/tag/display")
            if "Name" in display:
                custom_name = _string(
                    display["Name"], f"{stack_label}/tag/display/Name")
        row = {
            "slot": slot, "id": item_id, "count": count, "meta": meta,
            "enchants": enchantments, "repair_cost": repair_cost,
            "custom_name": custom_name, "nbt_subset_exact": True,
        }
        if tag_node is not None:
            row["stack_payload"] = {
                "kind": "item_tag",
                "nbt": {"name": "", "tag": tag_node},
            }
        output.append(row)
    return sorted(output, key=lambda row: row["slot"])


def _player_statistics(
    snapshot: pathlib.Path, semantic: dict[str, Any], player_path: str,
) -> tuple[bytes, int, int, str]:
    uuid = pathlib.PurePosixPath(player_path).stem
    relative = f"stats/{uuid}.json"
    document = semantic["load_inputs"].get(relative)
    if document is None:
        document = {}
        raw = b"{}"
    elif isinstance(document, dict):
        raw = (snapshot / "save" / relative).read_bytes()
    else:
        raise AnvilImportError(
            f"SAVE-02: authoritative statistics is not an object: {relative}")
    # A copied single-player directory can retain dormant files from previous
    # launcher identities. Java loads only the current EntityPlayerMP UUID;
    # multiplayer identity switching is outside this ledger's declared scope.
    play = document.get("stat.playOneMinute", 0)
    since = document.get("stat.timeSinceDeath", 0)
    if isinstance(play, bool) or not isinstance(play, int) or play < 0:
        raise AnvilImportError(
            f"SAVE-02: {relative} has invalid stat.playOneMinute")
    if isinstance(since, bool) or not isinstance(since, int) or since < 0:
        raise AnvilImportError(
            f"SAVE-02: {relative} has invalid stat.timeSinceDeath")
    if len(raw) < 2 or len(raw) > 1 << 20:
        raise AnvilImportError(
            f"SAVE-02: {relative} exceeds native statistics size fence")
    return raw, play, since, relative


def canonical_state(
    snapshot: pathlib.Path, semantic: dict[str, Any], box: list[int]
) -> tuple[dict[str, Any], int, str]:
    boundary = json.loads((snapshot / SAVE_FORK.ORACLE_RESPONSE).read_text())
    authoritative = boundary.get("authoritative")
    if not isinstance(authoritative, dict):
        raise AnvilImportError("SAVE-01: snapshot has no authoritative state")
    player_path, player_root = _player_document(semantic, authoritative)
    saved = _saved_player_values(player_root, player_path)
    observation = dict(saved)
    observation.update({
        key: value for key, value in authoritative.items()
        if key in {
            "x", "y", "z", "vx", "vy", "vz", "on_ground",
            "fall_distance", "sprinting", "in_water",
        }
    })
    observation["yaw"] = saved["yaw"]
    observation["pitch"] = saved["pitch"]
    observation["authoritative"] = authoritative
    state = TRACE_JAVA.canonicalize(-1, observation, box, tuple(range(4096)))
    try:
        hidden = json.loads((snapshot / SAVE_FORK.HIDDEN_STATE).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise AnvilImportError(f"SAVE-07: invalid hidden RNG state: {exc}") \
            from exc
    server_uuid_seed48 = hidden.get("server_uuid_seed48")
    if isinstance(server_uuid_seed48, bool) \
            or not isinstance(server_uuid_seed48, int) \
            or not 0 <= server_uuid_seed48 < (1 << 48):
        raise AnvilImportError(
            "SAVE-07: hidden server UUID RNG cursor is invalid")
    state["world_rng"]["server_uuid_seed48"] = server_uuid_seed48
    collections_seed48 = hidden.get("collections_seed48")
    if isinstance(collections_seed48, bool) \
            or not isinstance(collections_seed48, int) \
            or not 0 <= collections_seed48 < (1 << 48):
        raise AnvilImportError(
            "SAVE-07: hidden Collections RNG cursor is invalid")
    state["world_rng"]["collections_seed48"] = collections_seed48
    seed_helper = hidden.get("seed_helper")
    generators = seed_helper.get("generators") \
        if isinstance(seed_helper, dict) else None
    entity_generators = [
        value for value in generators or []
        if isinstance(value, dict) and value.get("key") == "entity"
    ]
    if len(entity_generators) != 1:
        raise AnvilImportError(
            "SAVE-07: hidden SeedHelper entity generator is missing")
    entity_seed48 = entity_generators[0].get("seed48")
    if isinstance(entity_seed48, bool) \
            or not isinstance(entity_seed48, int) \
            or not 0 <= entity_seed48 < (1 << 48):
        raise AnvilImportError(
            "SAVE-07: hidden SeedHelper entity RNG cursor is invalid")
    state["world_rng"]["entity_seed_generator_seed48"] = entity_seed48
    inventory_helper_seed48 = hidden.get("inventory_helper_seed48")
    inventory_helper_have_gaussian = hidden.get(
        "inventory_helper_have_gaussian")
    inventory_helper_gaussian_bits = hidden.get(
        "inventory_helper_gaussian_bits")
    if isinstance(inventory_helper_seed48, bool) \
            or not isinstance(inventory_helper_seed48, int) \
            or not 0 <= inventory_helper_seed48 < (1 << 48):
        raise AnvilImportError(
            "SAVE-07: hidden InventoryHelper RNG cursor is invalid")
    if not isinstance(inventory_helper_have_gaussian, bool):
        raise AnvilImportError(
            "SAVE-07: hidden InventoryHelper Gaussian flag is invalid")
    if not isinstance(inventory_helper_gaussian_bits, str) \
            or not re.fullmatch(
                r"[0-9a-fA-F]{16}", inventory_helper_gaussian_bits):
        raise AnvilImportError(
            "SAVE-07: hidden InventoryHelper Gaussian value is invalid")
    inventory_helper_gaussian = struct.unpack(
        ">d", bytes.fromhex(inventory_helper_gaussian_bits))[0]
    if not math.isfinite(inventory_helper_gaussian):
        raise AnvilImportError(
            "SAVE-07: hidden InventoryHelper Gaussian value is not finite")
    state["world_rng"].update({
        "inventory_helper_seed48": inventory_helper_seed48,
        "inventory_helper_have_gaussian":
            inventory_helper_have_gaussian,
        "inventory_helper_gaussian": inventory_helper_gaussian,
    })
    state["ender_inventory"] = _saved_ender_inventory(
        player_root, player_path)
    # fork_runner's mandatory normalize_reload_locked boundary constructor-zeros
    # this client-only cursor before t=0.  It is not persisted in Anvil and is
    # therefore reconstructed by the same explicit Java reload rule, never from
    # a guessed save value.
    if state["player"].get("position_update_ticks") is None:
        state["player"]["position_update_ticks"] = 0
    if state["player"].get("position_packet_pending") is None:
        state["player"]["position_packet_pending"] = 0
    level = semantic["load_inputs"].get("level.dat")
    data = _compound(_compound(level.get("tag"), "level.dat/root").get("Data"),
                     "level.dat/Data")
    game_rules = _compound(
        data.get("GameRules"), "level.dat/Data/GameRules")
    do_mob_spawning = _string(
        game_rules.get("doMobSpawning"),
        "level.dat/Data/GameRules/doMobSpawning")
    if do_mob_spawning not in {"true", "false"}:
        raise AnvilImportError(
            "SAVE-04: doMobSpawning is not a canonical boolean string")
    state["do_mob_spawning"] = do_mob_spawning == "true"
    do_mob_loot = _string(
        game_rules.get("doMobLoot"), "level.dat/Data/GameRules/doMobLoot")
    if do_mob_loot not in {"true", "false"}:
        raise AnvilImportError(
            "SAVE-04: doMobLoot is not a canonical boolean string")
    state["do_mob_loot"] = do_mob_loot == "true"
    seed = _integer(data.get("RandomSeed"), "level.dat/Data/RandomSeed")
    return state, seed, player_path


def _nonempty_chunk_payloads(
    semantic: dict[str, Any], dimension: int
) -> dict[str, int]:
    counts = {"entities": 0, "tiles": 0, "scheduled_ticks": 0}
    chunks = semantic["load_inputs"]["chunks"]
    for key, document in chunks.items():
        if not key.startswith(f"dim={dimension},"):
            continue
        level = _chunk_level(document, key)
        for nbt_name, report_name in (
            ("Entities", "entities"), ("TileEntities", "tiles"),
            ("TileTicks", "scheduled_ticks"),
        ):
            node = level.get(nbt_name)
            if node is not None:
                counts[report_name] += len(_list(node, f"{key}/{nbt_name}"))
    return counts


def _entity_registry_capabilities(
    semantic: dict[str, Any], state: dict[str, Any], dimension: int,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    try:
        registry = json.loads(REGISTRY_MANIFEST.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise AnvilImportError(
            f"HAR-01: could not load entity registry manifest: {exc}") from exc
    rows = registry.get("entities")
    if not isinstance(rows, list) or len(rows) != 81:
        raise AnvilImportError(
            "HAR-01: entity registry manifest must contain 81 rows")
    by_name = {row.get("name"): row for row in rows
               if isinstance(row, dict) and isinstance(row.get("name"), str)}
    if len(by_name) != len(rows):
        raise AnvilImportError(
            "HAR-01: entity registry manifest names must be unique")

    active_by_uuid: dict[tuple[int, int], dict[str, Any]] = {}
    for index, entity in enumerate(state.get("entities", [])):
        if not isinstance(entity, dict):
            raise AnvilImportError(
                f"HAR-02: canonical entity {index} is not an object")
        if "uuid_most" not in entity or "uuid_least" not in entity:
            continue
        key = (entity["uuid_most"], entity["uuid_least"])
        if key in active_by_uuid:
            raise AnvilImportError(
                f"SAVE-05: duplicate active entity UUID {key}")
        active_by_uuid[key] = entity

    summary = {
        row["name"]: {
            "name": row["name"], "class": row["class"],
            "implementation_status": row["status"], "todo": row["todo"],
            "persisted": 0, "active_exact": 0,
            "active_unsupported": 0, "inactive_persisted": 0,
        }
        for row in rows
    }
    limitations: list[dict[str, Any]] = []

    def reject(surface: str, todo: str, reason: str) -> None:
        limitations.append({
            "surface": surface, "status": "reject", "todo": todo,
            "reason": reason, "count": 1,
        })

    chunks = semantic["load_inputs"]["chunks"]
    for chunk_key, document in sorted(chunks.items()):
        level = _chunk_level(document, chunk_key)
        node = level.get("Entities")
        if node is None:
            continue
        entities = _list(node, f"{chunk_key}/Entities")
        if entities and node.get("element_type") != "compound":
            raise AnvilImportError(
                f"{chunk_key}/Entities must contain TAG_compound, got "
                f"{node.get('element_type')!r}")
        for index, entity_node in enumerate(entities):
            label = f"{chunk_key}/Entities[{index}]"
            fields = _compound(entity_node, label)
            resource = _string(fields.get("id"), f"{label}/id")
            name = resource.split(":", 1)[-1]
            row = by_name.get(name)
            if row is None:
                reject(
                    f"world.entities.{resource}", "HAR-01",
                    "persisted entity is absent from the Java registry manifest")
                continue
            item = summary[name]
            item["persisted"] += 1
            if "UUIDMost" not in fields or "UUIDLeast" not in fields:
                item["active_unsupported"] += 1
                reject(
                    f"world.entities.{resource}", row["todo"],
                    "persisted entity has no exact UUID identity")
                continue
            uuid = (
                _integer(fields["UUIDMost"], f"{label}/UUIDMost"),
                _integer(fields["UUIDLeast"], f"{label}/UUIDLeast"),
            )
            active = active_by_uuid.get(uuid)
            if active is None:
                item["inactive_persisted"] += 1
                reject(
                    f"world.entities.{resource}", "SAVE-09",
                    "persisted entity is outside the active Java loaded "
                    "boundary and native cold-chunk entity restore is open")
                continue
            if active.get("type") != row["class"]:
                item["active_unsupported"] += 1
                reject(
                    f"world.entities.{resource}", "SAVE-05",
                    "Anvil registry identity disagrees with the active "
                    "authoritative entity class")
                continue
            if not CAPSULE.entity_payload_is_restorable(active):
                item["active_unsupported"] += 1
                reject(
                    f"world.entities.{resource}", row["todo"],
                    "active authoritative entity has no complete native "
                    "reconstruction schema")
                continue
            item["active_exact"] += 1

    # An entity present at the active Java boundary but not tied to persisted
    # Anvil identity is transient or was reconstructed incorrectly. It cannot
    # be accepted merely because the native schema knows its class.
    persisted_active = sum(
        value["active_exact"] + value["active_unsupported"]
        for value in summary.values())
    nonplayer_active = len(state.get("entities", []))
    if persisted_active < nonplayer_active:
        reject(
            "world.entities.active_identity", "SAVE-05",
            "one or more active entities have no matching persisted UUID")

    capabilities = []
    for row in rows:
        item = summary[row["name"]]
        if item["persisted"] == 0:
            item["capability"] = "unobserved"
        elif item["active_exact"] == item["persisted"]:
            item["capability"] = "exact_active_boundary"
        else:
            item["capability"] = "rejected"
        capabilities.append(item)
    return capabilities, limitations


def _uuid_text(most: int, least: int) -> str:
    value = ((most & 0xFFFFFFFFFFFFFFFF) << 64) \
        | (least & 0xFFFFFFFFFFFFFFFF)
    return str(uuid.UUID(int=value))


def _entity_relationship_capabilities(
    snapshot: pathlib.Path, state: dict[str, Any], dimension: int,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Classify the complete loaded riding/passenger graph fail-closed.

    Passenger identity and list order are not reconstructible from the flat
    Anvil entity list alone. The parked Java boundary records both sides of
    every edge, including the player, so compare that graph against the full
    canonical loaded membership before deciding whether an empty graph is
    exact or a nonempty graph still belongs to SAVE-03.
    """
    try:
        hidden = json.loads((snapshot / SAVE_FORK.HIDDEN_STATE).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise AnvilImportError(
            f"SAVE-03: invalid hidden relationship state: {exc}") from exc
    if hidden.get("schema") != "qrl.hidden_state.v1" \
            or not isinstance(hidden.get("worlds"), list):
        raise AnvilImportError(
            "SAVE-03: hidden state has no supported world relationship graph")
    worlds = [world for world in hidden["worlds"]
              if isinstance(world, dict) and world.get("dim") == dimension]
    if len(worlds) != 1 or not isinstance(worlds[0].get("entities"), list):
        raise AnvilImportError(
            f"SAVE-03: hidden state has {len(worlds)} entity graphs for "
            f"dimension {dimension}")

    limitations: list[dict[str, Any]] = []

    def reject(reason: str) -> None:
        limitations.append({
            "surface": "world.entities.passenger_order",
            "status": "reject", "todo": "SAVE-03",
            "reason": reason, "count": 1,
        })

    player = state.get("player", {})
    try:
        player_uuid = _uuid_text(
            int(player["uuid_most_hex"], 16),
            int(player["uuid_least_hex"], 16))
    except (KeyError, TypeError, ValueError) as exc:
        raise AnvilImportError(
            "SAVE-03: canonical player has no exact UUID identity") from exc
    expected_by_uuid: dict[str, dict[str, Any] | None] = {player_uuid: None}
    for index, entity in enumerate(state.get("entities", [])):
        if not isinstance(entity, dict) \
                or "uuid_most" not in entity or "uuid_least" not in entity:
            reject(
                f"canonical loaded entity {index} has no UUID for relationship "
                "membership")
            continue
        identity = _uuid_text(entity["uuid_most"], entity["uuid_least"])
        if identity in expected_by_uuid:
            reject(f"duplicate canonical relationship UUID {identity}")
        expected_by_uuid[identity] = entity
    expected = set(expected_by_uuid)

    rows = worlds[0]["entities"]
    by_uuid: dict[str, dict[str, Any]] = {}
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise AnvilImportError(
                f"SAVE-03: hidden entity relation {index} is not an object")
        identity = row.get("uuid")
        try:
            canonical_uuid = str(uuid.UUID(identity))
        except (AttributeError, TypeError, ValueError) as exc:
            raise AnvilImportError(
                f"SAVE-03: hidden entity relation {index} has invalid UUID") \
                from exc
        if identity != canonical_uuid or identity in by_uuid:
            raise AnvilImportError(
                f"SAVE-03: hidden entity relation {index} has noncanonical or "
                "duplicate UUID")
        if row.get("order") != index:
            reject(
                "hidden loaded-entity relationship order is not contiguous "
                f"at index {index}")
        by_uuid[identity] = row

    hidden_members = set(by_uuid)
    if hidden_members != expected:
        reject(
            "hidden relationship membership differs from canonical loaded "
            f"membership (missing={sorted(expected - hidden_members)}, "
            f"extra={sorted(hidden_members - expected)})")

    relation_edges = 0
    passenger_lists = 0
    for identity, row in by_uuid.items():
        riding = row.get("riding_uuid")
        passengers = row.get("passenger_uuids")
        if not isinstance(riding, str) or not isinstance(passengers, list) \
                or any(not isinstance(value, str) for value in passengers):
            raise AnvilImportError(
                f"SAVE-03: hidden relation payload is invalid for {identity}")
        if riding and riding not in by_uuid:
            reject(f"riding target {riding} for {identity} is not loaded")
        if identity in passengers or riding == identity \
                or len(passengers) != len(set(passengers)):
            reject(f"self or duplicate passenger relationship for {identity}")
        unknown = [value for value in passengers if value not in by_uuid]
        if unknown:
            reject(f"passengers for {identity} are not loaded: {unknown}")
        if passengers:
            passenger_lists += 1
        relation_edges += bool(riding)
        for passenger in passengers:
            if by_uuid.get(passenger, {}).get("riding_uuid") != identity:
                reject(
                    f"passenger edge {identity}->{passenger} is not reciprocal")
        if riding and identity not in by_uuid.get(riding, {}).get(
                "passenger_uuids", []):
            reject(f"riding edge {identity}->{riding} is not reciprocal")

    exact_player_minecart = False
    player_relation = by_uuid.get(player_uuid, {})
    riding_uuid = player_relation.get("riding_uuid", "")
    if relation_edges == 1 and passenger_lists == 1 and riding_uuid:
        vehicle = expected_by_uuid.get(riding_uuid)
        exact_player_minecart = isinstance(vehicle, dict) \
            and vehicle.get("type") == "EntityMinecartEmpty" \
            and by_uuid.get(riding_uuid, {}).get("passenger_uuids") == [
                player_uuid]
        if exact_player_minecart:
            player["riding_eid"] = vehicle["eid"]
    elif relation_edges == 0:
        player["riding_eid"] = -1
    if relation_edges and not exact_player_minecart:
        reject(
            f"{relation_edges} loaded riding edge(s) across {passenger_lists} "
            "ordered passenger list(s) have no native reconstruction event")
    if limitations:
        status = "rejected"
    elif exact_player_minecart:
        status = "exact_player_minecart"
    else:
        status = "exact_empty_graph"
    return {
        "loaded_members": len(by_uuid),
        "canonical_members": len(expected),
        "riding_edges": relation_edges,
        "ordered_passenger_lists": passenger_lists,
        "status": status,
    }, limitations


def _tile_nbt_supported(name: str, fields: dict[str, Any]) -> bool:
    common = {"id", "x", "y", "z"}
    allowed = {
        "chest": common | {"Items", "Lock"},
        "furnace": common | {
            "Items", "Lock", "BurnTime", "CookTime", "CookTimeTotal"},
        "brewing_stand": common | {"Items", "Lock", "BrewTime", "Fuel"},
        "dispenser": common | {"Items", "Lock"},
        "dropper": common | {"Items", "Lock"},
        "hopper": common | {"Items", "Lock", "TransferCooldown"},
        "daylight_detector": common,
        "piston": common | {
            "blockId", "blockData", "facing", "progress",
            "extending", "source"},
        "mob_spawner": common | {
            "Delay", "MinSpawnDelay", "MaxSpawnDelay", "SpawnCount",
            "MaxNearbyEntities", "RequiredPlayerRange", "SpawnRange",
            "SpawnData", "SpawnPotentials"},
    }.get(name)
    optional = {"CustomName"} if name == "furnace" else set()
    if allowed is None or not allowed <= set(fields) \
            or not set(fields) <= allowed | optional:
        return False
    if "Lock" in fields:
        lock = fields["Lock"]
        if not isinstance(lock, dict) or lock.get("type") != "string" \
                or lock.get("value") != "":
            return False
    return True


def _tile_state_is_restorable(
    state: dict[str, Any], name: str, position: tuple[int, int, int],
    fields: dict[str, Any],
) -> bool:
    container_types = {
        "chest": {
            "single_chest", "single_trapped_chest",
            "double_chest_half", "double_trapped_chest_half"},
        "furnace": {"furnace"},
        "brewing_stand": {"brewing_stand"},
        "dispenser": {"dispenser"},
        "dropper": {"dropper"},
        "hopper": {"hopper"},
    }
    if name == "daylight_detector":
        return True
    if name == "piston":
        moving = next((
            row for row in state.get("moving_pistons", [])
            if isinstance(row, dict)
            and tuple(row.get(axis) for axis in ("x", "y", "z"))
                == position
        ), None)
        if moving is None:
            return False
        try:
            progress = _number(fields.get("progress"), "piston/progress")
            progress_bits = struct.unpack(
                "<I", struct.pack("<f", progress))[0]
            return (
                _integer(fields.get("blockId"), "piston/blockId")
                    == moving.get("moved_block")
                and _integer(fields.get("blockData"), "piston/blockData")
                    == moving.get("moved_meta")
                and _integer(fields.get("facing"), "piston/facing")
                    == moving.get("facing")
                and bool(_integer(
                    fields.get("extending"), "piston/extending"))
                    is moving.get("extending")
                and bool(_integer(fields.get("source"), "piston/source"))
                    is moving.get("source")
                and progress_bits == moving.get("progress_bits")
                and progress_bits == moving.get("last_progress_bits")
            )
        except (AnvilImportError, OverflowError, struct.error):
            return False
    if name == "mob_spawner":
        if state.get("spawners_complete") is not True:
            return False
        spawner = next((
            row for row in state.get("spawners", [])
            if isinstance(row, dict)
            and tuple(row.get(axis) for axis in ("x", "y", "z"))
                == position
        ), None)
        if spawner is None:
            return False
        try:
            scalar_fields = {
                "Delay": "delay",
                "MinSpawnDelay": "min_delay",
                "MaxSpawnDelay": "max_delay",
                "SpawnCount": "spawn_count",
                "MaxNearbyEntities": "max_nearby",
                "RequiredPlayerRange": "activate_range",
                "SpawnRange": "spawn_range",
            }
            if any(
                    _integer(fields.get(nbt_name),
                             f"mob_spawner/{nbt_name}")
                        != spawner.get(state_name)
                    for nbt_name, state_name in scalar_fields.items()):
                return False
            spawn_data = _compound(
                fields.get("SpawnData"), "mob_spawner/SpawnData")
            spawn_document = {
                "name": "",
                "tag": {"type": "compound", "value": spawn_data},
            }
            if _string(
                        spawn_data.get("id"), "mob_spawner/SpawnData/id") \
                        != spawner.get("entity_id") \
                    or CAPSULE.nbt_codec.decode_hex(
                        spawner.get("spawn_data_nbt")) != spawn_document \
                    or spawner.get("default_entity_nbt") \
                        is not (set(spawn_data) == {"id"}):
                return False
            node = fields.get("SpawnPotentials")
            potentials = _list(node, "mob_spawner/SpawnPotentials")
            if potentials and node.get("element_type") != "compound":
                return False
            state_potentials = spawner.get("potentials")
            if not isinstance(state_potentials, list) \
                    or len(potentials) != len(state_potentials):
                return False
            for index, (potential_node, expected) in enumerate(
                    zip(potentials, state_potentials)):
                row = _compound(
                    potential_node,
                    f"mob_spawner/SpawnPotentials[{index}]")
                if set(row) != {"Entity", "Weight"}:
                    return False
                entity = _compound(
                    row.get("Entity"),
                    f"mob_spawner/SpawnPotentials[{index}]/Entity")
                entity_document = {
                    "name": "",
                    "tag": {"type": "compound", "value": entity},
                }
                if _string(
                            entity.get("id"),
                            f"mob_spawner/SpawnPotentials[{index}]/Entity/id") \
                            != expected.get("entity_id") \
                        or CAPSULE.nbt_codec.decode_hex(
                            expected.get("entity_nbt")) != entity_document \
                        or _integer(
                            row.get("Weight"),
                            f"mob_spawner/SpawnPotentials[{index}]/Weight") \
                            != expected.get("weight") \
                        or expected.get("default_entity_nbt") \
                            is not (set(entity) == {"id"}):
                    return False
            return True
        except (AnvilImportError, AttributeError, TypeError,
                CAPSULE.nbt_codec.NbtError):
            return False
    expected = container_types.get(name)
    if expected is None:
        return False
    container = next((
        container for container in state.get("containers", [])
        if isinstance(container, dict)
        and tuple(container.get(axis) for axis in ("x", "y", "z"))
            == position
        and container.get("type") in expected
    ), None)
    if container is None:
        return False
    if name == "furnace":
        try:
            custom_name = _string(
                fields.get("CustomName"), "furnace/CustomName") \
                if "CustomName" in fields else ""
        except AnvilImportError:
            return False
        return container.get("custom_name") == custom_name
    return True


def _tile_registry_capabilities(
    semantic: dict[str, Any], state: dict[str, Any], dimension: int,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    try:
        registry = json.loads(REGISTRY_MANIFEST.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise AnvilImportError(
            f"HAR-01: could not load tile registry manifest: {exc}") from exc
    rows = registry.get("tile_entities")
    if not isinstance(rows, list) or len(rows) != 24:
        raise AnvilImportError(
            "HAR-01: tile registry manifest must contain 24 rows")
    by_name = {row.get("name"): row for row in rows
               if isinstance(row, dict) and isinstance(row.get("name"), str)}
    if len(by_name) != len(rows):
        raise AnvilImportError(
            "HAR-01: tile registry manifest names must be unique")
    loaded = state.get("loaded_tiles", [])
    if not isinstance(loaded, list):
        raise AnvilImportError("HAR-02: canonical loaded_tiles is not an array")
    active_by_position: dict[tuple[int, int, int], dict[str, Any]] = {}
    for index, tile in enumerate(loaded):
        if not isinstance(tile, dict):
            raise AnvilImportError(
                f"HAR-02: canonical loaded tile {index} is not an object")
        try:
            position = tuple(int(tile[axis]) for axis in ("x", "y", "z"))
        except (KeyError, TypeError, ValueError) as exc:
            raise AnvilImportError(
                f"HAR-02: canonical loaded tile {index} has no position") \
                from exc
        if position in active_by_position:
            raise AnvilImportError(
                f"SAVE-06: duplicate active tile position {position}")
        active_by_position[position] = tile

    summary = {
        row["name"]: {
            "name": row["name"], "class": row["class"],
            "implementation_status": row["status"], "todo": row["todo"],
            "persisted": 0, "active_exact": 0,
            "active_unsupported": 0, "inactive_persisted": 0,
        }
        for row in rows
    }
    limitations: list[dict[str, Any]] = []
    matched_active: set[tuple[int, int, int]] = set()

    def reject(surface: str, todo: str, reason: str) -> None:
        limitations.append({
            "surface": surface, "status": "reject", "todo": todo,
            "reason": reason, "count": 1,
        })

    chunks = semantic["load_inputs"]["chunks"]
    for chunk_key, document in sorted(chunks.items()):
        level = _chunk_level(document, chunk_key)
        node = level.get("TileEntities")
        if node is None:
            continue
        tiles = _list(node, f"{chunk_key}/TileEntities")
        if tiles and node.get("element_type") != "compound":
            raise AnvilImportError(
                f"{chunk_key}/TileEntities must contain TAG_compound, got "
                f"{node.get('element_type')!r}")
        active_dimension = chunk_key.startswith(f"dim={dimension},")
        for index, tile_node in enumerate(tiles):
            label = f"{chunk_key}/TileEntities[{index}]"
            fields = _compound(tile_node, label)
            resource = _string(fields.get("id"), f"{label}/id")
            name = resource.split(":", 1)[-1]
            row = by_name.get(name)
            if row is None:
                reject(
                    f"world.tiles.{resource}", "HAR-01",
                    "persisted tile is absent from the Java registry manifest")
                continue
            item = summary[name]
            item["persisted"] += 1
            position = tuple(
                _integer(fields.get(axis), f"{label}/{axis}")
                for axis in ("x", "y", "z"))
            active = active_by_position.get(position) \
                if active_dimension else None
            if active is None:
                item["inactive_persisted"] += 1
                reject(
                    f"world.tiles.{resource}", "SAVE-09",
                    "persisted tile is outside the active Java loaded "
                    "boundary and native cold-chunk tile restore is open")
                continue
            matched_active.add(position)
            if active.get("class") != row["class"]:
                item["active_unsupported"] += 1
                reject(
                    f"world.tiles.{resource}", "SAVE-06",
                    "Anvil registry identity disagrees with the active "
                    "authoritative tile class")
                continue
            if not _tile_nbt_supported(name, fields) \
                    or not _tile_state_is_restorable(
                        state, name, position, fields):
                item["active_unsupported"] += 1
                reject(
                    f"world.tiles.{resource}", row["todo"],
                    "active tile NBT has no complete native reconstruction "
                    "schema for this payload")
                continue
            item["active_exact"] += 1

    for position, tile in active_by_position.items():
        if position not in matched_active:
            reject(
                "world.tiles.active_identity", "SAVE-06",
                f"active {tile.get('class', 'tile')} at {position} has no "
                "matching persisted tile identity")

    capabilities = []
    for row in rows:
        item = summary[row["name"]]
        if item["persisted"] == 0:
            item["capability"] = "unobserved"
        elif item["active_exact"] == item["persisted"]:
            item["capability"] = "exact_active_boundary"
        else:
            item["capability"] = "rejected"
        capabilities.append(item)
    return capabilities, limitations


def _scheduled_tick_capabilities(
    semantic: dict[str, Any], state: dict[str, Any], dimension: int,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    canonical = state.get("scheduled_ticks")
    if not isinstance(canonical, list):
        raise AnvilImportError(
            "HAR-02: canonical scheduled_ticks is not an array")
    total_time = state.get("time", {}).get("total_time")
    if isinstance(total_time, bool) or not isinstance(total_time, int):
        raise AnvilImportError(
            "HAR-02: canonical total_time is not an integer")
    unmatched = list(range(len(canonical)))
    persisted = 0
    active_matched = 0
    inactive = 0
    inactive_identities: set[tuple[int, int, int, int, int]] = set()
    inactive_duplicates = 0
    border_duplicates = 0
    unresolved_active: list[tuple[tuple[int, int, int, int], str]] = []
    matched_identities: set[tuple[int, int, int, int]] = set()
    limitations: list[dict[str, Any]] = []

    def reject(surface: str, todo: str, reason: str) -> None:
        limitations.append({
            "surface": surface, "status": "reject", "todo": todo,
            "reason": reason, "count": 1,
        })

    for chunk_key, document in sorted(
            semantic["load_inputs"]["chunks"].items()):
        level = _chunk_level(document, chunk_key)
        node = level.get("TileTicks")
        if node is None:
            continue
        ticks = _list(node, f"{chunk_key}/TileTicks")
        if ticks and node.get("element_type") != "compound":
            raise AnvilImportError(
                f"{chunk_key}/TileTicks must contain TAG_compound, got "
                f"{node.get('element_type')!r}")
        active_dimension = chunk_key.startswith(f"dim={dimension},")
        for index, tick_node in enumerate(ticks):
            label = f"{chunk_key}/TileTicks[{index}]"
            fields = _compound(tick_node, label)
            persisted += 1
            values = tuple(
                _integer(fields.get(field), f"{label}/{field}")
                for field in ("x", "y", "z", "t", "p"))
            block_id = _scheduled_block_id(fields.get("i"), f"{label}/i")
            identity = (values[0], values[1], values[2], block_id)
            if not active_dimension:
                inactive += 1
                inactive_identity = (
                    _chunk_coordinates(chunk_key)[0],) + identity
                if inactive_identity in inactive_identities:
                    inactive_duplicates += 1
                else:
                    inactive_identities.add(inactive_identity)
                continue
            match = next((candidate for candidate in unmatched
                          if (
                              canonical[candidate].get("x"),
                              canonical[candidate].get("y"),
                              canonical[candidate].get("z"),
                              canonical[candidate].get("time") - total_time,
                              canonical[candidate].get("priority"),
                          ) == values
                          and canonical[candidate].get("block") == block_id),
                         None)
            if match is None:
                unresolved_active.append((identity, label))
                continue
            unmatched.remove(match)
            active_matched += 1
            matched_identities.add(identity)
    # WorldServer serializes a scheduled entry into every chunk whose
    # expanded +/-2 save box overlaps it. At a border, multiple TileTicks rows
    # can therefore carry different remaining delays from different save
    # moments. Java's NextTickListEntry HashSet admits the first loaded row and
    # ignores later rows with the same position/block identity. The normalized
    # active queue tells us which duplicate won; only unmatched identities are
    # genuinely cold pending work.
    for identity, _label in unresolved_active:
        if identity in matched_identities:
            border_duplicates += 1
            continue
        inactive += 1
        reject(
            "world.scheduled_ticks.inactive_chunk", "SAVE-09",
            "persisted scheduled tick is outside the active Java loaded "
            "boundary and cold-chunk pending-work restore is open")
    for index in unmatched:
        entry = canonical[index]
        reject(
            "world.scheduled_ticks.active_identity", "SAVE-07",
            "active scheduled tick has no matching persisted Anvil identity "
            f"at {(entry.get('x'), entry.get('y'), entry.get('z'))}")
    return {
        "persisted": persisted,
        "active_matched": active_matched,
        "border_duplicates_ignored": border_duplicates,
        "inactive_persisted": inactive,
        "inactive_unique_retained": len(inactive_identities),
        "inactive_duplicates_ignored": inactive_duplicates,
        "canonical": len(canonical),
        "capsule_accepted": None,
        "status": "rejected" if limitations else "pending_capsule_proof",
    }, limitations


def _inactive_scheduled_tick_events(
    semantic: dict[str, Any], active_dimension: int, total_time: int,
    first_order: int,
) -> list[dict[str, Any]]:
    """Return one future-load row per inactive-dimension callback identity.

    TileTicks deliberately overlap chunk borders. Iterating the semantic chunk
    map in canonical coordinate order mirrors the cold-load winner used by the
    active queue normalizer, while the identity set models Java's HashSet
    rejection of later duplicates.
    """
    events: list[dict[str, Any]] = []
    seen: set[tuple[int, int, int, int, int]] = set()
    order = first_order
    for chunk_key, document in sorted(
            semantic["load_inputs"]["chunks"].items()):
        saved_dimension, _chunk_x, _chunk_z = _chunk_coordinates(chunk_key)
        if saved_dimension == active_dimension:
            continue
        node = _chunk_level(document, chunk_key).get("TileTicks")
        if node is None:
            continue
        for index, tick_node in enumerate(_list(
                node, f"{chunk_key}/TileTicks")):
            label = f"{chunk_key}/TileTicks[{index}]"
            fields = _compound(tick_node, label)
            x, y, z, delay, priority = tuple(
                _integer(fields.get(field), f"{label}/{field}")
                for field in ("x", "y", "z", "t", "p"))
            block = _scheduled_block_id(fields.get("i"), f"{label}/i")
            identity = (saved_dimension, x, y, z, block)
            if identity in seen:
                continue
            seen.add(identity)
            events.append({
                "tick": 0,
                "type": "restore_scheduled_tick",
                "dimension": saved_dimension,
                "x": x, "y": y, "z": z, "block": block,
                "time": total_time + delay,
                "priority": priority,
                "order": order,
            })
            order += 1
    return events


def _limitations(
    snapshot: pathlib.Path, semantic: dict[str, Any],
    coverage: dict[str, Any], dimension: int,
    block_light: bytes, state: dict[str, Any],
) -> tuple[
    list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]],
    dict[str, Any], dict[str, Any],
]:
    entity_registry, limitations = _entity_registry_capabilities(
        semantic, state, dimension)
    tile_registry, tile_limitations = _tile_registry_capabilities(
        semantic, state, dimension)
    limitations.extend(tile_limitations)
    scheduled_ticks, scheduled_limitations = _scheduled_tick_capabilities(
        semantic, state, dimension)
    limitations.extend(scheduled_limitations)
    relationships, relationship_limitations = \
        _entity_relationship_capabilities(snapshot, state, dimension)
    limitations.extend(relationship_limitations)
    return (limitations, entity_registry, tile_registry, scheduled_ticks,
            relationships)


def _sha256(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _fnv64(raw: bytes) -> int:
    value = 1469598103934665603
    for byte in raw:
        value ^= byte
        value = value * 1099511628211 & 0xFFFFFFFFFFFFFFFF
    return value


def _normalized_world(normalized: dict[str, Any], dimension: int) -> dict[str, Any]:
    if normalized.get("ok") is not True or not isinstance(normalized.get("worlds"), list):
        raise AnvilImportError("SAVE-03: invalid reload-normalization document")
    matches = [world for world in normalized["worlds"]
               if isinstance(world, dict) and world.get("dim") == dimension]
    if len(matches) != 1:
        raise AnvilImportError(
            f"SAVE-03: normalization has {len(matches)} worlds for dimension "
            f"{dimension}")
    return matches[0]


def write_active_chunk_bundle(
    semantic: dict[str, Any], normalized: dict[str, Any], dimension: int,
    center_cx: int, center_cz: int, path: pathlib.Path,
) -> dict[str, Any]:
    world = _normalized_world(normalized, dimension)
    entries = world.get("watched_entries")
    if not isinstance(entries, list) or not entries:
        raise AnvilImportError(
            "SAVE-03: normalization has no ordered watched chunk entries")
    seen: set[tuple[int, int]] = set()
    watched: list[tuple[int, int, int]] = []
    radius = 0
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict) or entry.get("order") != index \
                or isinstance(entry.get("x"), bool) \
                or not isinstance(entry.get("x"), int) \
                or isinstance(entry.get("z"), bool) \
                or not isinstance(entry.get("z"), int):
            raise AnvilImportError(
                f"SAVE-03: invalid watched chunk entry at order {index}")
        coordinate = (entry["x"], entry["z"])
        if coordinate in seen:
            raise AnvilImportError(
                f"SAVE-03: duplicate watched chunk {coordinate}")
        seen.add(coordinate)
        flags = (
            (1 if entry.get("ticked") is True else 0)
            | (2 if entry.get("terrain") is True else 0)
            | (4 if entry.get("light") is True else 0))
        watched.append((coordinate[0], coordinate[1], flags))
        radius = max(radius, abs(coordinate[0] - center_cx),
                     abs(coordinate[1] - center_cz))
    if radius > 32:
        raise AnvilImportError(
            f"SAVE-03: watched chunk radius {radius} exceeds native loader limit")
    ticking = world.get("ticking_chunks")
    if not isinstance(ticking, list):
        raise AnvilImportError(
            "SAVE-07: normalization has no ordered ticking chunk entries")
    ticking_by_coordinate: dict[tuple[int, int], tuple[int, int]] = {}
    for index, entry in enumerate(ticking):
        if not isinstance(entry, dict) \
                or isinstance(entry.get("x"), bool) \
                or not isinstance(entry.get("x"), int) \
                or isinstance(entry.get("z"), bool) \
                or not isinstance(entry.get("z"), int) \
                or isinstance(entry.get("random_tick_mask"), bool) \
                or not isinstance(entry.get("random_tick_mask"), int) \
                or not 0 <= entry["random_tick_mask"] <= 0xFFFF:
            raise AnvilImportError(
                f"SAVE-07: invalid ticking chunk entry at order {index}")
        coordinate = (entry["x"], entry["z"])
        if coordinate not in seen:
            raise AnvilImportError(
                f"SAVE-07: ticking chunk is not watched: {coordinate}")
        if coordinate in ticking_by_coordinate:
            raise AnvilImportError(
                f"SAVE-07: duplicate ticking chunk {coordinate}")
        ticking_by_coordinate[coordinate] = (
            index, entry["random_tick_mask"])
    chunks = semantic["load_inputs"]["chunks"]
    loaded = world.get("loaded_chunks")
    if not isinstance(loaded, list) or not loaded:
        raise AnvilImportError(
            "SAVE-07: normalization has no loaded-chunk membership")
    if len(loaded) > 16384:
        raise AnvilImportError(
            f"SAVE-07: {len(loaded)} loaded chunks exceed native limit")
    loaded_coordinates: list[tuple[int, int]] = []
    loaded_seen: set[tuple[int, int]] = set()
    for index, entry in enumerate(loaded):
        if not isinstance(entry, dict) \
                or isinstance(entry.get("x"), bool) \
                or not isinstance(entry.get("x"), int) \
                or isinstance(entry.get("z"), bool) \
                or not isinstance(entry.get("z"), int) \
                or ("order" in entry and entry.get("order") != index):
            raise AnvilImportError(
                f"SAVE-03: invalid loaded chunk at index {index}")
        coordinate = (entry["x"], entry["z"])
        if coordinate in loaded_seen:
            raise AnvilImportError(
                "SAVE-03: loaded chunks are not unique")
        if _chunk_key(dimension, *coordinate) not in chunks:
            raise AnvilImportError(
                "SAVE-01: loaded chunk is absent from saved Anvil data: "
                f"{coordinate}")
        loaded_coordinates.append(coordinate)
        loaded_seen.add(coordinate)
    loaded_set = set(loaded_coordinates)
    pending = world.get("pending_chunk_unloads")
    if not isinstance(pending, list) or len(pending) > 16384:
        raise AnvilImportError(
            "SAVE-09: invalid pending chunk-unload queue")
    pending_unloads: list[tuple[int, int, int]] = []
    pending_seen: set[tuple[int, int]] = set()
    for index, entry in enumerate(pending):
        if not isinstance(entry, dict) \
                or isinstance(entry.get("x"), bool) \
                or not isinstance(entry.get("x"), int) \
                or isinstance(entry.get("z"), bool) \
                or not isinstance(entry.get("z"), int) \
                or not isinstance(entry.get("loaded"), bool) \
                or not isinstance(entry.get("unloaded"), bool):
            raise AnvilImportError(
                f"SAVE-09: invalid pending chunk unload at index {index}")
        coordinate = (entry["x"], entry["z"])
        if coordinate in pending_seen:
            raise AnvilImportError(
                f"SAVE-09: duplicate pending chunk unload {coordinate}")
        if entry["loaded"] and coordinate not in loaded_set:
            raise AnvilImportError(
                f"SAVE-09: pending loaded chunk is absent from membership: "
                f"{coordinate}")
        pending_seen.add(coordinate)
        flags = (1 if entry["loaded"] else 0) \
            | (2 if entry["unloaded"] else 0)
        pending_unloads.append((coordinate[0], coordinate[1], flags))
    ordered = [(
        chunk_x, chunk_z, flags,
        ticking_by_coordinate.get((chunk_x, chunk_z), (-1, 0))[0],
        ticking_by_coordinate.get((chunk_x, chunk_z), (-1, 0))[1],
    ) for chunk_x, chunk_z, flags in watched]
    digest = hashlib.sha256()
    total = 0
    with path.open("wb") as stream:
        header = struct.pack(
            "<8sIiIiiIII", CHUNK_BUNDLE_MAGIC, CHUNK_BUNDLE_VERSION,
            dimension, len(ordered), center_cx, center_cz, len(ticking),
            len(loaded_coordinates), len(pending_unloads))
        stream.write(header)
        digest.update(header)
        total += len(header)
        for chunk_x, chunk_z in loaded_coordinates:
            payload = struct.pack("<ii", chunk_x, chunk_z)
            stream.write(payload)
            digest.update(payload)
            total += len(payload)
        for chunk_x, chunk_z, flags in pending_unloads:
            payload = struct.pack("<iiI", chunk_x, chunk_z, flags)
            stream.write(payload)
            digest.update(payload)
            total += len(payload)
        for chunk_x, chunk_z, flags, tick_order, random_tick_mask in ordered:
            key = _chunk_key(dimension, chunk_x, chunk_z)
            document = chunks.get(key)
            if document is None:
                raise AnvilImportError(
                    f"SAVE-01: watched chunk is absent from saved Anvil data: {key}")
            blocks, sky, block_light = _chunk_arrays(document, key, dimension)
            payload = struct.pack(
                "<iiIiI", chunk_x, chunk_z, flags,
                tick_order, random_tick_mask) \
                + blocks + sky + block_light
            stream.write(payload)
            digest.update(payload)
            total += len(payload)
    return {
        "file": CHUNK_BUNDLE_FILE,
        "version": CHUNK_BUNDLE_VERSION,
        "dimension": dimension,
        "chunks": len(ordered),
        "ticking_chunks": len(ticking),
        "loaded_chunks": len(loaded_coordinates),
        "pending_chunk_unloads": len(pending_unloads),
        "eligible_chunk_unloads": sum(
            1 for _x, _z, flags in pending_unloads if flags == 3),
        "center_cx": center_cx,
        "center_cz": center_cz,
        "radius": radius,
        "bytes": total,
        "sha256": digest.hexdigest(),
    }


def import_snapshot(
    snapshot: pathlib.Path, output: pathlib.Path, box: list[int], bounded: bool,
    normalized: dict[str, Any] | None = None,
) -> dict[str, Any]:
    SAVE_FORK.validate_snapshot(snapshot)
    if output.exists():
        raise AnvilImportError(f"output already exists: {output}")
    semantic = ANVIL.read_save(snapshot / "save")
    boundary = json.loads((snapshot / SAVE_FORK.ORACLE_RESPONSE).read_text())
    authoritative = boundary["authoritative"]
    dimension = int(authoritative["dim"])
    blocks, sky, block_light, coverage = extract_cuboid(
        semantic, dimension, box)
    height = extract_height_cuboid(semantic, dimension, box)
    state, seed, player_path = canonical_state(snapshot, semantic, box)
    statistics_raw, statistics_play, statistics_since, statistics_path = \
        _player_statistics(snapshot, semantic, player_path)
    limitations, entity_registry, tile_registry, scheduled_ticks, \
        relationships = _limitations(
            snapshot, semantic, coverage, dimension, block_light, state)
    report = {
        "schema": "netherite.anvil_native_import",
        "version": 1,
        "source": str(snapshot),
        "dimension": dimension,
        "box": box,
        "playerdata": player_path,
        "player_statistics": {
            "source": statistics_path,
            "file": STATISTICS_FILE,
            "bytes": len(statistics_raw),
            "sha256": _sha256(statistics_raw),
            "fnv64": f"{_fnv64(statistics_raw):016x}",
            "play_one_minute": statistics_play,
            "time_since_death": statistics_since,
            "status": "exact_opaque_plus_tick_counters",
        },
        "payloads": {
            "blocks": {"bytes": len(blocks), "sha256": _sha256(blocks)},
            "sky_light": {"bytes": len(sky), "sha256": _sha256(sky)},
            "block_light": {
                "bytes": len(block_light), "sha256": _sha256(block_light),
            },
            "height": {"bytes": len(height), "sha256": _sha256(height)},
        },
        "coverage": coverage,
        "entity_registry": entity_registry,
        "tile_registry": tile_registry,
        "scheduled_ticks": scheduled_ticks,
        "entity_relationships": relationships,
        "limitations": limitations,
        "strict_status": "rejected" if limitations else "exact",
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f".{output.name}.", dir=str(output.parent)
    ) as raw:
        staging = pathlib.Path(raw) / "import"
        staging.mkdir()
        (staging / STATE_FILE).write_text(
            json.dumps(state, indent=2, sort_keys=True) + "\n")
        (staging / BLOCK_FILE).write_bytes(blocks)
        (staging / SKY_FILE).write_bytes(sky)
        (staging / BLOCK_LIGHT_FILE).write_bytes(block_light)
        (staging / HEIGHT_FILE).write_bytes(height)
        (staging / STATISTICS_FILE).write_bytes(statistics_raw)
        cold_stores = write_cold_chunk_stores(semantic, staging)
        report["cold_chunk_stores"] = cold_stores
        bundle = None
        if normalized is not None:
            center_cx = math.floor(float(authoritative["x"])) >> 4
            center_cz = math.floor(float(authoritative["z"])) >> 4
            bundle = write_active_chunk_bundle(
                semantic, normalized, dimension, center_cx, center_cz,
                staging / CHUNK_BUNDLE_FILE)
            report["active_chunk_bundle"] = bundle
        capsule_dir = staging / "capsule"
        try:
            CAPSULE.create_capsule(
                staging / STATE_FILE, staging / BLOCK_FILE, box, capsule_dir,
                sky_light_path=staging / SKY_FILE, seed=seed,
                block_light_path=staging / BLOCK_LIGHT_FILE,
                source_engine="minecraft-java-anvil",
                source_version="1.11.2",
            )
            capsule_manifest = json.loads(
                (capsule_dir / CAPSULE.MANIFEST_FILE).read_text())
            accepted_scheduled = capsule_manifest["state"]["scheduled_ticks"]
            scheduled_ticks["capsule_accepted"] = len(accepted_scheduled)
            if accepted_scheduled != state["scheduled_ticks"]:
                scheduled_ticks["status"] = "rejected"
                limitations.append({
                    "surface": "world.scheduled_ticks.capsule",
                    "status": "reject", "todo": "SAVE-07",
                    "reason": "one or more active scheduled ticks are "
                    "outside the exact native callback/context subset",
                    "count": len(state["scheduled_ticks"])
                        - len(accepted_scheduled),
                })
                report["strict_status"] = "rejected"
            elif scheduled_ticks["status"] == "pending_capsule_proof":
                scheduled_ticks["status"] = "exact"
            script_path = staging / "magma_restore.jsonl"
            CAPSULE.emit_magma(capsule_dir, script_path)
            prefix_events = []
            shutil.copy2(
                staging / STATISTICS_FILE, capsule_dir / STATISTICS_FILE)
            prefix_events.append({
                "tick": 0,
                "type": "restore_player_statistics",
                "file": STATISTICS_FILE,
                "play_one_minute": statistics_play,
                "time_since_death": statistics_since,
            })
            for store in cold_stores:
                shutil.copy2(staging / store["file"], capsule_dir / store["file"])
                prefix_events.append({
                    "tick": 0,
                    "type": "attach_chunk_store",
                    "file": store["file"],
                    "dim": store["dimension"],
                })
            inactive_events = _inactive_scheduled_tick_events(
                semantic, dimension, int(state["time"]["total_time"]),
                max((int(row["order"])
                     for row in state["scheduled_ticks"]), default=-1) + 1)
            if len(inactive_events) != scheduled_ticks[
                    "inactive_unique_retained"]:
                raise AnvilImportError(
                    "inactive scheduled-work retention count changed "
                    "between classification and emission")
            prefix_events.extend(inactive_events)
            if bundle is not None:
                shutil.copy2(
                    staging / CHUNK_BUNDLE_FILE,
                    capsule_dir / CHUNK_BUNDLE_FILE)
                prefix_events.append({
                    "tick": 0,
                    "type": "snapshot_chunk_bundle",
                    "file": CHUNK_BUNDLE_FILE,
                    "dim": dimension,
                    "cx": bundle["center_cx"],
                    "cz": bundle["center_cz"],
                    "radius": bundle["radius"],
                })
            nx = box[3] - box[0] + 1
            nz = box[5] - box[2] + 1
            plane_cells = nx * nz
            if len(height) != plane_cells * (box[4] - box[1] + 1) * 2:
                raise AnvilImportError("invalid height cuboid byte length")
            first_plane = height[:plane_cells * 2]
            if any(
                height[offset:offset + plane_cells * 2] != first_plane
                for offset in range(plane_cells * 2, len(height), plane_cells * 2)
            ):
                raise AnvilImportError(
                    "height cuboid is not constant across repeated y planes")
            height_events = []
            for index, value in enumerate(
                    struct.unpack(f"<{plane_cells}H", first_plane)):
                height_events.append({
                    "tick": 0,
                    "type": "snapshot_height",
                    "dim": dimension,
                    "x": box[0] + index % nx,
                    "z": box[2] + index // nx,
                    "value": value,
                })
            if prefix_events:
                script_path.write_text(
                    "".join(json.dumps(event, separators=(",", ":")) + "\n"
                            for event in prefix_events)
                    + script_path.read_text()
                    + "".join(json.dumps(event, separators=(",", ":")) + "\n"
                              for event in height_events))
            report["native_capsule"] = {"status": "exact"}
        except CAPSULE.CapsuleError as exc:
            report["native_capsule"] = {
                "status": "reject", "todo": "HAR-03", "reason": str(exc),
            }
            limitations.append({
                "surface": "native_capsule.validation",
                "status": "reject",
                "todo": "HAR-03",
                "reason": str(exc),
                "count": 1,
            })
            report["strict_status"] = "rejected"
        (staging / REPORT_FILE).write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n")
        staging.rename(output)
    if limitations and not bounded:
        first = limitations[0]
        raise AnvilImportError(
            f"{first['todo']}: {first['surface']}: {first['reason']} "
            f"(count={first['count']}); bounded artifacts retained at {output}")
    return report


def _default_box(snapshot: pathlib.Path) -> list[int]:
    boundary = json.loads((snapshot / SAVE_FORK.ORACLE_RESPONSE).read_text())
    authoritative = boundary.get("authoritative") or {}
    try:
        x, y, z = (math.floor(float(authoritative[name]))
                   for name in ("x", "y", "z"))
    except (KeyError, TypeError, ValueError) as exc:
        raise AnvilImportError(
            "snapshot has no authoritative player position for default box") \
            from exc
    return [x - 8, max(0, y - 8), z - 8,
            x + 8, min(255, y + 8), z + 8]


def _parse_box(text: str | None, snapshot: pathlib.Path) -> list[int]:
    if text is None:
        return _default_box(snapshot)
    try:
        box = [int(value) for value in text.split(",")]
    except ValueError as exc:
        raise AnvilImportError("--box must contain six comma-separated integers") \
            from exc
    CAPSULE.cell_count(box)
    return box


def first_cuboid_difference(
    left: pathlib.Path, right: pathlib.Path, box: list[int]
) -> dict[str, Any] | None:
    left_semantic = ANVIL.read_save(left / "save")
    right_semantic = ANVIL.read_save(right / "save")
    boundary = json.loads((left / SAVE_FORK.ORACLE_RESPONSE).read_text())
    dimension = int(boundary["authoritative"]["dim"])
    left_values = extract_cuboid(left_semantic, dimension, box)[:3]
    right_values = extract_cuboid(right_semantic, dimension, box)[:3]
    labels = ("block_state", "sky_light", "block_light")
    widths = (2, 1, 1)
    for label, width, left_raw, right_raw in zip(
            labels, widths, left_values, right_values):
        if left_raw == right_raw:
            continue
        cells = len(left_raw) // width
        for index in range(cells):
            start = index * width
            if left_raw[start:start + width] == right_raw[start:start + width]:
                continue
            lv = int.from_bytes(left_raw[start:start + width], "little")
            rv = int.from_bytes(right_raw[start:start + width], "little")
            return {
                "surface": label,
                "coordinate": CAPSULE.coordinate(index, box),
                "left": lv, "right": rv,
            }
    return None


def _section_document(block_id: int, meta: int, sky: int, light: int):
    blocks = bytearray(4096)
    data = bytearray(2048)
    sky_raw = bytearray(2048)
    light_raw = bytearray(2048)
    index = (2 << 8) | (3 << 4) | 4
    blocks[index] = block_id & 0xFF
    if index & 1:
        data[index >> 1] |= (meta & 15) << 4
        sky_raw[index >> 1] |= (sky & 15) << 4
        light_raw[index >> 1] |= (light & 15) << 4
    else:
        data[index >> 1] |= meta & 15
        sky_raw[index >> 1] |= sky & 15
        light_raw[index >> 1] |= light & 15

    def array(raw: bytes) -> dict[str, Any]:
        return {"type": "byte_array", "count": len(raw),
                "value_hex": raw.hex()}

    def int_array(values: list[int]) -> dict[str, Any]:
        raw = struct.pack(f">{len(values)}i", *values)
        return {"type": "int_array", "count": len(values),
                "value_hex": raw.hex()}

    return {
        "name": "",
        "tag": {"type": "compound", "value": {
            "Level": {"type": "compound", "value": {
                "xPos": {"type": "int", "value": 0},
                "zPos": {"type": "int", "value": 0},
                "HeightMap": int_array([4] * 256),
                "Biomes": array(bytes([1]) * 256),
                "Sections": {"type": "list", "element_type": "compound",
                             "value": [{"type": "compound", "value": {
                                 "Y": {"type": "byte", "value": 0},
                                 "Blocks": array(blocks),
                                 "Data": array(data),
                                 "SkyLight": array(sky_raw),
                                 "BlockLight": array(light_raw),
                             }}]},
                "Entities": {"type": "list", "element_type": "end",
                             "value": []},
                "TileEntities": {"type": "list", "element_type": "end",
                                 "value": []},
            }}
        }},
    }


def selftest() -> None:
    ender_root = {"EnderItems": {
        "type": "list", "element_type": "compound", "value": [{
            "type": "compound", "value": {
                "Slot": {"type": "byte", "value": 7},
                "id": {"type": "string", "value": "minecraft:diamond"},
                "Count": {"type": "byte", "value": 5},
                "Damage": {"type": "short", "value": 0},
                "tag": {"type": "compound", "value": {
                    "RepairCost": {"type": "int", "value": 3},
                    "display": {"type": "compound", "value": {
                        "Name": {"type": "string", "value": "Vault"},
                    }},
                }},
            },
        }],
    }}
    ender = _saved_ender_inventory(ender_root, "playerdata/selftest.dat")
    if len(ender) != 1 or ender[0]["slot"] != 7 \
            or ender[0]["id"] != 264 or ender[0]["count"] != 5 \
            or ender[0]["repair_cost"] != 3 \
            or ender[0]["custom_name"] != "Vault" \
            or ender[0]["stack_payload"]["kind"] != "item_tag":
        raise AnvilImportError(
            "player EnderItems did not decode into exact capsule stacks")
    box = [4, 2, 3, 4, 2, 3]
    left_doc = _section_document(35, 14, 12, 7)
    semantic = {"load_inputs": {
        "chunks": {_chunk_key(0, 0, 0): left_doc}}}
    blocks, sky, light, coverage = extract_cuboid(semantic, 0, box)
    if struct.unpack("<H", blocks)[0] != (35 << 4 | 14) \
            or sky != bytes([12]) or light != bytes([7]) \
            or coverage["unselected_chunks"]:
        raise AnvilImportError("Anvil block/light nibble decode is incorrect")
    if struct.unpack("<H", extract_height_cuboid(semantic, 0, box))[0] != 4:
        raise AnvilImportError("Anvil height-map decode is incorrect")
    observer = {"load_inputs": {"chunks": {
        _chunk_key(0, 0, 0): _section_document(218, 12, 15, 0),
    }}}
    observer_blocks = extract_cuboid(observer, 0, box)[0]
    if struct.unpack("<H", observer_blocks)[0] != (218 << 4 | 4):
        raise AnvilImportError(
            "observer cold-load metadata canonicalization is incorrect")
    right_doc = _section_document(35, 13, 12, 7)
    right = {"load_inputs": {
        "chunks": {_chunk_key(0, 0, 0): right_doc}}}
    right_blocks = extract_cuboid(right, 0, box)[0]
    if blocks == right_blocks:
        raise AnvilImportError("block-state negative control was not detected")
    # A missing selected chunk must never become implicit air.
    try:
        extract_cuboid({"load_inputs": {"chunks": {}}}, 0, box)
    except AnvilImportError as exc:
        if "absent saved chunk" not in str(exc):
            raise
    else:
        raise AnvilImportError("missing chunk was silently accepted")
    # Registry-aware persistence must admit a represented EntityItem while
    # detecting both an inactive cold-chunk row and an unknown registry ID.
    entity_doc = _section_document(1, 0, 15, 0)
    entity_level = _chunk_level(entity_doc, "selftest/entity")
    entity_level["Entities"] = {
        "type": "list", "element_type": "compound", "value": [{
            "type": "compound", "value": {
                "id": {"type": "string", "value": "minecraft:item"},
                "UUIDMost": {"type": "long", "value": -7},
                "UUIDLeast": {"type": "long", "value": 11},
            },
        }],
    }
    entity_semantic = {"load_inputs": {"chunks": {
        _chunk_key(0, 0, 0): entity_doc,
    }}}
    entity_state = {"entities": [{
        "type": "EntityItem", "eid": 0, "uuid_most": -7,
        "uuid_least": 11, "item_exact": True,
    }]}
    registry, entity_limits = _entity_registry_capabilities(
        entity_semantic, entity_state, 0)
    item_row = next(row for row in registry if row["name"] == "item")
    if entity_limits or item_row["capability"] != "exact_active_boundary" \
            or item_row["active_exact"] != 1:
        raise AnvilImportError(
            "represented EntityItem was rejected by registry classification")
    _registry, inactive_limits = _entity_registry_capabilities(
        entity_semantic, {"entities": []}, 0)
    if len(inactive_limits) != 1 \
            or inactive_limits[0].get("todo") != "SAVE-09":
        raise AnvilImportError(
            "inactive persisted-entity negative control was not detected")
    entity_level["Entities"]["value"][0]["value"]["id"]["value"] = \
        "minecraft:not_registered"
    _registry, unknown_limits = _entity_registry_capabilities(
        entity_semantic, {"entities": []}, 0)
    if not unknown_limits or unknown_limits[0].get("todo") != "HAR-01":
        raise AnvilImportError(
            "unknown entity-registry negative control was not detected")
    # WorldServer writes border callbacks into overlapping +/-2 chunk save
    # boxes. Prove that a second persisted row for the canonical identity is
    # classified as serialization duplication, while a distinct position is
    # still rejected as unsupported cold pending work.
    def tick_node(x: int, delay: int) -> dict[str, Any]:
        return {"type": "compound", "value": {
            "i": {"type": "string", "value": "minecraft:stone"},
            "x": {"type": "int", "value": x},
            "y": {"type": "int", "value": 4},
            "z": {"type": "int", "value": 8},
            "t": {"type": "int", "value": delay},
            "p": {"type": "int", "value": 2},
        }}

    tick_left = _section_document(1, 0, 15, 0)
    tick_right = _section_document(1, 0, 15, 0)
    _chunk_level(tick_left, "selftest/tick-left")["TileTicks"] = {
        "type": "list", "element_type": "compound",
        "value": [tick_node(15, 4)],
    }
    _chunk_level(tick_right, "selftest/tick-right")["TileTicks"] = {
        "type": "list", "element_type": "compound",
        "value": [tick_node(15, 10)],
    }
    tick_semantic = {"load_inputs": {"chunks": {
        _chunk_key(0, 0, 0): tick_left,
        _chunk_key(0, 1, 0): tick_right,
    }}}
    tick_state = {
        "time": {"total_time": 100},
        "scheduled_ticks": [{
            "x": 15, "y": 4, "z": 8, "block": 1,
            "time": 104, "priority": 2,
        }],
    }
    tick_report, tick_limits = _scheduled_tick_capabilities(
        tick_semantic, tick_state, 0)
    if tick_limits or tick_report["persisted"] != 2 \
            or tick_report["active_matched"] != 1 \
            or tick_report["border_duplicates_ignored"] != 1 \
            or tick_report["status"] != "pending_capsule_proof":
        raise AnvilImportError(
            "scheduled border-duplicate serialization was not exact")
    _chunk_level(tick_right, "selftest/tick-right")["TileTicks"][
        "value"][0] = tick_node(16, 10)
    rejected_report, rejected_limits = _scheduled_tick_capabilities(
        tick_semantic, tick_state, 0)
    if rejected_report["inactive_persisted"] != 1 \
            or len(rejected_limits) != 1 \
            or rejected_limits[0].get("todo") != "SAVE-09":
        raise AnvilImportError(
            "distinct cold scheduled-work negative control was accepted")
    relationship_state = {
        "player": {
            "uuid_most_hex": "0123456789abcdef",
            "uuid_least_hex": "fedcba9876543210",
        },
        "entities": entity_state["entities"],
    }
    player_uuid = _uuid_text(0x0123456789ABCDEF, 0xFEDCBA9876543210)
    entity_uuid = _uuid_text(-7, 11)
    with tempfile.TemporaryDirectory(
            prefix="netherite-relationships-") as raw:
        snapshot = pathlib.Path(raw)
        hidden = {
            "schema": "qrl.hidden_state.v1",
            "worlds": [{
                "dim": 0,
                "entities": [{
                    "order": 0, "uuid": player_uuid,
                    "riding_uuid": "", "passenger_uuids": [],
                }, {
                    "order": 1, "uuid": entity_uuid,
                    "riding_uuid": "", "passenger_uuids": [],
                }],
            }],
        }
        (snapshot / SAVE_FORK.HIDDEN_STATE).write_text(json.dumps(hidden))
        relationship_report, relationship_limits = \
            _entity_relationship_capabilities(
                snapshot, relationship_state, 0)
        if relationship_limits \
                or relationship_report["status"] != "exact_empty_graph":
            raise AnvilImportError(
                "empty loaded passenger graph was not classified exact")
        hidden["worlds"][0]["entities"][0]["riding_uuid"] = entity_uuid
        hidden["worlds"][0]["entities"][1]["passenger_uuids"] = [
            player_uuid]
        (snapshot / SAVE_FORK.HIDDEN_STATE).write_text(json.dumps(hidden))
        relationship_report, relationship_limits = \
            _entity_relationship_capabilities(
                snapshot, relationship_state, 0)
        if relationship_report["riding_edges"] != 1 \
                or not relationship_limits \
                or relationship_limits[-1].get("todo") != "SAVE-03":
            raise AnvilImportError(
                "mounted passenger negative control was silently accepted")
        minecart_relationship_state = {
            "player": dict(relationship_state["player"]),
            "entities": [{
                "type": "EntityMinecartEmpty", "eid": 90,
                "uuid_most": -7, "uuid_least": 11,
            }],
        }
        relationship_report, relationship_limits = \
            _entity_relationship_capabilities(
                snapshot, minecart_relationship_state, 0)
        if relationship_limits \
                or relationship_report["status"] != \
                    "exact_player_minecart" \
                or minecart_relationship_state["player"].get(
                    "riding_eid") != 90:
            raise AnvilImportError(
                "player-minecart passenger graph was not classified exact")
    normalized = {
        "ok": True,
        "worlds": [{
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
            "loaded_chunks": [{"x": 0, "z": 0}],
            "pending_chunk_unloads": [],
        }],
    }
    with tempfile.TemporaryDirectory(prefix="netherite-chunk-bundle-") as raw:
        path = pathlib.Path(raw) / CHUNK_BUNDLE_FILE
        report = write_active_chunk_bundle(
            semantic, normalized, 0, 0, 0, path)
        payload = path.read_bytes()
        expected_size = struct.calcsize("<8sIiIiiIII") \
            + 8 + 20 + 4 * 65536
        if report["chunks"] != 1 or len(payload) != expected_size \
                or report["ticking_chunks"] != 1 \
                or report["loaded_chunks"] != 1 \
                or report["pending_chunk_unloads"] != 0 \
                or hashlib.sha256(payload).hexdigest() != report["sha256"] \
                or payload[:8] != CHUNK_BUNDLE_MAGIC:
            raise AnvilImportError("active chunk bundle is not deterministic")
    print(
        "PASS Anvil native import selftest: "
        "block/meta/light/order/bundle/registry/rejection")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("create")
    create.add_argument("snapshot", type=pathlib.Path)
    create.add_argument("output", type=pathlib.Path)
    create.add_argument("--box")
    create.add_argument("--normalized", type=pathlib.Path)
    create.add_argument("--bounded", action="store_true")
    compare = sub.add_parser("compare-cuboid")
    compare.add_argument("left", type=pathlib.Path)
    compare.add_argument("right", type=pathlib.Path)
    compare.add_argument("--box")
    sub.add_parser("selftest")
    args = parser.parse_args()
    if args.command == "selftest":
        selftest()
        return 0
    if args.command == "create":
        snapshot = args.snapshot.resolve()
        normalized = None
        if args.normalized is not None:
            try:
                normalized = json.loads(args.normalized.read_text())
            except (OSError, json.JSONDecodeError) as exc:
                raise AnvilImportError(
                    f"invalid --normalized document: {exc}") from exc
        report = import_snapshot(
            snapshot, args.output.resolve(), _parse_box(args.box, snapshot),
            args.bounded, normalized=normalized)
        print(json.dumps({
            "status": report["strict_status"],
            "box": report["box"],
            "selected_chunks": len(report["coverage"]["selected_chunks"]),
            "saved_dimension_chunks": len(
                report["coverage"]["dimension_chunks"]),
            "limitations": report["limitations"],
            "output": str(args.output.resolve()),
        }, indent=2, sort_keys=True))
        return 0
    left, right = args.left.resolve(), args.right.resolve()
    SAVE_FORK.validate_snapshot(left)
    SAVE_FORK.validate_snapshot(right)
    difference = first_cuboid_difference(
        left, right, _parse_box(args.box, left))
    if difference is None:
        print("PASS Anvil cuboids are identical")
        return 0
    print(json.dumps(difference, indent=2, sort_keys=True))
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        AnvilImportError, ANVIL.AnvilSemanticError, SAVE_FORK.SaveForkError,
        CAPSULE.CapsuleError, KeyError, OSError, ValueError,
    ) as exc:
        print(f"FAIL Anvil native import: {exc}", file=sys.stderr)
        raise SystemExit(1)
