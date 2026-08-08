#!/usr/bin/env python3
"""Real-1.11.2 locked oracle for player critical-hit damage."""

import argparse
import pathlib
import struct
import sys


JAVA = pathlib.Path(__file__).resolve().parents[2] / "java"
sys.path.insert(0, str(JAVA))
from qrl_client import NetheriteEnv  # noqa: E402


CASES = (
    ("critical", {}, "41080000", 0),
    ("blindness", {"blindness": True}, "41100000", 0),
    ("sprinting", {"sprinting": True}, "41100000", 0),
    ("grounded", {"on_ground": True}, "41100000", 0),
    ("zero_fall", {"fall_distance": 0.0}, "41100000", 0),
    ("riding", {"riding": True}, "41100000", 0),
    ("water", {"in_water": True}, "41100000", 0),
    ("cooldown_09", {"cooldown_ticks": 4}, "41126e98", 0),
    ("sharpness_v", {"enchant_id": 16, "enchant_level": 5}, "40b00000", 0),
    ("smite_i", {
        "target": "zombie", "enchant_id": 17, "enchant_level": 1,
    }, "41808312", 0),
    ("diamond_sword_partial", {
        "held_item": 276, "cooldown_ticks": 5,
    }, "40f081c3", 1),
    ("diamond_sword_critical_zombie", {
        "held_item": 276, "cooldown_ticks": 12, "target": "zombie",
    }, "411ab022", 1),
    ("diamond_axe_partial_zombie", {
        "held_item": 279, "cooldown_ticks": 12, "target": "zombie",
    }, "4177617c", 2),
)

WEAPON_CASES = (
    (256, "410d3e77", 2), (257, "410d9fd3", 2),
    (258, "40f8495c", 2), (267, "40fbdcf0", 1),
    (268, "410949a6", 1), (269, "4115947b", 2),
    (270, "4116cfea", 2), (271, "4105436c", 2),
    (272, "41039c0f", 1), (273, "41116979", 2),
    (274, "411237de", 2), (275, "40fb3fa7", 2),
    (276, "40f081c3", 1), (277, "41091375", 2),
    (278, "410907c8", 2), (279, "40f4f9db", 2),
    (283, "410949a6", 1), (284, "4115947b", 2),
    (285, "4116cfea", 2), (286, "4102d2f2", 2),
    (290, "411bd4fe", 1), (291, "4118ed91", 1),
    (292, "41141687", 1), (293, "41080000", 1),
    (294, "411bd4fe", 1),
)

MOTION_CASES = (
    ("ordinary_air", {
        "on_ground": True, "fall_distance": 0.0,
    }, ("0000000000000000", "0000000000000000", "bfd99999a0000000"),
     ("0000000000000000", "0000000000000000", "0000000000000000"), False),
    ("ordinary_ground", {
        "on_ground": True, "fall_distance": 0.0, "target_on_ground": True,
    }, ("0000000000000000", "3fd99999a0000000", "bfd99999a0000000"),
     ("0000000000000000", "0000000000000000", "0000000000000000"), False),
    ("sprint_ground_motion", {
        "on_ground": True, "fall_distance": 0.0, "sprinting": True,
        "target_on_ground": True, "target_motion_x": 0.2,
        "target_motion_y": 0.3, "target_motion_z": -0.4,
        "player_motion_x": 1.0, "player_motion_z": -2.0,
    }, ("3fa9999999999991", "3fd99999a0000000", "bfe999999b333333"),
     ("3fe3333333333333", "0000000000000000", "bff3333333333333"), False),
    ("knockback_ii_ground_motion", {
        "on_ground": True, "fall_distance": 0.0, "target_on_ground": True,
        "enchant_id": 19, "enchant_level": 2, "target_motion_x": 0.2,
        "target_motion_y": 0.3, "target_motion_z": -0.4,
        "player_motion_x": 1.0, "player_motion_z": -2.0,
    }, ("3fa9999999999988", "3fd99999a0000000", "bff4cccccd99999a"),
     ("3fe3333333333333", "0000000000000000", "bff3333333333333"), False),
    ("sprint_partial", {
        "on_ground": True, "fall_distance": 0.0, "sprinting": True,
        "cooldown_ticks": 4, "target_on_ground": True,
    }, ("0000000000000000", "3fd99999a0000000", "bfd99999a0000000"),
     ("0000000000000000", "0000000000000000", "0000000000000000"), True),
    ("knockback_i_partial", {
        "on_ground": True, "fall_distance": 0.0, "cooldown_ticks": 4,
        "target_on_ground": True, "enchant_id": 19, "enchant_level": 1,
    }, ("bc91a62640000000", "3fd99999a0000000", "bfe6666668000000"),
     ("0000000000000000", "0000000000000000", "0000000000000000"), False),
)

