#!/usr/bin/env python3
"""Drive qrl entity_pin + frame captures for ui_entities oracle goldens.

Frames come only from the live Java client (qrl cmd \"frame\"); never synthesized.
"""
from __future__ import print_function

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.getcwd())
REPO = os.path.abspath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "magma", "trace"))
import qrl_client  # noqa: E402
import nbt_codec  # noqa: E402

# Flat platform near origin. Superflat surface ~y=3; pad at y=4.
# Note: flat-world spawn is often far from (0,0); fill/setblock fail until the
# player is posed here long enough for server chunks to load (executeCommand
# returns 0 for fill into unloaded chunks).
PLAT_Y = 4
CX, CZ = 8, 8
# Camera: look +Z toward subject at z=CX+4. Pitch ~25 so nearby pad is in frame
# (eye height ~1.62; pitch 10 from y=5 mostly sees sky/horizon).
CAM = {
    "x": CX + 0.5,
    "y": float(PLAT_Y + 1),
    "z": CZ + 0.5,
    "yaw": 0.0,   # MC: 0 = +Z
    "pitch": 25.0,
    "no_gravity": True,
}
ENDER_CHEST_CAM = dict(CAM, pitch=18.0)
ENDER_CHEST_POS = (CX, PLAT_Y + 1, CZ + 4)
BEACON_CAM = {
    "x": CX + 2.5, "y": float(PLAT_Y + 1.5), "z": CZ - 2.0,
    "yaw": 19.5, "pitch": -4.0, "no_gravity": True,
}
BEACON_POS = (CX, PLAT_Y + 2, CZ + 4)
BEACON_STATE_IDS = ("beacon_world_colored", "beacon_world_background")
SPAWNER_STATE_IDS = (
    "spawner_pig_saved", "spawner_zombie_noai_saved",
    "spawner_background",
)
MINECART_TNT_STATE_IDS = (
    "minecart_tnt_fuse80_flash", "minecart_tnt_fuse79_dark",
    "minecart_tnt_fuse4_flash", "minecart_tnt_fuse5_dark",
    "minecart_tnt_unprimed", "minecart_tnt_background",
)
MINECART_VARIANT_CASES = (
    ("minecart_empty_model", "minecart_empty"),
    ("minecart_chest_model", "minecart_chest"),
    ("minecart_furnace_model", "minecart_furnace"),
    ("minecart_hopper_model", "minecart_hopper"),
    ("minecart_spawner_model", "minecart_spawner"),
    ("minecart_command_model", "minecart_command"),
)
MINECART_VARIANT_STATE_IDS = tuple(row[0] for row in MINECART_VARIANT_CASES)
BOAT_VARIANT_CASES = (
    ("boat_oak_model", 0),
    ("boat_spruce_model", 1),
    ("boat_birch_model", 2),
    ("boat_jungle_model", 3),
    ("boat_acacia_model", 4),
    ("boat_darkoak_model", 5),
)
BOAT_STATE_IDS = tuple(s for s, _ in BOAT_VARIANT_CASES) + (
    "boat_background",)
GALLERY_BACKGROUND = "entity_gallery_background"
DRAGON_BACKGROUND = "dragon_background"
DIG_BACKGROUND = "dig_background"
PROJECTILE_BACKGROUND = "projectile_background"
BAT_STATE_IDS = ("bat_flying", "bat_hanging", "bat_background")
SQUID_STATE_IDS = ("squid_swim_pose", "squid_dry_pose", "squid_background")
MOOSHROOM_STATE_IDS = (
    "mooshroom_adult_idle", "mooshroom_adult_head_pose",
    "mooshroom_child", "mooshroom_background",
)
CHEST_STATE_IDS = (
    "chest_normal_closed", "chest_normal_open",
    "chest_trapped_closed", "chest_trapped_open",
    "chest_normal_double_x_open", "chest_trapped_double_z_open",
    "chest_background",
)
SHULKER_BOX_CASES = (
    ("shulker_box_white_up_closed", 219, 1, 0.0),
    ("shulker_box_white_up_open", 219, 1, 1.0),
    ("shulker_box_orange_down_open", 220, 0, 1.0),
    ("shulker_box_purple_north_half", 229, 2, 0.5),
    ("shulker_box_blue_south_open", 230, 3, 1.0),
    ("shulker_box_red_west_open", 233, 4, 1.0),
    ("shulker_box_black_east_open", 234, 5, 1.0),
)
SHULKER_BOX_STATE_IDS = tuple(row[0] for row in SHULKER_BOX_CASES) + (
    "shulker_box_background",
)
# Subject feet in front of camera
SUBJ = {"x": CX + 0.5, "y": float(PLAT_Y + 1), "z": CZ + 4.5, "yaw": 180.0, "pitch": 0.0}
# Keep llama model ownership away from the crosshair.  A centered living
# entity changes Java's block-hit outline while the native background fixture
# keeps it, which measures selection UI rather than entity pixels.
LLAMA_SUBJ = dict(SUBJ, x=CX + 1.5)
# Dragon needs more distance
DRAGON_CAM = {
    "x": 0.5, "y": 70.0, "z": -40.5,
    "yaw": 0.0, "pitch": 15.0, "no_gravity": True,
}
DRAGON_SUBJ = {"x": 0.5, "y": 80.0, "z": 0.5, "yaw": 180.0, "pitch": 0.0}
WITHER_CAM = {
    "x": CX + 0.5, "y": float(PLAT_Y + 2), "z": CZ - 4.5,
    "yaw": 0.0, "pitch": 0.0, "no_gravity": True,
}
WITHER_SUBJ = {
    "x": CX + 0.5, "y": float(PLAT_Y + 1), "z": CZ + 5.5,
    "yaw": 180.0, "pitch": -8.0,
}
TRAP_CAM = {
    "x": CX + 0.5, "y": float(PLAT_Y + 1), "z": CZ - 4.5,
    "yaw": 0.0, "pitch": 8.0, "no_gravity": True,
}
TRAP_XS = (5.8, 7.6, 9.4, 11.2)
TRAP_Z = 10.0
LLAMA_COAT_CASES = (
    ("llama_creamy_idle", 0),
    ("llama_white_idle", 1),
    ("llama_brown_idle", 2),
    ("llama_gray_idle", 3),
)
LLAMA_DECOR_NAMES = (
    "white", "orange", "magenta", "light_blue",
    "yellow", "lime", "pink", "gray",
    "silver", "cyan", "purple", "blue",
    "brown", "green", "red", "black",
)
LLAMA_STATE_IDS = tuple(sid for sid, _ in LLAMA_COAT_CASES) + tuple(
    "llama_decor_%s" % name for name in LLAMA_DECOR_NAMES) + (
    "llama_gait", "llama_gray_decor_chest", "llama_child_decor",
    "llama_spit", "llama_background",
)
# Keep the crosshair off hanging subjects.  A centered item frame acquires
# Java's white hit outline, which turns a renderer comparison into a selection
# UI comparison.  The offset is shared by the Java capture and native metadata.
HANGING_CAM = dict(CAM, x=CX + 2.0, pitch=-10.0)
HANGING_STATE_IDS = (
    "hanging_painting_kebab", "hanging_painting_pointer",
    "hanging_frame_empty", "hanging_frame_stick", "hanging_frame_dirt",
    "hanging_frame_map",
    "hanging_leash_knot", "hanging_leashed_llama",
    "hanging_wall_background", "hanging_fence_background",
)
HANGING_FRAME_ACTION = {"hide_gui": True}


def log(msg):
    print("[ui_entities_driver] " + msg, file=sys.stderr)


def runcmds(e, cmds):
    return e._cmd({"cmd": "runcmds", "action": {"cmds": cmds}})


def set_pose(e, pose):
    return e._cmd({"cmd": "set_pose", "action": pose})


def entity_pin(e, **kwargs):
    return e._cmd({"cmd": "entity_pin", "action": kwargs})


def grab(e, path):
    r = e._cmd({"cmd": "frame", "action": {"file": path, "rerender": True}})
    if not r.get("ok"):
        raise RuntimeError("frame failed for %s: %s" % (path, r))
    if not os.path.isfile(path) or os.path.getsize(path) < 100:
        raise RuntimeError("frame file missing/empty: %s (%s)" % (path, r))
    return r


def grab_pair(e, path_a, path_b, extra=None):
    """Atomic A/B re-render on one client-thread turn (no free-running ticks)."""
    action = {"file_a": path_a, "file_b": path_b, "rerender": True}
    if extra:
        action.update(extra)
    r = e._cmd({"cmd": "frame_pair", "action": action})
    if not r.get("ok"):
        raise RuntimeError("frame_pair failed for %s/%s: %s" % (path_a, path_b, r))
    for path in (path_a, path_b):
        if not os.path.isfile(path) or os.path.getsize(path) < 100:
            raise RuntimeError("frame_pair file missing/empty: %s (%s)" % (path, r))
    return r


def settle(e, n=8):
    for _ in range(n):
        e.step({})


