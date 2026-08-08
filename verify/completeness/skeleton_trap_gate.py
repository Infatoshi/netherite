#!/usr/bin/env python3
"""Compare the native 120-tick skeleton-trap trace to checked Java 1.11.2."""

from __future__ import annotations

import argparse
import csv
import json
import os
import pathlib
import struct
import subprocess
import sys


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GOLDEN = HERE / "skeleton_trap_oracle_120.json"
NATIVE = ROOT / "magma" / "game" / "test_horse_runtime"


class TrapGateError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise TrapGateError(message)


def double_bits(value: str) -> float:
    return struct.unpack(">d", bytes.fromhex(value))[0]


def float_bits(value: str) -> float:
    return struct.unpack(">f", bytes.fromhex(value))[0]


def reduce_oracle(source: pathlib.Path, output: pathlib.Path,
                  x_offset: float, z_offset: float) -> None:
    full = json.loads(source.read_text(encoding="utf-8"))
    require(full.get("schema") == "netherite.skeleton_trap_oracle",
            "input is not a skeleton-trap oracle")
    ticks = []
    for row in full.get("trace", []):
        observed = row["observed"]
        entities = []
        for entity in observed.get("entities", []):
            if entity.get("class") not in {
                    "EntitySkeletonHorse", "EntitySkeleton"}:
                continue
            entities.append({
                "eid": entity["eid"],
                "class": entity["class"],
                "position_bits": entity["position_bits"],
                "motion_bits": entity["motion_bits"],
                "yaw_bits": entity["yaw_bits"],
                "entity_seed48": entity["entity_seed48"],
            })
        projectiles = []
        for projectile in observed.get("projectiles", []):
            projectiles.append({
                "eid": projectile["eid"],
                "shooter_eid": projectile["shooter_eid"],
                "position_bits": projectile["position_bits"],
                "motion_bits": projectile["motion_bits"],
                "ticks_in_air": projectile["ticks_in_air"],
                "entity_seed48": projectile["entity_seed48"],
            })
        ticks.append({
            "native_tick": row["completed_tick"] - 2,
            "entities": entities,
            "projectiles": projectiles,
        })
    require(len(ticks) == 120, "oracle must contain exactly 120 native ticks")
    require(all(len(row["entities"]) == 8 for row in ticks),
            "oracle must contain all eight trap living entities on every tick")
    output.write_text(json.dumps({
        "schema": "netherite.skeleton_trap_gate",
        "version": 1,
        "source_version": "Minecraft Java 1.11.2",
        "todo": "ENT-02",
        "java_minus_native": {"x": x_offset, "z": z_offset},
        "ticks": ticks,
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run_native() -> tuple[dict[tuple[int, int], list[str]],
                          dict[tuple[int, int], list[str]]]:
    require(NATIVE.is_file(), f"native test is not built: {NATIVE}")
    result = subprocess.run(
        [str(NATIVE)], cwd=ROOT,
        env={**os.environ,
             "GM_TRACE_TRAP": "1", "GM_TRACE_TRAP_TICKS": "120"},
        text=True, capture_output=True, check=False)
    require(result.returncode == 0,
            f"native horse runtime failed rc={result.returncode}:\n"
            f"{result.stdout}\n{result.stderr[-4000:]}")
    living: dict[tuple[int, int], list[str]] = {}
    arrows: dict[tuple[int, int], list[str]] = {}
    for row in csv.reader(result.stderr.splitlines()):
        if not row:
            continue
        if row[0] == "TRAP":
            key = (int(row[1]), int(row[2]))
            require(key not in living, f"duplicate native living row {key}")
            living[key] = row
        elif row[0] == "ARROW":
            key = (int(row[1]), int(row[2]))
            require(key not in arrows, f"duplicate native arrow row {key}")
            arrows[key] = row
    require(living and arrows, "native trace produced no living or arrow rows")
    return living, arrows


def compare() -> tuple[float, float, float, float, int, int]:
    golden = json.loads(GOLDEN.read_text(encoding="utf-8"))
    require(golden.get("schema") == "netherite.skeleton_trap_gate"
            and golden.get("version") == 1
            and golden.get("todo") == "ENT-02",
            "invalid skeleton-trap golden identity")
    offset = golden["java_minus_native"]
    living, arrows = run_native()
    expected_living: set[tuple[int, int]] = set()
    expected_arrows: set[tuple[int, int]] = set()
    max_position = max_motion = max_yaw = max_arrow = 0.0
    for tick in golden.get("ticks", []):
        native_tick = int(tick["native_tick"])
        for entity in tick["entities"]:
            key = (native_tick, int(entity["eid"]))
            expected_living.add(key)
            require(key in living, f"missing native living row {key}")
            row = living[key]
            expected_position = [
                double_bits(entity["position_bits"][0]) - offset["x"],
                double_bits(entity["position_bits"][1]),
                double_bits(entity["position_bits"][2]) - offset["z"],
            ]
            expected_motion = [double_bits(value)
                               for value in entity["motion_bits"]]
            actual_position = [float(row[index]) for index in (3, 4, 5)]
            actual_motion = [float(row[index]) for index in (6, 7, 8)]
            position_error = max(abs(a - b) for a, b in
                                 zip(actual_position, expected_position))
            motion_error = max(abs(a - b) for a, b in
                               zip(actual_motion, expected_motion))
            yaw_error = abs(float(row[16]) - float_bits(entity["yaw_bits"]))
            max_position = max(max_position, position_error)
            max_motion = max(max_motion, motion_error)
            max_yaw = max(max_yaw, yaw_error)
            require(position_error <= 1.0e-12,
                    f"living position mismatch {key}: {position_error:.9g}")
            # Java performed this fixture near x=990,z=-100 while native
            # replays an exactly translated arena near the origin. Two
            # MathHelper sine-table boundaries expose cancellation at under
            # 1e-6 block/tick; a same-absolute-coordinate run has no delta.
            require(motion_error <= 1.0e-6,
                    f"living motion mismatch {key}: {motion_error:.9g}")
            require(yaw_error <= 4.0e-4,
                    f"living yaw mismatch {key}: {yaw_error:.9g}")
            require(int(row[9]) == entity["entity_seed48"],
                    f"living RNG mismatch {key}")
        for projectile in tick["projectiles"]:
            key = (native_tick, int(projectile["eid"]))
            expected_arrows.add(key)
            require(key in arrows, f"missing native arrow row {key}")
            row = arrows[key]
            expected_position = [
                double_bits(projectile["position_bits"][0]) - offset["x"],
                double_bits(projectile["position_bits"][1]),
                double_bits(projectile["position_bits"][2]) - offset["z"],
            ]
            expected_motion = [double_bits(value)
                               for value in projectile["motion_bits"]]
            actual_position = [float(row[index]) for index in (4, 5, 6)]
            actual_motion = [float(row[index]) for index in (7, 8, 9)]
            arrow_error = max(
                *(abs(a - b) for a, b in
                  zip(actual_position, expected_position)),
                *(abs(a - b) for a, b in
                  zip(actual_motion, expected_motion)))
            max_arrow = max(max_arrow, arrow_error)
            require(arrow_error <= 1.0e-12,
                    f"arrow state mismatch {key}: {arrow_error:.9g}")
            require(int(row[3]) == projectile["shooter_eid"],
                    f"arrow shooter mismatch {key}")
            require(int(row[10]) == projectile["ticks_in_air"],
                    f"arrow age mismatch {key}")
            require(int(row[11]) == projectile["entity_seed48"],
                    f"arrow RNG mismatch {key}")
    require(set(living) == expected_living,
            f"native living row set changed: extra={set(living)-expected_living} "
            f"missing={expected_living-set(living)}")
    require(set(arrows) == expected_arrows,
            f"native arrow row set changed: extra={set(arrows)-expected_arrows} "
            f"missing={expected_arrows-set(arrows)}")
    return (max_position, max_motion, max_yaw, max_arrow,
            len(expected_living), len(expected_arrows))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command")
    reduce_parser = subparsers.add_parser(
        "reduce", help="reduce a full probe artifact to the checked golden")
    reduce_parser.add_argument("source", type=pathlib.Path)
    reduce_parser.add_argument("output", type=pathlib.Path)
    reduce_parser.add_argument("--x-offset", type=float, required=True)
    reduce_parser.add_argument("--z-offset", type=float, required=True)
    args = parser.parse_args()
    if args.command == "reduce":
        reduce_oracle(args.source, args.output,
                      args.x_offset, args.z_offset)
        print(f"PASS reduced skeleton-trap oracle -> {args.output}")
        return 0
    result = compare()
    print("PASS skeleton trap: 120 translated Java ticks, "
          f"{result[4]} living rows and {result[5]} projectile rows; "
          f"max position={result[0]:.3g}, motion={result[1]:.3g}, "
          f"yaw={result[2]:.3g}, arrow={result[3]:.3g}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, ValueError, TrapGateError) as error:
        print(f"FAIL skeleton trap: {error}", file=sys.stderr)
        raise SystemExit(1)