FIRE_CASES = (
    ("fire_i", {
        "on_ground": True, "fall_distance": 0.0,
        "enchant_id": 20, "enchant_level": 1,
    }, "41100000", 80),
    ("fire_ii", {
        "on_ground": True, "fall_distance": 0.0,
        "enchant_id": 20, "enchant_level": 2,
    }, "41100000", 160),
    ("fire_i_existing", {
        "on_ground": True, "fall_distance": 0.0,
        "enchant_id": 20, "enchant_level": 1,
        "target_fire_ticks": 120,
    }, "41100000", 120),
    ("fire_i_rejected_new", {
        "on_ground": True, "fall_distance": 0.0,
        "enchant_id": 20, "enchant_level": 1,
        "target_hurt_resistant": 20, "target_last_damage": 2.0,
    }, "41200000", 0),
    ("fire_i_rejected_existing", {
        "on_ground": True, "fall_distance": 0.0,
        "enchant_id": 20, "enchant_level": 1,
        "target_fire_ticks": 120, "target_hurt_resistant": 20,
        "target_last_damage": 2.0,
    }, "41200000", 120),
    ("fire_i_lethal", {
        "on_ground": True, "fall_distance": 0.0,
        "enchant_id": 20, "enchant_level": 1, "target_health": 1.0,
    }, "00000000", 80),
)

SOUND_CASES = (
    ("critical_sound", {}, ("entity.player.attack.crit",), ""),
    ("strong_sound", {
        "on_ground": True, "fall_distance": 0.0,
    }, ("entity.player.attack.strong",), ""),
    ("weak_sound", {
        "on_ground": True, "fall_distance": 0.0, "cooldown_ticks": 1,
    }, ("entity.player.attack.weak",), ""),
    ("sprint_sound_order", {
        "on_ground": True, "fall_distance": 0.0, "sprinting": True,
    }, ("entity.player.attack.knockback", "entity.player.attack.strong"), ""),
    ("rejected_sprint_sound_order", {
        "on_ground": True, "fall_distance": 0.0, "sprinting": True,
        "target_hurt_resistant": 20, "target_last_damage": 2.0,
    }, ("entity.player.attack.knockback", "entity.player.attack.nodamage"), ""),
    ("nodamage_sound", {
        "on_ground": True, "fall_distance": 0.0,
        "target_hurt_resistant": 20, "target_last_damage": 2.0,
    }, ("entity.player.attack.nodamage",), ""),
    ("sweep_sound_and_damage", {
        "held_item": 276, "cooldown_ticks": 12,
        "on_ground": True, "fall_distance": 0.0,
        "sweep_neighbor": True,
    }, ("entity.player.attack.sweep",), "41100000"),
    ("sweep_i_damage", {
        "held_item": 276, "cooldown_ticks": 12,
        "on_ground": True, "fall_distance": 0.0,
        "sweep_neighbor": True, "enchant_id": 22, "enchant_level": 1,
    }, ("entity.player.attack.sweep",), "40b00001"),
    ("sweep_iii_damage", {
        "held_item": 276, "cooldown_ticks": 12,
        "on_ground": True, "fall_distance": 0.0,
        "sweep_neighbor": True, "enchant_id": 22, "enchant_level": 3,
    }, ("entity.player.attack.sweep",), "40700004"),
    ("movement_threshold_suppresses_sweep", {
        "held_item": 276, "cooldown_ticks": 12,
        "on_ground": True, "fall_distance": 0.0,
        "sweep_neighbor": True, "distance_walked_delta": 0.1,
    }, ("entity.player.attack.strong",), "41200000"),
)

