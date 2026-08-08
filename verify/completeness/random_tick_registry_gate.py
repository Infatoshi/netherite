#!/usr/bin/env python3
"""Pin Java Block.getTickRandomly against native dispatch ownership."""

from __future__ import annotations

import pathlib
import json
import re


ROOT = pathlib.Path(__file__).resolve().parents[2]
JAVA_RANDOM_TICK_IDS = {
    2, 6, 8, 10, 11, 18, 28, 31, 32, 37, 38, 39, 40, 50, 51, 59,
    60, 70, 72, 74, 75, 76, 77, 78, 79, 80, 81, 83, 86, 90, 91, 92,
    104, 105, 106, 110, 111, 115, 127, 131, 132, 141, 142, 143, 147,
    148, 161, 171, 175, 200, 207, 212, 213,
}


def main() -> int:
    manifest = json.loads(
        (ROOT / "verify/completeness/surface_registry_manifest.json")
        .read_text())
    captured_ids = {
        row["id"] for row in manifest["blocks"]
        if row.get("tick_randomly")
    }
    if captured_ids != JAVA_RANDOM_TICK_IDS:
        raise RuntimeError(
            "checked real-Java random-tick census changed: missing="
            f"{sorted(JAVA_RANDOM_TICK_IDS - captured_ids)} extra="
            f"{sorted(captured_ids - JAVA_RANDOM_TICK_IDS)}")
    table = (ROOT / "blaze/core/block_props_table.h").read_text()
    marker = "Block.getTickRandomly(), exhaustively captured"
    if table.count(marker) != 1:
        raise RuntimeError("native random-tick registry marker changed")
    body = table.split(marker, 1)[1].split("return d;", 1)[0]
    native_ids = {int(value) for value in re.findall(r"case (\d+):", body)}
    if native_ids != JAVA_RANDOM_TICK_IDS:
        raise RuntimeError(
            "random-tick registry mismatch: missing="
            f"{sorted(JAVA_RANDOM_TICK_IDS - native_ids)} extra="
            f"{sorted(native_ids - JAVA_RANDOM_TICK_IDS)}")
    recorder = (ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java").read_text()
    if 'row.addProperty("tick_randomly", block.getTickRandomly())' not in recorder:
        raise RuntimeError("real-Java registry exporter lost tick_randomly")
    runtime = (ROOT / "magma/game/runtime.c").read_text()
    for token in ("runtime_tick_loaded_random_blocks",
                  "BF_TICK_RANDOM", "gm_runtime_random_tick_block"):
        if token not in runtime:
            raise RuntimeError(f"native random dispatcher lost {token}")
    callback = runtime.split(
        "int gm_runtime_random_tick_block(", 1)[1].split(
            "int gm_runtime_random_tick_selection(", 1)[0]
    dispatched_ids = {
        int(value) for value in re.findall(
            r"expected_block\s*==\s*(\d+)", callback)
    }
    if dispatched_ids != JAVA_RANDOM_TICK_IDS:
        raise RuntimeError(
            "native callback classification mismatch: missing="
            f"{sorted(JAVA_RANDOM_TICK_IDS - dispatched_ids)} extra="
            f"{sorted(dispatched_ids - JAVA_RANDOM_TICK_IDS)}")
    no_op_body = callback.split(
        "deliberately override Block.randomTick", 1)[1].split(
            "if (expected_block == 8)", 1)[0]
    no_op_ids = {
        int(value) for value in re.findall(
            r"expected_block\s*==\s*(\d+)", no_op_body)
    }
    expected_no_op = {
        28, 50, 70, 72, 75, 76, 77, 86, 91, 92,
        131, 132, 143, 147, 148, 171,
    }
    if no_op_ids != expected_no_op:
        raise RuntimeError(
            "native Java-randomTick no-op classification changed")
    receipt = json.loads((
        ROOT / "verify/completeness/random_tick_java_receipt.json"
    ).read_text())
    capture = (ROOT /
        "verify/completeness/capture_random_tick_java.py").read_text()
    for token in ("random_tick_locked", "controlled_input",
                  "getblocks_locked", "NO_OP_CASES"):
        if token not in capture:
            raise RuntimeError(f"real-Java receipt generator lost {token}")
    if receipt.get("schema") != "netherite.random_tick_java_receipt" \
            or receipt.get("source") \
                != "real Minecraft Java 1.11.2 WorldServer Block.randomTick" \
            or {row["block"] for row in receipt.get("no_op_cases", [])} \
                != expected_no_op:
        raise RuntimeError("real-Java random-tick receipt is stale")
    for row in receipt["no_op_cases"]:
        if row["before_world_rng48"] != row["after_world_rng48"]:
            raise RuntimeError(
                f"Java no-op block {row['block']} unexpectedly consumed RNG")
    active = {row["case"]: row for row in receipt.get("active_cases", [])}
    if set(active) != {
            "lit_redstone_ore", "magma_static_water",
            "supported_flower", "unsupported_flower",
            "dynamic_water_flat", "dynamic_lava_flat",
            "water_replaces_flower", "water_replaces_snow",
            "lava_replaces_flower", "lava_down_into_water"} \
            or active["lit_redstone_ore"]["raw_y_y1"][:2] != [144, 4] \
            or active["magma_static_water"]["raw_y_y1"] != [80, 13, 0, 0] \
            or active["supported_flower"]["raw_y_y1"][:2] != [80, 2] \
            or active["unsupported_flower"]["raw_y_y1"][:2] != [0, 0] \
            or active["magma_static_water"]["after_world_rng48"] \
                != 15386904305625 \
            or active["unsupported_flower"]["after_world_rng48"] \
                != 13493716152507 \
            or active["dynamic_water_flat"]["raw_plane_3x3"] \
                != [0, 129, 0, 129, 128, 129, 0, 129, 0] \
            or active["dynamic_lava_flat"]["raw_plane_3x3"] \
                != [0, 162, 0, 162, 160, 162, 0, 162, 0] \
            or active["water_replaces_flower"]["raw_source_target"] \
                != [128, 129] \
            or active["water_replaces_flower"]["after_world_rng48"] \
                != 13493716152507 \
            or active["water_replaces_snow"]["raw_source_target"] \
                != [128, 129] \
            or active["water_replaces_snow"]["after_world_rng48"] \
                != 25214903879 \
            or active["lava_replaces_flower"]["raw_source_target"] \
                != [160, 162] \
            or active["lava_replaces_flower"]["after_world_rng48"] \
                != 15386904305625 \
            or active["lava_down_into_water"]["raw_below_source"] \
                != [16, 160] \
            or active["lava_down_into_water"]["after_world_rng48"] \
                != 15386904305625:
        raise RuntimeError("real-Java active callback receipt changed")
    test = (ROOT / "magma/game/test_randtick.c").read_text()
    for token in (
            "registry no-op random callback consumes no RNG",
            "unsupported flower drops and becomes air",
            "lit redstone ore decays to default unlit state",
            "magma emits the exact server particle batch descriptor",
            "water replacement matches flower drop RNG",
            "snow-layer water replacement consumes no drop RNG",
            "lava replacement matches mixing-effect RNG",
            "lava-water mixing matches real-Java effect RNG"):
        if token not in test:
            raise RuntimeError(f"native callback regression lost: {token}")
    print("PASS random-tick registry: all 53 initialized Java identities are "
          "exactly flagged for native WorldServer dispatch")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        print(f"FAIL random-tick registry: {exc}")
        raise SystemExit(1)
