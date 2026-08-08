#!/usr/bin/env python3
"""Bit-compare represented EntityPlayer.interactOn animal paths with magma."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
EID = 672000
FOODS = {
    "sheep": (296,),
    "cow": (296,),
    "pig": (391, 392, 434),
    "chicken": (295, 361, 362, 435),
}
CASES = [
    ("adult_survival", dict()),
    ("adult_creative", dict(creative=True)),
    ("already_love", dict(in_love=100)),
    ("cooldown", dict(growing_age=6000)),
    ("nonwheat", dict(main_item=280, main_count=3)),
    ("child_full", dict(growing_age=-24000, main_count=3)),
    ("child_near", dict(growing_age=-19, main_count=3)),
    ("child_int_min", dict(growing_age=-2147483648, main_count=3)),
    ("child_large_negative", dict(growing_age=-2147483600, main_count=3)),
    ("child_creative", dict(growing_age=-24000, main_count=3, creative=True)),
    ("offhand", dict(
        hands="offhand", main_item=280, main_count=1,
        off_item=296, off_count=2)),
    ("client_order", dict(
        hands="client_order", main_item=280, main_count=1,
        off_item=296, off_count=2)),
    ("main_precedes_off", dict(
        hands="client_order", main_item=296, main_count=2,
        off_item=296, off_count=2)),
]
for species in ("cow", "pig", "chicken"):
    food = FOODS[species][0]
    for item in FOODS[species]:
        CASES.append((f"{species}_food_{item}",
                      dict(species=species, main_item=item)))
    rejected = 391 if species == "cow" else 296
    CASES.extend((
        (f"{species}_creative", dict(species=species, creative=True)),
        (f"{species}_already_love", dict(species=species, in_love=100)),
        (f"{species}_cooldown", dict(species=species, growing_age=6000)),
        (f"{species}_rejected", dict(
            species=species, main_item=rejected, main_count=3)),
        (f"{species}_child_full", dict(
            species=species, growing_age=-24000, main_count=3)),
        (f"{species}_child_near", dict(
            species=species, growing_age=-19, main_count=3)),
        (f"{species}_child_creative", dict(
            species=species, growing_age=-24000,
            main_count=3, creative=True)),
        (f"{species}_client_order", dict(
            species=species, hands="client_order",
            main_item=280, main_count=1, off_item=food, off_count=2)),
        (f"{species}_main_precedes_off", dict(
            species=species, hands="client_order",
            main_item=food, main_count=2, off_item=food, off_count=2)),
    ))
CASES.extend((
    ("sheep_rejects_carrot", dict(main_item=391, main_count=3)),
    ("sheep_rejects_seed", dict(main_item=295, main_count=3)),
    ("cow_rejects_seed", dict(
        species="cow", main_item=295, main_count=3)),
    ("pig_rejects_seed", dict(
        species="pig", main_item=295, main_count=3)),
    ("chicken_rejects_carrot", dict(
        species="chicken", main_item=391, main_count=3)),
    ("cow_milk_main", dict(
        species="cow", main_item=325, main_count=1)),
    ("cow_milk_offhand", dict(
        species="cow", hands="client_order",
        main_item=280, main_count=1, off_item=325, off_count=1)),
    ("cow_milk_preempts_feed", dict(
        species="cow", hands="client_order",
        main_item=325, main_count=1, off_item=296, off_count=2)),
    ("cow_child_bucket_falls_through", dict(
        species="cow", hands="client_order", growing_age=-24000,
        main_item=325, main_count=1, off_item=296, off_count=2)),
    ("cow_creative_bucket_falls_through", dict(
        species="cow", hands="client_order", creative=True,
        main_item=325, main_count=1, off_item=296, off_count=2)),
    ("cow_cooldown_milks", dict(
        species="cow", growing_age=6000,
        main_item=325, main_count=1)),
    ("pig_saddle_main", dict(
        species="pig", hands="client_order",
        main_item=329, main_count=2, off_item=391, off_count=2)),
    ("pig_saddle_creative", dict(
        species="pig", main_item=329, main_count=1, creative=True)),
    ("pig_saddle_offhand", dict(
        species="pig", hands="client_order",
        main_item=280, main_count=1, off_item=329, off_count=1)),
    ("pig_child_saddle_preempts_feed", dict(
        species="pig", hands="client_order", growing_age=-24000,
        main_item=329, main_count=1, off_item=391, off_count=2)),
    ("pig_name_tag_preempts_feed", dict(
        species="pig", hands="client_order",
        main_item=421, main_count=1, off_item=391, off_count=2)),
    ("pig_saddled_empty_mount", dict(
        species="pig", saddled=True, main_item=0, main_count=0)),
    ("pig_saddled_saddle_mount", dict(
        species="pig", saddled=True, main_item=329, main_count=1)),
    ("pig_saddled_carrot_feeds", dict(
        species="pig", saddled=True, main_item=391, main_count=2)),
    ("pig_saddled_love_carrot_mounts", dict(
        species="pig", saddled=True, in_love=100,
        main_item=391, main_count=2)),
    ("pig_saddled_cooldown_carrot_mounts", dict(
        species="pig", saddled=True, growing_age=6000,
        main_item=391, main_count=2)),
    ("pig_saddled_name_tag_no_mount", dict(
        species="pig", saddled=True, main_item=421, main_count=1)),
    ("pig_saddled_sneaking_mounts", dict(
        species="pig", saddled=True, sneaking=True,
        main_item=0, main_count=0)),
    ("pig_saddled_child_mounts", dict(
        species="pig", saddled=True, growing_age=-24000,
        main_item=0, main_count=0)),
))


def normalized(case):
    species = case.get("species", "sheep")
    out = {
        "species": species,
        "hands": "main",
        "growing_age": 0,
        "in_love": 0,
        "main_item": FOODS[species][0],
        "main_count": 1,
        "off_item": 0,
        "off_count": 0,
        "creative": False,
        "saddled": False,
        "sneaking": False,
        "next_entity_id": EID,
    }
    out.update(case)
    return out


def native(case):
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_sheep_feed_oracle"),
        case["species"], case["hands"],
        str(case["growing_age"]), str(case["in_love"]),
        str(case["main_item"]), str(case["main_count"]),
        str(case["off_item"]), str(case["off_count"]),
        str(int(case["creative"])), str(case["next_entity_id"]),
        str(int(case["saddled"])), str(int(case["sneaking"])),
    ], text=True)
    return json.loads(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    cases = [case for case in CASES if not args.case or case[0] == args.case]
    if not cases:
        parser.error(f"unknown case: {args.case}")

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
        time.sleep(2.0)
        subprocess.run(
            ["make", "-C", str(MAGMA), "game/test_sheep_feed_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for name, overrides in cases:
            case = normalized(overrides)
            java = request(args.port, "feed_animal_locked", case)
            magma = native(case)
            if java != magma:
                raise AssertionError(
                    f"{name}: java={java!r} magma={magma!r}")
        print(f"PASS java==magma: {len(cases)} animal interactions, "
              "feed/milk/saddle inventory, events, state, and hand order")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
