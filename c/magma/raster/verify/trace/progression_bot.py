#!/usr/bin/env python3
"""progression_bot.py - scripted oracle playthrough, taped segment by segment.

The full-game verify loop: drive the REAL game (qrl bridge, paced steps so the
renderer tapes fresh frames) through the progression segments below, record
each as a tape (tape.py), then replay through magma (replay_tape.py --cuda
--report) and pixel-match. Fix the top divergence, re-run the segment, repeat
until the route to the dragon kill verifies end to end.

Cheats are allowed by design (this is a render/sim verifier, not a speedrun):
/fill arenas, /give gear, tp pose pinning for exact aim. What must stay
organic is everything the tape replays: movement inputs, block placement,
portal transits, bow draws.

Usage (game live with the qrl bridge up):
  uv run --no-project --with pyarrow python progression_bot.py --segment overworld
  uv run --no-project --with pyarrow python progression_bot.py --segment nether
  uv run --no-project --with pyarrow python progression_bot.py --segment endportal
  uv run --no-project --with pyarrow python progression_bot.py --segment dragon
  ... --replay   also replay+report through magma afterwards (GPU1)
"""
import argparse
import math
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import oracle_lib as ol  # noqa: E402

PACE = 0.06  # s/step; unpaced stepping tapes stale duplicate frames


class Bridge:
    """Reconnectable qrl handle. Segments close over THIS object, so main can
    drop the connection for tape.py (single-client bridge) and reconnect
    without stranding the closures on a dead socket."""

    def __init__(self):
        self.env = None

    def connect(self):
        self.close()
        self.env = ol.connect()

    def close(self):
        if self.env is not None:
            self.env.close()
            self.env = None

    def _cmd(self, obj):
        return self.env._cmd(obj)

    def step(self, action):
        return self.env.step(action)

    def obs(self):
        return self.env.obs()

    def reset(self, world=None, timeout=120.0):
        return self.env.reset(world, timeout)
SPAWN = (44.5, 68.0, 176.5)   # seed-0 world spawn area (spawns.py pose)
BASE_Y = 120                  # sky platforms live above spawn chunks


def run(env, ticks, **fields):
    for _ in range(ticks):
        env.step(fields)
        time.sleep(PACE)


def face(env, yaw, pitch):
    """Exact facing via tp self-pin (obs pose is authoritative next tick)."""
    o = env.obs()
    ol.runcmds(env, [f"tp @a {o['x']:.6f} {o['y']:.6f} {o['z']:.6f} "
                     f"{yaw:.4f} {pitch:.4f}"])
    run(env, 2)


def face_point(env, tx, ty, tz, eye=1.62):
    o = env.obs()
    dx, dy, dz = tx - o["x"], ty - (o["y"] + eye), tz - o["z"]
    yaw = math.degrees(math.atan2(-dx, dz))
    pitch = math.degrees(-math.atan2(dy, math.hypot(dx, dz)))
    face(env, yaw, pitch)
    return yaw, pitch


def cmds(env, lines, pin=None):
    """Run commands one by one. pin=(x,y,z[,yaw,pitch]) re-teleports the player
    after EVERY command: a fresh sky arena has no floor until its fill lands,
    and an unpinned player free-falls during the command gaps (build tape
    122726Z: died on spawn terrain mid-setup, wiping the given inventory; the
    final settle masked it by teleporting the respawned player back up).
    /tp also resets fallDistance, so the pin doubles as fall-damage rescue."""
    for c in lines:
        print(ol.runcmds(env, [c]), "<-", c)
        if pin is not None:
            p = tuple(pin) + (0.0, 10.0)[max(0, len(pin) - 3):]
            ol.runcmds(env, [f"tp @a {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} "
                             f"{p[3]:.4f} {p[4]:.4f}"])


def settle(env, x, y, z, yaw=0.0, pitch=10.0):
    ol.hold_pose(env, x, y, z, yaw, pitch, settle_ticks=40)


def ensure_dim(env, target=0):
    """A previous segment can leave the save's player in another dimension;
    tp-based settle stays in the CURRENT dim, so setup must fix it first
    (the 095331Z tape built its 'overworld' arena inside the Nether)."""
    if env.obs().get("dim") == target:
        return
    # qrl nesting quirk: command params live under "action", not top-level
    print(f"ensure_dim: -> {target}",
          env._cmd({"cmd": "dim", "action": {"id": target}}))
    for _ in range(60):
        o = env.step({})
        time.sleep(PACE)
        if o.get("dim") == target:
            return
    raise SystemExit(f"ensure_dim: still dim {o.get('dim')}, wanted {target}")


def respawn_reset(env):
    """The qrl dim command's End->overworld transfer leaves the server player
    unregistered in the destination world: @a/@p/tp/replaceitem/effect all
    report 'found nothing' while obs/physics keep working (observed 160617Z:
    every overworld replaceitem failed, so build had no cobble). Entering the
    End re-registers, but 1->0 always breaks. A void death + auto-respawn
    recreates a fresh registered EntityPlayerMP at the overworld spawn.
    SETUP ONLY (pre-recstart): death clears inventory and effects.

    Robust from ANY prior state: an unregistered player is not even TICKED
    server-side (observed falling at y=-5800 at full hp - void damage never
    applied), but a transfer INTO the End always re-registers. So park the
    player, hop into the End (via the overworld if already in the End), and
    void-die there; the respawn recreates a fresh overworld player."""
    def hop(target):
        env._cmd({"cmd": "set_pose", "action": {
            "x": 0.0, "y": 80.0, "z": 0.0,
            "yaw": 0.0, "pitch": 0.0, "no_gravity": True}})
        env._cmd({"cmd": "dim", "action": {"id": target}})
        for _ in range(120):
            o = env.step({})
            time.sleep(PACE)
            if o.get("dim") == target:
                return
        raise SystemExit(f"respawn_reset: dim hop to {target} failed")

    if env.obs().get("dim") == 1:
        hop(0)
    hop(1)
    env._cmd({"cmd": "set_pose", "action": {
        "x": 0.0, "y": -30.0, "z": 0.0,
        "yaw": 0.0, "pitch": 0.0, "no_gravity": False}})
    for _ in range(400):
        o = env.step({})
        time.sleep(PACE)
        if (o.get("dim") == 0 and not o.get("dead")
                and o.get("health", 0.0) >= 20.0 and o.get("y", -99) > 0):
            break
    else:
        raise SystemExit("respawn_reset: no overworld respawn after void death")
    run(env, 10)
    if not ol.runcmds(env, ["testfor @a"]).get("ran"):
        raise SystemExit("respawn_reset: selectors still dead after respawn")
    print("respawn_reset: player re-registered in overworld")