def place_pad(e):
    """Place stone pad + dig targets via setblocks (numeric ids).

    Vanilla /fill through runcmds often returns 0 (counted as failed) for
    unloaded chunks or no-op fills. setblocks mutates WorldServer directly.
    Block ids: 0=air, 1=stone, 2=grass.
    """
    blocks = []
    for x in range(CX - 6, CX + 7):
        for z in range(CZ - 2, CZ + 11):
            blocks.append([x, PLAT_Y, z, 1, 0])
            # clear a column of air above the pad for subject visibility
            for y in range(PLAT_Y + 1, PLAT_Y + 8):
                blocks.append([x, y, z, 0, 0])
    # dig targets (stone + grass) one block above pad
    blocks.append([CX + 2, PLAT_Y + 1, CZ + 3, 1, 0])
    blocks.append([CX + 3, PLAT_Y + 1, CZ + 3, 2, 0])
    r = e._cmd({"cmd": "setblocks", "action": {"blocks": blocks}})
    if not r.get("ok"):
        raise RuntimeError("setblocks pad failed: %s" % r)
    n = int(r.get("set") or 0)
    if n < 10:
        raise RuntimeError("setblocks pad too few blocks: %s" % r)
    log("place_pad: set=%d" % n)
    return r


def ground_visible(path):
    """True if frame is not empty sky.

    Prefer Pillow ROI stats when available; otherwise fall back to compressed
    PNG size (empty sky ~8–12 KiB, pad+subject ~40–90 KiB on this profile).
    """
    try:
        sz = os.path.getsize(path)
    except OSError:
        return False, None, 0.0
    try:
        from PIL import Image
        import numpy as np
        a = np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)
        roi = a[200:400, 200:650]
        mean = roi.mean(axis=(0, 1))
        dark = float((roi.max(axis=2) < 160).mean())
        # sky-dominant: high blue, almost no dark ground pixels
        skyish = (mean[2] > 220 and mean[1] > 190 and mean[0] > 150
                  and (mean[2] - mean[0]) > 40 and dark < 0.02)
        ok = (not skyish) and dark >= 0.05 and sz >= 20000
        return ok, (float(mean[0]), float(mean[1]), float(mean[2]), sz), dark
    except Exception:
        # stdlib-only: size is reliable for this capture profile
        ok = sz >= 25000
        return ok, ("bytes", sz), 0.0 if not ok else 1.0


def ensure_pad_rendered(e, outdir):
    """Pose + settle until a probe frame shows ground (not empty sky)."""
    probe = os.path.join(outdir, "_ground_probe.png")
    for attempt in range(12):
        set_pose(e, CAM)
        settle(e, 25)
        try:
            e._cmd({"cmd": "reload_renderers", "action": {}})
        except Exception:
            pass
        settle(e, 15)
        set_pose(e, CAM)
        grab(e, probe)
        ok, mean, dark = ground_visible(probe)
        log("ground probe attempt %d ok=%s mean=%s dark_frac=%.3f" % (
            attempt, ok, mean, dark))
        if ok:
            return True
        # re-assert pad blocks each miss (client may have missed the batch)
        place_pad(e)
        settle(e, 20)
    raise RuntimeError("client never rendered pad ground (empty sky probes)")


def base_scene(e, outdir=None):
    # 1) rules that do not need local chunks
    r0 = runcmds(e, [
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
        "gamemode 1 @a",
        # Peaceful despawns EntitySlime (onUpdate isDead). Chat may fail; entity_pin
        # also forces EnumDifficulty.EASY on the server before slime/magma spawn.
        "difficulty easy",
        "difficulty 1",
        "clear @a",
        "effect @a clear",
        "kill @e[type=!player]",
    ])
    log("base_scene rules: %s" % ({k: r0.get(k) for k in ("ok", "ran", "failed") if k in r0},))
    # 2) pose onto the pad FIRST so server loads those chunks for rendering
    #    and setblocks/summon land in loaded columns (flat spawn is far from 0,0).
    set_pose(e, CAM)
    settle(e, 60)
    # 3) pad via setblocks (not /fill — fill return codes race unloaded chunks)
    place_pad(e)
    runcmds(e, ["difficulty easy", "kill @e[type=!player]"])
    set_pose(e, CAM)
    settle(e, 40)
    # 4) do not capture entities until client has meshed the pad (avoids empty-sky goldens)
    if outdir:
        ensure_pad_rendered(e, outdir)


def capture_pair(e, outdir, state_id, pin_fn, meta_extra=None, cam=None,
                 stable_ab=True, frame_action=None):
    """pin_fn(e) -> pin reply dict; dumps A then (optionally re-pin and) dump B.

    Re-pin after settle so client has the server entity before frame{} readback.
    stable_ab=True: back-to-back A/B under the same pin (no re-spawn between
    grabs). Required for xp_orb so free-running gravity/xpColor++ cannot
    re-roll the subject; frame{} re-applies the client render pin each grab.
    Writes into outdir; caller may skip when a valid golden already exists.
    """
    os.makedirs(outdir, exist_ok=True)
    meta_dir = os.path.join(outdir, "meta")
    os.makedirs(meta_dir, exist_ok=True)
    pose = dict(cam or CAM)

    set_pose(e, pose)
    settle(e, 4)
    r1 = pin_fn(e)
    if not r1.get("ok"):
        raise RuntimeError("entity_pin A failed for %s: %s" % (state_id, r1))
    # Server spawn -> client packet: need a few ticks before re-render.
    settle(e, 8)
    set_pose(e, pose)
    r1 = pin_fn(e)
    settle(e, 6)
    set_pose(e, pose)
    path_a = os.path.join(outdir, "%s_a.png" % state_id)
    path_b = os.path.join(outdir, "%s_b.png" % state_id)
    # Atomic frame_pair is the only valid hard-pixel A/B: re-spawning or
    # advancing between captures changes entity pose, lighting, and particle
    # samples, so it measures two scenes rather than renderer noise.
    if stable_ab:
        r2 = r1
        pair = grab_pair(e, path_a, path_b, frame_action)
        particle_state = pair.get("particle_state")
        fa = dict(pair)
        fa["file"] = path_a
        fb = dict(pair)
        fb["file"] = path_b
        fa.pop("particle_state", None)
        fb.pop("particle_state", None)
    else:
        fa = grab(e, path_a)
        set_pose(e, pose)
        r2 = pin_fn(e)
        if not r2.get("ok"):
            raise RuntimeError("entity_pin B failed for %s: %s" % (state_id, r2))
        settle(e, 6)
        set_pose(e, pose)
        fb = grab(e, path_b)

    meta = {
        "id": state_id,
        "pin_reply_a": r1,
        "pin_reply_b": r2,
        "frame_a": fa,
        "frame_b": fb,
        "pose": pose,
        "width": fa.get("w"),
        "height": fa.get("h"),
        "gui_scale": 2,
        "partial_ticks": 1.0,
        "stable_ab": bool(stable_ab),
        "notes": ("A/B from qrl frame{} at partialTicks=1; client render pin "
                  "applied immediately before renderWorld for "
                  "squish/deathTicks/xp_orb"),
    }
    if meta_extra:
        meta.update(meta_extra)
    if stable_ab and particle_state is not None:
        if ((frame_action or {}).get("horse_status") is not None \
                and len(particle_state) != 7):
            raise RuntimeError("%s captured %d particles" % (
                state_id, len(particle_state)))
        if ((frame_action or {}).get("capture_dig_particles") \
                and len(particle_state) < 1):
            raise RuntimeError("%s captured no dig particles" % state_id)
        meta["particle_state"] = particle_state
    with open(os.path.join(meta_dir, "%s.json" % state_id), "w") as f:
        json.dump(meta, f, indent=2)
    log("captured %s  a=%s b=%s pin=%s frame_pin=%s/%s stable_ab=%s" % (
        state_id, fa.get("w"), fb.get("w"),
        {k: r1.get(k) for k in ("ok", "kind", "size", "squish", "death_ticks",
                                "eid", "uuid", "render_pin_armed", "value", "face")
         if k in r1},
        fa.get("render_pin"), fb.get("render_pin"), stable_ab))
    # Squish/dragon/xp goldens are worthless without a live client render pin.
    needs_pin = (state_id.endswith("_squish")
                 or state_id.startswith("dragon_death_")
                 or (state_id.startswith("wither_")
                     and state_id != "wither_empty")
                 or (state_id.startswith("horse_")
                     and state_id != "horse_background")
                 or (state_id.startswith("llama_")
                     and state_id != "llama_background")
                 or state_id.startswith("donkey_")
                 or state_id.startswith("mule_")
                 or state_id in ("bat_flying", "bat_hanging")
                 or (state_id.startswith("mooshroom_")
                     and state_id != "mooshroom_background")
                 or state_id in ("skeleton_horse", "zombie_horse",
                                 "skeleton_trap_rider", "skeleton_trap_group")
                 or state_id == "xp_orb")
    if needs_pin and not (fa.get("render_pin") and fb.get("render_pin")):
        raise RuntimeError(
            "frame{} did not apply client render pin for %s (a=%s b=%s pin=%s)" % (
                state_id, fa.get("render_pin"), fb.get("render_pin"), r1))
    if (state_id.startswith("ender_chest_")
            and state_id != "ender_chest_background"
            and not (fa.get("ender_chest_tile_pin")
                     and fb.get("ender_chest_tile_pin"))):
        raise RuntimeError(
            "frame{} did not apply Ender Chest tile render pin for %s "
            "(a=%s b=%s)" % (
                state_id, fa.get("ender_chest_tile_pin"),
                fb.get("ender_chest_tile_pin")))
    if state_id.startswith("chest_") and state_id != "chest_background":
        expected = len((frame_action or {}).get("chest_tiles") or [])
        if (fa.get("chest_tile_pin_count") != expected
                or fb.get("chest_tile_pin_count") != expected):
            raise RuntimeError(
                "frame{} did not apply every wooden Chest tile render pin "
                "for %s (expected=%s a=%s b=%s)" % (
                    state_id, expected,
                    fa.get("chest_tile_pin_count"),
                    fb.get("chest_tile_pin_count")))
    if (state_id.startswith("shulker_box_")
            and state_id != "shulker_box_background"):
        expected = len((frame_action or {}).get("shulker_box_tiles") or [])
        if (fa.get("shulker_box_tile_pin_count") != expected
                or fb.get("shulker_box_tile_pin_count") != expected):
            raise RuntimeError(
                "frame{} did not apply every Shulker Box tile render pin "
                "for %s (expected=%s a=%s b=%s)" % (
                    state_id, expected,
                    fa.get("shulker_box_tile_pin_count"),
                    fb.get("shulker_box_tile_pin_count")))
    if state_id == "beacon_world_colored":
        expected = len((frame_action or {}).get("beacon_tiles") or [])
        if (fa.get("beacon_tile_pin_count") != expected
                or fb.get("beacon_tile_pin_count") != expected):
            raise RuntimeError(
                "frame{} did not apply every Beacon tile render pin for %s "
                "(expected=%s a=%s b=%s)" % (
                    state_id, expected,
                    fa.get("beacon_tile_pin_count"),
                    fb.get("beacon_tile_pin_count")))
    if state_id.startswith("spawner_") \
            and state_id != "spawner_background":
        expected = len((frame_action or {}).get("spawner_tiles") or [])
        if (fa.get("spawner_tile_pin_count") != expected
                or fb.get("spawner_tile_pin_count") != expected):
            raise RuntimeError(
                "frame{} did not apply every Spawner tile render pin for %s "
                "(expected=%s a=%s b=%s)" % (
                    state_id, expected,
                    fa.get("spawner_tile_pin_count"),
                    fb.get("spawner_tile_pin_count")))
    if state_id == "skeleton_trap_group":
        if (fa.get("render_pin_count") != 8
                or fb.get("render_pin_count") != 8):
            raise RuntimeError(
                "skeleton trap group requires 8 atomic render pins (a=%s b=%s)" % (
                    fa.get("render_pin_count"), fb.get("render_pin_count")))
    return meta


