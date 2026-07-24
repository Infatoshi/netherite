#!/usr/bin/env python3
"""Drive qrl hud_pin + frame captures for ui_hud oracle goldens.

Produces <id>_a.png / <id>_b.png plus meta/<id>.json and capture_manifest.json.
Frames come only from the live Java client (qrl cmd \"frame\"); never synthesized.
"""
from __future__ import print_function

import argparse
import json
import os
import sys
import time

# java/ is cwd when launched from capture_ui_hud.sh
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.getcwd())
import qrl_client  # noqa: E402

# Flat seed-0 ground is y=3/4; stand on a raised stone pad near origin.
PLAT_Y = 4
CX, CZ = 8, 8
# Standing feet on platform, looking +Z into a plain wall at z=CX+3
POSE = {
    "x": CX + 0.5,
    "y": float(PLAT_Y + 1),
    "z": CZ + 0.5,
    "yaw": 0.0,
    "pitch": 0.0,
    "no_gravity": True,
}

# Item ids 1.11.2
IRON_HELM, IRON_CHEST, IRON_LEGS, IRON_BOOTS = 306, 307, 308, 309
WOOD_PICK, BOW, BREAD, IRON_SWORD = 270, 261, 297, 267
# armorInventory order: feet, legs, chest, head
ARMOR_IDS = [IRON_BOOTS, IRON_LEGS, IRON_CHEST, IRON_HELM]


def log(msg):
    print("[ui_hud_driver] " + msg, file=sys.stderr)


def runcmds(e, cmds):
    return e._cmd({"cmd": "runcmds", "action": {"cmds": cmds}})


def hud_pin(e, **kwargs):
    return e._cmd({"cmd": "hud_pin", "action": kwargs})


def set_pose(e, pose=None):
    p = dict(POSE if pose is None else pose)
    return e._cmd({"cmd": "set_pose", "action": p})


def grab(e, path):
    r = e._cmd({"cmd": "frame", "action": {"file": path, "rerender": True}})
    if not r.get("ok"):
        raise RuntimeError("frame failed for %s: %s" % (path, r))
    if not os.path.isfile(path) or os.path.getsize(path) < 100:
        raise RuntimeError("frame file missing/empty: %s (%s)" % (path, r))
    return r


def settle(e, n=8):
    for _ in range(n):
        e.step({})


def base_scene(e):
    """Frozen clear noon, stone platform + backdrop wall, empty effects."""
    cmds = [
        "gamerule sendCommandFeedback false",
        "gamerule logAdminCommands false",
        "gamerule doDaylightCycle false",
        "gamerule doWeatherCycle false",
        "gamerule doMobSpawning false",
        "gamerule doFireTick false",
        "gamerule randomTickSpeed 0",
        "gamerule keepInventory true",
        "time set 6000",
        "weather clear 1000000",
        "gamemode 0 @a",
        "difficulty peaceful",
        # platform + backdrop wall (plain stone for viewmodel stills)
        "fill %d %d %d %d %d %d minecraft:stone" % (
            CX - 3, PLAT_Y, CZ - 3, CX + 3, PLAT_Y, CZ + 3),
        "fill %d %d %d %d %d %d minecraft:air" % (
            CX - 3, PLAT_Y + 1, CZ - 3, CX + 3, PLAT_Y + 4, CZ + 3),
        "fill %d %d %d %d %d %d minecraft:stone" % (
            CX - 2, PLAT_Y + 1, CZ + 3, CX + 2, PLAT_Y + 4, CZ + 3),
        "clear @a",
        "effect @a clear",
    ]
    r = runcmds(e, cmds)
    log("base_scene: %s" % r)
    set_pose(e, POSE)
    settle(e, 20)