def find_ent(env, substr, exact=False):
    """Nearest loaded entity whose type contains (or equals, exact=True)
    substr. exact matters for the dragon: substr "Dragon" also matches
    EntityDragonFireball (health -1), which read as "dragon dead" mid-fight."""
    r = ol.get_entities(env)
    best, bd = None, None
    p = r.get("player")
    for e in r["ents"]:
        t = e.get("type", "")
        if (t != substr) if exact else (substr not in t):
            continue
        if p is None:
            return e
        d = ((e["x"] - p["x"]) ** 2 + (e["y"] - p["y"]) ** 2
             + (e["z"] - p["z"]) ** 2)
        if bd is None or d < bd:
            best, bd = e, d
    return best


def bow_shot(env, tx, ty, tz, lead=(0.0, 0.0, 0.0), draw=24, flight=30):
    """Aim (tp pin) and fire one full-draw arrow. Organic draw/release."""
    face_point(env, tx + lead[0], ty + lead[1], tz + lead[2])
    run(env, draw, use=1)
    run(env, 2)
    run(env, flight)


# --------------------------------------------------------------------------
# Segments. Each: (setup builds/teleports BEFORE taping starts, drive is the
# taped play). Chat feedback must expire before taping -> sleep(11) at the end
# of setup.

def seg_overworld(env):
    """Organic locomotion at spawn terrain: walk, sprint, jump, look around."""
    def setup():
        ensure_dim(env, 0)
        settle(env, *SPAWN)
        time.sleep(11)

    def drive():
        run(env, 20)
        run(env, 60, forward=1)                     # walk
        run(env, 60, forward=1, sprint=1)           # sprint
        run(env, 30, forward=1, jump=1)             # jump-run terrain
        for d in (1, 1, -1, -1):                    # look around (15 deg steps)
            run(env, 6, yaw=d)
        run(env, 40, forward=1, sprint=1, jump=1)
        run(env, 20, back=1)
        run(env, 30, left=1)
        run(env, 30, right=1)
        run(env, 20)
    return setup, drive


def seg_mine(env):
    """Hold-to-break: chop a 2-log tree trunk, tunnel a 1-thick stone wall,
    dig 2 down. Exercises break-progress, block removal, drops, pickup."""
    px, pz = int(SPAWN[0]), int(SPAWN[2]) + 24
    y = BASE_Y

    def setup():
        ensure_dim(env, 0)
        settle(env, px + 0.5, float(y), pz + 0.5)
        cmds(env, [
            f"fill {px - 3} {y} {pz - 5} {px + 10} {y + 6} {pz + 5} air 0",
            # 4-thick floor so the dig-down finale has stone to stand in
            f"fill {px - 3} {y - 4} {pz - 5} {px + 10} {y - 1} {pz + 5} stone 0",
            # 2-log trunk 3 ahead, wall 6 ahead (3x3, 1 thick)
            f"fill {px + 3} {y} {pz} {px + 3} {y + 1} {pz} log 0",
            f"fill {px + 6} {y} {pz - 1} {px + 6} {y + 2} {pz + 1} stone 0",
            "replaceitem entity @a slot.hotbar.0 minecraft:iron_axe 1 0",
            "replaceitem entity @a slot.hotbar.1 minecraft:iron_pickaxe 1 0",
        ], pin=(px + 0.5, float(y), pz + 0.5))
        time.sleep(11)
        settle(env, px + 0.5, float(y), pz + 0.5, yaw=-90.0)  # face +X

    def drive():
        run(env, 20, hotbar=0)
        # trunk: bottom log then top log (iron axe ~15 ticks each)
        face_point(env, px + 3.5, y + 0.5, pz + 0.5)
        run(env, 30, attack=1)
        face_point(env, px + 3.5, y + 1.5, pz + 0.5)
        run(env, 30, attack=1)
        run(env, 30, forward=1)          # walk through, pick up the drops
        run(env, 10)
        # wall: head block then feet block, walk the tunnel
        run(env, 5, hotbar=1)
        face_point(env, px + 6.5, y + 1.5, pz + 0.5)
        run(env, 25, attack=1)
        face_point(env, px + 6.5, y + 0.5, pz + 0.5)
        run(env, 25, attack=1)
        run(env, 30, forward=1)
        run(env, 10)
        # dig down 2 (classic straight-down)
        face(env, -90.0, 90.0)
        for _ in range(2):
            run(env, 25, attack=1)
            run(env, 10)
        for d in (1, 1, -1, -1):
            run(env, 6, yaw=d)
        run(env, 20)
    return setup, drive


def seg_build(env, cobble_slot=0):
    """Block placement: jump-place a pillar, then sneak-bridge off the top.
    Exercises placement raycast, AO/light recompute, sneak edge-guard."""
    px, pz = int(SPAWN[0]), int(SPAWN[2]) - 24
    y = BASE_Y

    def setup():
        ensure_dim(env, 0)
        settle(env, px + 0.5, float(y), pz + 0.5)
        cmds(env, [
            f"fill {px - 6} {y} {pz - 6} {px + 6} {y + 14} {pz + 6} air 0",
            f"fill {px - 6} {y - 1} {pz - 6} {px + 6} {y - 1} {pz + 6} stone 0",
            f"replaceitem entity @a slot.hotbar.{cobble_slot} "
            "minecraft:cobblestone 64 0",
        ], pin=(px + 0.5, float(y), pz + 0.5))
        time.sleep(11)
        settle(env, px + 0.5, float(y), pz + 0.5, yaw=0.0)

    def drive():
        run(env, 20, hotbar=cobble_slot)
        face(env, 0.0, 90.0)             # straight down
        start_y = env.obs()["y"]
        run(env, 110, jump=1, use=1)     # hold-jump-place pillar
        run(env, 20)
        print("pillar height:", env.obs()["y"] - start_y)
        for d in (1, 1, 1, 1, -1, -1, -1, -1):
            run(env, 6, yaw=d)
        face(env, 0.0, 75.0)             # bridge: sneak-back placing at edge
        run(env, 90, back=1, sneak=1, use=1)
        run(env, 20)
        for d in (1, 1, -1, -1):
            run(env, 6, yaw=d)
        run(env, 20)
    return setup, drive


