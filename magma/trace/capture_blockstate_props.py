#!/usr/bin/env python3
"""Capture immutable 1.11.2 block-state predicates from a QRL oracle."""

import argparse
import hashlib
import json
import pathlib
import socket


def request(host, port):
    with socket.create_connection((host, port), timeout=10.0) as sock:
        stream = sock.makefile("rwb")
        stream.write(b'{"cmd":"blockstate_props"}\n')
        stream.flush()
        line = stream.readline()
    if not line:
        raise RuntimeError("oracle closed without a blockstate_props response")
    response = json.loads(line)
    if not response.get("ok"):
        raise RuntimeError(f"oracle rejected blockstate_props: {response}")
    return response


def validate(response):
    if response.get("schema") != "qrl.blockstate_props.v2":
        raise ValueError("unexpected blockstate property schema")
    if response.get("minecraft") != "1.11.2":
        raise ValueError("blockstate property source is not Minecraft 1.11.2")
    reported_errors = response.get("error_count")
    if not isinstance(reported_errors, int) or reported_errors < 0:
        raise ValueError("invalid metadata error count")
    ids = set()
    invalid_slots = 0
    for row in response.get("blocks", []):
        block_id = row.get("id")
        if not isinstance(block_id, int) or not 0 <= block_id < 256:
            raise ValueError(f"invalid legacy block id: {block_id!r}")
        if block_id in ids:
            raise ValueError(f"duplicate legacy block id: {block_id}")
        ids.add(block_id)
        for field in (
                "normal_cube_mask", "full_cube_mask", "fully_opaque_mask",
                "opaque_material_mask", "solid_material_mask",
                "can_provide_power_mask",
                "piston_destroy_mask", "piston_block_mask",
                "piston_unbreakable_mask", "tile_entity_mask"):
            value = row.get(field)
            if not isinstance(value, int) or not 0 <= value <= 0xFFFF:
                raise ValueError(
                    f"invalid {field} for block {block_id}: {value!r}")
        canonical = row.get("canonical_meta")
        if (not isinstance(canonical, list) or len(canonical) != 16
                or any(not isinstance(value, int) or not -1 <= value <= 15
                       for value in canonical)):
            raise ValueError(
                f"invalid canonical_meta for block {block_id}")
        for meta, value in enumerate(canonical):
            if value != -1:
                continue
            invalid_slots += 1
            bit = 1 << meta
            for field in (
                    "normal_cube_mask", "full_cube_mask",
                    "fully_opaque_mask",
                    "opaque_material_mask", "solid_material_mask",
                    "can_provide_power_mask",
                    "piston_destroy_mask", "piston_block_mask",
                    "piston_unbreakable_mask", "tile_entity_mask"):
                if row[field] & bit:
                    raise ValueError(
                        f"invalid metadata {block_id}:{meta} set in {field}")
    expected_ids = set(range(256))
    if ids != expected_ids:
        raise ValueError(
            f"oracle legacy block id coverage differs: "
            f"missing={sorted(expected_ids - ids)}, "
            f"extra={sorted(ids - expected_ids)}")
    if invalid_slots != reported_errors:
        raise ValueError(
            f"oracle metadata error count {reported_errors} does not match "
            f"{invalid_slots} canonical -1 slots")


def canonical_bytes(response):
    return (json.dumps(
        response, indent=2, sort_keys=True, separators=(",", ": "),
    ) + "\n").encode()