def clear_player(e):
    runcmds(e, ["clear @a", "effect @a clear", "kill @e[type=!player]"])
    hud_pin(
        e,
        hold_death=False,
        clear_effects=True,
        health=20.0,
        food=20,
        air=300,
        absorption=0.0,
        xp_level=0,
        xp_frac=0.0,
        fire=0,
        portal=0.0,
        use_action=0,
        boss={"show": False},
        hotbar=[[0, 0, 0]] * 9,
        armor=[0, 0, 0, 0],
        **POSE
    )
    # Ensure not dead
    settle(e, 2)


def capture_pair(e, outdir, state_id, pin_kwargs, meta_extra=None, settle_n=4):
    """Pin state, dump A then re-pin and dump B (no tick drift)."""
    os.makedirs(outdir, exist_ok=True)
    meta_dir = os.path.join(outdir, "meta")
    os.makedirs(meta_dir, exist_ok=True)

    # Merge pose defaults
    pin = dict(POSE)
    pin.update(pin_kwargs)

    # Equip armor via replaceitem as well as hud_pin (server attributes).
    if pin.get("armor"):
        slots = ("feet", "legs", "chest", "head")
        names = {
            IRON_BOOTS: "minecraft:iron_boots",
            IRON_LEGS: "minecraft:iron_leggings",
            IRON_CHEST: "minecraft:iron_chestplate",
            IRON_HELM: "minecraft:iron_helmet",
        }
        cmds = []
        for i, aid in enumerate(pin["armor"][:4]):
            if aid and aid in names:
                cmds.append("replaceitem entity @p slot.armor.%s %s 1" % (
                    slots[i], names[aid]))
        if cmds:
            runcmds(e, cmds)

    r1 = hud_pin(e, **pin)
    settle(e, settle_n)
    # Re-pin immediately before each grab so tick bookkeeping stays frozen.
    # No wall-clock sleep between A/B: the client tick races the socket and
    # advances updateCounter (hurt flash) / portal animation if we wait.
    r1 = hud_pin(e, **pin)
    r1 = hud_pin(e, **pin)
    path_a = os.path.join(outdir, "%s_a.png" % state_id)
    fa = grab(e, path_a)
    r2 = hud_pin(e, **pin)
    path_b = os.path.join(outdir, "%s_b.png" % state_id)
    fb = grab(e, path_b)

    meta = {
        "id": state_id,
        "pin": pin,
        "pin_reply_a": r1,
        "pin_reply_b": r2,
        "frame_a": fa,
        "frame_b": fb,
        "pose": {k: pin.get(k) for k in ("x", "y", "z", "yaw", "pitch")},
        "width": fa.get("w"),
        "height": fa.get("h"),
        "gui_scale": 2,
        "partial_ticks": 1.0,
        "notes": "A/B from qrl frame{} at partialTicks=1 with hud_pin freeze",
    }
    if meta_extra:
        meta.update(meta_extra)
    with open(os.path.join(meta_dir, "%s.json" % state_id), "w") as f:
        json.dump(meta, f, indent=2)
    log("captured %s  a=%s b=%s  pin=%s" % (
        state_id, fa.get("w"), fb.get("w"),
        {k: r1.get(k) for k in ("health", "food", "air", "armor", "absorption",
                                "xp_level", "xp_frac", "hand_active")
         if k in r1}))
    return meta


def build_water_pool(e):
    """Glass box of water on the platform for air/underwater states."""
    x0, y0, z0 = CX - 2, PLAT_Y + 1, CZ - 2
    x1, y1, z1 = CX + 2, PLAT_Y + 4, CZ + 2
    cmds = [
        "fill %d %d %d %d %d %d minecraft:glass" % (x0, y0, z0, x1, y1, z1),
        "fill %d %d %d %d %d %d minecraft:water" % (
            x0 + 1, y0, z0 + 1, x1 - 1, y1 - 1, z1 - 1),
    ]
    runcmds(e, cmds)