def pin_mob(kind, size, squish, subj=None, **extra):
    s = dict(subj or SUBJ)

    def _pin(e):
        action = {
            "kind": kind, "clear": True,
            "x": s["x"], "y": s["y"], "z": s["z"],
            "yaw": s["yaw"], "pitch": s.get("pitch", 0.0),
            "size": size, "squish": squish,
        }
        action.update(extra)
        return entity_pin(e, **action)
    return _pin


def pin_bat(hanging):
    subject = {
        "x": CX + 0.5, "y": float(PLAT_Y + 2), "z": CZ + 4.5,
        "yaw": 180.0, "pitch": 0.0,
    }

    def _pin(e):
        return entity_pin(
            e, kind="bat", clear=True, hanging=bool(hanging),
            ticks_existed=40, **subject)
    return _pin


def pin_squid(squid_pitch, squid_yaw, tentacle_angle):
    subject = {
        "x": CX + 0.5, "y": float(PLAT_Y + 2), "z": CZ + 4.5,
        "yaw": 180.0, "pitch": 0.0,
    }

    def _pin(e):
        return entity_pin(
            e, kind="squid", clear=True, ticks_existed=40,
            squid_pitch=float(squid_pitch), squid_yaw=float(squid_yaw),
            tentacle_angle=float(tentacle_angle), **subject)
    return _pin


def pin_mooshroom(*, child=False, head_yaw=180.0,
                  pitch=0.0, limb_swing=0.0, limb_amount=0.0):
    subject = dict(SUBJ)

    def _pin(e):
        return entity_pin(
            e, kind="mooshroom", clear=True, ticks_existed=40,
            child=bool(child), head_yaw=float(head_yaw),
            limb_swing=float(limb_swing), limb_amount=float(limb_amount),
            x=subject["x"], y=subject["y"], z=subject["z"],
            yaw=subject["yaw"], pitch=float(pitch))
    return _pin


def pin_ender_chest(present):
    def _pin(e):
        x, y, z = ENDER_CHEST_POS
        # Metadata 2 is NORTH: the latch faces the camera at lower Z.
        return e._cmd({
            "cmd": "setblocks",
            "action": {"blocks": [[x, y, z, 130 if present else 0,
                                     2 if present else 0]]},
        })
    return _pin


def pin_spawner(present):
    def _pin(e):
        x, y, z = ENDER_CHEST_POS
        blocks = [[x, y, z, 0, 0]]
        if present:
            # Recreate the server tile on every pin so its default 20-tick
            # delay cannot reach a real spawn while capture_pair settles.
            blocks.append([x, y, z, 52, 0])
        result = e._cmd({
            "cmd": "setblocks",
            "action": {"blocks": blocks},
        })
        runcmds(e, ["kill @e[type=!player]"])
        return result
    return _pin


def spawner_saved_nbt(entity_id, *, no_ai=False):
    """Canonical TileEntityMobSpawner save tag used by the render pin."""
    x, y, z = ENDER_CHEST_POS
    entity = {
        "id": {"type": "string", "value": entity_id},
    }
    if no_ai:
        entity["NoAI"] = {"type": "byte", "value": 1}
    document = {
        "name": "",
        "tag": {"type": "compound", "value": {
            "id": {"type": "string", "value": "minecraft:mob_spawner"},
            "x": {"type": "int", "value": x},
            "y": {"type": "int", "value": y},
            "z": {"type": "int", "value": z},
            "Delay": {"type": "short", "value": 200},
            "MinSpawnDelay": {"type": "short", "value": 200},
            "MaxSpawnDelay": {"type": "short", "value": 800},
            "SpawnCount": {"type": "short", "value": 4},
            "MaxNearbyEntities": {"type": "short", "value": 6},
            "RequiredPlayerRange": {"type": "short", "value": 0},
            "SpawnRange": {"type": "short", "value": 4},
            "SpawnData": {"type": "compound", "value": entity},
            "SpawnPotentials": {
                "type": "list", "element_type": "compound", "value": [{
                    "type": "compound", "value": {
                        "Entity": {"type": "compound", "value": entity},
                        "Weight": {"type": "int", "value": 1},
                    },
                }],
            },
        }},
    }
    return nbt_codec.encode_hex(document)


def pin_wood_chest(block_id, meta, offsets):
    """Clear the tile fixture and place one exact single/double chest."""
    def _pin(e):
        bx, by, bz = ENDER_CHEST_POS
        blocks = []
        for dx in range(-1, 3):
            for dz in range(-1, 3):
                blocks.append([bx + dx, by, bz + dz, 0, 0])
        for dx, dz in offsets:
            blocks.append([bx + dx, by, bz + dz, block_id, meta])
        return e._cmd({"cmd": "setblocks", "action": {"blocks": blocks}})
    return _pin


def wood_chest_tiles(offsets, lid):
    bx, by, bz = ENDER_CHEST_POS
    return [
        {"x": bx + dx, "y": by, "z": bz + dz,
         "lid": lid, "prev_lid": lid}
        for dx, dz in offsets
    ]


def pin_shulker_box(block_id, meta):
    def _pin(e):
        bx, by, bz = ENDER_CHEST_POS
        blocks = []
        for dx in range(-1, 2):
            for dz in range(-1, 2):
                blocks.append([bx + dx, by, bz + dz, 0, 0])
        blocks.append([bx, by, bz, block_id, meta])
        reply = e._cmd({"cmd": "setblocks", "action": {"blocks": blocks}})
        # Replacing a Shulker Box runs breakBlock and spawns its item. The pin
        # is called repeatedly for one A/B state, so remove that fixture debris
        # after the replacement rather than letting dropped boxes pollute the
        # supposedly tile-only golden.
        cleanup = runcmds(e, ["kill @e[type=item]"])
        if not cleanup.get("ok"):
            raise RuntimeError("Shulker Box item cleanup failed: %s" % cleanup)
        return reply
    return _pin


def pin_beacon(present):
    """Stage the level-one Beacon plus two color-segment boundaries."""
    def _pin(e):
        bx, by, bz = BEACON_POS
        blocks = []
        for x in range(bx - 1, bx + 2):
            for z in range(bz - 1, bz + 2):
                for y in range(by - 1, by + 3):
                    blocks.append([x, y, z, 0, 0])
        if present:
            for x in range(bx - 1, bx + 2):
                for z in range(bz - 1, bz + 2):
                    blocks.append([x, by - 1, z, 42, 0])
            blocks.extend([
                [bx, by, bz, 138, 0],
                [bx, by + 1, bz, 95, 14],
                [bx, by + 2, bz, 95, 11],
            ])
        return e._cmd({"cmd": "setblocks", "action": {"blocks": blocks}})
    return _pin


def pin_dragon(death_ticks):
    s = dict(DRAGON_SUBJ)

    def _pin(e):
        return entity_pin(
            e, kind="dragon", clear=True,
            x=s["x"], y=s["y"], z=s["z"],
            yaw=s["yaw"], pitch=0.0,
            death_ticks=death_ticks,
        )
    return _pin


def pin_fireball(kind):
    s = dict(SUBJ)
    s["y"] = float(PLAT_Y + 2)

    def _pin(e):
        return entity_pin(
            e, kind=kind, clear=True,
            x=s["x"], y=s["y"], z=s["z"],
            yaw=s["yaw"], pitch=0.0,
        )
    return _pin


