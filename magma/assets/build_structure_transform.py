#!/usr/bin/env python3
"""Record Minecraft 1.11.2's legacy state mirror/rotation table."""

import argparse
import base64
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "java"))
from qrl_client import NetheriteEnv  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path(__file__).with_name(
            "structure_transform_1_11_2.h"),
    )
    args = parser.parse_args()
    env = NetheriteEnv(args.host, args.port)
    locked = None
    try:
        reset = env.reset({
            "seed": 708, "mode": "creative", "type": "flat",
            "structures": False,
        })
        if not reset.get("ok"):
            raise SystemExit(reset)
        locked = env._cmd({"cmd": "server_step_lock"})
        if not locked.get("ok"):
            raise SystemExit(locked)
        result = env._cmd({
            "cmd": "structure_transform_table_locked", "action": {},
        })
    finally:
        if locked and locked.get("ok"):
            unlocked = env._cmd({"cmd": "server_step_unlock"})
            if not unlocked.get("ok"):
                raise RuntimeError(unlocked)
        env.close()
    if not result.get("ok"):
        raise SystemExit(result)
    shape = tuple(result[key] for key in (
        "block_count", "meta_count", "mirror_count", "rotation_count",
    ))
    if shape != (256, 16, 3, 4):
        raise SystemExit(f"unexpected transform table shape: {shape}")
    raw = base64.b64decode(result["table_b64"], validate=True)
    if len(raw) != 256 * 16 * 3 * 4:
        raise SystemExit(f"unexpected transform table length: {len(raw)}")
    categories = base64.b64decode(
        result["categories_b64"], validate=True)
    if len(categories) != 256 * 16 or any(value > 2 for value in categories):
        raise SystemExit("invalid structure block-category table")
    lines = [
        "/* GENERATED from the real Minecraft 1.11.2 block registry by",
        " * magma/assets/build_structure_transform.py. DO NOT EDIT. */",
        "#ifndef MAGMA_STRUCTURE_TRANSFORM_1_11_2_H",
        "#define MAGMA_STRUCTURE_TRANSFORM_1_11_2_H",
        "",
        "static const unsigned char GM_STRUCTURE_TRANSFORM_META[49152] = {",
    ]
    for start in range(0, len(raw), 32):
        lines.append("    " + ",".join(
            str(value) for value in raw[start:start + 32]) + ",")
    lines += [
        "};", "",
        "/* Template.takeBlocksFromWorld order: full, tile, non-full. */",
        "static const unsigned char GM_STRUCTURE_BLOCK_CATEGORY[4096] = {",
    ]
    for start in range(0, len(categories), 32):
        lines.append("    " + ",".join(
            str(value) for value in categories[start:start + 32]) + ",")
    lines += ["};", "", "#endif", ""]
    args.output.write_text("\n".join(lines), encoding="ascii")
    print(f"wrote {args.output} ({len(raw)} exact state transforms, "
          f"{len(categories)} exact template categories)")


if __name__ == "__main__":
    main()