def build_solid_cell(e, block):
    """3x3x3 solid shell at eye height so inside-block overlay samples hit."""
    # eye ~ feet_y + 1.62 -> block y = PLAT_Y+2 when feet at PLAT_Y+1? 
    # feet at PLAT_Y+0.0 inside hole: place floor at PLAT_Y-1, solid at PLAT_Y+1
    # Simpler: feet at PLAT_Y+1, solid at y=PLAT_Y+2 (eye in block)
    by = PLAT_Y + 2
    cmds = [
        "fill %d %d %d %d %d %d minecraft:air" % (
            CX - 1, PLAT_Y + 1, CZ - 1, CX + 1, PLAT_Y + 3, CZ + 1),
        "fill %d %d %d %d %d %d %s" % (
            CX - 1, by, CZ - 1, CX + 1, by, CZ + 1, block),
    ]
    runcmds(e, cmds)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    outdir = args.out
    os.makedirs(outdir, exist_ok=True)

    e = qrl_client.QRLEnv()
    log("reset seed=%d flat survival fresh..." % args.seed)
    o = e.reset({
        "seed": args.seed,
        "mode": "survival",
        "type": "flat",
        "structures": False,
        "fresh": True,
    }, timeout=180.0)
    if not o.get("ok"):
        log("reset failed: %s" % o)
        sys.exit(1)
    log("spawn ~ (%.1f,%.1f,%.1f)" % (o["x"], o["y"], o["z"]))

    base_scene(e)
    manifest = {"seed": args.seed, "states": [], "blocked": []}

    # ---- HUD states ----
    clear_player(e)
    base_scene(e)
    manifest["states"].append(capture_pair(e, outdir, "hud_armor_iron", {
        "health": 20.0, "food": 20, "air": 300, "absorption": 0.0,
        "armor": ARMOR_IDS,
        "hotbar": [[0, 0, 0]] * 9,
        "xp_level": 0, "xp_frac": 0.0,
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
    }, {"roi": "armor+hearts"}))

    clear_player(e)
    base_scene(e)
    manifest["states"].append(capture_pair(e, outdir, "hud_absorption_armor", {
        "health": 20.0, "food": 20, "air": 300, "absorption": 20.0,
        "armor": ARMOR_IDS,
        "hotbar": [[0, 0, 0]] * 9,
        "xp_level": 0, "xp_frac": 0.0,
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        "effects": [{"id": 22, "duration": 600, "amplifier": 4}],  # absorption
    }, {"roi": "armor lifted by absorption rows"}))

    # Hurt flash on/off: health dropped 20->14, pin GuiIngame flash phase.
    clear_player(e)
    base_scene(e)
    for flash_on, sid in ((True, "hud_hurt_flash_on"), (False, "hud_hurt_flash_off")):
        manifest["states"].append(capture_pair(e, outdir, sid, {
            "health": 14.0, "food": 20, "air": 300, "absorption": 0.0,
            "armor": [0, 0, 0, 0],
            "hotbar": [[0, 0, 0]] * 9,
            "hurt_time": 10, "max_hurt_time": 10, "hurt_yaw": 0.0,
            "hud_flash": flash_on,
            "hud_health": 14, "hud_last_health": 20,
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
        }, {"roi": "hearts row only", "hud_flash": flash_on}))

    clear_player(e)
    base_scene(e)
    manifest["states"].append(capture_pair(e, outdir, "hud_hunger_poison", {
        "health": 20.0, "food": 8, "air": 300, "absorption": 0.0,
        "armor": [0, 0, 0, 0],
        "hotbar": [[0, 0, 0]] * 9,
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        "effects": [{"id": 17, "duration": 600, "amplifier": 0}],  # hunger
    }, {"roi": "hunger haunches"}))

    # Air bubbles: submerged, air=123
    clear_player(e)
    base_scene(e)
    build_water_pool(e)
    water_pose = dict(POSE)
    water_pose["y"] = float(PLAT_Y + 1)  # feet in water column
    water_pose["x"] = CX + 0.5
    water_pose["z"] = CZ + 0.5
    manifest["states"].append(capture_pair(e, outdir, "hud_air_partial", {
        "health": 20.0, "food": 20, "air": 123, "absorption": 0.0,
        "armor": [0, 0, 0, 0],
        "hotbar": [[0, 0, 0]] * 9,
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        **water_pose
    }, {"roi": "air bubbles sh-49 right"}))

    clear_player(e)
    base_scene(e)
    manifest["states"].append(capture_pair(e, outdir, "hud_xp_half", {
        "health": 20.0, "food": 20, "air": 300, "absorption": 0.0,
        "armor": [0, 0, 0, 0],
        "hotbar": [[0, 0, 0]] * 9,
        "xp_level": 7, "xp_frac": 0.5,
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
    }, {"roi": "xp bar + level"}))

    clear_player(e)
    base_scene(e)
    # wood pick damage 30/59 in slot 0
    hotbar_dur = [[WOOD_PICK, 1, 30]] + [[0, 0, 0]] * 8
    manifest["states"].append(capture_pair(e, outdir, "hud_durability_half", {
        "health": 20.0, "food": 20, "air": 300, "absorption": 0.0,
        "armor": [0, 0, 0, 0],
        "hotbar": hotbar_dur, "hotbar_sel": 0,
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
    }, {"roi": "slot0 durability strip"}))

    clear_player(e)
    base_scene(e)
    manifest["states"].append(capture_pair(e, outdir, "hud_boss_half", {
        "health": 20.0, "food": 20, "air": 300, "absorption": 0.0,
        "armor": [0, 0, 0, 0],
        "hotbar": [[0, 0, 0]] * 9,
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": True, "frac": 0.5, "name": "Ender Dragon"},
        "clear_effects": True,
    }, {"roi": "boss bar top center"}))

    clear_player(e)
    base_scene(e)
    manifest["states"].append(capture_pair(e, outdir, "hud_death", {
        "dead": True,
        "hold_death": True,
        "health": 0.0,
        "food": 20, "air": 300,
        "armor": [0, 0, 0, 0],
        "hotbar": [[0, 0, 0]] * 9,
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
    }, {"roi": "full-frame death wash"}))
    # revive for remaining captures
    hud_pin(e, hold_death=False, health=20.0, dead=False)
    # close death screen
    try:
        e._cmd({"cmd": "runcmds", "action": {"cmds": ["gamemode 0 @a"]}})
    except Exception:
        pass
    settle(e, 5)
    # force respawn path if still dead
    clear_player(e)
    base_scene(e)

    # ---- Viewmodels ----
    clear_player(e)
    base_scene(e)
    wall_pose = dict(POSE)
    wall_pose["yaw"] = 0.0
    wall_pose["pitch"] = 0.0
    wall_pose["z"] = CZ + 0.5  # face +Z wall
    manifest["states"].append(capture_pair(e, outdir, "hand_bow_pull20", {
        "health": 20.0, "food": 20, "air": 300,
        "hotbar": [[BOW, 1, 0]] + [[0, 0, 0]] * 8,
        "hotbar_sel": 0,
        "bow_pull": 20,
        "armor": [0, 0, 0, 0],
        "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        **wall_pose
    }, {"roi": "lower-right viewmodel"}))

    clear_player(e)
    base_scene(e)
    manifest["states"].append(capture_pair(e, outdir, "hand_eat_mid", {
        "health": 20.0, "food": 10, "air": 300,
        "hotbar": [[BREAD, 1, 0]] + [[0, 0, 0]] * 8,
        "hotbar_sel": 0,
        "use_action": 1, "use_remaining": 16,  # mid-eat of max 32
        "armor": [0, 0, 0, 0],
        "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        **wall_pose
    }, {"roi": "lower-right viewmodel"}))

    clear_player(e)
    base_scene(e)
    manifest["states"].append(capture_pair(e, outdir, "hand_block_sword", {
        "health": 20.0, "food": 20, "air": 300,
        "hotbar": [[IRON_SWORD, 1, 0]] + [[0, 0, 0]] * 8,
        "hotbar_sel": 0,
        "use_action": 2, "use_remaining": 72000,
        "armor": [0, 0, 0, 0],
        "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        **wall_pose
    }, {"roi": "lower-right viewmodel"}))

    # ---- Overlays ----
    clear_player(e)
    base_scene(e)
    build_solid_cell(e, "minecraft:stone")
    # feet on platform, eye in stone at y=PLAT_Y+2
    stone_pose = dict(POSE)
    stone_pose["y"] = float(PLAT_Y + 1)
    manifest["states"].append(capture_pair(e, outdir, "overlay_inside_stone", {
        "health": 20.0, "food": 20, "air": 300,
        "hotbar": [[0, 0, 0]] * 9,
        "armor": [0, 0, 0, 0],
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        **stone_pose
    }, {"roi": "full-frame inside-block darken"}))

    clear_player(e)
    base_scene(e)
    build_solid_cell(e, "minecraft:grass")
    manifest["states"].append(capture_pair(e, outdir, "overlay_inside_grass", {
        "health": 20.0, "food": 20, "air": 300,
        "hotbar": [[0, 0, 0]] * 9,
        "armor": [0, 0, 0, 0],
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        **stone_pose
    }, {"roi": "full-frame grass particle darken"}))

    clear_player(e)
    base_scene(e)
    # portal swirl: pin timeInPortal without needing a real portal block for the overlay alpha
    # (GuiIngame.renderPortal uses timeInPortal; place a portal block for texture authenticity)
    runcmds(e, [
        "fill %d %d %d %d %d %d minecraft:obsidian" % (
            CX - 1, PLAT_Y + 1, CZ + 1, CX + 1, PLAT_Y + 4, CZ + 1),
        "setblock %d %d %d minecraft:portal" % (CX, PLAT_Y + 2, CZ + 1),
    ])
    portal_pose = dict(POSE)
    portal_pose["z"] = CZ + 1.5
    manifest["states"].append(capture_pair(e, outdir, "overlay_portal_050", {
        "health": 20.0, "food": 20, "air": 300,
        "hotbar": [[0, 0, 0]] * 9,
        "armor": [0, 0, 0, 0],
        "use_action": 0, "fire": 0, "portal": 0.5,
        "boss": {"show": False},
        "clear_effects": True,
        **portal_pose
    }, {"roi": "full-frame portal swirl"}))

    clear_player(e)
    base_scene(e)
    manifest["states"].append(capture_pair(e, outdir, "overlay_fire", {
        "health": 20.0, "food": 20, "air": 300,
        "hotbar": [[0, 0, 0]] * 9,
        "armor": [0, 0, 0, 0],
        "use_action": 0, "fire": 80, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        **POSE
    }, {"roi": "first-person fire quads"}))

    clear_player(e)
    base_scene(e)
    build_water_pool(e)
    uw_pose = dict(POSE)
    uw_pose["y"] = float(PLAT_Y + 1)
    uw_pose["yaw"] = 0.0
    uw_pose["pitch"] = 0.0
    manifest["states"].append(capture_pair(e, outdir, "overlay_underwater", {
        "health": 20.0, "food": 20, "air": 200,
        "hotbar": [[0, 0, 0]] * 9,
        "armor": [0, 0, 0, 0],
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        **uw_pose
    }, {"roi": "full-frame underwater.png"}))

    man_path = os.path.join(outdir, "capture_manifest.json")
    with open(man_path, "w") as f:
        json.dump({
            "seed": args.seed,
            "n_states": len(manifest["states"]),
            "ids": [s["id"] for s in manifest["states"]],
            "blocked": manifest["blocked"],
            "resolution": "from frame replies",
            "java_home": os.environ.get("JAVA_HOME"),
            "notes": (
                "Genuine Java 1.11.2 FBO dumps via qrl frame{rerender:true}. "
                "State frozen with hud_pin per A/B pair."
            ),
        }, f, indent=2)
    log("manifest -> %s (%d states)" % (man_path, len(manifest["states"])))
    e.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