def header_text(response, digest):
    canonical_masks = [0] * 256
    normal_masks = [0] * 256
    full_masks = [0] * 256
    fully_opaque_masks = [0] * 256
    solid_material_masks = [0] * 256
    power_masks = [0] * 256
    piston_destroy_masks = [0] * 256
    piston_block_masks = [0] * 256
    piston_unbreakable_masks = [0] * 256
    tile_entity_masks = [0] * 256
    for row in response["blocks"]:
        canonical_masks[row["id"]] = sum(
            1 << meta
            for meta, value in enumerate(row["canonical_meta"])
            if value != -1
        )
        normal_masks[row["id"]] = row["normal_cube_mask"]
        full_masks[row["id"]] = row["full_cube_mask"]
        fully_opaque_masks[row["id"]] = row["fully_opaque_mask"]
        solid_material_masks[row["id"]] = row["solid_material_mask"]
        power_masks[row["id"]] = row["can_provide_power_mask"]
        piston_destroy_masks[row["id"]] = row["piston_destroy_mask"]
        piston_block_masks[row["id"]] = row["piston_block_mask"]
        piston_unbreakable_masks[row["id"]] = (
            row["piston_unbreakable_mask"])
        tile_entity_masks[row["id"]] = row["tile_entity_mask"]
    lines = [
        "#ifndef MAGMA_BLOCK_NORMAL_CUBE_1_11_2_H",
        "#define MAGMA_BLOCK_NORMAL_CUBE_1_11_2_H",
        "",
        "#include <stdint.h>",
        "",
        "/* Generated from the live Java 1.11.2 block registry by",
        " * trace/capture_blockstate_props.py.",
        f" * Canonical JSON sha256: {digest}",
        " */",
        "static const uint16_t gm_canonical_meta_masks_1_11_2[256] = {",
    ]
    for start in range(0, 256, 8):
        chunk = ", ".join(
            f"0x{value:04x}"
            for value in canonical_masks[start:start + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "static const uint16_t gm_normal_cube_masks_1_11_2[256] = {",
    ])
    for start in range(0, 256, 8):
        chunk = ", ".join(
            f"0x{value:04x}"
            for value in normal_masks[start:start + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "static const uint16_t gm_full_cube_masks_1_11_2[256] = {",
    ])
    for start in range(0, 256, 8):
        chunk = ", ".join(
            f"0x{value:04x}"
            for value in full_masks[start:start + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "static const uint16_t gm_fully_opaque_masks_1_11_2[256] = {",
    ])
    for start in range(0, 256, 8):
        chunk = ", ".join(
            f"0x{value:04x}"
            for value in fully_opaque_masks[start:start + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "static const uint16_t gm_solid_material_masks_1_11_2[256] = {",
    ])
    for start in range(0, 256, 8):
        chunk = ", ".join(
            f"0x{value:04x}"
            for value in solid_material_masks[start:start + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "static const uint16_t gm_power_provider_masks_1_11_2[256] = {",
    ])
    for start in range(0, 256, 8):
        chunk = ", ".join(
            f"0x{value:04x}"
            for value in power_masks[start:start + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "static const uint16_t gm_piston_destroy_masks_1_11_2[256] = {",
    ])
    for start in range(0, 256, 8):
        chunk = ", ".join(
            f"0x{value:04x}"
            for value in piston_destroy_masks[start:start + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "static const uint16_t gm_piston_block_masks_1_11_2[256] = {",
    ])
    for start in range(0, 256, 8):
        chunk = ", ".join(
            f"0x{value:04x}"
            for value in piston_block_masks[start:start + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "static const uint16_t gm_piston_unbreakable_masks_1_11_2[256] = {",
    ])
    for start in range(0, 256, 8):
        chunk = ", ".join(
            f"0x{value:04x}"
            for value in piston_unbreakable_masks[start:start + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "static const uint16_t gm_tile_entity_masks_1_11_2[256] = {",
    ])
    for start in range(0, 256, 8):
        chunk = ", ".join(
            f"0x{value:04x}"
            for value in tile_entity_masks[start:start + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "static inline int gm_block_meta_canonical_1_11_2(",
        "        int id, int meta) {",
        "    return id >= 0 && id < 256 && meta >= 0 && meta < 16",
        "        && (gm_canonical_meta_masks_1_11_2[id] &",
        "            (uint16_t)(1u << meta)) != 0;",
        "}",
        "",
        "static inline int gm_block_is_normal_cube_1_11_2(int id, int meta) {",
        "    return id >= 0 && id < 256",
        "        && (gm_normal_cube_masks_1_11_2[id] &",
        "            (uint16_t)(1u << (meta & 15))) != 0;",
        "}",
        "",
        "static inline int gm_block_is_full_cube_1_11_2(int id, int meta) {",
        "    return id >= 0 && id < 256",
        "        && (gm_full_cube_masks_1_11_2[id] &",
        "            (uint16_t)(1u << (meta & 15))) != 0;",
        "}",
        "",
        "static inline int gm_block_is_fully_opaque_1_11_2(",
        "        int id, int meta) {",
        "    return id >= 0 && id < 256",
        "        && (gm_fully_opaque_masks_1_11_2[id] &",
        "            (uint16_t)(1u << (meta & 15))) != 0;",
        "}",
        "",
        "static inline int gm_block_material_is_solid_1_11_2(",
        "        int id, int meta) {",
        "    return id >= 0 && id < 256",
        "        && (gm_solid_material_masks_1_11_2[id] &",
        "            (uint16_t)(1u << (meta & 15))) != 0;",
        "}",
        "",
        "static inline int gm_block_can_provide_power_1_11_2(",
        "        int id, int meta) {",
        "    return id >= 0 && id < 256",
        "        && (gm_power_provider_masks_1_11_2[id] &",
        "            (uint16_t)(1u << (meta & 15))) != 0;",
        "}",
        "",
        "static inline int gm_block_piston_destroy_1_11_2(",
        "        int id, int meta) {",
        "    return id >= 0 && id < 256",
        "        && (gm_piston_destroy_masks_1_11_2[id] &",
        "            (uint16_t)(1u << (meta & 15))) != 0;",
        "}",
        "",
        "static inline int gm_block_piston_block_1_11_2(",
        "        int id, int meta) {",
        "    return id >= 0 && id < 256",
        "        && (gm_piston_block_masks_1_11_2[id] &",
        "            (uint16_t)(1u << (meta & 15))) != 0;",
        "}",
        "",
        "static inline int gm_block_piston_unbreakable_1_11_2(",
        "        int id, int meta) {",
        "    return id >= 0 && id < 256",
        "        && (gm_piston_unbreakable_masks_1_11_2[id] &",
        "            (uint16_t)(1u << (meta & 15))) != 0;",
        "}",
        "",
        "static inline int gm_block_has_tile_entity_1_11_2(",
        "        int id, int meta) {",
        "    return id >= 0 && id < 256",
        "        && (gm_tile_entity_masks_1_11_2[id] &",
        "            (uint16_t)(1u << (meta & 15))) != 0;",
        "}",
        "",
        "#endif",
        "",
    ])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--out", required=True)
    parser.add_argument("--header")
    args = parser.parse_args()

    response = request(args.host, args.port)
    validate(response)
    payload = canonical_bytes(response)
    digest = hashlib.sha256(payload).hexdigest()
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(payload)
    print(
        f"wrote {len(response['blocks'])} block rows -> {out} "
        f"({response['error_count']} invalid metadata slots, "
        f"sha256={digest})")
    if args.header:
        header = pathlib.Path(args.header)
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(header_text(response, digest))
        print(f"wrote block-state masks -> {header}")


if __name__ == "__main__":
    main()