def pin_minecart(kind, fuse=-1):
    subject = {
        "x": CX + 0.5, "y": float(PLAT_Y + 1) + 0.0625,
        "z": CZ + 4.5, "yaw": 180.0, "pitch": 0.0,
    }

    def _pin(e):
        return entity_pin(
            e, kind=kind, clear=True,
            x=subject["x"], y=subject["y"], z=subject["z"],
            yaw=subject["yaw"], pitch=subject["pitch"], fuse=fuse,
        )
    return _pin


def pin_minecart_tnt(fuse):
    return pin_minecart("tnt_minecart", fuse=fuse)


def pin_boat(variant):
    subject = {
        "x": CX + 0.5, "y": float(PLAT_Y + 1),
        "z": CZ + 4.5, "yaw": 180.0, "pitch": 0.0,
    }

    def _pin(e):
        return entity_pin(
            e, kind="boat", clear=True, variant=variant,
            x=subject["x"], y=subject["y"], z=subject["z"],
            yaw=subject["yaw"], pitch=subject["pitch"])
    return _pin


def pin_xp():
    # Closer/higher than generic SUBJ so the 0.3-scale billboard is well inside
    # the center ROI and not buried in pad/horizon. color=0 is a green-gold
    # phase (sin≈0 → R mid, G 255, B low). Client render pin freezes pose+tint.
    s = {
        "x": CX + 0.5,
        "y": float(PLAT_Y + 2.0),
        "z": CZ + 2.5,  # ~2 blocks in front of camera at z=CX+0.5
    }

    def _pin(e):
        return entity_pin(
            e, kind="xp_orb", clear=True,
            x=s["x"], y=s["y"], z=s["z"],
            value=17, age=0, color=0,  # tier-3 sheet cell; larger on atlas
        )
    return _pin


def pin_wither(health=300.0, invul=0):
    s = dict(WITHER_SUBJ)

    def _pin(e):
        return entity_pin(
            e, kind="wither", clear=True,
            x=s["x"], y=s["y"], z=s["z"],
            yaw=s["yaw"], pitch=s["pitch"],
            health=health, invul=invul, ticks_existed=40,
            head0_yaw=135.0, head0_pitch=12.0,
            head1_yaw=225.0, head1_pitch=-18.0,
        )
    return _pin


def pin_wither_background(e):
    """Keep Wither boss lighting active while placing its model behind camera."""
    return entity_pin(
        e, kind="wither", clear=True,
        x=WITHER_CAM["x"], y=WITHER_SUBJ["y"], z=WITHER_CAM["z"] - 32.0,
        yaw=180.0, pitch=-8.0,
        health=300.0, invul=0, ticks_existed=40,
        head0_yaw=135.0, head0_pitch=12.0,
        head1_yaw=225.0, head1_pitch=-18.0)


def pin_wither_skull(invulnerable=False):
    s = {
        "x": CX + 0.5, "y": float(PLAT_Y + 3.0), "z": CZ + 2.5,
        "yaw": 32.0, "pitch": -17.0,
    }

    def _pin(e):
        return entity_pin(
            e, kind="wither_skull", clear=True,
            x=s["x"], y=s["y"], z=s["z"],
            yaw=s["yaw"], pitch=s["pitch"],
            invulnerable=bool(invulnerable), ticks_existed=20,
        )
    return _pin


def pin_horse(kind, variant=0, armor=0, saddled=False, chested=False,
              child=False, head_lean=0.0, rearing=0.0, mouth=0.0,
              limb_swing=0.0, limb_amount=0.0, tail=0, ticks_existed=40):
    s = dict(SUBJ)

    def _pin(e):
        return entity_pin(
            e, kind=kind, clear=True,
            x=s["x"], y=s["y"], z=s["z"],
            yaw=s["yaw"], pitch=s.get("pitch", 0.0),
            variant=variant, armor=armor, saddled=bool(saddled),
            chested=bool(chested), child=bool(child),
            head_lean=head_lean, rearing=rearing, mouth=mouth,
            limb_swing=limb_swing, limb_amount=limb_amount, tail=tail,
            ticks_existed=ticks_existed,
        )
    return _pin


def pin_llama(variant=0, decor=-1, chested=False, child=False,
              limb_swing=0.0, limb_amount=0.0, ticks_existed=40):
    s = dict(LLAMA_SUBJ)

    def _pin(e):
        return entity_pin(
            e, kind="llama", clear=True,
            x=s["x"], y=s["y"], z=s["z"],
            yaw=s["yaw"], pitch=s.get("pitch", 0.0),
            variant=variant, decor=decor, chested=bool(chested),
            child=bool(child), limb_swing=limb_swing,
            limb_amount=limb_amount, ticks_existed=ticks_existed,
        )
    return _pin


def pin_llama_spit():
    s = {
        "x": CX + 1.5, "y": float(PLAT_Y + 2.5), "z": CZ + 2.5,
        "yaw": 32.0, "pitch": -17.0,
    }

    def _pin(e):
        return entity_pin(
            e, kind="llama_spit", clear=True,
            x=s["x"], y=s["y"], z=s["z"],
            yaw=s["yaw"], pitch=s["pitch"], ticks_existed=20,
        )
    return _pin


def pin_skeleton(limb_swing=0.0, limb_amount=0.0, swinging_arms=False):
    """Freeze the bow-equipped rider used by a 1.11.2 skeleton trap."""
    s = dict(SUBJ)

    def _pin(e):
        return entity_pin(
            e, kind="skeleton", clear=True,
            x=s["x"], y=s["y"], z=s["z"],
            yaw=s["yaw"], pitch=s.get("pitch", 0.0),
            limb_swing=limb_swing, limb_amount=limb_amount,
            swinging_arms=bool(swinging_arms), ticks_existed=40,
        )
    return _pin


def pin_skeleton_trap_group(e):
    """Stage four real mounted skeleton-horse/rider pairs in one frame."""
    clear_trap_stage(e)
    replies = []
    for index, x in enumerate(TRAP_XS):
        horse = entity_pin(
            e, kind="skeleton_horse", clear=index == 0,
            x=x, y=float(PLAT_Y + 1), z=TRAP_Z,
            yaw=180.0, pitch=0.0, saddled=True, ticks_existed=40,
            render_pin_group=True)
        if not horse.get("ok"):
            return horse
        replies.append(horse)
        rider = entity_pin(
            e, kind="skeleton", clear=False,
            x=x, y=float(PLAT_Y + 2.2), z=TRAP_Z,
            yaw=180.0, pitch=0.0, limb_swing=0.0, limb_amount=0.0,
            swinging_arms=True, ticks_existed=40,
            vehicle_eid=horse.get("eid"), render_pin_group=True)
        if not rider.get("ok"):
            return rider
        replies.append(rider)
    result = dict(replies[-1])
    result["group_eids"] = [row.get("eid") for row in replies]
    result["group_count"] = len(replies)
    return result


def clear_trap_stage(e):
    result = e._cmd({
        "cmd": "setblocks",
        "action": {"blocks": [
            [CX + 2, PLAT_Y + 1, CZ + 3, 0, 0],
            [CX + 3, PLAT_Y + 1, CZ + 3, 0, 0],
        ]},
    })
    if not result.get("ok"):
        raise RuntimeError("trap stage clear failed: %s" % result)
    return result


def pin_skeleton_trap_background(e):
    clear_trap_stage(e)
    return pin_empty(e)


def pin_empty(e):
    """Remove non-player entities for a same-scene render baseline."""
    return e._cmd({"cmd": "killentities", "action": {}})


def stage_hanging_support(e, support):
    """Install the same wall/fence geometry used by the native candidate."""
    blocks = []
    # Remove the other fixture without cutting the y=4 pad surface.
    for x in range(CX - 3, CX + 4):
        for y in range(PLAT_Y + 1, PLAT_Y + 7):
            blocks.append([x, y, CZ + 5, 0, 0])
            blocks.append([x, y, CZ + 4, 0, 0])
    if support == "wall":
        for x in range(CX - 3, CX + 4):
            for y in range(PLAT_Y, PLAT_Y + 7):
                blocks.append([x, y, CZ + 5, 1, 0])
    elif support == "fence":
        blocks.append([CX, PLAT_Y + 3, CZ + 4, 85, 0])
    else:
        raise ValueError("unknown hanging support: %s" % support)
    result = e._cmd({"cmd": "setblocks", "action": {"blocks": blocks}})
    if not result.get("ok"):
        raise RuntimeError("hanging support failed: %s" % result)
    return result


def pin_hanging(kind, support, **values):
    def _pin(e):
        stage_hanging_support(e, support)
        action = dict(
            kind=kind, clear=True,
            hanging_x=CX, hanging_y=PLAT_Y + 3, hanging_z=CZ + 4,
        )
        action.update(values)
        return entity_pin(e, **action)
    return _pin


def pin_hanging_background(support):
    def _pin(e):
        stage_hanging_support(e, support)
        return pin_empty(e)
    return _pin


