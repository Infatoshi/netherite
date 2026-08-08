#!/usr/bin/env python3
"""Capture canonical metadata-zero item display names from MC 1.11.2."""

import argparse
import json
import pathlib
import sys

OUTPUT = pathlib.Path(__file__).with_name("item_display_manifest.h")
sys.path.insert(0, str(OUTPUT.parents[1] / "trace"))
from test_dragon_crystal_notification import request  # noqa: E402


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    request(args.port, "server_step_lock")
    try:
        payload = request(args.port, "item_registry_locked")
    finally:
        request(args.port, "server_step_unlock")
    rows = payload.get("items", [])
    if len(rows) != 392 or any(not row.get("name") for row in rows):
        raise RuntimeError(f"expected 392 named item rows, got {len(rows)}")
    rows.sort(key=lambda row: int(row["id"]))
    lines = [
        "#ifndef MAGMA_ITEM_DISPLAY_MANIFEST_H",
        "#define MAGMA_ITEM_DISPLAY_MANIFEST_H",
        "typedef struct { int id; const char *name; const char *display; } GmItemDisplay;",
        f"static const GmItemDisplay gm_item_displays[{len(rows)}] = {{",
    ]
    lines.extend(
        f"    {{{int(row['id'])}, {c_string(row['name'])}, "
        f"{c_string(row['display'])}}}," for row in rows
    )
    lines.extend([
        "};",
        "#endif",
        "",
    ])
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"item display manifest: {len(rows)} real-Java rows")


if __name__ == "__main__":
    main()