TARGET_SOUND_SEED48 = 123456789
NEIGHBOR_SOUND_SEED48 = 987654321
TARGET_SOUND_CASES = (
    ("pig_hurt_sound", {"target": "pig"}, (
        ("entity.pig.hurt", "neutral", "3f800000", "3f82b1e2", "target"),
        ("entity.player.attack.crit", "player", "3f800000", "3f800000", "player"),
    )),
    ("pig_death_sound", {"target": "pig", "target_health": 1.0}, (
        ("entity.pig.death", "neutral", "3f800000", "3f82b1e2", "target"),
        ("entity.player.attack.crit", "player", "3f800000", "3f800000", "player"),
    )),
    ("cow_hurt_sound", {"target": "cow"}, (
        ("entity.cow.hurt", "neutral", "3ecccccd", "3f82b1e2", "target"),
        ("entity.player.attack.crit", "player", "3f800000", "3f800000", "player"),
    )),
    ("cow_death_sound", {"target": "cow", "target_health": 1.0}, (
        ("entity.cow.death", "neutral", "3ecccccd", "3f82b1e2", "target"),
        ("entity.player.attack.crit", "player", "3f800000", "3f800000", "player"),
    )),
    ("sheep_hurt_sound", {"target": "sheep"}, (
        ("entity.sheep.hurt", "neutral", "3f800000", "3f82b1e2", "target"),
        ("entity.player.attack.crit", "player", "3f800000", "3f800000", "player"),
    )),
    ("sheep_death_sound", {"target": "sheep", "target_health": 1.0}, (
        ("entity.sheep.death", "neutral", "3f800000", "3f82b1e2", "target"),
        ("entity.player.attack.crit", "player", "3f800000", "3f800000", "player"),
    )),
    ("chicken_hurt_sound", {"target": "chicken"}, (
        ("entity.chicken.hurt", "neutral", "3f800000", "3f82b1e2", "target"),
        ("entity.player.attack.crit", "player", "3f800000", "3f800000", "player"),
    )),
    ("chicken_death_sound", {
        "target": "chicken", "target_health": 1.0,
    }, (
        ("entity.chicken.death", "neutral", "3f800000", "3f82b1e2", "target"),
        ("entity.player.attack.crit", "player", "3f800000", "3f800000", "player"),
    )),
    ("rejected_target_sound", {
        "target": "pig", "on_ground": True, "fall_distance": 0.0,
        "target_hurt_resistant": 20, "target_last_damage": 2.0,
    }, (
        ("entity.player.attack.nodamage", "player", "3f800000", "3f800000", "player"),
    )),
    ("accepted_delta_no_target_sound", {
        "target": "pig", "on_ground": True, "fall_distance": 0.0,
        "target_hurt_resistant": 20, "target_last_damage": 0.5,
    }, (
        ("entity.player.attack.strong", "player", "3f800000", "3f800000", "player"),
    )),
    ("sprint_target_sound_order", {
        "target": "pig", "on_ground": True, "fall_distance": 0.0,
        "sprinting": True,
    }, (
        ("entity.player.attack.knockback", "player", "3f800000", "3f800000", "player"),
        ("entity.pig.hurt", "neutral", "3f800000", "3f82b1e2", "target"),
        ("entity.player.attack.strong", "player", "3f800000", "3f800000", "player"),
    )),
    ("sweep_target_sound_order", {
        "target": "pig", "held_item": 276, "cooldown_ticks": 12,
        "on_ground": True, "fall_distance": 0.0, "sweep_neighbor": True,
    }, (
        ("entity.pig.hurt", "neutral", "3f800000", "3f82b1e2", "target"),
        ("entity.pig.hurt", "neutral", "3f800000", "3f8516b8", "neighbor"),
        ("entity.player.attack.sweep", "player", "3f800000", "3f800000", "player"),
    )),
)

