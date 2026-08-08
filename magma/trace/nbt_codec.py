#!/usr/bin/env python3
"""Bounded, lossless codec for Minecraft's uncompressed binary NBT format.

The parity harness uses this module at cold save/load and trace boundaries.
The canonical JSON-compatible form preserves numeric tag widths and raw
floating-point bits, while compound equality remains independent of Java map
iteration order.
"""

from __future__ import annotations

import argparse
import struct
from typing import Any


MAX_NBT_BYTES = 1 << 20
MAX_NBT_DEPTH = 64
MAX_NBT_NODES = 65536

TAG_NAMES = {
    0: "end",
    1: "byte",
    2: "short",
    3: "int",
    4: "long",
    5: "float",
    6: "double",
    7: "byte_array",
    8: "string",
    9: "list",
    10: "compound",
    11: "int_array",
    12: "long_array",
}
TAG_IDS = {name: tag_id for tag_id, name in TAG_NAMES.items()}


class NbtError(ValueError):
    pass


def _decode_modified_utf8(raw: bytes) -> str:
    units: list[int] = []
    index = 0
    while index < len(raw):
        first = raw[index]
        if first >> 4 <= 7:
            units.append(first)
            index += 1
        elif first >> 4 in (12, 13):
            if index + 1 >= len(raw) or raw[index + 1] & 0xC0 != 0x80:
                raise NbtError("malformed two-byte modified UTF-8 sequence")
            units.append(((first & 0x1F) << 6) | (raw[index + 1] & 0x3F))
            index += 2
        elif first >> 4 == 14:
            if (index + 2 >= len(raw)
                    or raw[index + 1] & 0xC0 != 0x80
                    or raw[index + 2] & 0xC0 != 0x80):
                raise NbtError("malformed three-byte modified UTF-8 sequence")
            units.append(
                ((first & 0x0F) << 12)
                | ((raw[index + 1] & 0x3F) << 6)
                | (raw[index + 2] & 0x3F)
            )
            index += 3
        else:
            raise NbtError("malformed modified UTF-8 lead byte")
    packed = b"".join(struct.pack(">H", unit) for unit in units)
    return packed.decode("utf-16-be", errors="surrogatepass")


def _encode_modified_utf8(value: str) -> bytes:
    if not isinstance(value, str):
        raise NbtError("NBT name/string must be text")
    utf16 = value.encode("utf-16-be", errors="surrogatepass")
    out = bytearray()
    for offset in range(0, len(utf16), 2):
        unit = struct.unpack_from(">H", utf16, offset)[0]
        if 0x0001 <= unit <= 0x007F:
            out.append(unit)
        elif unit <= 0x07FF:
            out.extend((0xC0 | (unit >> 6), 0x80 | (unit & 0x3F)))
        else:
            out.extend((
                0xE0 | (unit >> 12),
                0x80 | ((unit >> 6) & 0x3F),
                0x80 | (unit & 0x3F),
            ))
    if len(out) > 0xFFFF:
        raise NbtError("modified UTF-8 value exceeds Java's 65535-byte limit")
    return bytes(out)


