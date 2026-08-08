#!/usr/bin/env python3
"""Compare live anvil repair/combination output to real MC 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


MAGMA = pathlib.Path(__file__).resolve().parents[1]


def stack(item=0, count=0, meta=0, repair=0, name="", enchants=()):
    value = {
        "item": item, "count": count, "meta": meta, "repair": repair,
        "name": name,
    }
    if enchants:
        value["enchants"] = [
            {"id": enchant, "level": level}
            for enchant, level in enchants
        ]
    return value


CASES = (
    ("diamond_one_material", stack(276, 1, 1000), stack(264, 1), "", False),
    ("diamond_three_material", stack(276, 1, 1000), stack(264, 3), "", False),
    ("undamaged_material", stack(276, 1), stack(264, 1), "", False),
    ("iron_chest_material", stack(307, 1, 200), stack(265, 2), "", False),
    ("chain_chest_material", stack(303, 1, 200), stack(265, 1), "", False),
    ("elytra_leather", stack(443, 1, 300), stack(334, 2), "", False),
    ("shield_planks", stack(442, 1, 200), stack(5, 2), "", False),
    ("shears_not_material", stack(359, 1, 200), stack(265, 1), "", False),
    ("diamond_same_item", stack(276, 1, 1000), stack(276, 1, 1000), "", False),
    ("diamond_same_item_full", stack(276, 1, 500), stack(276, 1, 1000), "", False),
    ("bow_same_item", stack(261, 1, 300), stack(261, 1, 300), "", False),
    ("sharpness_upgrade", stack(276, 1, 0, enchants=((16, 3),)),
     stack(403, 1, enchants=((16, 3),)), "", False),
    ("sharpness_cap", stack(276, 1, 0, enchants=((16, 5),)),
     stack(403, 1, enchants=((16, 5),)), "", False),
    ("looting_book", stack(276, 1),
     stack(403, 1, enchants=((21, 3),)), "", False),
    ("sharpness_smite_conflict", stack(276, 1, 0, enchants=((16, 3),)),
     stack(403, 1, enchants=((17, 4),)), "", False),
    ("mixed_reject_apply", stack(276, 1, 0, enchants=((16, 3),)),
     stack(403, 1, enchants=((17, 4), (34, 3))), "", False),
    ("silk_fortune_conflict", stack(278, 1, 0, enchants=((33, 1),)),
     stack(403, 1, enchants=((35, 3),)), "", False),
    ("depth_frost_conflict", stack(313, 1, 0, enchants=((8, 3),)),
     stack(403, 1, enchants=((9, 2),)), "", False),
    ("infinity_mending_conflict", stack(261, 1, 0, enchants=((51, 1),)),
     stack(403, 1, enchants=((70, 1),)), "", False),
    ("book_treasure_merge", stack(403, 1, 0, enchants=((70, 1),)),
     stack(403, 1, enchants=((71, 1),)), "", False),
    ("invalid_protection_sword", stack(276, 1),
     stack(403, 1, enchants=((0, 4),)), "", False),
    ("same_item_enchant", stack(276, 1, 0, enchants=((16, 2),)),
     stack(276, 1, 0, enchants=((16, 2),)), "", False),
    ("prior_work", stack(276, 1, 1000, repair=3),
     stack(264, 1, repair=7), "", False),
    ("rename_stone", stack(1, 64), stack(), "New", False),
    ("rename_name_tags", stack(421, 16), stack(), "New", False),
    ("rename_same", stack(1, 1, name="Old"), stack(), "Old", False),
    ("rename_clear", stack(1, 1, name="Old"), stack(), "", False),
    ("rename_prior_expensive", stack(1, 1, repair=50), stack(), "New", False),
    ("survival_too_expensive", stack(276, 1, 1000, repair=39),
     stack(264, 1), "", False),
    ("creative_expensive", stack(276, 1, 1000, repair=39),
     stack(264, 1), "", True),
)


def generated_cases():
    """Finite semantic partition of ContainerRepair.updateRepairOutput.

    The product below deliberately crosses boundaries that change a branch in
    the 1.11.2 implementation.  It is not a random sample and its row count is
    pinned by the caller so accidentally shrinking the corpus fails closed.
    """
    rows = []

    # Material repair: no damage, each side of the quarter-durability step,
    # insufficient/exact/excess material, prior-work penalties, and the 40
    # level survival boundary.  Representatives cover every repair material
    # dispatch family, including shield and Elytra special cases.
    material_items = (
        (268, 59, 5), (272, 131, 4), (267, 250, 265),
        (276, 1561, 264), (283, 32, 266), (298, 55, 334),
        (303, 240, 265), (307, 528, 265), (311, 528, 264),
        (315, 112, 266), (442, 336, 5), (443, 432, 334),
    )
    for item, maximum, material in material_items:
        quarter = maximum // 4
        for damage in (0, 1, quarter - 1, quarter, quarter + 1,
                       maximum - 1, maximum):
            for count in (1, 2, 4, 5):
                for prior in (0, 1, 7, 39):
                    rows.append((
                        f"mat_{item}_{damage}_{count}_{prior}",
                        stack(item, 1, damage, repair=prior),
                        stack(material, count), "", False))

    # Same-item durability combine, including the bonus crossing zero damage.
    for item, maximum in ((268, 59), (267, 250), (276, 1561),
                          (261, 384), (442, 336), (443, 432)):
        for left_damage in (0, 1, maximum // 2, maximum - 1, maximum):
            for right_damage in (0, 1, maximum // 2,
                                 maximum - 1, maximum):
                for prior in (0, 3, 19, 39):
                    rows.append((
                        f"same_{item}_{left_damage}_{right_damage}_{prior}",
                        stack(item, 1, left_damage, repair=prior),
                        stack(item, 1, right_damage, repair=prior),
                        "", False))

    # Rename-only and rename-plus-work partitions.  Counts include the name
    # tag exception and an ordinary over-one stack, while prior work brackets
    # the too-expensive clamp.
    for item, count in ((1, 1), (1, 64), (421, 1), (421, 16), (276, 1)):
        for prior in (0, 1, 19, 38, 39, 40, 50):
            for old_name, desired in (("", ""), ("", "New"),
                                      ("Old", "Old"), ("Old", ""),
                                      ("Old", "New")):
                for creative in (False, True):
                    rows.append((
                        f"rename_{item}_{count}_{prior}_{old_name}_{desired}_{creative}",
                        stack(item, count, repair=prior, name=old_name),
                        stack(), desired, creative))

    # Enchantment level/applicability/compatibility and book-cost branches.
    # IDs span all weights, caps, treasure restrictions, item types, and the
    # three explicit incompatibility families in 1.11.2.
    enchants = (
        (0, 4), (1, 4), (2, 4), (3, 4), (4, 4), (5, 3),
        (6, 1), (7, 3), (8, 3), (9, 2), (10, 1),
        (16, 5), (17, 5), (18, 5), (19, 2), (20, 2),
        (21, 3), (22, 3), (32, 5), (33, 1), (34, 3), (35, 3),
        (48, 5), (49, 2), (50, 1), (51, 1), (61, 3),
        (62, 3), (70, 1), (71, 1),
    )
    targets = (276, 278, 313, 261, 346, 403)
    for target in targets:
        for enchant, maximum in enchants:
            for old_level, incoming in ((0, 1), (0, maximum),
                                        (1, 1), (1, maximum),
                                        (maximum, maximum),
                                        (maximum + 1, maximum + 1)):
                left_enchants = () if old_level == 0 else ((enchant, old_level),)
                for right_item in (403, target):
                    rows.append((
                        f"ench_{target}_{enchant}_{old_level}_{incoming}_{right_item}",
                        stack(target, 1, enchants=left_enchants),
                        stack(right_item, 1,
                              enchants=((enchant, incoming),)),
                        "", False))

    conflicts = ((16, 17), (16, 18), (17, 18), (0, 1), (0, 3),
                 (1, 3), (33, 35), (8, 9), (51, 70))
    for target in targets:
        for left_enchant, right_enchant in conflicts:
            for right_item in (403, target):
                for creative in (False, True):
                    rows.append((
                        f"conflict_{target}_{left_enchant}_{right_enchant}_{right_item}_{creative}",
                        stack(target, 1, enchants=((left_enchant, 1),)),
                        stack(right_item, 1,
                              enchants=((right_enchant, 1),)),
                        "", creative))
    return rows


def name_ids(left, right, desired):
    values = {}
    for value in (left.get("name", ""), right.get("name", ""), desired):
        if value and value not in values:
            values[value] = len(values) + 1
    return values


def native_stack_args(value, names):
    enchants = value.get("enchants", ())
    args = [
        value.get("item", 0), value.get("count", 0), value.get("meta", 0),
        value.get("repair", 0), names.get(value.get("name", ""), 0),
        len(enchants),
    ]
    for enchant in enchants:
        args.extend((enchant["id"], enchant["level"]))
    return [str(value) for value in args]


def native(left, right, desired, creative):
    names = name_ids(left, right, desired)
    args = [
        str(MAGMA / "game" / "test_anvil_oracle"),
        str(int(creative)), str(names.get(desired, 0)),
        *native_stack_args(left, names), *native_stack_args(right, names),
    ]
    row = json.loads(subprocess.check_output(args, text=True))
    return row, names


def normalize_java(row, names):
    row = dict(row)
    for key in ("left", "right", "output"):
        value = dict(row[key])
        value["name"] = names.get(value.get("name", ""), 0)
        row[key] = value
    return row


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    corpus = list(CASES) + generated_cases()
    if len(corpus) != 4700:
        raise SystemExit(f"anvil corpus changed: expected 4,700, got {len(corpus)}")
    selected = [case for case in corpus if not args.case or case[0] == args.case]
    if not selected:
        raise SystemExit("unknown case")
    locked = False
    try:
        deadline = time.monotonic() + 120.0
        while True:
            try:
                request(args.port, "obs")
                break
            except (OSError, RuntimeError, ValueError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.5)
        request(args.port, "server_step_lock")
        locked = True
        for name, left, right, desired, creative in selected:
            c_row, names = native(left, right, desired, creative)
            action = {
                "left": left, "right": right, "name": desired,
                "creative": creative, "level": 100,
            }
            java = normalize_java(
                request(args.port, "anvil_locked", action), names)
            if java != c_row:
                raise AssertionError(
                    f"{name}\njava={json.dumps(java, sort_keys=True)}"
                    f"\nnative={json.dumps(c_row, sort_keys=True)}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    print(f"PASS real Java/shared CPU: {len(selected)} exact anvil computations")


if __name__ == "__main__":
    main()
