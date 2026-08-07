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
import qrl_client  # noqa: E402

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
# Subject feet in front of camera
SUBJ = {"x": CX + 0.5, "y": float(PLAT_Y + 1), "z": CZ + 4.5, "yaw": 180.0, "pitch": 0.0}
# Dragon needs more distance
DRAGON_CAM = {
    "x": 0.5, "y": 70.0, "z": -40.5,
    "yaw": 0.0, "pitch": 15.0, "no_gravity": True,
}
DRAGON_SUBJ = {"x": 0.5, "y": 80.0, "z": 0.5, "yaw": 180.0, "pitch": 0.0}


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


def grab_pair(e, path_a, path_b):
    """Atomic A/B re-render on one client-thread turn (no free-running ticks)."""
    r = e._cmd({"cmd": "frame_pair", "action": {
        "file_a": path_a, "file_b": path_b, "rerender": True,
    }})
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
                 stable_ab=True):
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
        pair = grab_pair(e, path_a, path_b)
        fa = dict(pair)
        fa["file"] = path_a
        fb = dict(pair)
        fb["file"] = path_b
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
                 or state_id == "xp_orb")
    if needs_pin and not (fa.get("render_pin") and fb.get("render_pin")):
        raise RuntimeError(
            "frame{} did not apply client render pin for %s (a=%s b=%s pin=%s)" % (
                state_id, fa.get("render_pin"), fb.get("render_pin"), r1))
    return meta


def pin_mob(kind, size, squish, subj=None):
    s = dict(subj or SUBJ)

    def _pin(e):
        return entity_pin(
            e, kind=kind, clear=True,
            x=s["x"], y=s["y"], z=s["z"],
            yaw=s["yaw"], pitch=s.get("pitch", 0.0),
            size=size, squish=squish,
        )
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
            or sid.startswith("fireball") or sid == "xp_orb":
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
    out = args.out
    base_scene(e, outdir=out)
    states = []

    def maybe_capture(sid, pin_fn, meta_extra=None, cam=None, stable_ab=True):
        if not _state_wanted(only, sid):
            log("not in --only, skip %s" % sid)
            return
        if _skip_if_valid(out, sid, args.skip_valid):
            states.append(sid)
            return
        meta = capture_pair(
            e, out, sid, pin_fn, meta_extra=meta_extra, cam=cam,
            stable_ab=stable_ab)
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

    # Dragon: pose into high air FIRST so chunks load, then place platform, then pin.
    need_dragon = any(_state_wanted(only, "dragon_death_%d" % dt)
                      for dt in (50, 100, 190))
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

    # Restore pad for dig/fireball/xp
    need_pad = any(_state_wanted(only, s) for s in (
        "dig_stone", "dig_grass", "fireball_small", "fireball_dragon", "xp_orb"))
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
                    "pose": dig_cam})

    maybe_capture(
        "dig_grass",
        pin_dig(grass_pos[0], grass_pos[1], grass_pos[2], face=1, count=8),
        cam=dig_cam,
        meta_extra={"dig": {"bx": grass_pos[0], "by": grass_pos[1], "bz": grass_pos[2],
                            "face": 1, "block_id": 2, "stage": 4, "count": 8},
                    "pose": dig_cam})

    maybe_capture(
        "fireball_small", pin_fireball("small_fireball"),
        meta_extra={"entity": {"type": "small_fireball",
                               "subject": dict(SUBJ, y=PLAT_Y + 2)}})

    maybe_capture(
        "fireball_dragon", pin_fireball("dragon_fireball"),
        meta_extra={"entity": {"type": "dragon_fireball",
                               "subject": dict(SUBJ, y=PLAT_Y + 2)}})

    maybe_capture(
        "xp_orb", pin_xp(), stable_ab=True,
        meta_extra={"entity": {
            "type": "xp_orb", "value": 17, "age": 0, "color": 0,
            "subject": {
                "x": CX + 0.5, "y": float(PLAT_Y + 2.0), "z": CZ + 2.5,
            }}})

    # Merge with any pre-existing states when using --only / --skip-valid
    all_ids = [
        "slime_size1", "slime_size2", "slime_size4", "slime_squish",
        "magma_size1", "magma_size2", "magma_size4", "magma_squish",
        "dragon_death_50", "dragon_death_100", "dragon_death_190",
        "dig_stone", "dig_grass",
        "fireball_small", "fireball_dragon", "xp_orb",
    ]
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
