#!/usr/bin/env python3
"""Real-1.11.2 locked oracle for player critical-hit damage."""

import argparse
import pathlib
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
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    print("player critical oracle: PASS "
          f"({len(CASES) + len(WEAPON_CASES) + len(MOTION_CASES) + len(FIRE_CASES)} cases)")


if __name__ == "__main__":
    main()