TARGET_SOUND_FAMILIES = (
    ("zombie", "entity.zombie", "hostile", "3f800000", "3f82b1e2"),
    ("pigman", "entity.zombie_pig", "hostile", "3f800000", "3f6a3a5b"),
    ("skeleton", "entity.skeleton", "hostile", "3f800000", "3f82b1e2"),
    ("wither_skeleton", "entity.wither_skeleton", "hostile", "3f800000", "3f82b1e2"),
    ("creeper", "entity.creeper", "hostile", "3f800000", "3f82b1e2"),
    ("spider", "entity.spider", "hostile", "3f800000", "3f82b1e2"),
    ("cave_spider", "entity.spider", "hostile", "3f800000", "3f82b1e2"),
    ("enderman", "entity.endermen", "hostile", "3f800000", "3f82b1e2"),
    ("blaze", "entity.blaze", "hostile", "3f800000", "3f82b1e2"),
    ("ghast", "entity.ghast", "hostile", "41200000", "3f82b1e2"),
    ("silverfish", "entity.silverfish", "hostile", "3f800000", "3f82b1e2"),
    ("villager", "entity.villager", "neutral", "3f800000", "3f82b1e2"),
    ("witch", "entity.witch", "hostile", "3f800000", "3f82b1e2"),
)
TARGET_SOUND_CASES += tuple(
    (f"{target}_{edge}_sound",
     {"target": target, **({"target_health": 1.0} if lethal else {})},
     ((f"{event}.{edge}", category, volume, pitch, "target"),
      ("entity.player.attack.crit", "player", "3f800000", "3f800000",
       "player")))
    for target, event, category, volume, pitch in TARGET_SOUND_FAMILIES
    for edge, lethal in (("hurt", False), ("death", True))
)

SLIME_TARGET_SOUND_FAMILIES = (
    ("slime", 1, "entity.small_slime", "3ecccccd"),
    ("slime", 2, "entity.slime", "3f4ccccd"),
    ("slime", 4, "entity.slime", "3fcccccd"),
    ("magma_cube", 1, "entity.small_magmacube", "3ecccccd"),
    ("magma_cube", 2, "entity.magmacube", "3f4ccccd"),
    ("magma_cube", 4, "entity.magmacube", "3fcccccd"),
)
TARGET_SOUND_CASES += tuple(
    (f"{target}_{size}_{edge}_sound",
     {"target": target, "target_size": size,
      **({"on_ground": True, "fall_distance": 0.0,
          "cooldown_ticks": 1} if size == 1 and not lethal else {}),
      **({"target_health": 0.25 if target == "magma_cube" and size == 4
          else 1.0} if lethal else {})},
     ((f"{event}.{edge}", "neutral", volume, "3f82b1e2", "target"),
      ("entity.player.attack.weak" if size == 1 and not lethal
       else "entity.player.attack.crit",
       "player", "3f800000", "3f800000",
       "player")))
    for target, size, event, volume in SLIME_TARGET_SOUND_FAMILIES
    for edge, lethal in (("hurt", False), ("death", True))
)

PARTICLE_CASES = (
    ("critical_particle", {}, ((9, -1),)),
    ("strong_no_particle", {
        "on_ground": True, "fall_distance": 0.0,
    }, ()),
    ("sharp_v_particles", {
        "enchant_id": 16, "enchant_level": 5,
    }, ((9, -1), (10, -1), (44, 2))),
    ("sweep_particles", {
        "held_item": 276, "cooldown_ticks": 12,
        "on_ground": True, "fall_distance": 0.0,
        "sweep_neighbor": True,
    }, ((45, 0), (44, 3))),
    ("moving_sword_damage_particle", {
        "held_item": 276, "cooldown_ticks": 12,
        "on_ground": True, "fall_distance": 0.0,
        "distance_walked_delta": 0.1,
    }, ((44, 3),)),
    ("lethal_sword_clamped_health_particle", {
        "held_item": 276, "cooldown_ticks": 12,
        "on_ground": True, "fall_distance": 0.0,
        "target_health": 1.0,
    }, ((45, 0),)),
    ("rejected_no_particle", {
        "on_ground": True, "fall_distance": 0.0,
        "target_hurt_resistant": 20, "target_last_damage": 2.0,
    }, ()),
)


def double_bits(value):
    return struct.pack(">d", value).hex()


def double_from_bits(value):
    return struct.unpack(">d", bytes.fromhex(value))[0]


