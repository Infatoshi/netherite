#!/usr/bin/env python3
"""Lock the measured WORLD-05 live-bounded sign/banner boundary."""

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def source_has(path, tokens):
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main():
    manifest = json.loads((HERE / "decorative_tile_manifest.json")
                          .read_text(encoding="utf-8"))
    require(manifest["schema"] == "netherite.decorative_tile_gate"
            and manifest["version"] == 1
            and manifest["todo"] == "WORLD-05"
            and manifest["classification"] == "live_bounded",
            "invalid decorative tile manifest identity")
    registry = json.loads((HERE / "registry_manifest.json")
                          .read_text(encoding="utf-8"))
    rows = [row for row in registry["tile_entities"]
            if row["class"] in ("TileEntitySign", "TileEntityBanner")]
    require(len(rows) == 2 and all(
        row["status"] == "live_bounded" and row["todo"] == "WORLD-05"
        for row in rows), "sign/banner registry rows are not live-bounded")
    source_has(ROOT / "magma/trace/state_capsule.py", (
        '"tile_entities.sign_persistent_nbt": "exact"',
        '"tile_entities.banner_persistent_nbt": "exact"',
        '"type": "set_decorative_tile"'))
    source_has(ROOT / "magma/game/runtime.c", (
        "gm_runtime_decorative_tile_set_nbt",
        "runtime_spawn_decorative_tile_item"))
    source_has(ROOT / "magma/trace/run_oracle_matrix.py", (
        '"standing_sign_support_teardown"',
        '"standing_banner_support_teardown"',
        '"mixed_command_sign_support_teardown_seed_0"',
        "rich_sign_tile_nbt.json", "rich_banner_tile_nbt.json"))
    source_has(ROOT / "magma/trace/diff_trace.py", (
        "tile_entities.decorative", "exact sign/banner tile and drop NBT"))
    print("PASS decorative tiles: rich sign text and banner pattern/name NBT, "
          "exact support-loss drops, continuation, and direct Java parity")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, StopIteration, ValueError, RuntimeError) as error:
        print(f"FAIL decorative tiles: {error}")
        raise SystemExit(1)