def seg_nether(env):
    """Portal transit: light a frame, stand in, roam the Nether, return."""
    px, pz = int(SPAWN[0]) + 6, int(SPAWN[2])      # frame base near spawn
    y = BASE_Y

    def walk_until(cond, max_ticks, **fields):
        for _ in range(max_ticks):
            o = env.step(fields)
            time.sleep(PACE)
            if cond(o):
                return o
        return env.obs()

    def setup():
        ensure_dim(env, 0)
        settle(env, px - 4.5, float(y), pz + 0.5)
        cmds(env, [
            # clear earlier arenas (cobble walls etc.) around the pad first
            f"fill {px - 8} {y} {pz - 6} {px + 8} {y + 8} {pz + 6} air 0",
            f"fill {px - 6} {y - 1} {pz - 5} {px + 6} {y - 1} {pz + 5} stone 0",
            # portal INTERIOR must start at feet level (y) or the walk-in
            # hits the obsidian base row and the transit never fires
            f"fill {px} {y - 1} {pz - 1} {px} {y + 3} {pz + 2} obsidian 0",
            # meta 2 = axis Z (pane spans z); "portal 0" bakes axis X, which
            # fails the frame check here so the blocks pop on neighbor update
            f"fill {px} {y} {pz} {px} {y + 2} {pz + 1} portal 2",
        ], pin=(px - 4.5, float(y), pz + 0.5))
        time.sleep(11)
        settle(env, px - 4.5, float(y), pz + 0.5, yaw=-90.0)  # face +X, portal ahead

    def wait_transit(target_dim, max_ticks):
        """Stand in the pane and let the NATURAL vanilla transit fire (80-tick
        survival counter). With the portal axis correct the server-side AABB
        collision works even headless; portal_touch pulses are NOT used here -
        pulsing setPortal/timeUntilPortal every tick raced the transfer and
        left the 100112Z return unscaled at raw nether coords with no
        Teleporter placement."""
        for _ in range(max_ticks):
            o = env.step({})
            time.sleep(PACE)
            if o.get("dim") == target_dim:
                # wait out the loading screen: the transit-tick obs is a
                # stale frozen pose; report the arrival only once stable
                last = None
                for _ in range(40):
                    o = env.step({})
                    time.sleep(PACE)
                    p = (round(o["x"], 3), round(o["y"], 3), round(o["z"], 3))
                    if p == last:
                        return o
                    last = p
                return o
        return None

    def pulse_transit(target_dim, max_ticks):
        """Stand in the portal pulsing portal_touch each tick. Headless
        lockstep misses the server-side AABB collision callback, so the qrl
        portal_touch command re-dispatches the REAL BlockPortal collision
        handler per tick (survival counter still needs its 80 ticks)."""
        said = None
        for _ in range(max_ticks):
            try:
                r = env._cmd({"cmd": "portal_touch"})
            except Exception as e:
                r = {"error": str(e)}
            msg = r.get("error")
            if msg != said:
                print("portal_touch:", r)
                said = msg
            o = env.step({})
            time.sleep(PACE)
            if o.get("dim") == target_dim:
                # the transit-tick obs can be a stale loading pose (095648Z
                # returned a PREVIOUS session's portal coords and the bot
                # later teleported into solid rock); wait until the pose
                # stabilises post-loading before reporting the arrival
                last = None
                for _ in range(40):
                    o = env.step({})
                    time.sleep(PACE)
                    p = (round(o["x"], 3), round(o["y"], 3), round(o["z"], 3))
                    if p == last:
                        return o
                    last = p
                return o
        return None

    def drive():
        run(env, 20)
        # walk to the portal and stop INSIDE the pane: the portal collision
        # box is only the middle 0.375..0.625 slab of the cell, so release
        # forward early and let the momentum slide land the bb over it
        walk_until(lambda o: o["x"] >= px - 0.15, 120, forward=1)
        run(env, 12)                     # slide settles inside the pane
        arr = wait_transit(-1, 220)      # arrival pose = inside the exit portal
        if arr is None:
            run(env, 20)                 # no transit: end the tape safely
            return
        run(env, 10, forward=1)          # step OFF the exit pane immediately -
        off = env.obs()                  # idling in it re-arms the transit
        run(env, 50)
        run(env, 15, forward=1)
        for d in (1, 1, 1, -1, -1, -1):  # look around the Nether
            run(env, 6, yaw=d)
        # Return by WALKING back into the pane: a teleport-entry (set_pose)
        # transit fires but lands at RAW unscaled coords with no Teleporter
        # placement (100112Z/100953Z), while a walked entry places correctly
        # at the x8-paired portal (verified live 2026-07-13). Teleport only
        # to the step-off pose, then re-enter on foot.
        settle(env, off["x"], off["y"], off["z"],
               yaw=(off["yaw"] + 180.0) % 360.0, pitch=10.0)
        walk_until(lambda o: o.get("dim") == 0 or
                   abs(o["x"] - arr["x"]) + abs(o["z"] - arr["z"]) < 0.35,
                   40, forward=1)
        for _ in range(30):              # slid past the thin pane: creep back
            o = env.obs()
            if o.get("dim") == 0:
                break
            if abs(o["x"] - arr["x"]) + abs(o["z"] - arr["z"]) < 0.35:
                break
            run(env, 1, back=1, sneak=1)
        if wait_transit(0, 200) is None:
            run(env, 20)
            return
        run(env, 40)
    return setup, drive