def float32(value):
    return struct.unpack(">f", struct.pack(">f", value))[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    env = NetheriteEnv(args.host, args.port)
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(f"server lock failed: {locked}")
    try:
        for name, action, expected_bits, expected_damage in CASES:
            result = env._cmd({
                "cmd": "player_critical_locked", "action": action,
            })
            if not result.get("ok"):
                raise AssertionError(f"{name}: {result}")
            if result["after_bits"] != expected_bits:
                raise AssertionError(
                    f"{name}: after_bits={result['after_bits']} "
                    f"expected={expected_bits}")
            if result["held_damage"] != expected_damage:
                raise AssertionError(
                    f"{name}: held_damage={result['held_damage']} "
                    f"expected={expected_damage}")
        for item, expected_bits, expected_damage in WEAPON_CASES:
            result = env._cmd({
                "cmd": "player_critical_locked",
                "action": {"held_item": item, "cooldown_ticks": 5},
            })
            if not result.get("ok"):
                raise AssertionError(f"item_{item}: {result}")
            if (result["after_bits"] != expected_bits
                    or result["held_damage"] != expected_damage):
                raise AssertionError(
                    f"item_{item}: after_bits={result['after_bits']} "
                    f"held_damage={result['held_damage']} expected_bits="
                    f"{expected_bits} expected_damage={expected_damage}")
        for name, action, target_motion, player_motion, sprinting in MOTION_CASES:
            result = env._cmd({
                "cmd": "player_critical_locked", "action": action,
            })
            if not result.get("ok"):
                raise AssertionError(f"{name}: {result}")
            if (tuple(result["target_motion_bits"]) != target_motion
                    or tuple(result["player_motion_bits"]) != player_motion
                    or result["player_sprinting"] != sprinting):
                raise AssertionError(
                    f"{name}: target_motion={result['target_motion_bits']} "
                    f"player_motion={result['player_motion_bits']} "
                    f"sprinting={result['player_sprinting']} expected="
                    f"{target_motion} {player_motion} {sprinting}")
        for name, action, expected_bits, expected_fire in FIRE_CASES:
            result = env._cmd({
                "cmd": "player_critical_locked", "action": action,
            })
            if not result.get("ok"):
                raise AssertionError(f"{name}: {result}")
            if (result["after_bits"] != expected_bits
                    or result["target_fire_ticks"] != expected_fire
                    or result["held_damage"] != 0):
                raise AssertionError(
                    f"{name}: after_bits={result['after_bits']} "
                    f"fire={result['target_fire_ticks']} held_damage="
                    f"{result['held_damage']} expected={expected_bits} "
                    f"{expected_fire} 0")
        for name, action, expected_sounds, expected_neighbor in SOUND_CASES:
            result = env._cmd({
                "cmd": "player_critical_locked", "action": action,
            })
            if not result.get("ok"):
                raise AssertionError(f"{name}: {result}")
            player_sounds = tuple(
                row["sound"].removeprefix("minecraft:")
                for row in result["sounds"]
                if row["sound"].startswith("minecraft:entity.player.attack."))
            if player_sounds != expected_sounds:
                raise AssertionError(
                    f"{name}: sounds={player_sounds} expected={expected_sounds}")
            for row in result["sounds"]:
                if not row["sound"].startswith("minecraft:entity.player.attack."):
                    continue
                if (row["category"] != "player"
                        or row["volume_bits"] != "3f800000"
                        or row["pitch_bits"] != "3f800000"
                        or row["position_bits"]
                            != result["player_position_bits"]):
                    raise AssertionError(f"{name}: bad player sound scalar {row}")
            if result["sweep_neighbor_after_bits"] != expected_neighbor:
                raise AssertionError(
                    f"{name}: neighbor={result['sweep_neighbor_after_bits']} "
                    f"expected={expected_neighbor}")
        for name, action, expected_rows in TARGET_SOUND_CASES:
            seeded_action = {
                "target_seed48": TARGET_SOUND_SEED48,
                "neighbor_seed48": NEIGHBOR_SOUND_SEED48,
                **action,
            }
            result = env._cmd({
                "cmd": "player_critical_locked", "action": seeded_action,
            })
            if not result.get("ok"):
                raise AssertionError(f"{name}: {result}")
            player_x, player_y, player_z = (
                double_from_bits(value)
                for value in result["player_position_bits"])
            positions = {
                "player": tuple(result["player_position_bits"]),
                "target": (
                    double_bits(player_x), double_bits(220.0),
                    double_bits(player_z - 2.0)),
                "neighbor": (
                    double_bits(player_x + 1.0), double_bits(220.0),
                    double_bits(player_z - 2.0)),
            }
            got_rows = tuple(
                (row["sound"].removeprefix("minecraft:"),
                 row["category"], row["volume_bits"], row["pitch_bits"],
                 tuple(row["position_bits"]))
                for row in result["sounds"])
            want_rows = tuple(
                (sound, category, volume, pitch, positions[position])
                for sound, category, volume, pitch, position in expected_rows)
            if got_rows != want_rows:
                raise AssertionError(
                    f"{name}: sound rows={got_rows} expected={want_rows}")
        for name, action, expected_rows in PARTICLE_CASES:
            result = env._cmd({
                "cmd": "player_critical_locked", "action": action,
            })
            if not result.get("ok"):
                raise AssertionError(f"{name}: {result}")
            rows = result["particles"]
            got_rows = tuple((row["id"], row["count"]) for row in rows)
            if got_rows != expected_rows:
                raise AssertionError(
                    f"{name}: particle rows={got_rows} expected={expected_rows}")
            player_x, player_y, player_z = (
                double_from_bits(value)
                for value in result["player_position_bits"])
            target_position = (
                double_bits(player_x), double_bits(220.0),
                double_bits(player_z - 2.0))
            attached_eid = None
            for row in rows:
                if row["long_distance"]:
                    raise AssertionError(f"{name}: long-distance particle {row}")
                if row["id"] in (9, 10):
                    if (tuple(row["descriptor_bits"]) != ("0" * 16,) * 4
                            or tuple(row["entity_size_bits"])
                                != ("3f666666", "3f666666")
                            or tuple(row["position_bits"]) != target_position
                            or row["entity_id"] <= 0):
                        raise AssertionError(
                            f"{name}: bad attached critical descriptor {row}")
                    if attached_eid is None:
                        attached_eid = row["entity_id"]
                    elif row["entity_id"] != attached_eid:
                        raise AssertionError(
                            f"{name}: attached emitters disagree on target")
                elif row["id"] == 44:
                    damage_y = float32(float32(0.9) * float32(0.5))
                    if (tuple(row["descriptor_bits"]) != (
                            "3fb999999999999a", "0" * 16,
                            "3fb999999999999a", "3fc999999999999a")
                            or tuple(row["entity_size_bits"])
                                != ("00000000", "00000000")
                            or row["entity_id"] != -1
                            or tuple(row["position_bits"]) != (
                                double_bits(player_x),
                                double_bits(220.0 + damage_y),
                                double_bits(player_z - 2.0))):
                        raise AssertionError(
                            f"{name}: bad damage-indicator descriptor {row}")
                elif row["id"] == 45:
                    sweep_dx = double_from_bits("bca1a62640000000")
                    sweep_height = float32(1.8)
                    if (tuple(row["descriptor_bits"]) != (
                            "bca1a62640000000", "0" * 16,
                            "bff0000000000000", "0" * 16)
                            or tuple(row["entity_size_bits"])
                                != ("00000000", "00000000")
                            or row["entity_id"] != -1
                            or tuple(row["position_bits"]) != (
                                double_bits(player_x + sweep_dx),
                                double_bits(player_y + sweep_height * 0.5),
                                double_bits(player_z - 1.0))):
                        raise AssertionError(
                            f"{name}: bad sweep descriptor {row}")
                else:
                    raise AssertionError(f"{name}: unexpected particle {row}")
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    case_count = sum(map(len, (
        CASES, WEAPON_CASES, MOTION_CASES, FIRE_CASES,
        SOUND_CASES, TARGET_SOUND_CASES, PARTICLE_CASES,
    )))
    print(f"player critical oracle: PASS ({case_count} cases)")


if __name__ == "__main__":
    main()