def xp_orb_visible(path):
    """True if experience_orb green-gold/yellow pixels exist near frame center."""
    try:
        from PIL import Image
        import numpy as np
        a = np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)
        # Match compare_ui_entities_oracle subject_seg xp_orb cuts (center ROI).
        r = a[:, :, 0]
        g = a[:, :, 1]
        b = a[:, :, 2]
        sky = (b > 200) & (g > 180) & (r > 140)
        pad = (np.abs(r - g) <= 6) & (np.abs(g - b) <= 6) & (r >= 90) & (r <= 150)
        green_gold = (
            (g > 90) & (g >= r - 15) & (g > b + 10)
            & (r > 40) & (b < 200) & ~sky & ~pad
        )
        yellow = (
            (r > 140) & (g > 140) & (b < r - 20) & (b < g - 20) & ~sky & ~pad
        )
        n = int((green_gold | yellow).sum())
        return n >= 8, n
    except Exception as ex:
        log("xp_orb_visible check failed: %s" % ex)
        return False, 0


def pin_dig(bx, by, bz, face=1, count=6):
    def _pin(e):
        e._cmd({"cmd": "killentities", "action": {}})
        return entity_pin(
            e, kind="dig_hit", clear=False,
            bx=bx, by=by, bz=bz, face=face, count=count,
        )
    return _pin


def place_dragon_platform(e):
    """End-stone shelf under the dragon camera after the player is posed there.

    Sky above is already air on flat worlds; only place the visible shelf.
    """
    blocks = []
    for x in range(-8, 9):
        for z in range(-50, 21):
            blocks.append([x, 60, z, 121, 0])  # end_stone
    total = 0
    bs = 2000
    for i in range(0, len(blocks), bs):
        chunk = blocks[i:i + bs]
        r = e._cmd({"cmd": "setblocks", "action": {"blocks": chunk}})
        if not r.get("ok"):
            raise RuntimeError("dragon platform setblocks failed: %s" % r)
        total += int(r.get("set") or 0)
    log("place_dragon_platform: set=%d" % total)
    return total


def _state_wanted(only, sid):
    return (not only) or (sid in only)