def seg_pearl(env, pearl_slot=0, eye_slot=1):
    """Projectile throws: two ender pearls (teleport lands), one eye of ender
    (rise + hover). New ghost types for entity_render (like arrows)."""
    px, pz = int(SPAWN[0]) + 24, int(SPAWN[2])
    y = BASE_Y

    def setup():
        ensure_dim(env, 0)
        settle(env, px + 0.5, float(y), pz + 0.5)
        cmds(env, [
            f"fill {px - 4} {y} {pz - 8} {px + 30} {y + 10} {pz + 8} air 0",
            f"fill {px - 4} {y - 1} {pz - 8} {px + 30} {y - 1} {pz + 8} stone 0",
            f"replaceitem entity @a slot.hotbar.{pearl_slot} "
            "minecraft:ender_pearl 16 0",
            f"replaceitem entity @a slot.hotbar.{eye_slot} "
            "minecraft:ender_eye 16 0",
        ], pin=(px + 0.5, float(y), pz + 0.5))
        time.sleep(11)
        settle(env, px + 0.5, float(y), pz + 0.5, yaw=-90.0)  # face +X (long pad)

    def drive():
        run(env, 20, hotbar=pearl_slot)
        for _ in range(2):               # pearl arc + teleport + fall damage tick
            face(env, -90.0, -10.0)
            run(env, 1, use=1)
            run(env, 60)
        run(env, 10, hotbar=eye_slot)    # eye of ender: rises, hovers, drops
        face(env, -90.0, -30.0)
        run(env, 1, use=1)
        run(env, 100)
        for d in (1, 1, -1, -1):
            run(env, 6, yaw=d)
        run(env, 20)
    return setup, drive


def seg_endportal(env):
    """End entry: a 3x3 end_portal pad, walk in, look around the End island."""
    px, pz = int(SPAWN[0]) - 10, int(SPAWN[2])
    y = BASE_Y

    def setup():
        ensure_dim(env, 0)
        settle(env, px + 6.5, float(y), pz + 0.5)
        cmds(env, [
            # clear leftover structures (a selector-dead run's build phase
            # once bridged cobble across this walk path at x=40, 163654Z)
            f"fill {px - 4} {y} {pz - 4} {px + 8} {y + 8} {pz + 4} air 0",
            f"fill {px - 4} {y - 1} {pz - 4} {px + 8} {y - 1} {pz + 4} stone 0",
            f"fill {px - 1} {y} {pz - 1} {px + 1} {y} {pz + 1} end_portal 0",
        ], pin=(px + 6.5, float(y), pz + 0.5))
        time.sleep(11)
        settle(env, px + 6.5, float(y), pz + 0.5, yaw=90.0)  # face -X toward pad

    def drive():
        run(env, 20)
        run(env, 80, forward=1)          # walk onto the pad -> End
        run(env, 160)                    # End arrival (obsidian platform) render
        run(env, 40, forward=1)          # walk off the platform
        for d in (1, 1, 1, 1, -1, -1, -1, -1):
            run(env, 6, yaw=d)
        run(env, 60, forward=1, sprint=1)
        run(env, 30)
    return setup, drive


def seg_crystal(env):
    """End: pillar-up a cobble tower on the platform, bow the nearest end
    crystal until it explodes. Crystal is not living (health -1): death =
    eid disappearing from getentities."""
    target = {}

    def setup():
        ensure_dim(env, 1)
        cmds(env, [
            "replaceitem entity @a slot.hotbar.0 minecraft:bow 1 0",
            "replaceitem entity @a slot.hotbar.1 minecraft:cobblestone 64 0",
            "replaceitem entity @a slot.hotbar.8 minecraft:arrow 64 0",
            "effect @a minecraft:regeneration 1000000 4 true",
        ])
        c = find_ent(env, "EnderCrystal")
        if c:
            # staging pad ~22 blocks from the crystal at its height, built in
            # SETUP so the tick-0 snapshot carries it (the recorder only
            # captures entities near the player; the 132311Z attempt shot
            # from the spawn platform 73 blocks out and taped nothing).
            target.update(c)
            px = int(c["x"]) + 0.5
            py = int(c["y"]) + 1
            pz = int(c["z"]) + 22 + 0.5
            cmds(env, [f"fill {int(px) - 2} {py - 1} {int(pz) - 2} "
                       f"{int(px) + 2} {py - 1} {int(pz) + 2} obsidian 0"],
                 pin=(px, float(py), pz))
            settle(env, px, float(py), pz, yaw=180.0, pitch=0.0)
        else:
            time.sleep(11)

    def drive():
        run(env, 20)
        if not target:
            print("no crystal loaded; ending tape")
            run(env, 20)
            return
        c = target
        print("crystal target:", c)
        run(env, 10, hotbar=0)
        for shot in range(12):
            bow_shot(env, c["x"], c["y"] + 1.0, c["z"], flight=25)
            live = ol.get_entities(env)["ents"]
            if not any(e["eid"] == c["eid"] for e in live):
                print(f"crystal destroyed after {shot + 1} shots")
                break
        run(env, 60)                     # explosion aftermath renders
    return setup, drive


