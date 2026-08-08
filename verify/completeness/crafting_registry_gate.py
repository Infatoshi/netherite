#!/usr/bin/env python3
"""Compare every ordinary crafting recipe with the initialized Java client."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys
import time


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
START = ROOT / "java" / "start_oracle_instance.sh"
OUT = ROOT / ".tmp" / "crafting-registry"
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402
sys.path.insert(0, str(ROOT / "magma" / "trace"))
import nbt_codec  # noqa: E402


def special_record(line: str) -> tuple[list[str], dict]:
    fields = line.split()
    tag_index = next(
        index for index, value in enumerate(fields)
        if value.startswith("tag="))
    tag = nbt_codec.decode_hex(fields[tag_index].split("=", 1)[1])
    return fields[:tag_index] + fields[tag_index + 1:], tag


def main() -> int:
    environment = dict(os.environ)
    run_root = ROOT / ".tmp" / f"crafting-registry-java-{os.getpid()}"
    environment.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
        "ORACLE_POOL_WAIT": "1",
        "TMPDIR": str(ROOT / ".tmp"),
        "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
    })
    instance = 98
    port = int(environment.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["uv", "run", "--no-project", "python",
         str(ROOT / "blaze" / "tools" / "gen_crafting_registry.py")],
        cwd=ROOT, env=environment, check=True,
    )
    subprocess.run([
        environment.get("CC", "gcc"), "-O2", "-ffp-contract=off",
        "-Wall", "-Wextra", f"-I{ROOT / 'blaze' / 'core'}",
        f"-I{ROOT / 'magma' / 'game'}",
        str(ROOT / "magma" / "game" / "test_crafting_registry.c"),
        "-o", str(OUT / "candidate"),
    ], cwd=ROOT, env=environment, check=True)
    subprocess.run([
        environment.get("CC", "gcc"), "-O2", "-ffp-contract=off",
        "-Wall", "-Wextra", f"-I{ROOT / 'blaze' / 'core'}",
        f"-I{ROOT / 'magma'}",
        str(ROOT / "magma" / "game" / "test_crafting_special.c"),
        str(ROOT / "magma" / "game" / "nbt_blob.c"),
        "-o", str(OUT / "special-candidate"),
    ], cwd=ROOT, env=environment, check=True)
    started = False
    try:
        subprocess.run(
            ["bash", str(START), "start", str(instance), "0"],
            cwd=ROOT, env=environment, check=True)
        started = True
        deadline = time.monotonic() + 120.0
        while True:
            try:
                response = save_fork.request(port, "crafting_registry_cases")
                break
            except save_fork.SaveForkError as exc:
                if ("world not ready" not in str(exc)
                        or time.monotonic() >= deadline):
                    raise
                time.sleep(0.1)
        java_lines = response["lines"]
        special_lines = response["special"]
    finally:
        if started:
            subprocess.run(
                ["bash", str(START), "stop", str(instance)],
                cwd=ROOT, env=environment, check=True)
    candidate = subprocess.run(
        [str(OUT / "candidate")], cwd=ROOT, env=environment,
        check=True, text=True, stdout=subprocess.PIPE,
    ).stdout.splitlines()
    special_candidate = subprocess.run(
        [str(OUT / "special-candidate")], cwd=ROOT, env=environment,
        check=True, text=True, stdout=subprocess.PIPE,
    ).stdout.splitlines()
    (OUT / "java.txt").write_text("\n".join(java_lines) + "\n")
    (OUT / "java-special.txt").write_text(
        "\n".join(special_lines) + "\n")
    (OUT / "c-special.txt").write_text(
        "\n".join(special_candidate) + "\n")
    (OUT / "c.txt").write_text("\n".join(candidate) + "\n")
    if java_lines != candidate:
        for index, (java, native) in enumerate(
                zip(java_lines, candidate), 1):
            if java != native:
                print(f"FAIL crafting registry line {index}")
                print(f"java:   {java}")
                print(f"native: {native}")
                return 1
        print(
            f"FAIL crafting registry line count: "
            f"java={len(java_lines)} native={len(candidate)}")
        return 1
    expected_special_names = {
        "map_extend", "armor_dye", "armor_dye_uncolored", "firework",
        "firework_star_repeat_modifiers", "firework_fade",
        "firework_rocket_stars", "tipped_arrow",
        "map_clone", "book_clone", "repair", "shield_decor",
        "banner_duplicate",
        *{f"armor_dye_{value}" for value in range(16)},
        *{f"shulker_color_{value}" for value in range(16)},
        *{
            "banner_pattern_" + value for value in (
                "bl", "br", "tl", "tr", "bs", "ts", "ls", "rs",
                "cs", "ms", "drs", "dls", "ss", "cr", "sc", "bt",
                "tt", "bts", "tts", "ld", "rd", "lud", "rud", "mc",
                "mr", "vh", "hh", "vhr", "hhb", "bo", "cbo", "cre",
                "gra", "gru", "bri", "sku", "flo", "moj",
            )
        },
    }
    item_rows = json.loads((HERE / "surface_registry_manifest.json")
                           .read_text())["items"]
    expected_special_names.update(
        f"repair_{row['id']}" for row in item_rows
        if row.get("repairable") is True and row["max_damage"] > 0)
    expected_special_names = {
        name + suffix
        for name in expected_special_names
        for suffix in (
            "", "_edge_remove", "_edge_replace", "_edge_extra")
    }
    invalid_world_data_names = {
        "map_extend_scale4", "map_extend_mansion",
        "map_extend_monument", "map_extend_missing_data",
    }
    expected_special_names.update(invalid_world_data_names)
    actual_special_names = {line.split()[1] for line in special_lines}
    invalid_special_match = any(
        (("_edge_" in line.split()[1])
         or line.split()[1] in invalid_world_data_names)
        != (int(line.split()[2]) < 0)
        for line in special_lines)
    if actual_special_names != expected_special_names or invalid_special_match:
        print("FAIL crafting special corpus is incomplete or nonmatching")
        for line in special_lines:
            print(line)
        return 1
    if len(special_candidate) != len(expected_special_names):
        print("FAIL crafting special native line count: "
              f"{len(special_candidate)}")
        return 1
    for index, (java, native) in enumerate(
            zip(special_lines, special_candidate), 1):
        java_fields, java_tag = special_record(java)
        native_fields, native_tag = special_record(native)
        if java_fields != native_fields or java_tag != native_tag:
            print(f"FAIL crafting special line {index}")
            print(f"java:   {java}")
            print(f"native: {native}")
            return 1
    print(
        "PASS crafting registry: 389 ordinary recipes, 1288 "
        f"canonical/offset/mirror/negative/remainder rows, "
        f"{len(special_lines)} NBT-special "
        f"cases including all 38 banner patterns, all dye colors, all 52 "
        "repairable item classes, firework modifier/fade/payload "
        "boundaries, and three generated rejection edges per valid case")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