class _Reader:
    def __init__(self, data: bytes):
        if not isinstance(data, bytes):
            raise NbtError("NBT payload must be bytes")
        if not data:
            raise NbtError("NBT payload is empty")
        if len(data) > MAX_NBT_BYTES:
            raise NbtError(
                f"NBT payload exceeds {MAX_NBT_BYTES}-byte harness bound")
        self.data = data
        self.offset = 0
        self.nodes = 0

    def take(self, size: int) -> bytes:
        if size < 0 or self.offset + size > len(self.data):
            raise NbtError("truncated NBT payload")
        raw = self.data[self.offset:self.offset + size]
        self.offset += size
        return raw

    def unpack(self, fmt: str) -> Any:
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, self.take(size))[0]

    def utf(self) -> str:
        size = self.unpack(">H")
        return _decode_modified_utf8(self.take(size))

    def node(self, tag_id: int, depth: int) -> dict[str, Any]:
        if tag_id not in TAG_NAMES or tag_id == 0:
            raise NbtError(f"invalid payload tag id {tag_id}")
        if depth > MAX_NBT_DEPTH:
            raise NbtError(f"NBT exceeds depth bound {MAX_NBT_DEPTH}")
        self.nodes += 1
        if self.nodes > MAX_NBT_NODES:
            raise NbtError(f"NBT exceeds node bound {MAX_NBT_NODES}")
        kind = TAG_NAMES[tag_id]
        if tag_id == 1:
            value = self.unpack(">b")
        elif tag_id == 2:
            value = self.unpack(">h")
        elif tag_id == 3:
            value = self.unpack(">i")
        elif tag_id == 4:
            value = self.unpack(">q")
        elif tag_id == 5:
            value = self.take(4).hex()
        elif tag_id == 6:
            value = self.take(8).hex()
        elif tag_id in (7, 11, 12):
            count = self.unpack(">i")
            if count < 0:
                raise NbtError(f"negative {kind} length")
            width, fmt = (
                (1, ">b") if tag_id == 7
                else (4, ">i") if tag_id == 11
                else (8, ">q")
            )
            if count > (len(self.data) - self.offset) // width:
                raise NbtError(f"truncated or excessive {kind}")
            value = [self.unpack(fmt) for _ in range(count)]
        elif tag_id == 8:
            value = self.utf()
        elif tag_id == 9:
            child_id = self.unpack(">B")
            count = self.unpack(">i")
            if child_id not in TAG_NAMES:
                raise NbtError(f"invalid list element tag id {child_id}")
            if count < 0:
                raise NbtError("negative list length")
            if child_id == 0 and count != 0:
                raise NbtError("non-empty list cannot contain TAG_End")
            if count > MAX_NBT_NODES - self.nodes:
                raise NbtError("list exceeds remaining NBT node bound")
            value = [self.node(child_id, depth + 1) for _ in range(count)]
            return {
                "type": kind,
                "element_type": TAG_NAMES[child_id],
                "value": value,
            }
        elif tag_id == 10:
            value = {}
            while True:
                child_id = self.unpack(">B")
                if child_id == 0:
                    break
                if child_id not in TAG_NAMES:
                    raise NbtError(f"invalid compound child tag id {child_id}")
                name = self.utf()
                if name in value:
                    raise NbtError(f"duplicate compound key {name!r}")
                value[name] = self.node(child_id, depth + 1)
            value = {key: value[key] for key in sorted(value)}
        else:
            raise AssertionError(tag_id)
        return {"type": kind, "value": value}


def decode(data: bytes) -> dict[str, Any]:
    """Decode one complete named root compound into canonical typed data."""
    reader = _Reader(data)
    tag_id = reader.unpack(">B")
    if tag_id != 10:
        raise NbtError("root tag must be TAG_Compound")
    name = reader.utf()
    tag = reader.node(tag_id, 0)
    if reader.offset != len(data):
        raise NbtError("trailing bytes after root NBT compound")
    return {"name": name, "tag": tag}


def decode_hex(value: str) -> dict[str, Any]:
    if not isinstance(value, str) or len(value) % 2:
        raise NbtError("NBT hex must be an even-length string")
    try:
        raw = bytes.fromhex(value)
    except ValueError as exc:
        raise NbtError("NBT hex contains a non-hexadecimal character") from exc
    return decode(raw)


def _integer(value: Any, low: int, high: int, kind: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) \
            or not low <= value <= high:
        raise NbtError(f"{kind} value is outside {low}..{high}")
    return value


def _encode_node(node: Any, depth: int) -> tuple[int, bytes]:
    if not isinstance(node, dict):
        raise NbtError("canonical NBT node must be an object")
    kind = node.get("type")
    tag_id = TAG_IDS.get(kind, -1)
    if tag_id <= 0:
        raise NbtError(f"invalid canonical NBT node type {kind!r}")
    if depth > MAX_NBT_DEPTH:
        raise NbtError(f"NBT exceeds depth bound {MAX_NBT_DEPTH}")
    expected = {"type", "value"}
    if tag_id == 9:
        expected.add("element_type")
    if set(node) != expected:
        raise NbtError(f"{kind} node has unknown or missing fields")
    value = node["value"]
    if tag_id == 1:
        payload = struct.pack(">b", _integer(value, -128, 127, kind))
    elif tag_id == 2:
        payload = struct.pack(">h", _integer(value, -32768, 32767, kind))
    elif tag_id == 3:
        payload = struct.pack(">i", _integer(
            value, -(1 << 31), (1 << 31) - 1, kind))
    elif tag_id == 4:
        payload = struct.pack(">q", _integer(
            value, -(1 << 63), (1 << 63) - 1, kind))
    elif tag_id in (5, 6):
        width = 4 if tag_id == 5 else 8
        if not isinstance(value, str) or len(value) != width * 2:
            raise NbtError(f"{kind} value must contain {width} raw bytes as hex")
        try:
            payload = bytes.fromhex(value)
        except ValueError as exc:
            raise NbtError(f"{kind} bits are not hexadecimal") from exc
    elif tag_id in (7, 11, 12):
        if not isinstance(value, list):
            raise NbtError(f"{kind} value must be an array")
        width, fmt, low, high = (
            (1, ">b", -128, 127) if tag_id == 7
            else (4, ">i", -(1 << 31), (1 << 31) - 1)
            if tag_id == 11
            else (8, ">q", -(1 << 63), (1 << 63) - 1)
        )
        if len(value) > (MAX_NBT_BYTES - 4) // width:
            raise NbtError(f"{kind} exceeds payload bound")
        payload = struct.pack(">i", len(value)) + b"".join(
            struct.pack(fmt, _integer(item, low, high, kind))
            for item in value
        )
    elif tag_id == 8:
        raw = _encode_modified_utf8(value)
        payload = struct.pack(">H", len(raw)) + raw
    elif tag_id == 9:
        child_kind = node["element_type"]
        child_id = TAG_IDS.get(child_kind, -1)
        if child_id < 0 or not isinstance(value, list):
            raise NbtError("list has invalid element_type or value")
        if child_id == 0 and value:
            raise NbtError("non-empty list cannot contain TAG_End")
        children = []
        for child in value:
            encoded_id, child_payload = _encode_node(child, depth + 1)
            if encoded_id != child_id:
                raise NbtError("list child type does not match element_type")
            children.append(child_payload)
        payload = bytes([child_id]) + struct.pack(">i", len(value)) \
            + b"".join(children)
    elif tag_id == 10:
        if not isinstance(value, dict) or any(
                not isinstance(key, str) for key in value):
            raise NbtError("compound value must be a string-keyed object")
        children = []
        for name in sorted(value):
            child_id, child_payload = _encode_node(value[name], depth + 1)
            encoded_name = _encode_modified_utf8(name)
            children.append(
                bytes([child_id]) + struct.pack(">H", len(encoded_name))
                + encoded_name + child_payload
            )
        payload = b"".join(children) + b"\x00"
    else:
        raise AssertionError(tag_id)
    return tag_id, payload