def seg_dragon(env, bow_slot=0):
    """End fight: Power V bow, velocity-lead loop until the dragon dies, then
    hold through the death animation (beams, fade, XP, exit portal).

    Aim is cheat-assisted (face_point / obs-entity lead); draws and releases
    are organic inputs so the tape replays them.
    """
    def setup():
        # get into the End first; entry method doesn't matter for this tape -
        # its header starts in dim 1 and magma replays from the header.
        ensure_dim(env, 1)
        cmds(env, [
            # Power X: dragon body parts take damage/4+1 (head only is full),
            # so Power V body hits were ~6 hp of 200
            f"replaceitem entity @a slot.hotbar.{bow_slot} minecraft:bow 1 0 "
            "{ench:[{id:48,lvl:10}]}",
            "replaceitem entity @a slot.hotbar.8 minecraft:arrow 64 0",
            "replaceitem entity @a slot.hotbar.7 minecraft:arrow 64 0",
            "replaceitem entity @a slot.hotbar.6 minecraft:arrow 64 0",
            "replaceitem entity @a slot.hotbar.5 minecraft:arrow 64 0",
            "effect @a minecraft:regeneration 1000000 4 true",
            # dragon charges one-shot the fight otherwise: on 142155Z the
            # player was knocked off the pad and chipped to death at t=1303
            # mid-tape. Resistance V = 100% damage reduction (void still kills
            # but the pad is 40 blocks inside the island edge).
            "effect @a minecraft:resistance 1000000 4 true",
            # shoot from island center: from the spawn platform (x=100) rd8
            # unloads the far half of the orbit and the dragon "vanished"
            # from the entity list mid-fight (141118Z tape, t=1234)
            "fill -2 74 -42 2 74 -38 obsidian 0",
        ])
        settle(env, 0.5, 75.0, -39.5, yaw=180.0, pitch=-20.0)
        # crystals heal the dragon (+1 hp / 10 ticks observed on 142155Z);
        # kill them from CENTER so every pillar crystal is chunk-loaded (the
        # kill from the spawn platform matched 0 and left 2 alive)
        for _ in range(5):
            cmds(env, ["kill @e[type=ender_crystal]"])
            run(env, 10)
            if find_ent(env, "Crystal") is None:
                break
        else:
            print("WARNING: crystals still alive after 5 kill passes")
        time.sleep(11)

    def drive():
        def flight_ticks(dist):
            # horizontal range with drag .99: x(t) = 3*(1-.99^t)/.01
            f = 1.0 - dist * 0.01 / 3.0
            if f <= 0.05:
                return 30
            return max(4, int(math.log(f) / math.log(0.99)) + 1)

        def arrow_drop(ticks):
            # cumulative gravity sag over the flight (0.05/tick, drag .99)
            vy, y = 0.0, 0.0
            for _ in range(ticks):
                y += vy
                vy = (vy - 0.05) * 0.99
            return -y

        run(env, 20, hotbar=bow_slot)
        stall, last_hp, missing = 0, None, 0
        for shot in range(300):
            d0 = find_ent(env, "EntityDragon", exact=True)
            if d0 is None:
                missing += 1
                if missing >= 6:
                    print("dragon missing from entity list 6 polls in a row")
                    break
                run(env, 20)
                continue
            missing = 0
            if d0["health"] <= 0.0:
                last_hp = 0.0
                break
            # perched at the fountain: arrows deflect off a stationary
            # dragon (and it sits low, y~65 near 0,0) - hold fire, wait
            if d0["y"] < 70.0 and math.hypot(d0["x"], d0["z"]) < 14.0:
                run(env, 40)
                continue
            if last_hp is not None and d0["health"] >= last_hp:
                stall += 1
            else:
                stall = 0
            last_hp = d0["health"]
            if stall >= 5:
                print("hp stalled (perched?); waiting out")
                run(env, 100)
                stall = 0
                continue
            o0 = env.obs()
            if math.sqrt((d0["x"] - o0["x"]) ** 2 + (d0["y"] - o0["y"]) ** 2
                         + (d0["z"] - o0["z"]) ** 2) > 70.0:
                # 256 arrows, no mid-tape refill; past ~70 blocks the sag
                # compensation and turn-rate error make hits rare - hold
                run(env, 20)
                continue
            # draw FIRST, aim 2 ticks before release: face_point before the
            # 26-tick draw left the launch ~30 blocks behind the dragon
            # (143353Z: 0/5 hits even at dist 10-14)
            run(env, 14, use=1)           # charge toward full draw
            da = find_ent(env, "EntityDragon", exact=True) or d0
            run(env, 8, use=1)            # two samples -> velocity estimate
            d1 = find_ent(env, "EntityDragon", exact=True) or da
            o = env.obs()
            dist = math.sqrt((d1["x"] - o["x"]) ** 2
                             + (d1["y"] - o["y"]) ** 2
                             + (d1["z"] - o["z"]) ** 2)
            ft = flight_ticks(dist)
            print(f"shot {shot}: hp={d0['health']:.1f} at "
                  f"({d1['x']:.1f},{d1['y']:.1f},{d1['z']:.1f}) "
                  f"dist={dist:.0f} ft={ft}")
            # lead per-tick velocity over (flight + 3 remaining draw/release
            # ticks); aim up for arrow drop plus +3.0 into the body slab
            # (dragonPartBody is 8x8 anchored at posY = AABB bottom edge)
            vt = ft + 3.0
            lead = (vt * (d1["x"] - da["x"]) / 8.0,
                    vt * (d1["y"] - da["y"]) / 8.0 + arrow_drop(ft) + 3.0,
                    vt * (d1["z"] - da["z"]) / 8.0)
            face_point(env, d1["x"] + lead[0], d1["y"] + lead[1],
                       d1["z"] + lead[2])
            run(env, 2, use=1)            # settle the pin, still drawn
            run(env, 2)                   # release
            run(env, ft + 6)              # arrow flight
        d = find_ent(env, "EntityDragon", exact=True)
        dead = (last_hp is not None and last_hp <= 0.0) or (
            d is not None and d["health"] <= 0.0)
        if dead:
            print("DRAGON DEAD - holding for death animation")
            run(env, 220)                # full death animation + XP + portal
        elif d is None:
            print("dragon unloaded but never seen at hp<=0 - NOT a kill")
            run(env, 40)
        else:
            print(f"dragon still alive at hp={d['health']:.1f}")
            run(env, 40)
    return setup, drive


