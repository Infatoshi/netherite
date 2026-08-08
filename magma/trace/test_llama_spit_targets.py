#!/usr/bin/env python3
"""Pin real-1.11.2 EntityLlamaSpit target and damage dispatch."""

import argparse
import struct
import sys

from test_dragon_crystal_notification import request


def by_eid(state, eid):
    return next((value for value in state["entities"]
                 if value.get("eid") == eid), None)


def fbits(value):
    return struct.pack(">f", float(value))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    locked = False
    request(args.port, "obs")
    request(args.port, "runcmds", {"cmds": [
        "gamerule doMobSpawning false",
        "replaceitem entity @p slot.weapon.mainhand minecraft:fishing_rod",
    ]})
    request(args.port, "killentities")
    try:
        parked = request(args.port, "server_step_lock")
        locked = True
        player = parked["authoritative"]
        px = float(player["x"])
        py = float(player["y"])
        pz = float(player["z"])

        def clear():
            response = request(args.port, "clear_entities_locked")
            if not response.get("ok"):
                raise AssertionError(response)

        def owner(z_offset=0.0):
            return request(args.port, "summon_locked", {
                "type": "horse", "horse_kind": "llama", "no_ai": True,
                "x": px + 30.0, "y": py + 8.0, "z": pz + z_offset,
                "yaw": 90.0, "variant": 0, "llama_strength": 3,
            })

        def spit(owner_eid, z_offset=0.0):
            return request(args.port, "summon_locked", {
                "type": "llama_spit", "owner_eid": owner_eid,
                "x": px + 16.0, "y": py + 8.5, "z": pz + z_offset,
                "mx": 3.0, "my": 0.0, "mz": 0.0,
                "no_gravity": True,
            })

        passive_cases = [
            ("arrow", {}),
            ("armor_stand", {"health": 20.0, "no_gravity": True}),
            ("minecart", {}),
            ("area_effect_cloud", {
                "radius": 0.5, "radius_on_use": 0.0,
                "radius_per_tick": 0.0, "duration": 600,
                "wait_time": 10, "age": 20,
            }),
            ("falling_block", {"block": 12, "fall_time": 5}),
            ("primed_tnt", {"fuse": 100}),
            ("fish_hook", {}),
            ("small_fireball", {
                "mx": 0.0, "my": 0.0, "mz": 0.0,
                "ax": 0.0, "ay": 0.0, "az": 0.0,
            }),
            ("wither_skull", {
                "shooter_eid": 0, "mx": 0.0, "my": 0.0, "mz": 0.0,
                "ax": 0.0, "ay": 0.0, "az": 0.0,
                "no_gravity": True,
            }),
        ]
        for kind, extra in passive_cases:
            clear()
            llama = owner()
            target_action = {
                "type": kind,
                "x": px + 18.0, "y": py + 8.0, "z": pz,
            }
            target_action.update(extra)
            target = request(args.port, "summon_locked", target_action)
            projectile = spit(llama["eid"])
            state = request(args.port, "step")["authoritative"]
            observed = by_eid(state, target["eid"])
            if observed is None or by_eid(state, projectile["eid"]) is not None:
                raise AssertionError(
                    f"{kind}: target/spit survival mismatch: {state!r}")
            if kind == "armor_stand" and observed["health"] != 20.0:
                raise AssertionError("indirect llama damage changed Armor Stand")
            if kind == "minecart" and observed.get("damage") != 10.0:
                raise AssertionError(f"minecart hurt state mismatch: {observed}")
            if kind == "primed_tnt" and observed.get("fuse") != 99:
                raise AssertionError(f"TNT fuse ordering mismatch: {observed}")
            if kind == "small_fireball" and any(
                    observed[field] != 0.0 for field in
                    ("vx", "vy", "vz", "ax", "ay", "az")):
                raise AssertionError(
                    f"SmallFireball damage override changed motion: {observed}")

        clear()
        llama = owner()
        wither = request(args.port, "summon_locked", {
            "type": "wither", "x": px + 18.0, "y": py + 8.0, "z": pz,
            "health": 300.0, "no_ai": True, "no_gravity": True,
            "entity_seed48": 0x123456789ABC,
        })
        projectile = spit(llama["eid"])
        state = request(args.port, "step")["authoritative"]
        observed = by_eid(state, wither["eid"])
        if observed is None or by_eid(state, projectile["eid"]) is not None:
            raise AssertionError("Wither/spit survival mismatch")
        if fbits(observed["health"]) != fbits(299.14) \
                or observed["revenge_eid"] != llama["eid"] \
                or observed["revenge_is_player"] \
                or observed["recently_hit"] != 0 \
                or observed["attacking_player"] \
                or observed["vx"] != -0.4000000059604645:
            raise AssertionError(f"Wither llama-source damage mismatch: {observed}")

        clear()
        llama = owner()
        crystal = request(args.port, "summon_locked", {
            "type": "end_crystal", "x": px + 18.0,
            "y": py + 8.0, "z": pz,
            "inner_rotation": 0, "show_bottom": 1,
        })
        projectile = spit(llama["eid"])
        state = request(args.port, "step")["authoritative"]
        if by_eid(state, crystal["eid"]) is not None \
                or by_eid(state, projectile["eid"]) is not None:
            raise AssertionError("llama spit did not destroy End crystal")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
        request(args.port, "runcmds", {"cmds": [
            "gamerule doMobSpawning true",
            "replaceitem entity @p slot.weapon.mainhand minecraft:air",
        ]})
    print("PASS real Java llama spit targets: nine nonliving intercepts, "
          "Armor Stand rejection, minecart hurt, TNT order, SmallFireball "
          "override, Wither owner attribution, and End-crystal destruction")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, KeyError, OSError, RuntimeError) as error:
        print(f"FAIL real Java llama spit targets: {error}")
        raise SystemExit(1)