def encode(document: Any) -> bytes:
    """Encode canonical typed data as one uncompressed named root compound."""
    if not isinstance(document, dict) or set(document) != {"name", "tag"}:
        raise NbtError("canonical NBT document must contain exactly name and tag")
    name = _encode_modified_utf8(document["name"])
    tag_id, payload = _encode_node(document["tag"], 0)
    if tag_id != 10:
        raise NbtError("root tag must be TAG_Compound")
    result = bytes([tag_id]) + struct.pack(">H", len(name)) + name + payload
    if len(result) > MAX_NBT_BYTES:
        raise NbtError(
            f"NBT payload exceeds {MAX_NBT_BYTES}-byte harness bound")
    # Apply the decoder's node/depth/UTF contract to generated documents too;
    # encoding must never create a payload that this harness would refuse.
    decode(result)
    return result


def encode_hex(document: Any) -> str:
    return encode(document).hex()


def canonical_hex(value: str) -> dict[str, Any]:
    """Return the order-independent, typed semantic form of NBT hex."""
    return decode_hex(value)


def _selftest() -> None:
    document = {
        "name": "",
        "tag": {
            "type": "compound",
            "value": {
                "a_byte": {"type": "byte", "value": -7},
                "a_short": {"type": "short", "value": -30000},
                "an_int": {"type": "int", "value": -123456789},
                "a_long": {"type": "long", "value": -(1 << 62)},
                "a_float": {"type": "float", "value": "7fc00001"},
                "a_double": {"type": "double", "value": "fff0000000000000"},
                "bytes": {"type": "byte_array", "value": [-128, 0, 127]},
                "text": {
                    "type": "string",
                    "value": "nul:\u0000 edge:\u07ff bmp:\u0800 face:\U0001f642",
                },
                "empty": {
                    "type": "list", "element_type": "end", "value": [],
                },
                "list": {
                    "type": "list",
                    "element_type": "compound",
                    "value": [{
                        "type": "compound",
                        "value": {"x": {"type": "int", "value": 4}},
                    }],
                },
                "compound": {"type": "compound", "value": {}},
                "ints": {
                    "type": "int_array",
                    "value": [-(1 << 31), 0, (1 << 31) - 1],
                },
                "longs": {
                    "type": "long_array",
                    "value": [-(1 << 63), 0, (1 << 63) - 1],
                },
            },
        },
    }
    raw = encode(document)
    assert decode(raw) == document
    assert encode(decode(raw)) == raw

    malformed = [
        b"",
        b"\x01\x00\x00\x00",
        raw + b"\x00",
        b"\x0a\x00\x00\x07\x00\x01x\xff\xff\xff\xff\x00",
        b"\x0a\x00\x00\x09\x00\x01x\x00\x00\x00\x00\x01\x00",
        b"\x0a\x00\x00\x08\x00\x01x\x00\x01\xff\x00",
    ]
    for candidate in malformed:
        try:
            decode(candidate)
        except NbtError:
            pass
        else:
            raise AssertionError(f"malformed NBT passed: {candidate.hex()}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("selftest",))
    args = parser.parse_args()
    if args.command == "selftest":
        _selftest()
        print("nbt_codec selftest PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