def seg_e2e(env):
    """Milestone 9: every mechanic class in ONE tape on one save. Chains the
    segment drives (locomotion, mining, building, projectiles, Nether portal
    roundtrip, End entry, crystal, dragon kill) with in-tape tp transitions
    (tp pose rows replay like face()). ALL staging - arenas in every dim, a
    summoned crystal + a fresh Health:30 dragon (this save's fight dragon is
    dead), unified gear - happens BEFORE recstart so the world snapshot
    carries it.

    Unified hotbar: 0 axe, 1 pick, 2 cobble, 3 pearl, 4 eye, 5 Power-X bow,
    6/7/8 arrows. mine keeps its native 0/1; others take slot params.
    """
    DP = (0.5, 75.0, -39.5, 180.0, -20.0)         # End dragon/crystal pad
    CRYS = (12.5, 75.0, -20.5)                     # summoned crystal (d~22)
    ow = seg_overworld(env)
    mn = seg_mine(env)
    bd = seg_build(env, cobble_slot=2)
    pl = seg_pearl(env, pearl_slot=3, eye_slot=4)
    nt = seg_nether(env)
    ep = seg_endportal(env)
    dr = seg_dragon(env, bow_slot=5)
    # settle poses replicated from the segs' literals (SPAWN/BASE_Y derived)
    sx, sz, y = int(SPAWN[0]), int(SPAWN[2]), float(BASE_Y)
    HOMES = {
        "mine":      (sx + 0.5, y, sz + 24 + 0.5, -90.0, 10.0),
        "build":     (sx + 0.5, y, sz - 24 + 0.5, 0.0, 10.0),
        "pearl":     (sx + 24 + 0.5, y, sz + 0.5, -90.0, 10.0),
        "nether":    (sx + 6 - 4.5, y, sz + 0.5, -90.0, 10.0),
        "endportal": (sx - 10 + 6.5, y, sz + 0.5, 90.0, 10.0),
    }

    def setup():
        # Normalize to a KNOWN-GOOD state first: a prior run can leave the
        # player unregistered (selectors dead) with the End world unloaded
        # (fills there fail "outside of the world"). Void-death gives a fresh
        # registered overworld player; 0->1 then registers properly in the
        # End and the pad settle loads the staging chunks.
        respawn_reset(env)
        # End staging first (needs dim 1), then the overworld arenas.
        ensure_dim(env, 1)
        settle(env, 0.5, 75.0, -39.5, yaw=180.0, pitch=0.0)
        cmds(env, [
            # killed crystals EXPLODE (strength 6); protect the player first
            "effect @a minecraft:resistance 1000000 4 true",
            "kill @e[type=ender_crystal]",         # leftovers heal the dragon
            "kill @e[type=arrow]",
            "fill -2 74 -42 2 74 -38 obsidian 0",  # dragon pad (idempotent)
        ])
        # Fresh FIGHT dragon (the save's died in 144207Z; a /summon dragon is
        # ignored by DragonFightManager once dragonKilled - no boss bar/fog).
        # Official ritual: 4 crystal ENTITIES at exitPortal.up().offset(f,2),
        # scan runs only from ItemEndCrystal placement -> summon 3, place the
        # 4th organically. Skipped when a live dragon survived a prior run.
        if find_ent(env, "EntityDragon", exact=True) is None:
            # Locate the podium by its central bedrock pillar (the dragon egg
            # is one-time: the ceremony's podium regen clears it and no new
            # egg drops once previouslyKilled). Pillar top = exitPortal.y+3,
            # scan cells at up(1) -> ritual crystals sit at top-2.
            top = None
            for yy in range(90, 50, -1):
                if ol.runcmds(env, [f"testforblock 0 {yy} 0 bedrock"]) \
                        .get("ran"):
                    top = yy
                    break
            if top is None:
                raise SystemExit("e2e setup: no bedrock pillar at (0,*,0) - "
                                 "cannot locate the exit portal")
            ry = top - 2
            print(f"e2e setup: pillar top y={top}, ritual crystals at y={ry}")
            cmds(env, [
                f"summon ender_crystal 2.5 {ry} 0.5 {{ShowBottom:0b}}",
                f"summon ender_crystal -1.5 {ry} 0.5 {{ShowBottom:0b}}",
                f"summon ender_crystal 0.5 {ry} -1.5 {{ShowBottom:0b}}",
                # 4th (south, +z): click the podium's own bedrock rim at
                # (0,ry-1,3); crystal spawns at (0.5,ry,3.5) over cell (0,ry,2)
                "replaceitem entity @a slot.hotbar.0 minecraft:end_crystal 1 0",
                # ceremony end explodes all 4 ritual crystals (strength 6)
                "effect @a minecraft:resistance 1000000 4 true",
            ])
            # Stand ON the terrain shelf adjacent to the rim block and click
            # its TOP face from above; from farther back the ray dips below
            # y=ry and hits the end-stone shelf at (0,ry-1,4), not bedrock.
            settle(env, 0.5, float(ry), 4.5, yaw=180.0, pitch=60.0)
            run(env, 10, hotbar=0)
            face_point(env, 0.5, ry - 0.1, 3.5)    # enters rim top face
            run(env, 2, use=1)
            run(env, 5)
            # retreat before the ceremony's crystal explosions
            settle(env, 0.5, 75.0, -39.5, yaw=180.0, pitch=0.0)
            # ceremony: beams sweep the pillars, crystals regenerate, dragon
            # spawns at full hp. Wait for the dragon, then trim the fight.
            for _ in range(120):
                if find_ent(env, "EntityDragon", exact=True) is not None:
                    break
                run(env, 10)
            if find_ent(env, "EntityDragon", exact=True) is None:
                raise SystemExit("e2e setup: respawn ritual produced no dragon")
            print("e2e setup: dragon respawned, trimming fight")
            run(env, 100)                          # let pillar crystals finish
        else:
            print("e2e setup: live dragon found, skipping respawn ritual")
        cmds(env, [
            "kill @e[type=ender_crystal]",         # incl. regenerated cages
            "entitydata @e[type=ender_dragon] {Health:30f}",
            # crystal step target (re-summon AFTER the purge)
            "fill 10 74 -22 14 74 -18 obsidian 0",
            "summon ender_crystal 12.5 75 -20.5 {ShowBottom:0b}",
        ])
        run(env, 10)
        # Leave the End via void-death respawn, NOT ensure_dim(0): the qrl
        # 1->0 transfer breaks selector registration (see respawn_reset).
        respawn_reset(env)
        # overworld arenas (each seg settles itself; gear overridden below)
        for s in (mn, bd, pl, nt, ep):
            s[0]()
        cmds(env, [
            "replaceitem entity @a slot.hotbar.0 minecraft:iron_axe 1 0",
            "replaceitem entity @a slot.hotbar.1 minecraft:iron_pickaxe 1 0",
            "replaceitem entity @a slot.hotbar.2 minecraft:cobblestone 64 0",
            "replaceitem entity @a slot.hotbar.3 minecraft:ender_pearl 16 0",
            "replaceitem entity @a slot.hotbar.4 minecraft:ender_eye 16 0",
            "replaceitem entity @a slot.hotbar.5 minecraft:bow 1 0 "
            "{ench:[{id:48,lvl:10}]}",
            "replaceitem entity @a slot.hotbar.6 minecraft:arrow 64 0",
            "replaceitem entity @a slot.hotbar.7 minecraft:arrow 64 0",
            "replaceitem entity @a slot.hotbar.8 minecraft:arrow 64 0",
            "effect @a minecraft:regeneration 1000000 4 true",
            "effect @a minecraft:resistance 1000000 4 true",
        ])
        ow[0]()                                    # tape starts at SPAWN

    def goto(home):
        settle(env, *home)

    def drive():
        print("e2e: overworld");  ow[1]()
        print("e2e: mine");       goto(HOMES["mine"]);      mn[1]()
        print("e2e: build");      goto(HOMES["build"]);     bd[1]()
        print("e2e: pearl");      goto(HOMES["pearl"]);     pl[1]()
        print("e2e: nether");     goto(HOMES["nether"]);    nt[1]()
        print("e2e: endportal");  goto(HOMES["endportal"]); ep[1]()
        o = env.obs()
        if o.get("dim") != 1:
            print("e2e: END ENTRY FAILED (dim %s), stopping" % o.get("dim"))
            run(env, 20)
            return
        print("e2e: crystal")
        settle(env, *DP)
        run(env, 20, hotbar=5)
        for shot in range(10):
            if find_ent(env, "Crystal") is None:
                print("e2e: crystal destroyed after %d shots" % shot)
                break
            # crystal AABB is 2x2 from its base; aim mid-body + sag comp
            bow_shot(env, *CRYS, lead=(0.0, 2.0, 0.0), flight=14)
        run(env, 60)                               # explosion aftermath
        print("e2e: dragon")
        settle(env, *DP)
        dr[1]()
    return setup, drive


