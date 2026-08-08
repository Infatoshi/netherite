#!/usr/bin/env python3
"""Lossless semantic reader and comparator for Minecraft 1.11.2 saves.

The on-disk region allocator is intentionally not part of world semantics:
sector placement, padding and chunk timestamps can change without changing
what Java reloads.  Every load-relevant NBT value remains typed, list order is
preserved, and floating-point values remain raw IEEE bits through nbt_codec.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import json
import pathlib
import re
import struct
import sys
import tempfile
import zlib
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[2]
NBT_SOURCE = ROOT / "magma" / "trace" / "nbt_codec.py"
REGION_RE = re.compile(r"^r\.(-?[0-9]+)\.(-?[0-9]+)\.mca$")
SCHEMA = "netherite.anvil_semantic"
VERSION = 1

# These level.dat values are rewritten as save bookkeeping and are not read
# back into simulation state by WorldInfo.  Keep the policy narrow and named.
RELOAD_IRRELEVANT_NBT_PATHS = {
    "level.dat:/Data/LastPlayed",
    "level.dat:/Data/SizeOnDisk",
}


class AnvilSemanticError(RuntimeError):
    pass


def _load_nbt_codec():
    spec = importlib.util.spec_from_file_location("netherite_nbt_codec", NBT_SOURCE)
    if spec is None or spec.loader is None:
        raise AnvilSemanticError("could not load nbt_codec.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


NBT = _load_nbt_codec()


def _sha256(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _decompress_nbt(raw: bytes, source: str) -> bytes:
    try:
        if raw.startswith(b"\x1f\x8b"):
            return gzip.decompress(raw)
        return raw
    except (OSError, EOFError) as exc:
        raise AnvilSemanticError(f"invalid gzip NBT in {source}: {exc}") from exc


def _decode_nbt(raw: bytes, source: str) -> dict[str, Any]:
    try:
        return NBT.decode(_decompress_nbt(raw, source))
    except NBT.NbtError as exc:
        raise AnvilSemanticError(f"invalid NBT in {source}: {exc}") from exc


def _compound_value(node: Any, source: str) -> dict[str, Any]:
    if not isinstance(node, dict) or node.get("type") != "compound":
        raise AnvilSemanticError(f"{source} is not an NBT compound")
    value = node.get("value")
    if not isinstance(value, dict):
        raise AnvilSemanticError(f"{source} has an invalid compound payload")
    return value


def _int_value(node: Any, source: str) -> int:
    if not isinstance(node, dict) or node.get("type") not in {
        "byte", "short", "int", "long"
    } or not isinstance(node.get("value"), int):
        raise AnvilSemanticError(f"{source} is not an integer NBT tag")
    return node["value"]


def _compact_arrays(value: Any) -> Any:
    """Keep large primitive arrays exact without expanding every byte in JSON."""
    if isinstance(value, list):
        return [_compact_arrays(item) for item in value]
    if not isinstance(value, dict):
        return value
    kind = value.get("type")
    items = value.get("value")
    if set(value) == {"type", "value"} \
            and isinstance(kind, str) \
            and kind in {"byte_array", "int_array", "long_array"} \
            and isinstance(items, list):
        if kind == "byte_array":
            raw = bytes(item & 0xFF for item in items)
        else:
            fmt = ">i" if kind == "int_array" else ">q"
            raw = b"".join(struct.pack(fmt, item) for item in items)
        return {"type": kind, "count": len(items), "value_hex": raw.hex()}
    return {key: _compact_arrays(item) for key, item in value.items()}


def _dimension_for_region(relative: pathlib.PurePosixPath) -> int:
    parts = relative.parts
    if parts[0] == "region":
        return 0
    if len(parts) == 3 and parts[0] == "DIM-1" and parts[1] == "region":
        return -1
    if len(parts) == 3 and parts[0] == "DIM1" and parts[1] == "region":
        return 1
    raise AnvilSemanticError(f"unsupported region location: {relative}")


def _decode_region(
    path: pathlib.Path, relative: pathlib.PurePosixPath
) -> dict[str, dict[str, Any]]:
    match = REGION_RE.fullmatch(path.name)
    if match is None:
        raise AnvilSemanticError(f"invalid region filename: {relative}")
    region_x, region_z = (int(match.group(1)), int(match.group(2)))
    dimension = _dimension_for_region(relative)
    raw = path.read_bytes()
    if len(raw) < 8192 or len(raw) % 4096:
        raise AnvilSemanticError(
            f"region {relative} is not a whole number of sectors")
    chunks: dict[str, dict[str, Any]] = {}
    allocated: list[tuple[int, int, int]] = []
    for slot in range(1024):
        location = int.from_bytes(raw[slot * 4:slot * 4 + 3], "big")
        sectors = raw[slot * 4 + 3]
        if location == 0 and sectors == 0:
            continue
        if location < 2 or sectors == 0:
            raise AnvilSemanticError(
                f"region {relative} slot {slot} has a partial location")
        start = location * 4096
        limit = start + sectors * 4096
        if limit > len(raw):
            raise AnvilSemanticError(
                f"region {relative} slot {slot} points outside the file")
        for other_start, other_limit, other_slot in allocated:
            if start < other_limit and other_start < limit:
                raise AnvilSemanticError(
                    f"region {relative} slots {other_slot}/{slot} overlap")
        allocated.append((start, limit, slot))
        payload_length = struct.unpack_from(">I", raw, start)[0]
        if payload_length < 1 or payload_length > sectors * 4096 - 4:
            raise AnvilSemanticError(
                f"region {relative} slot {slot} has invalid chunk length")
        compression = raw[start + 4]
        compressed = raw[start + 5:start + 4 + payload_length]
        try:
            if compression == 1:
                uncompressed = gzip.decompress(compressed)
            elif compression == 2:
                uncompressed = zlib.decompress(compressed)
            else:
                raise AnvilSemanticError(
                    f"region {relative} slot {slot} uses compression {compression}")
        except (OSError, EOFError, zlib.error) as exc:
            raise AnvilSemanticError(
                f"region {relative} slot {slot} has corrupt compressed NBT: {exc}"
            ) from exc
        source = f"{relative}:slot[{slot}]"
        try:
            document = NBT.decode(uncompressed)
        except NBT.NbtError as exc:
            raise AnvilSemanticError(f"invalid NBT in {source}: {exc}") from exc
        local_x, local_z = slot & 31, slot >> 5
        chunk_x = region_x * 32 + local_x
        chunk_z = region_z * 32 + local_z
        root = _compound_value(document["tag"], source)
        level = _compound_value(root.get("Level"), f"{source}/Level")
        stored_x = _int_value(level.get("xPos"), f"{source}/Level/xPos")
        stored_z = _int_value(level.get("zPos"), f"{source}/Level/zPos")
        if (stored_x, stored_z) != (chunk_x, chunk_z):
            raise AnvilSemanticError(
                f"{source} claims chunk {stored_x},{stored_z}, expected "
                f"{chunk_x},{chunk_z}")
        key = f"dim={dimension},x={chunk_x},z={chunk_z}"
        chunks[key] = _compact_arrays(document)
    return chunks


def _delete_nbt_path(document: dict[str, Any], path: str) -> None:
    parts = [part for part in path.split("/") if part]
    value = _compound_value(document["tag"], "root")
    for part in parts[:-1]:
        child = value.get(part)
        if child is None:
            return
        value = _compound_value(child, "/" + "/".join(parts))
    if parts:
        value.pop(parts[-1], None)


def _json_document(path: pathlib.Path, relative: str) -> Any:
    try:
        return json.loads(path.read_text())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise AnvilSemanticError(f"invalid JSON save file {relative}: {exc}") from exc


def read_save(save_dir: pathlib.Path) -> dict[str, Any]:
    """Read every file and classify whether/how Java reload consumes it."""
    save_dir = save_dir.resolve()
    if not save_dir.is_dir():
        raise AnvilSemanticError(f"save directory does not exist: {save_dir}")
    level_path = save_dir / "level.dat"
    if not level_path.is_file():
        raise AnvilSemanticError("save has no level.dat")

    load_inputs: dict[str, Any] = {}
    file_policy: dict[str, dict[str, str]] = {}
    chunks: dict[str, Any] = {}
    for path in sorted(save_dir.rglob("*")):
        if path.is_symlink():
            raise AnvilSemanticError(f"save contains a symlink: {path}")
        if not path.is_file():
            continue
        relative = path.relative_to(save_dir).as_posix()
        pure = pathlib.PurePosixPath(relative)
        if path.name == "session.lock":
            file_policy[relative] = {
                "status": "derived",
                "rule": "exclusive-open marker; never simulation state",
            }
        elif REGION_RE.fullmatch(path.name) and (
            pure.parent.as_posix() == "region"
            or pure.parent.as_posix() in {"DIM-1/region", "DIM1/region"}
        ):
            decoded = _decode_region(path, pure)
            for key, value in decoded.items():
                if key in chunks:
                    raise AnvilSemanticError(f"duplicate chunk identity {key}")
                chunks[key] = value
            file_policy[relative] = {
                "status": "exact",
                "rule": "chunk NBT exact; allocator sectors/timestamps ignored",
            }
        elif relative == "level.dat":
            decoded = _decode_nbt(path.read_bytes(), relative)
            for ignored in sorted(RELOAD_IRRELEVANT_NBT_PATHS):
                prefix, nbt_path = ignored.split(":", 1)
                if prefix == relative:
                    _delete_nbt_path(decoded, nbt_path)
            load_inputs[relative] = _compact_arrays(decoded)
            file_policy[relative] = {
                "status": "exact",
                "rule": "typed NBT exact except named reload-irrelevant bookkeeping",
            }
        elif relative == "level.dat_old":
            file_policy[relative] = {
                "status": "fallback",
                "rule": "used only when level.dat cannot be read",
            }
        elif pure.parts and pure.parts[0] == "playerdata" and path.suffix == ".dat":
            load_inputs[relative] = _compact_arrays(
                _decode_nbt(path.read_bytes(), relative))
            file_policy[relative] = {"status": "exact", "rule": "typed NBT exact"}
        elif path.suffix == ".dat":
            load_inputs[relative] = _compact_arrays(
                _decode_nbt(path.read_bytes(), relative))
            file_policy[relative] = {"status": "exact", "rule": "typed NBT exact"}
        elif path.suffix == ".json":
            load_inputs[relative] = _json_document(path, relative)
            file_policy[relative] = {
                "status": "exact",
                "rule": "parsed JSON exact; object key order ignored",
            }
        elif relative == "icon.png":
            raw = path.read_bytes()
            load_inputs[relative] = {"bytes": len(raw), "sha256": _sha256(raw)}
            file_policy[relative] = {
                "status": "ui_exact",
                "rule": "world-selection icon bytes; not simulation state",
            }
        else:
            raise AnvilSemanticError(
                f"unclassified save file {relative}; add an explicit load rule")

    if not chunks:
        raise AnvilSemanticError("save contains no readable Anvil chunks")
    load_inputs["chunks"] = {key: chunks[key] for key in sorted(chunks)}
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "load_inputs": {key: load_inputs[key] for key in sorted(load_inputs)},
        "file_policy": {key: file_policy[key] for key in sorted(file_policy)},
    }


def first_difference(left: Any, right: Any, path: str = "$") -> dict[str, Any] | None:
    if type(left) is not type(right):
        return {"path": path, "left": left, "right": right, "reason": "type"}
    if isinstance(left, dict):
        left_keys, right_keys = set(left), set(right)
        if left_keys != right_keys:
            missing_left = sorted(right_keys - left_keys)
            missing_right = sorted(left_keys - right_keys)
            return {
                "path": path,
                "left_only": missing_right,
                "right_only": missing_left,
                "reason": "keys",
            }
        if set(left) == {"type", "count", "value_hex"} and left["type"] in {
            "byte_array", "int_array", "long_array"
        } and left != right:
            if left["type"] != right["type"]:
                return {
                    "path": f"{path}/type", "left": left["type"],
                    "right": right["type"], "reason": "value",
                }
            if left["count"] != right["count"]:
                return {
                    "path": path, "left": left["count"], "right": right["count"],
                    "reason": "array_length",
                }
            width, fmt = {
                "byte_array": (1, ">b"),
                "int_array": (4, ">i"),
                "long_array": (8, ">q"),
            }[left["type"]]
            left_raw = bytes.fromhex(left["value_hex"])
            right_raw = bytes.fromhex(right["value_hex"])
            for index in range(left["count"]):
                offset = index * width
                left_value = struct.unpack_from(fmt, left_raw, offset)[0]
                right_value = struct.unpack_from(fmt, right_raw, offset)[0]
                if left_value != right_value:
                    return {
                        "path": f"{path}[{index}]", "left": left_value,
                        "right": right_value, "reason": "value",
                    }
            raise AnvilSemanticError(f"invalid compact array comparison at {path}")
        for key in sorted(left):
            found = first_difference(left[key], right[key], f"{path}/{key}")
            if found is not None:
                return found
        return None
    if isinstance(left, list):
        if len(left) != len(right):
            return {
                "path": path,
                "left": len(left),
                "right": len(right),
                "reason": "list_length",
            }
        for index, (left_item, right_item) in enumerate(zip(left, right)):
            found = first_difference(
                left_item, right_item, f"{path}[{index}]"
            )
            if found is not None:
                return found
        return None
    if left != right:
        return {"path": path, "left": left, "right": right, "reason": "value"}
    return None


def compare_saves(left: pathlib.Path, right: pathlib.Path) -> dict[str, Any] | None:
    return first_difference(read_save(left), read_save(right))


def _inventory_key(key: str) -> str:
    if key.startswith("dim=") and ",x=" in key and ",z=" in key:
        return "<chunk>"
    if re.fullmatch(r"[0-9a-fA-F-]{32,36}\.(?:dat|json)", key):
        return "<player>" + pathlib.PurePosixPath(key).suffix
    return key


def _restore_todo(path: str) -> str:
    if "/TileTicks" in path or "RandomSeed" in path or "/rng" in path.lower():
        return "SAVE-07"
    if "/TileEntities[]" in path:
        return "SAVE-06"
    if "/Entities[]" in path:
        return "SAVE-05"
    if "/Inventory[]" in path or "/Item/" in path or "/Items[]" in path:
        return "SAVE-04"
    if "/data/villages" in path:
        return "AI-02"
    if "/stats/" in path:
        return "SAVE-02"
    return "SAVE-01"


def field_inventory(document: dict[str, Any]) -> list[dict[str, Any]]:
    """Collapse observed values into a complete wildcarded field schema."""
    observed: dict[str, dict[str, Any]] = {}

    def visit(value: Any, path: str) -> None:
        if isinstance(value, dict):
            if set(value) == {"type", "count", "value_hex"}:
                signature = f"{value['type']}[{value['count']}]"
                row = observed.setdefault(path, {"representations": set(), "count": 0})
                row["representations"].add(signature)
                row["count"] += 1
                return
            if set(value) == {"type", "value"} and value.get("type") not in {
                "compound", "list"
            }:
                row = observed.setdefault(path, {"representations": set(), "count": 0})
                row["representations"].add(f"nbt:{value['type']}")
                row["count"] += 1
                return
            if set(value) == {"type", "element_type", "value"}:
                row = observed.setdefault(
                    path + "/@element_type", {"representations": set(), "count": 0})
                row["representations"].add(str(value["element_type"]))
                row["count"] += 1
            if not value:
                row = observed.setdefault(path, {"representations": set(), "count": 0})
                row["representations"].add("empty_object")
                row["count"] += 1
                return
            for key, child in value.items():
                if key == "value" and value.get("type") == "list":
                    visit(child, path)
                else:
                    visit(child, f"{path}/{_inventory_key(str(key))}")
            return
        if isinstance(value, list):
            if not value:
                row = observed.setdefault(path + "[]", {"representations": set(), "count": 0})
                row["representations"].add("empty_list")
                row["count"] += 1
            for child in value:
                visit(child, path + "[]")
            return
        representation = (
            "null" if value is None else "bool" if isinstance(value, bool)
            else "int" if isinstance(value, int) else "float"
            if isinstance(value, float) else "string" if isinstance(value, str)
            else type(value).__name__
        )
        row = observed.setdefault(path, {"representations": set(), "count": 0})
        row["representations"].add(representation)
        row["count"] += 1

    visit(document["load_inputs"], "$/load_inputs")
    return [
        {
            "path": path,
            "representations": sorted(row["representations"]),
            "observations": row["count"],
            "semantic_compare": "exact",
            "native_restore": "reject",
            "todo": _restore_todo(path),
        }
        for path, row in sorted(observed.items())
    ]


def _named_compound(value: dict[str, Any]) -> dict[str, Any]:
    return {"name": "", "tag": {"type": "compound", "value": value}}


def _int(value: int) -> dict[str, Any]:
    return {"type": "int", "value": value}


def _long(value: int) -> dict[str, Any]:
    return {"type": "long", "value": value}


def _compound(value: dict[str, Any]) -> dict[str, Any]:
    return {"type": "compound", "value": value}


def _write_gzip_nbt(path: pathlib.Path, document: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(gzip.compress(NBT.encode(document), mtime=0))


def _write_region(
    path: pathlib.Path, chunk_x: int, chunk_z: int, marker: int, timestamp: int
) -> None:
    document = _named_compound({
        "Level": _compound({
            "Marker": _int(marker),
            "xPos": _int(chunk_x),
            "zPos": _int(chunk_z),
        })
    })
    compressed = zlib.compress(NBT.encode(document))
    payload = bytes([2]) + compressed
    sectors = (len(payload) + 4 + 4095) // 4096
    slot = (chunk_x & 31) + (chunk_z & 31) * 32
    header = bytearray(8192)
    header[slot * 4:slot * 4 + 3] = (2).to_bytes(3, "big")
    header[slot * 4 + 3] = sectors
    struct.pack_into(">I", header, 4096 + slot * 4, timestamp)
    body = struct.pack(">I", len(payload)) + payload
    body += bytes(sectors * 4096 - len(body))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(header) + body)


def selftest() -> None:
    adversarial = {"type": {"type": "byte", "value": 4},
                   "value": {"type": "int_array", "value": [-1, 2]}}
    compacted = _compact_arrays(adversarial)
    if compacted != {
            "type": {"type": "byte", "value": 4},
            "value": {"type": "int_array", "count": 2,
                      "value_hex": "ffffffff00000002"}}:
        raise AnvilSemanticError(
            "user NBT fields named type/value confused compact array typing")
    with tempfile.TemporaryDirectory(prefix="netherite-anvil-semantic-") as raw:
        root = pathlib.Path(raw)
        left, right = root / "left", root / "right"
        for save, last_played, timestamp in ((left, 10, 100), (right, 20, 200)):
            _write_gzip_nbt(save / "level.dat", _named_compound({
                "Data": _compound({
                    "LastPlayed": _long(last_played),
                    "Seed": _long(7),
                })
            }))
            _write_gzip_nbt(
                save / "playerdata" / "p.dat", _named_compound({"Score": _int(3)})
            )
            _write_region(save / "region" / "r.0.0.mca", 0, 0, 4, timestamp)
        difference = compare_saves(left, right)
        if difference is not None:
            raise AnvilSemanticError(
                f"bookkeeping/timestamp negative control differed: {difference}")
        _write_region(right / "region" / "r.0.0.mca", 0, 0, 5, 200)
        difference = compare_saves(left, right)
        if difference is None or not difference["path"].endswith("/Marker/value"):
            raise AnvilSemanticError(
                f"chunk mutation was not localized: {difference}")
        inventory = field_inventory(read_save(left))
        if not inventory or any(not row.get("todo") for row in inventory):
            raise AnvilSemanticError("field capability inventory is incomplete")
        _write_region(right / "region" / "r.0.0.mca", 0, 0, 4, 200)
        right_doc = read_save(right)
        del right_doc["load_inputs"]["playerdata/p.dat"]["tag"]["value"]["Score"]
        difference = first_difference(read_save(left), right_doc)
        if difference is None or difference.get("reason") != "keys":
            raise AnvilSemanticError(
                f"deleted exporter field was not rejected: {difference}")
        print("PASS Anvil semantic selftest: reload noise ignored, NBT mutation localized")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    dump = sub.add_parser("dump")
    dump.add_argument("save", type=pathlib.Path)
    compare = sub.add_parser("compare")
    compare.add_argument("left", type=pathlib.Path)
    compare.add_argument("right", type=pathlib.Path)
    inventory = sub.add_parser("inventory")
    inventory.add_argument("save", type=pathlib.Path)
    sub.add_parser("selftest")
    args = parser.parse_args()
    if args.command == "dump":
        print(json.dumps(read_save(args.save), indent=2, sort_keys=True))
    elif args.command == "compare":
        difference = compare_saves(args.left, args.right)
        if difference is not None:
            print(json.dumps(difference, indent=2, sort_keys=True))
            return 1
        print("PASS semantic Anvil saves are identical")
    elif args.command == "inventory":
        document = read_save(args.save)
        print(json.dumps(field_inventory(document), indent=2, sort_keys=True))
    else:
        selftest()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AnvilSemanticError, OSError, ValueError) as exc:
        print(f"FAIL Anvil semantic: {exc}", file=sys.stderr)
        raise SystemExit(1)