def _skip_if_valid(outdir, sid, skip_valid):
    if not skip_valid:
        return False
    va = os.path.join(outdir, "%s_a.png" % sid)
    vb = os.path.join(outdir, "%s_b.png" % sid)
    vm = os.path.join(outdir, "meta", "%s.json" % sid)
    if not (os.path.isfile(va) and os.path.isfile(vb) and os.path.isfile(vm)):
        return False
    # Reject tiny / sky-only frames so we do not "preserve" empty slime/magma.
    if os.path.getsize(va) < 20000 or os.path.getsize(vb) < 20000:
        log("re-capture undersized golden: %s (%d/%d bytes)" % (
            sid, os.path.getsize(va), os.path.getsize(vb)))
        return False
    ok, mean, dark = ground_visible(va)
    # Entities in sky (dragon) may fail ground_visible; dig/fireball/xp need ground.
    if sid.startswith("slime") or sid.startswith("magma") or sid.startswith("dig") \
            or sid.startswith("fireball") or sid.startswith("llama") \
            or sid == "xp_orb":
        if not ok:
            log("re-capture sky-like golden: %s mean=%s dark=%.3f" % (sid, mean, dark))
            return False
    log("skip existing non-empty golden: %s" % sid)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=25575)
    ap.add_argument("--only", nargs="*", default=None,
                    help="capture only these state ids (default: all)")
    ap.add_argument("--skip-valid", action="store_true",
                    help="do not overwrite existing non-empty a/b+meta goldens")
    args = ap.parse_args()
    only = set(args.only) if args.only else None

    e = qrl_client.NetheriteEnv(host=args.host, port=args.port)
    log("reset fresh flat seed=%d" % args.seed)
    o = e.reset(world={
        "seed": args.seed,
        "mode": "creative",
        "type": "flat",
        "structures": False,
        "fresh": True,
    }, timeout=300.0)
    if not o.get("ok"):
        raise RuntimeError("reset failed: %s" % o)
    # Mild overclock: full freeze (1) can stall client chunk meshing → empty sky frames.
    try:
        e.overclock(10)
    except Exception:
        e.overclock(1)
    # qrl writes inside the oracle process, whose cwd is not the driver's.
    # Always send absolute paths so an isolated pool instance cannot resolve a
    # relative capture under its private run directory.
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    base_scene(e, outdir=out)
    states = []

    def maybe_capture(sid, pin_fn, meta_extra=None, cam=None, stable_ab=True,
                      frame_action=None):
        if not _state_wanted(only, sid):
            log("not in --only, skip %s" % sid)
            return
        if _skip_if_valid(out, sid, args.skip_valid):
            states.append(sid)
            return
        meta = capture_pair(
            e, out, sid, pin_fn, meta_extra=meta_extra, cam=cam,
            stable_ab=stable_ab, frame_action=frame_action)
        # xp_orb: require a visible green-gold orb (never commit pad-only).
        if sid == "xp_orb":
            path_a = os.path.join(out, "xp_orb_a.png")
            ok_vis, n_gg = xp_orb_visible(path_a)
            if not ok_vis:
                raise RuntimeError(
                    "xp_orb golden has no visible orb (green_gold_px=%d); "
                    "CAPTURE_BLOCKED — not writing acceptance" % n_gg)
            log("xp_orb presence ok green_gold_px=%d render_pin=%s/%s" % (
                n_gg,
                (meta.get("frame_a") or {}).get("render_pin"),
                (meta.get("frame_b") or {}).get("render_pin")))
        states.append(sid)

    maybe_capture(
        GALLERY_BACKGROUND, pin_empty,
        meta_extra={"entity": {"type": "empty"}, "pose": CAM})

    for size in (1, 2, 4):
        sid = "slime_size%d" % size
        maybe_capture(
            sid, pin_mob("slime", size, 0.0),
            meta_extra={"entity": {"type": "slime", "size": size, "squish": 0.0,
                                   "subject": SUBJ}})

    maybe_capture(
        "slime_squish", pin_mob("slime", 2, 1.0),
        meta_extra={"entity": {"type": "slime", "size": 2, "squish": 1.0,
                               "subject": SUBJ}})

    maybe_capture(
        "slime_size2_glowing", pin_mob("slime", 2, 0.0, glowing=True),
        meta_extra={"entity": {"type": "slime", "size": 2, "squish": 0.0,
                               "glowing": True, "subject": SUBJ}})

    for size in (1, 2, 4):
        sid = "magma_size%d" % size
        maybe_capture(
            sid, pin_mob("magma_cube", size, 0.0),
            meta_extra={"entity": {"type": "magma_cube", "size": size, "squish": 0.0,
                                   "subject": SUBJ}})

    maybe_capture(
        "magma_squish", pin_mob("magma_cube", 2, 1.0),
        meta_extra={"entity": {"type": "magma_cube", "size": 2, "squish": 1.0,
                               "subject": SUBJ}})

    maybe_capture(
        "magma_size2_glowing",
        pin_mob("magma_cube", 2, 0.0, glowing=True),
        meta_extra={"entity": {"type": "magma_cube", "size": 2,
                               "squish": 0.0, "glowing": True,
                               "subject": SUBJ}})

    # Dragon: pose into high air FIRST so chunks load, then place platform, then pin.
    need_dragon = (_state_wanted(only, DRAGON_BACKGROUND)
                   or any(_state_wanted(only, "dragon_death_%d" % dt)
                          for dt in (50, 100, 190)))
    if need_dragon:
        set_pose(e, DRAGON_CAM)
        settle(e, 50)
        place_dragon_platform(e)
        set_pose(e, DRAGON_CAM)
        settle(e, 25)
        for dt in (50, 100, 190):
            sid = "dragon_death_%d" % dt
            maybe_capture(
                sid, pin_dragon(dt), cam=DRAGON_CAM,
                meta_extra={"entity": {"type": "dragon", "death_ticks": dt,
                                       "subject": DRAGON_SUBJ},
                            "pose": DRAGON_CAM})
        maybe_capture(
            DRAGON_BACKGROUND, pin_empty, cam=DRAGON_CAM,
            meta_extra={"entity": {"type": "empty"}, "pose": DRAGON_CAM})

    # Restore pad for dig/fireball/xp
    need_pad = any(_state_wanted(only, s) for s in (
        "dig_stone", "dig_grass", DIG_BACKGROUND,
        "fireball_small", "fireball_dragon", "xp_orb",
        PROJECTILE_BACKGROUND,
        *BAT_STATE_IDS,
        *SQUID_STATE_IDS,
        *MOOSHROOM_STATE_IDS,
        "ender_chest_closed", "ender_chest_open",
        "ender_chest_background",
        *CHEST_STATE_IDS,
        *SHULKER_BOX_STATE_IDS,
        *BEACON_STATE_IDS,
        *SPAWNER_STATE_IDS,
        *MINECART_TNT_STATE_IDS,
        *MINECART_VARIANT_STATE_IDS,
        *BOAT_STATE_IDS,
        "wither_normal", "wither_invul", "wither_armored",
        "wither_skull_normal", "wither_skull_invulnerable",
        "wither_empty", "wither_background",
        "horse_marked_armor", "horse_iron_idle", "horse_saddled_idle",
        "horse_eating", "horse_rearing", "horse_mouth", "horse_gait",
        "horse_tail", "horse_saddled_pose", "horse_child",
        "horse_particle_control", "horse_taming_smoke",
        "horse_breeding_heart",
        "donkey_chested_saddled", "mule_base", "skeleton_horse",
        "zombie_horse", "skeleton_trap_rider", "horse_background")
        + LLAMA_STATE_IDS + HANGING_STATE_IDS)
    if need_pad:
        base_scene(e, outdir=out)

    stone_pos = (CX + 2, PLAT_Y + 1, CZ + 3)
    grass_pos = (CX + 3, PLAT_Y + 1, CZ + 3)
    dig_cam = dict(CAM)
    dig_cam["pitch"] = 25.0
    dig_cam["z"] = CZ + 1.5
    dig_cam["x"] = CX + 2.5

    maybe_capture(
        "dig_stone",
        pin_dig(stone_pos[0], stone_pos[1], stone_pos[2], face=1, count=8),
        cam=dig_cam,
        meta_extra={"dig": {"bx": stone_pos[0], "by": stone_pos[1], "bz": stone_pos[2],
                            "face": 1, "block_id": 1, "stage": 4, "count": 8},
                    "pose": dig_cam},
        frame_action={"capture_dig_particles": True})

    maybe_capture(
        "dig_grass",
        pin_dig(grass_pos[0], grass_pos[1], grass_pos[2], face=1, count=8),
        cam=dig_cam,
        meta_extra={"dig": {"bx": grass_pos[0], "by": grass_pos[1], "bz": grass_pos[2],
                            "face": 1, "block_id": 2, "stage": 4, "count": 8},
                    "pose": dig_cam},
        frame_action={"capture_dig_particles": True})

    maybe_capture(
        DIG_BACKGROUND, pin_empty, cam=dig_cam,
        meta_extra={"entity": {"type": "empty"}, "pose": dig_cam},
        frame_action={"clear_particles": True})

    maybe_capture(
        "fireball_small", pin_fireball("small_fireball"),
        meta_extra={"entity": {"type": "small_fireball",
                               "subject": dict(SUBJ, y=PLAT_Y + 2)}},
        frame_action={"clear_particles": True})

    maybe_capture(
        "fireball_dragon", pin_fireball("dragon_fireball"),
        meta_extra={"entity": {"type": "dragon_fireball",
                               "subject": dict(SUBJ, y=PLAT_Y + 2)}},
        frame_action={"clear_particles": True})

    maybe_capture(
        "xp_orb", pin_xp(), stable_ab=True,
        meta_extra={"entity": {
            "type": "xp_orb", "value": 17, "age": 0, "color": 0,
            "subject": {
                "x": CX + 0.5, "y": float(PLAT_Y + 2.0), "z": CZ + 2.5,
            }}})

    maybe_capture(
        PROJECTILE_BACKGROUND, pin_empty,
        meta_extra={"entity": {"type": "empty"}, "pose": CAM})

    bat_subject = {
        "x": CX + 0.5, "y": float(PLAT_Y + 2), "z": CZ + 4.5,
        "yaw": 180.0, "pitch": 0.0,
    }
    bat_cam = dict(CAM, pitch=0.0)
    for sid, hanging in (("bat_flying", False), ("bat_hanging", True)):
        maybe_capture(
            sid, pin_bat(hanging), cam=bat_cam,
            frame_action={"hide_gui": True, "clear_particles": True},
            meta_extra={
                "entity": dict(
                    bat_subject, type="bat", hanging=hanging,
                    ticks_existed=40, subject=bat_subject),
                "pose": bat_cam, "clean_world_render": True,
            })
    maybe_capture(
        "bat_background", pin_empty, cam=bat_cam,
        frame_action={"hide_gui": True, "clear_particles": True},
        meta_extra={"entity": {"type": "empty"}, "pose": bat_cam,
                    "clean_world_render": True})

    squid_subject = {
        "x": CX + 0.5, "y": float(PLAT_Y + 2), "z": CZ + 4.5,
        "yaw": 180.0, "pitch": 0.0,
    }
    squid_cam = dict(CAM, pitch=0.0)
    squid_cases = (
        ("squid_swim_pose", 35.0, 25.0, 0.72),
        ("squid_dry_pose", -70.0, 11.0, 0.30),
    )
    for sid, squid_pitch, squid_yaw, tentacle in squid_cases:
        maybe_capture(
            sid, pin_squid(squid_pitch, squid_yaw, tentacle), cam=squid_cam,
            frame_action={"hide_gui": True, "clear_particles": True},
            meta_extra={
                "entity": dict(
                    squid_subject, type="squid", ticks_existed=40,
                    squid_pitch=squid_pitch, squid_yaw=squid_yaw,
                    tentacle_angle=tentacle, subject=squid_subject),
                "pose": squid_cam, "clean_world_render": True,
            })
    maybe_capture(
        "squid_background", pin_empty, cam=squid_cam,
        frame_action={"hide_gui": True, "clear_particles": True},
        meta_extra={"entity": {"type": "empty"}, "pose": squid_cam,
                    "clean_world_render": True})

    mooshroom_cam = dict(CAM, pitch=12.0)
    mooshroom_cases = (
        ("mooshroom_adult_idle", False, 180.0, 0.0, 0.0, 0.0),
        ("mooshroom_adult_head_pose", False, 224.0, -18.0, 1.3, 0.72),
        ("mooshroom_child", True, 202.0, 8.0, 0.7, 0.5),
    )
    for sid, child, head_yaw, pitch, limb_swing, limb_amount in mooshroom_cases:
        subject = dict(SUBJ, pitch=pitch)
        maybe_capture(
            sid, pin_mooshroom(
                child=child, head_yaw=head_yaw, pitch=pitch,
                limb_swing=limb_swing, limb_amount=limb_amount),
            cam=mooshroom_cam,
            frame_action={"hide_gui": True, "clear_particles": True},
            meta_extra={
                "entity": dict(
                    subject, type="mooshroom", child=child,
                    ticks_existed=40, head_yaw=head_yaw,
                    limb_swing=limb_swing, limb_amount=limb_amount,
                    subject=subject),
                "pose": mooshroom_cam, "clean_world_render": True,
            })
    maybe_capture(
        "mooshroom_background", pin_empty, cam=mooshroom_cam,
        frame_action={"hide_gui": True, "clear_particles": True},
        meta_extra={"entity": {"type": "empty"}, "pose": mooshroom_cam,
                    "clean_world_render": True})

    ender_tile = {
        "x": ENDER_CHEST_POS[0], "y": ENDER_CHEST_POS[1],
        "z": ENDER_CHEST_POS[2], "meta": 2,
    }
    for sid, lid in (("ender_chest_closed", 0.0),
                     ("ender_chest_open", 1.0)):
        tile = dict(ender_tile, lid=lid, prev_lid=lid)
        maybe_capture(
            sid, pin_ender_chest(True), cam=ENDER_CHEST_CAM,
            frame_action={"hide_gui": True, "clear_particles": True,
                          "ender_chest_tile": tile},
            meta_extra={"tile": tile, "pose": ENDER_CHEST_CAM,
                        "clean_world_render": True})
    maybe_capture(
        "ender_chest_background", pin_ender_chest(False),
        cam=ENDER_CHEST_CAM,
        frame_action={"hide_gui": True, "clear_particles": True},
        meta_extra={"tile": dict(ender_tile, present=False),
                    "pose": ENDER_CHEST_CAM,
                    "clean_world_render": True})

    chest_cases = (
        ("chest_normal_closed", 54, 2, ((0, 0),), 0.0),
        ("chest_normal_open", 54, 2, ((0, 0),), 1.0),
        ("chest_trapped_closed", 146, 2, ((0, 0),), 0.0),
        ("chest_trapped_open", 146, 2, ((0, 0),), 1.0),
        ("chest_normal_double_x_open", 54, 2,
         ((0, 0), (1, 0)), 1.0),
        ("chest_trapped_double_z_open", 146, 5,
         ((0, 0), (0, 1)), 1.0),
    )
    for sid, block_id, meta, offsets, lid in chest_cases:
        tiles = wood_chest_tiles(offsets, lid)
        maybe_capture(
            sid, pin_wood_chest(block_id, meta, offsets),
            cam=ENDER_CHEST_CAM,
            frame_action={"hide_gui": True, "clear_particles": True,
                          "chest_tiles": tiles},
            meta_extra={
                "tiles": tiles, "block_id": block_id, "meta": meta,
                "pose": ENDER_CHEST_CAM, "clean_world_render": True,
            })
    maybe_capture(
        "chest_background", pin_wood_chest(0, 0, ()),
        cam=ENDER_CHEST_CAM,
        frame_action={"hide_gui": True, "clear_particles": True},
        meta_extra={"tiles": [], "present": False,
                    "pose": ENDER_CHEST_CAM,
                    "clean_world_render": True})

    for sid, block_id, meta, progress in SHULKER_BOX_CASES:
        tile = {
            "x": ENDER_CHEST_POS[0], "y": ENDER_CHEST_POS[1],
            "z": ENDER_CHEST_POS[2], "progress": progress,
            "prev_progress": progress,
        }
        maybe_capture(
            sid, pin_shulker_box(block_id, meta), cam=ENDER_CHEST_CAM,
            frame_action={
                "hide_gui": True, "clear_particles": True,
                "shulker_box_tiles": [tile],
            },
            meta_extra={
                "tile": tile, "block_id": block_id, "meta": meta,
                "color": block_id - 219, "pose": ENDER_CHEST_CAM,
                "clean_world_render": True,
            })
    maybe_capture(
        "shulker_box_background", pin_shulker_box(0, 0),
        cam=ENDER_CHEST_CAM,
        frame_action={"hide_gui": True, "clear_particles": True},
        meta_extra={"present": False, "pose": ENDER_CHEST_CAM,
                    "clean_world_render": True})

    beacon_tile = {
        "x": BEACON_POS[0], "y": BEACON_POS[1], "z": BEACON_POS[2],
        "render_scale_before": 0.975,
    }
    maybe_capture(
        "beacon_world_colored", pin_beacon(True), cam=BEACON_CAM,
        frame_action={
            "hide_gui": True, "clear_particles": True,
            "beacon_tiles": [beacon_tile],
        },
        meta_extra={
            "beacon": dict(beacon_tile, levels=1, glass=[14, 11]),
            "pose": BEACON_CAM, "clean_world_render": True,
        })
    maybe_capture(
        "beacon_world_background", pin_beacon(False), cam=BEACON_CAM,
        frame_action={"hide_gui": True, "clear_particles": True},
        meta_extra={
            "beacon": {"present": False},
            "pose": BEACON_CAM, "clean_world_render": True,
        })

    spawner_tile = {
        "x": ENDER_CHEST_POS[0], "y": ENDER_CHEST_POS[1],
        "z": ENDER_CHEST_POS[2],
        "mob_rotation": 0.0, "prev_mob_rotation": 0.0,
    }
    for sid, entity_id, no_ai in (
            ("spawner_pig_saved", "minecraft:pig", False),
            ("spawner_zombie_noai_saved", "minecraft:zombie", True)):
        tile = dict(
            spawner_tile,
            entity_id=entity_id,
            nbt=spawner_saved_nbt(entity_id, no_ai=no_ai))
        maybe_capture(
            sid, pin_spawner(True), cam=ENDER_CHEST_CAM,
            frame_action={
                "hide_gui": True, "clear_particles": True,
                "spawner_tiles": [tile],
            },
            meta_extra={
                "tile": tile, "pose": ENDER_CHEST_CAM,
                "clean_world_render": True,
            })
    maybe_capture(
        "spawner_background", pin_spawner(False), cam=ENDER_CHEST_CAM,
        frame_action={"hide_gui": True, "clear_particles": True},
        meta_extra={"present": False, "pose": ENDER_CHEST_CAM,
                    "clean_world_render": True})

    minecart_subject = {
        "x": CX + 0.5, "y": float(PLAT_Y + 1) + 0.0625,
        "z": CZ + 4.5, "yaw": 180.0, "pitch": 0.0,
    }
    for sid, fuse in (
            ("minecart_tnt_fuse80_flash", 80),
            ("minecart_tnt_fuse79_dark", 79),
            ("minecart_tnt_fuse4_flash", 4),
            ("minecart_tnt_fuse5_dark", 5),
            ("minecart_tnt_unprimed", -1)):
        maybe_capture(
            sid, pin_minecart_tnt(fuse), cam=CAM,
            frame_action={"hide_gui": True, "clear_particles": True},
            meta_extra={
                "entity": dict(
                    minecart_subject, type="tnt_minecart", fuse=fuse),
                "pose": CAM, "clean_world_render": True,
            })
    maybe_capture(
        "minecart_tnt_background", pin_empty, cam=CAM,
        frame_action={"hide_gui": True, "clear_particles": True},
        meta_extra={"entity": {"type": "empty"}, "pose": CAM,
                    "clean_world_render": True})
    for sid, kind in MINECART_VARIANT_CASES:
        maybe_capture(
            sid, pin_minecart(kind), cam=CAM,
            frame_action={"hide_gui": True, "clear_particles": True},
            meta_extra={
                "entity": dict(minecart_subject, type=kind),
                "pose": CAM, "clean_world_render": True,
            })
    boat_subject = {
        "x": CX + 0.5, "y": float(PLAT_Y + 1),
        "z": CZ + 4.5, "yaw": 180.0, "pitch": 0.0,
    }
    for sid, variant in BOAT_VARIANT_CASES:
        maybe_capture(
            sid, pin_boat(variant), cam=CAM,
            frame_action={"hide_gui": True, "clear_particles": True},
            meta_extra={
                "entity": dict(boat_subject, type="boat", variant=variant),
                "pose": CAM, "clean_world_render": True,
            })
    maybe_capture(
        "boat_background", pin_empty, cam=CAM,
        frame_action={"hide_gui": True, "clear_particles": True},
        meta_extra={"entity": {"type": "empty"}, "pose": CAM,
                    "clean_world_render": True})

    hanging_common = {
        "hanging_x": CX, "hanging_y": PLAT_Y + 3, "hanging_z": CZ + 4,
        "facing": 2, "support": "wall",
    }
    for sid, art in (("hanging_painting_kebab", 0),
                     ("hanging_painting_pointer", 21)):
        entity = dict(hanging_common, type="painting", art=art)
        maybe_capture(
            sid, pin_hanging("painting", "wall", facing=2, art=art),
            cam=HANGING_CAM,
            frame_action=HANGING_FRAME_ACTION,
            meta_extra={"entity": entity, "pose": HANGING_CAM,
                        "clean_world_render": True})
    for sid, item, rotation in (
            ("hanging_frame_empty", 0, 0),
            ("hanging_frame_stick", 280, 3),
            ("hanging_frame_dirt", 3, 1),
            ("hanging_frame_map", 358, 0)):
        entity = dict(
            hanging_common, type="item_frame", item=item, meta=0,
            rotation=rotation)
        maybe_capture(
            sid, pin_hanging(
                "item_frame", "wall", facing=2, item=item, meta=0,
                rotation=rotation),
            cam=HANGING_CAM,
            frame_action=HANGING_FRAME_ACTION,
            meta_extra={"entity": entity, "pose": HANGING_CAM,
                        "clean_world_render": True})
    fence_common = {
        "hanging_x": CX, "hanging_y": PLAT_Y + 3, "hanging_z": CZ + 4,
        "support": "fence",
    }
    maybe_capture(
        "hanging_leash_knot", pin_hanging("leash_knot", "fence"),
        cam=HANGING_CAM,
        frame_action=HANGING_FRAME_ACTION,
        meta_extra={"entity": dict(fence_common, type="leash_knot"),
                    "pose": HANGING_CAM, "clean_world_render": True})
    llama_subject = {
        "x": CX + 2.5, "y": float(PLAT_Y + 1), "z": CZ + 2.5,
        "yaw": 180.0, "pitch": 0.0,
    }
    maybe_capture(
        "hanging_leashed_llama",
        pin_hanging(
            "leashed_llama", "fence", x=llama_subject["x"],
            y=llama_subject["y"], z=llama_subject["z"],
            yaw=llama_subject["yaw"], pitch=0.0,
            variant=0, decor=-1, ticks_existed=40),
        cam=HANGING_CAM,
        frame_action=HANGING_FRAME_ACTION,
        meta_extra={"entity": dict(
            fence_common, type="leashed_llama", variant=0, decor=-1,
            ticks_existed=40, **llama_subject), "pose": HANGING_CAM,
            "clean_world_render": True})
    maybe_capture(
        "hanging_wall_background", pin_hanging_background("wall"),
        cam=HANGING_CAM,
        frame_action=HANGING_FRAME_ACTION,
        meta_extra={"entity": dict(
            hanging_common, type="hanging_background"),
            "pose": HANGING_CAM, "clean_world_render": True})
    maybe_capture(
        "hanging_fence_background", pin_hanging_background("fence"),
        cam=HANGING_CAM,
        frame_action=HANGING_FRAME_ACTION,
        meta_extra={"entity": dict(
            fence_common, type="hanging_background"),
            "pose": HANGING_CAM, "clean_world_render": True})

    horse_cases = [
        ("horse_marked_armor", "horse",
         dict(variant=6 | (4 << 8), armor=3)),
        ("horse_iron_idle", "horse", dict(variant=258, armor=1)),
        ("horse_saddled_idle", "horse", dict(variant=258, saddled=True)),
        ("horse_eating", "horse", dict(variant=258, head_lean=0.7)),
        ("horse_rearing", "horse", dict(variant=258, rearing=0.4)),
        ("horse_mouth", "horse", dict(variant=258, mouth=0.8)),
        ("horse_gait", "horse",
         dict(variant=258, limb_swing=3.0, limb_amount=0.65)),
        ("horse_tail", "horse", dict(variant=258, tail=1)),
        ("horse_saddled_pose", "horse",
         dict(variant=258, armor=1, saddled=True, head_lean=0.7,
              rearing=0.4, mouth=0.8, limb_swing=3.0,
              limb_amount=0.65, tail=1)),
        ("horse_child", "horse", dict(variant=4, child=True)),
        ("donkey_chested_saddled", "donkey",
         dict(saddled=True, chested=True)),
        ("mule_base", "mule", {}),
        ("skeleton_horse", "skeleton_horse", {}),
        ("zombie_horse", "zombie_horse", {}),
    ]
    for sid, kind, values in horse_cases:
        horse_meta = dict(values, ticks_existed=40)
        horse_meta.update({"type": kind, "subject": SUBJ})
        maybe_capture(
            sid, pin_horse(kind, **values),
            meta_extra={"entity": horse_meta})
    skeleton_meta = {
        "type": "skeleton", "ticks_existed": 40,
        "limb_swing": 0.0, "limb_amount": 0.0,
        "swinging_arms": True, "held_item": 261, "armor_head": 306,
        "subject": SUBJ,
    }
    maybe_capture(
        "skeleton_trap_rider", pin_skeleton(swinging_arms=True),
        meta_extra={"entity": skeleton_meta})
    trap_entities = []
    for x in TRAP_XS:
        trap_entities.append({
            "type": "skeleton_horse", "ticks_existed": 40,
            "saddled": True, "ridden": True,
            "subject": {"x": x, "y": float(PLAT_Y + 1), "z": TRAP_Z,
                        "yaw": 180.0, "pitch": 0.0},
        })
        trap_entities.append({
            "type": "skeleton", "ticks_existed": 40,
            "limb_swing": 0.0, "limb_amount": 0.0,
            "swinging_arms": True, "held_item": 261, "armor_head": 306,
            "vehicle": "previous", "mounted_y_offset": 1.2,
            "subject": {"x": x, "y": float(PLAT_Y + 2.2), "z": TRAP_Z,
                        "yaw": 180.0, "pitch": 0.0},
        })
    maybe_capture(
        "skeleton_trap_group", pin_skeleton_trap_group, cam=TRAP_CAM,
        meta_extra={"entity": {"type": "skeleton_trap_group"},
                    "entities": trap_entities, "pose": TRAP_CAM})
    maybe_capture(
        "skeleton_trap_group_background", pin_skeleton_trap_background,
        cam=TRAP_CAM,
        meta_extra={"entity": {"type": "empty"}, "pose": TRAP_CAM})
    particle_horse = dict(variant=258, ticks_existed=40)
    maybe_capture(
        "horse_particle_control", pin_horse("horse", **particle_horse),
        meta_extra={"entity": dict(
            particle_horse, type="horse", subject=SUBJ,
            particle_status=0)})
    for sid, status in (("horse_taming_smoke", 6),
                        ("horse_breeding_heart", 7)):
        horse_meta = dict(particle_horse, type="horse", subject=SUBJ,
                          particle_status=status)
        maybe_capture(
            sid, pin_horse("horse", **particle_horse),
            meta_extra={"entity": horse_meta},
            frame_action={"horse_status": status})
    maybe_capture(
        "horse_background", pin_empty,
        meta_extra={"entity": {"type": "empty"}, "pose": CAM})

    for sid, variant in LLAMA_COAT_CASES:
        llama_meta = {
            "type": "llama", "variant": variant, "decor": -1,
            "chested": False, "child": False, "ticks_existed": 40,
            "subject": LLAMA_SUBJ,
        }
        maybe_capture(
            sid, pin_llama(variant=variant),
            meta_extra={"entity": llama_meta})
    for decor, name in enumerate(LLAMA_DECOR_NAMES):
        sid = "llama_decor_%s" % name
        llama_meta = {
            "type": "llama", "variant": 0, "decor": decor,
            "chested": False, "child": False, "ticks_existed": 40,
            "subject": LLAMA_SUBJ,
        }
        maybe_capture(
            sid, pin_llama(variant=0, decor=decor),
            meta_extra={"entity": llama_meta})
    llama_gait = {
        "type": "llama", "variant": 2, "decor": -1,
        "chested": False, "child": False, "limb_swing": 3.0,
        "limb_amount": 0.65, "ticks_existed": 40,
        "subject": LLAMA_SUBJ,
    }
    maybe_capture(
        "llama_gait",
        pin_llama(variant=2, limb_swing=3.0, limb_amount=0.65),
        meta_extra={"entity": llama_gait})
    llama_chest = {
        "type": "llama", "variant": 3, "decor": 15,
        "chested": True, "child": False, "ticks_existed": 40,
        "subject": LLAMA_SUBJ,
    }
    maybe_capture(
        "llama_gray_decor_chest",
        pin_llama(variant=3, decor=15, chested=True),
        meta_extra={"entity": llama_chest})
    llama_child = {
        "type": "llama", "variant": 1, "decor": 3,
        "chested": True, "child": True, "ticks_existed": 40,
        "subject": LLAMA_SUBJ,
    }
    maybe_capture(
        "llama_child_decor",
        pin_llama(variant=1, decor=3, chested=True, child=True),
        meta_extra={"entity": llama_child})
    spit_subject = {
        "x": CX + 1.5, "y": float(PLAT_Y + 2.5), "z": CZ + 2.5,
        "yaw": 32.0, "pitch": -17.0,
    }
    maybe_capture(
        "llama_spit", pin_llama_spit(),
        meta_extra={"entity": {
            "type": "llama_spit", "ticks_existed": 20,
            "subject": spit_subject,
        }})
    maybe_capture(
        "llama_background", pin_empty,
        meta_extra={"entity": {"type": "empty"}, "pose": CAM})

    wither_common = {
        "ticks_existed": 40,
        "head0_yaw": 135.0, "head0_pitch": 12.0,
        "head1_yaw": 225.0, "head1_pitch": -18.0,
        "subject": WITHER_SUBJ,
    }
    maybe_capture(
        "wither_normal", pin_wither(health=300.0, invul=0), cam=WITHER_CAM,
        meta_extra={"entity": dict(wither_common, type="wither",
                                   health=300.0, invul=0),
                    "pose": WITHER_CAM})
    maybe_capture(
        "wither_invul", pin_wither(health=300.0, invul=220), cam=WITHER_CAM,
        meta_extra={"entity": dict(wither_common, type="wither",
                                   health=300.0, invul=220),
                    "pose": WITHER_CAM})
    maybe_capture(
        "wither_armored", pin_wither(health=150.0, invul=0), cam=WITHER_CAM,
        meta_extra={"entity": dict(wither_common, type="wither",
                                   health=150.0, invul=0),
                    "pose": WITHER_CAM})
    maybe_capture(
        "wither_empty", pin_empty, cam=WITHER_CAM,
        meta_extra={"entity": {"type": "empty"}, "pose": WITHER_CAM})
    background_subject = dict(WITHER_SUBJ)
    background_subject.update({
        "x": WITHER_CAM["x"], "y": WITHER_SUBJ["y"],
        "z": WITHER_CAM["z"] - 32.0,
    })
    maybe_capture(
        "wither_background", pin_wither_background, cam=WITHER_CAM,
        meta_extra={"entity": dict(wither_common, type="wither",
                                   health=300.0, invul=0,
                                   subject=background_subject),
                    "pose": WITHER_CAM})
    skull_subject = {
        "x": CX + 0.5, "y": float(PLAT_Y + 3.0), "z": CZ + 2.5,
        "yaw": 32.0, "pitch": -17.0,
    }
    skull_cam = dict(CAM)
    skull_cam["pitch"] = -10.0
    maybe_capture(
        "wither_skull_normal", pin_wither_skull(False), cam=skull_cam,
        meta_extra={"entity": {"type": "wither_skull", "invulnerable": 0,
                               "ticks_existed": 20, "subject": skull_subject},
                    "pose": skull_cam})
    maybe_capture(
        "wither_skull_invulnerable", pin_wither_skull(True), cam=skull_cam,
        meta_extra={"entity": {"type": "wither_skull", "invulnerable": 1,
                               "ticks_existed": 20, "subject": skull_subject},
                    "pose": skull_cam})

    # Merge with any pre-existing states when using --only / --skip-valid
    all_ids = [
        "slime_size1", "slime_size2", "slime_size4", "slime_squish",
        "slime_size2_glowing",
        GALLERY_BACKGROUND,
        "magma_size1", "magma_size2", "magma_size4", "magma_squish",
        "magma_size2_glowing",
        "dragon_death_50", "dragon_death_100", "dragon_death_190",
        DRAGON_BACKGROUND,
        "dig_stone", "dig_grass", DIG_BACKGROUND,
        "fireball_small", "fireball_dragon", "xp_orb",
        PROJECTILE_BACKGROUND,
        *BAT_STATE_IDS,
        *SQUID_STATE_IDS,
        *MOOSHROOM_STATE_IDS,
        "ender_chest_closed", "ender_chest_open",
        "ender_chest_background",
        *CHEST_STATE_IDS,
        *SHULKER_BOX_STATE_IDS,
        *BEACON_STATE_IDS,
        *SPAWNER_STATE_IDS,
        *MINECART_TNT_STATE_IDS,
        *MINECART_VARIANT_STATE_IDS,
        "wither_normal", "wither_invul", "wither_armored",
        "wither_empty", "wither_background",
        "wither_skull_normal", "wither_skull_invulnerable",
        "horse_marked_armor", "horse_iron_idle", "horse_saddled_idle",
        "horse_eating", "horse_rearing", "horse_mouth", "horse_gait",
        "horse_tail", "horse_saddled_pose", "horse_child",
        "horse_particle_control", "horse_taming_smoke",
        "horse_breeding_heart",
        "donkey_chested_saddled", "mule_base", "skeleton_horse",
        "zombie_horse", "skeleton_trap_rider", "skeleton_trap_group",
        "skeleton_trap_group_background",
    ] + list(LLAMA_STATE_IDS) + list(HANGING_STATE_IDS)
    present = []
    for sid in all_ids:
        if (os.path.isfile(os.path.join(out, "%s_a.png" % sid))
                and os.path.isfile(os.path.join(out, "%s_b.png" % sid))):
            present.append(sid)

    manifest = {
        "profile": "ui_entities_oracle",
        "seed": args.seed,
        "world": "flat",
        "width": 854,
        "height": 480,
        "states": present if present else states,
        "captured_this_run": states,
        "captured_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    with open(os.path.join(out, "capture_manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    log("manifest: %d present, %d this run" % (len(manifest["states"]), len(states)))
    e.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