def _dump_region(env, radius=2):
    """dumpblocks around the player -> (raw, cx0, cz0, ncx). Square region."""
    import tempfile
    f = tempfile.NamedTemporaryFile(suffix=".raw", delete=False)
    f.close()
    db = env._cmd({"cmd": "dumpblocks",
                   "action": {"radius": radius, "file": f.name}})
    if not db.get("ok"):
        raise SystemExit(f"dumpblocks failed: {db}")
    raw = open(f.name, "rb").read()
    os.unlink(f.name)
    return raw, db["cx0"], db["cz0"], db["cx1"] - db["cx0"] + 1


def _block_at(raw, cx0, cz0, ncx, x, y, z):
    ci = (z // 16 - cz0) * ncx + (x // 16 - cx0)
    off = ci * 16 * 16 * 256 * 2 + ((y * 16 + (z % 16)) * 16 + (x % 16)) * 2
    return raw[off] | (raw[off + 1] << 8)


def find_tree(env, px, pz, radius=2, ymin=60, ymax=90):
    """Nearest log-column base (id 17) in a dumpblocks region around the
    player. Returns (x, y, z) of the bottom trunk log."""
    raw, cx0, cz0, ncx = _dump_region(env, radius)
    x0, z0 = cx0 * 16, cz0 * 16
    best, bd = None, None
    for z in range(z0, z0 + ncx * 16):
        for x in range(x0, x0 + ncx * 16):
            for y in range(ymin, ymax):
                if _block_at(raw, cx0, cz0, ncx, x, y, z) >> 4 == 17:
                    if _block_at(raw, cx0, cz0, ncx, x, y - 1, z) >> 4 != 17:
                        d = (x - px) ** 2 + (z - pz) ** 2
                        if bd is None or d < bd:
                            best, bd = (x, y, z), d
                    break
    return best




def jump_tap(env, wait=20, **fields):
    """One organic jump: wait for ground contact (stepping with fields), then
    a single 1-tick jump press. The bridge forces the impulse on the rising
    edge regardless of onGround, and a HELD jump re-fires via the vanilla
    landing path with jumpTicks bookkeeping magma does not share (takes 4/7:
    t148/t270 divergences) - so jumps must be 1-tick taps issued on og=1."""
    for _ in range(wait):
        if env.obs().get("og"):
            break
        env.step(dict(fields))
        time.sleep(PACE)
    env.step(dict(fields, jump=1))
    time.sleep(PACE)


def face_org(env, tyaw, tpitch=None, rate=14.0, tol=0.8, max_ticks=80):
    """Organic look: continuous dyaw/dpitch deltas, no tp. Replay-safe for
    exact-physics tapes (face()'s tp position packets transiently exceed TOL)."""
    for _ in range(max_ticks):
        o = env.obs()
        dy = ((tyaw - o["yaw"] + 180.0) % 360.0) - 180.0
        dp = 0.0 if tpitch is None else (tpitch - o["pitch"])
        if abs(dy) <= tol and abs(dp) <= tol:
            return
        a = {}
        if abs(dy) > tol:
            a["dyaw"] = max(-rate, min(rate, dy))
        if abs(dp) > tol:
            a["dpitch"] = max(-rate, min(rate, dp))
        env.step(a)
        time.sleep(PACE)


def aim_at(env, tx, ty, tz, eye=1.62, rate=14.0):
    o = env.obs()
    dx, dyy, dz = tx - o["x"], ty - (o["y"] + eye), tz - o["z"]
    face_org(env, math.degrees(math.atan2(-dx, dz)),
             math.degrees(-math.atan2(dyy, math.hypot(dx, dz))), rate=rate)


def walk_to(env, tx, tz, tol=2.2, max_bursts=80):
    """Closed-loop walk toward (tx, tz); only organic inputs are taped."""
    for i in range(max_bursts):
        o = env.obs()
        dx, dz = tx - o["x"], tz - o["z"]
        dist = math.hypot(dx, dz)
        if dist <= tol:
            run(env, 4)
            return True
        face_org(env, math.degrees(math.atan2(-dx, dz)), 12.0, rate=16.0,
                 tol=2.5, max_ticks=20)
        spr = 1 if dist > 7 else 0
        if i % 3 == 2:
            jump_tap(env, forward=1, sprint=spr)
            run(env, 7, forward=1, sprint=spr)
        else:
            run(env, 8, forward=1, sprint=spr)
    return False


def break_block(env, x, y, z, max_ticks=240):
    """Hold-to-break one block organically; poll until it is gone."""
    aim_at(env, x + 0.5, y + 0.5, z + 0.5)
    for _ in range(max_ticks // 10):
        run(env, 10, attack=1)
        if ol.runcmds(env, [f"testforblock {x} {y} {z} air"]).get("ran"):
            run(env, 4)
            return True
    print(f"break_block: ({x},{y},{z}) still standing after {max_ticks}")
    return False


def seg_survival(env):
    """Canonical organic survival route, bot-driven: spawn locomotion, walk to
    a sheep, fist-chop a tree trunk and collect the drops, take craft OUTPUTS
    via replaceitem (GUI crafting clicks are untaped by design -
    OPEN_DIVERGENCES #9; replay patches slots from the taped inv rows), dig a
    stair into the ground with the wooden pick, place torches. Everything the
    tape replays (movement, look, attack, use, hotbar) stays organic."""
    state = {}

    def setup():
        # pristine world every take: leftovers from a prior run (chopped
        # trunk, sapling/item drops mid-despawn, placed torches) contaminate
        # both the pixels and the recstart snapshot timing
        print("survival: fresh world reset",
              env.reset({"seed": 0, "mode": "survival", "type": "default",
                         "structures": True, "fresh": True},
                        timeout=300.0) and "ok")
        ol.freeze_scene(env, world_time=6000, gamemode="survival")
        ensure_dim(env, 0)
        settle(env, *SPAWN)
        tree = find_tree(env, int(SPAWN[0]), int(SPAWN[2]))
        if tree is None:
            raise SystemExit("survival: no tree in dump radius")
        state["tree"] = tree
        print("survival: trunk base at", tree)
        sheep = find_ent(env, "Sheep")
        state["sheep"] = (sheep["x"], sheep["z"]) if sheep else None
        print("survival: sheep at", state["sheep"])
        settle(env, *SPAWN)
        time.sleep(11)

    def drive():
        tx, ty, tz = state["tree"]
        # spawn locomotion: walk, sprint, jump-run, look around. Face inland
        # (toward the tree) first: default yaw 0 walks off spawn into the
        # ocean, and jump-at-water-edge diverged replay physics at t121.
        run(env, 20)
        o = env.obs()
        face_org(env, math.degrees(math.atan2(-(tx - o["x"]), tz - o["z"])),
                 10.0)
        run(env, 50, forward=1)
        run(env, 50, forward=1, sprint=1)
        # tap-jump, never hold: a held jump re-fires via the vanilla landing
        # path whose cooldown timing differs one tick from the bridge's forced
        # rising-edge impulse; taps keep every jump on the bit-exact edge path
        for _ in range(3):
            jump_tap(env, forward=1, sprint=1)
            run(env, 13, forward=1, sprint=1)
        for d in (1, 1, -1, -1):
            run(env, 6, dyaw=7.5 * d)
        run(env, 20, back=1)
        # visit the sheep for entity render coverage
        if state["sheep"]:
            walk_to(env, state["sheep"][0], state["sheep"][1], tol=3.5,
                    max_bursts=40)
            for d in (1, -1, -1, 1):
                run(env, 6, dyaw=7.5 * d)
        # fist-chop two trunk logs, then walk through the drop spot
        walk_to(env, tx + 0.5, tz + 2.5)
        break_block(env, tx, ty, tz)
        break_block(env, tx, ty + 1, tz)
        run(env, 25, forward=1)
        run(env, 20, back=1)
        run(env, 15)
        # craft outputs only (GUI clicks untaped; inv rows patch the replay)
        cmds(env, [
            "replaceitem entity @a slot.hotbar.1 minecraft:wooden_pickaxe 1 0",
            "replaceitem entity @a slot.hotbar.2 minecraft:torch 8 0",
        ])
        run(env, 20)
        # dig a 2-deep stair into the ground with the pick
        run(env, 5, hotbar=1)
        o = env.obs()
        bx = int(math.floor(o["x"])) + 1
        bz = int(math.floor(o["z"]))
        by = int(math.floor(o["y"])) - 1
        break_block(env, bx, by, bz)
        break_block(env, bx, by - 1, bz)
        run(env, 10)
        # torch on the far wall of the stair, then one on the ground ahead
        run(env, 5, hotbar=2)
        aim_at(env, bx + 1.5, by - 0.5, bz + 0.5)
        run(env, 6, use=1)
        run(env, 10)
        aim_at(env, bx + 0.5, by - 2 + 0.5, bz + 0.5)
        run(env, 6, use=1)
        run(env, 15)
        # closing locomotion + look-around
        run(env, 40, back=1)
        run(env, 40, forward=1, sprint=1)
        for d in (1, 1, -1, -1):
            run(env, 6, dyaw=7.5 * d)
        run(env, 30)
    return setup, drive


SEGMENTS = {
    "survival": seg_survival,
    "overworld": seg_overworld,
    "mine": seg_mine,
    "build": seg_build,
    "nether": seg_nether,
    "pearl": seg_pearl,
    "endportal": seg_endportal,
    "crystal": seg_crystal,
    "dragon": seg_dragon,
    "e2e": seg_e2e,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--segment", required=True, choices=sorted(SEGMENTS))
    ap.add_argument("--replay", action="store_true",
                    help="replay+report through magma (CUDA, GPU1) afterwards")
    ap.add_argument("--frames-every", type=int, default=2)
    args = ap.parse_args()

    if not ol.bridge_up():
        raise SystemExit("qrl bridge not up on 25575 (launch Run A/B first)")
    env = Bridge()
    env.connect()
    tape_path = None
    try:
        ol.ensure_world(env, seed=0, mode="survival")
        # A prior run can leave the player selector-dead (see respawn_reset);
        # freeze_scene's @a commands then fail. Normalize first.
        if not ol.runcmds(env, ["testfor @a"]).get("ran"):
            print("main: selectors dead, running respawn_reset")
            respawn_reset(env)
        ol.freeze_scene(env, world_time=6000, gamemode="survival")
        # heal hp/food carried over from earlier segments on the same save:
        # pearl tape 125903Z started at 2.17 hp (build-segment fall) and the
        # pearl's 5.0 teleport damage killed the oracle at t70, ending the
        # replay at death. Effects land within a tick; settle() in setup
        # gives them time to apply before recstart.
        ol.runcmds(env, ["effect @a minecraft:instant_health 1 5 true",
                         "effect @a minecraft:saturation 1 9 true"])
        setup, drive = SEGMENTS[args.segment](env)
        setup()
        uv = ["uv", "run", "--no-project", "--with", "pyarrow", "python",
              os.path.join(HERE, "tape.py")]
        env.close()  # single-client bridge: free it for tape.py
        out = subprocess.run(uv + ["start", "--frames-every",
                                   str(args.frames_every)],
                             check=True, capture_output=True, text=True)
        print(out.stdout, end="")
        for ln in out.stdout.splitlines():
            if ln.startswith("tape: "):
                tape_path = ln.split("tape: ", 1)[1].strip()
        env.connect()
        try:
            drive()
        finally:
            env.close()
            time.sleep(0.3)
            subprocess.run(uv + ["stop"], check=True)
    finally:
        env.close()

    if args.replay and tape_path:
        rep = ["uv", "run", "--no-project", "--with", "numpy", "--with",
               "pillow", "--with", "nbt", "python",
               os.path.join(HERE, "replay_tape.py"), tape_path,
               "--cuda", "--report"]
        environ = dict(os.environ)
        environ.setdefault("CUDA_VISIBLE_DEVICES", "1")
        subprocess.run(rep, env=environ, check=False)


if __name__ == "__main__":
    main()
