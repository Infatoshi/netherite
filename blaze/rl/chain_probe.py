"""Scripted executive for the spawn-to-coal chain (RL protocol v2 probe).

Stages: chop 4 logs -> craft planks/sticks/table -> place table -> interact
-> craft wooden pickaxe -> dig to stone (cobble) -> locate + mine coal ->
craft torches. Every stage talks to magma --rl-bin through MagmaEnv and
verifies itself from inv_counts, so a silent protocol regression fails loudly.

The executive is scripted on purpose: it is the ground-truth demonstration
that the full chain is reachable in-sim, and the per-stage skills (approach,
break, burrow) are what the learners train against.

Run (anvil): cd magma && uv run --no-project --with numpy python rl/chain_probe.py
"""
import argparse
import json
import math
import os
import sys

import numpy as np

from look_at_tree import MagmaEnv, INV_IDS, EYE, log_dirs, wrap180

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")

# inv_counts indices (INV_IDS order: 17,5,280,4,58,270,274,263,50)
IX_LOG, IX_PLANK, IX_STICK, IX_COBBLE, IX_TABLE, IX_WPICK, IX_SPICK, \
    IX_COAL, IX_TORCH = range(9)

# Runtime knobs (historical env IRON/IRON_DBG/PREFIX); set by main() argparse.
IRON = False
IRON_DBG = False
PREFIX = False

# 16 planks: table 4 + sticks 2 + pick 3 leaves margin. The iron extension
# burns two more tables (underground kit + final craft) -> chop 6.
LOG_TARGET = 4
COBBLE_TARGET = 3
REACH = 4.5

# stage_coal exit reason for the caller's log ("reached" / "already-mined" /
# "scan-empty" / "budget-exhausted"). Diagnostic only: never read by the probe.
LAST_FAIL = None


def inv(env, ix):
    return env.obs["inv_counts"][ix]


def step(env, act):
    env.send(act)
    return env.recv()


def nearest_log(obs):
    logs = log_dirs(obs)
    return min(logs, key=lambda entry: entry[2]) if logs else None


def nearest_coal(obs):
    """(rel_yaw, rel_pitch, dist) to the nearest scanned coal ore, or None."""
    best = None
    ex, ey, ez = obs["x"], obs["y"] + EYE, obs["z"]
    for c in obs["coal"]:
        if c == [0, 0, 0]:
            break
        dx, dy, dz = c[0] + 0.5 - ex, c[1] + 0.5 - ey, c[2] + 0.5 - ez
        dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        ry = wrap180(math.degrees(math.atan2(-dx, dz)) - obs["yaw"])
        rp = math.degrees(-math.asin(dy / max(dist, 1e-9))) - obs["pitch"]
        if best is None or dist < best[2]:
            best = (ry, rp, dist)
    return best


def make_env(seed, prefix):
    """Replay a scripted prefix into a MagmaEnv. Camera only on the last tick."""
    env = MagmaEnv(seed)
    window, inflight = 3, 0
    last = len(prefix) - 1
    for i, a in enumerate(prefix):
        if i != last:
            a = dict(a)
            a["cam"] = 0
        env.send(a)
        inflight += 1
        if inflight > window:
            env.recv()
            inflight -= 1
    for _ in range(inflight):
        env.recv()
    return env


def aim_error(obs, wx, wy, wz):
    """(rel_yaw, rel_pitch, dist) from eye to the center of block (wx,wy,wz)."""
    dx = wx + 0.5 - obs["x"]
    dy = wy + 0.5 - (obs["y"] + EYE)
    dz = wz + 0.5 - obs["z"]
    dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    tyaw = math.degrees(math.atan2(-dx, dz))
    tpitch = math.degrees(-math.asin(dy / max(dist, 1e-9)))
    return wrap180(tyaw - obs["yaw"]), tpitch - obs["pitch"], dist


def turn_act(ry, rp, cap=25.0):
    return {"dyaw": float(np.clip(ry, -cap, cap)),
            "dpitch": float(np.clip(rp, -cap, cap))}


def select_hotbar(env, item_id):
    for slot, hid in enumerate(env.obs["hotbar_ids"]):
        if hid == item_id and env.obs["hotbar_counts"][slot] > 0:
            if env.obs["hotbar_sel"] != slot:
                step(env, {"hotbar": slot})
            return True
    return False


# ---------------------------------------------------------------- stages
def stage_chop(env, budget=1200):
    """Greedy approach + hold-attack until LOG_TARGET logs are in inventory."""
    for t in range(budget):
        if inv(env, IX_LOG) >= LOG_TARGET:
            return True
        nl = nearest_log(env.obs)
        if nl is None:
            step(env, {"dyaw": 20.0, "forward": 1})
            continue
        ry, rp, dist = nl
        a = turn_act(ry, rp)
        if dist > REACH - 0.5:
            a["forward"] = 1
            if t % 8 < 2:
                a["jump"] = 1
        if dist <= REACH and abs(ry) < 12 and abs(rp) < 12:
            a["attack"] = 1
        step(env, a)
    return False


def stage_craft_kit(env):
    """logs -> planks -> sticks + table (2x2 crafts, silent-failure checked)."""
    for _ in range(LOG_TARGET):
        step(env, {"craft": 0})
    if inv(env, IX_PLANK) < 4 * LOG_TARGET - 4:
        return False
    step(env, {"craft": 1})            # 2 planks -> 4 sticks
    step(env, {"craft": 2})            # 4 planks -> table
    return inv(env, IX_STICK) >= 4 and inv(env, IX_TABLE) >= 1


def stage_place_table(env, budget=120):
    """Back up, aim ~35 deg down, hold use until a table block exists nearby."""
    if not select_hotbar(env, 58):
        return False
    for _ in range(4):
        step(env, {"forward": -1})
    for t in range(budget):
        near = [b for b in env.obs["blocks"]
                if b[0] == 58 and abs(b[1] - env.obs["x"]) < 6
                and abs(b[3] - env.obs["z"]) < 6]
        if near:
            return True
        rp = 35.0 - env.obs["pitch"]
        a = {"dpitch": float(np.clip(rp, -25, 25))}
        if abs(rp) < 10:
            a["use"] = 1
        step(env, a)
        if t == budget // 2:            # plan B: different spot
            step(env, {"dyaw": 90.0})
            for _ in range(3):
                step(env, {"forward": -1})
    return False


def close_container(env, budget=400):
    """Leave the open block GUI before anything tries to dig again.

    Magma pins Minecraft.leftClickCounter at 10000 for every tick a container
    screen is open (game/runtime.c gm_runtime_tick, mirroring vanilla's
    blocked mouse input), so clickMouse/clickBlock no-op and progressive dig
    is frozen solid while the crafting table is up. The RL action protocol has
    no ESC verb, and rl_do_interact only ever opens a screen - the one close
    magma exposes is vanilla's Container.canInteractWith range drop
    (runtime.c: dist^2 > 36 from the container block, or the block is gone).
    So walk away from the table until the screen drops.

    Without this, stage_dig and stage_coal hold attack into a frozen
    controller and never break a block: a burrow that cannot move is also a
    burrow that never leaves interaction range, so the state is an absorbing
    deadlock rather than a slow run.
    """
    # Straight away-from-the-table walks into whatever terrain happens to be
    # behind the player (measured: seed 11 pinned at x=14.70 against a 2-high
    # wall, jump-bouncing for the whole budget). Fan the heading out whenever
    # the walk stops making horizontal progress.
    fan = (0.0, 40.0, -40.0, 75.0, -75.0, 110.0, -110.0, 150.0, -150.0, 180.0)
    fan_i, stuck = 0, 0
    lx, lz = env.obs["x"], env.obs["z"]
    for t in range(budget):
        o = env.obs
        if o["container"] == 0:
            return True
        px, pz = o["x"], o["z"]
        if (px - lx) ** 2 + (pz - lz) ** 2 < 4e-4:
            stuck += 1
            if stuck >= 8:              # blocked: try the next heading
                fan_i = (fan_i + 1) % len(fan)
                stuck = 0
        else:
            stuck = 0
        lx, lz = px, pz
        tables = [b for b in o["blocks"] if b[0] == 58]
        a = {"forward": 1}
        if tables:
            tb = min(tables, key=lambda b: (b[1] + .5 - px) ** 2
                     + (b[3] + .5 - pz) ** 2)
            away = math.degrees(math.atan2(-(px - tb[1] - .5),
                                           pz - tb[3] - .5))
            a["dyaw"] = float(np.clip(wrap180(away + fan[fan_i] - o["yaw"]),
                                      -25, 25))
        if t % 4 < 2:
            a["jump"] = 1               # clear lips / step up terrain
        step(env, a)
    return env.obs["container"] == 0


def stage_pick(env, budget=40):
    """Open the placed table, craft the wooden pickaxe on the 3x3 grid, then
    step out of range so the screen closes and dig is live again."""
    for _ in range(budget):
        if env.obs["container"] == 1:
            break
        step(env, {"interact": 1})
    else:
        return False
    step(env, {"craft": 3})
    if inv(env, IX_WPICK) < 1:
        return False
    return close_container(env)


def center_probe(obs):
    """(block id, depth units) under the crosshair. 1 block ~ 5.3 depth units
    (RL_CAM_FAR 48.0 mapped to 0..255)."""
    cy, cx = 18, 32
    return int(obs["cam"][cy * 64 + cx]), int(obs["depth"][cy * 64 + cx])


def mine_block(env, bx, by, bz, budget=90):
    """Aim at block (bx,by,bz) and hold attack until the crosshair ray
    punches through (center depth jumps) or the budget runs out. Returns
    True if a break was observed."""
    base_depth = None
    for _ in range(budget):
        ry, rp, dist = aim_error(env.obs, bx, by, bz)
        if dist > 5.5:
            return False
        a = turn_act(ry, rp)
        if abs(ry) < 6 and abs(rp) < 6:
            a["attack"] = 1
            _, d = center_probe(env.obs)
            if base_depth is None:
                base_depth = d
            elif d > base_depth + 4:    # ray reaches past the mined cell
                return True
        step(env, a)
    return False


def stage_dig(env, budget=1600):
    """Block-targeted shaft straight down with the pick until COBBLE_TARGET
    cobble is banked and coal is visible in the scan (or we are deep)."""
    if not select_hotbar(env, 270):
        return False
    spent = 0
    while spent < budget:
        ok_cobble = inv(env, IX_COBBLE) >= COBBLE_TARGET
        coal = [c for c in env.obs["coal"] if c != [0, 0, 0]]
        if ok_cobble and (coal or env.obs["y"] < 30):
            return True
        bx = math.floor(env.obs["x"])
        bz = math.floor(env.obs["z"])
        by = math.floor(env.obs["y"]) - 1
        y0 = env.obs["y"]
        mine_block(env, bx, by, bz)
        spent += 90
        if env.obs["y"] > y0 - 0.5:     # did not fall: nudge to column center
            ry, _, _ = aim_error(env.obs, bx, by, bz)
            step(env, {"dyaw": float(np.clip(ry, -25, 25))})
            step(env, {"forward": 0.4})
            spent += 2
    return False


def stage_coal(env, budget=3000, stop_dist=None):
    """Burrow a 2-high staircase tunnel toward the nearest scanned coal ore,
    then mine it. Iron runs bank a second coal for furnace fuel. Wooden pick
    breaks both stone and coal ore. With stop_dist, stop (success) once the
    nearest ore is within that distance - used to build training prefixes
    that end on the last mile."""
    global LAST_FAIL
    spent = 0
    target = 2 if IRON else 1
    while spent < budget:
        if inv(env, IX_COAL) >= target:
            LAST_FAIL = "already-mined"
            return True
        coal = [c for c in env.obs["coal"] if c != [0, 0, 0]]
        if not coal:
            LAST_FAIL = "scan-empty"
            return False                # scan lost the ore; give up loudly
        if stop_dist is not None:
            px, py, pz = env.obs["x"], env.obs["y"], env.obs["z"]
            d2 = min((c[0] + 0.5 - px) ** 2 + (c[1] + 0.5 - py - EYE) ** 2
                     + (c[2] + 0.5 - pz) ** 2 for c in coal)
            if d2 <= stop_dist * stop_dist:
                LAST_FAIL = "reached"
                return True
        px, py, pz = env.obs["x"], env.obs["y"], env.obs["z"]
        tx, ty, tz = min(coal, key=lambda c: (c[0] + 0.5 - px) ** 2
                         + (c[1] + 0.5 - py - EYE) ** 2
                         + (c[2] + 0.5 - pz) ** 2)
        _, _, dist = aim_error(env.obs, tx, ty, tz)
        if dist <= 3.2:
            # close enough that the drop lands in pickup range: mine the ore,
            # then walk onto where it stood to collect (vanilla ~10 tick
            # pickup delay). Mining from farther away strands the drop on
            # ledges above and the inventory check never fires.
            mine_block(env, tx, ty, tz, budget=120)
            ry, _, _ = aim_error(env.obs, tx, ty, tz)
            step(env, {"dyaw": float(np.clip(ry, -25, 25))})
            for _ in range(14):
                step(env, {"forward": 1, "jump": 1})
            spent += 135
            continue
        # clear the next tunnel cell toward the ore
        dx, dz = tx + 0.5 - px, tz + 0.5 - pz
        norm = max(math.hypot(dx, dz), 1e-9)
        cx = math.floor(px + dx / norm * 1.3)
        cz = math.floor(pz + dz / norm * 1.3)
        feet = math.floor(py)
        if ty > py + 1.5:
            # staircase UP: keep (cx,feet,cz) as the step, clear the two
            # cells above it, then jump-walk onto the step
            mine_block(env, cx, feet + 1, cz)
            mine_block(env, cx, feet + 2, cz)
            spent += 180
            ry, _, _ = aim_error(env.obs, cx, feet + 1, cz)
            step(env, {"dyaw": float(np.clip(ry, -25, 25))})
            for _ in range(8):
                step(env, {"forward": 1, "jump": 1})
            spent += 9
            continue
        mine_block(env, cx, feet + 1, cz)          # head level
        mine_block(env, cx, feet, cz)              # feet level
        spent += 180
        if ty < py - 1.5:
            mine_block(env, cx, feet - 1, cz)      # staircase down
            spent += 90
        y0, x0, z0 = env.obs["y"], env.obs["x"], env.obs["z"]
        ry, _, _ = aim_error(env.obs, cx, feet + 1, cz)
        step(env, {"dyaw": float(np.clip(ry, -25, 25))})
        for _ in range(6):
            step(env, {"forward": 1})
        spent += 7
        if (abs(env.obs["x"] - x0) + abs(env.obs["z"] - z0) < 0.2
                and abs(env.obs["y"] - y0) < 0.2):
            step(env, {"forward": 1, "jump": 1})   # lip hop
            spent += 1
    LAST_FAIL = "budget-exhausted"
    return False


def stage_torch(env):
    step(env, {"craft": 5})             # coal + stick -> 4 torches
    if inv(env, IX_TORCH) < 4:
        return False
    if not IRON:
        return True
    # Light the tunnel and free the hotbar slot before the table/furnace/
    # stone-pick outputs compete with incidental dirt and split wood stacks.
    for facing in range(12):
        if inv(env, IX_TORCH) == 0:
            return True
        if not select_hotbar(env, 50):
            return False
        if facing:
            step(env, {"dyaw": 90.0})
        for pitch in (15.0, 35.0, 55.0, 0.0):
            for _ in range(4):
                if inv(env, IX_TORCH) == 0:
                    return True
                before = inv(env, IX_TORCH)
                rp = pitch - env.obs["pitch"]
                a = {"dpitch": float(np.clip(rp, -25, 25))}
                if abs(rp) < 10:
                    a["use"] = 1
                step(env, a)
                if inv(env, IX_TORCH) < before:
                    # Leave the occupied face behind before placing the next.
                    step(env, {"forward": -1})
                    step(env, {"forward": -1})
                    break
    return inv(env, IX_TORCH) == 0


# ------------------------------------------------- iron extension (--iron)
# item ids past the 9-slot inv_counts window: tracked via the hotbar
IRON_ORE, INGOT, FURNACE_ITEM, IPICK, COAL_ITEM = 15, 265, 61, 257, 263


def hotbar_count(env, item_id):
    return sum(c for i, c in zip(env.obs["hotbar_ids"],
                                 env.obs["hotbar_counts"]) if i == item_id)


def iron_count(env, item_id):
    """Full-inventory count of an iron-chain item via the JSON-only inv_iron
    obs field (furnace, iron ore, ingot, iron pick). The hotbar is FULL by
    the iron stages, so pickups overflow to the backpack and hotbar_count
    cannot confirm them. Needs the env in JSON mode (bin=False)."""
    ids = (FURNACE_ITEM, IRON_ORE, INGOT, IPICK)
    return env.obs["inv_iron"][ids.index(item_id)]


def stage_cobble_bank(env, target=15, budget=1600):
    """More cobble for the stone pick (3) + furnace (8): resume the shaft."""
    if not select_hotbar(env, 270):
        return False
    spent = 0
    while spent < budget:
        if inv(env, IX_COBBLE) >= target:
            return True
        bx, bz = math.floor(env.obs["x"]), math.floor(env.obs["z"])
        by = math.floor(env.obs["y"]) - 1
        y0 = env.obs["y"]
        mine_block(env, bx, by, bz)
        spent += 90
        if env.obs["y"] > y0 - 0.5:
            ry, _, _ = aim_error(env.obs, bx, by, bz)
            step(env, {"dyaw": float(np.clip(ry, -25, 25))})
            step(env, {"forward": 0.4})
            spent += 2
    return False


def place_from_hotbar(env, item_id, budget=80):
    """Carve a pocket one cell ahead (pick), then place item_id into it
    (aim ~40 deg down, hold use). Retries up to 3 facings 90 deg apart.
    Success = the item left the hotbar; returns the pocket cell."""
    for attempt in range(3):
        if attempt:
            step(env, {"dyaw": 90.0})
        pxx, pyy, pzz = env.obs["x"], env.obs["y"], env.obs["z"]
        yaw = math.radians(env.obs["yaw"])
        cx = math.floor(pxx - math.sin(yaw) * 1.3)
        cz = math.floor(pzz + math.cos(yaw) * 1.3)
        feet = math.floor(pyy)
        if IRON_DBG:
            print(f"    [place] item={item_id} attempt={attempt + 1} "
                  f"pos=({pxx:.1f},{pyy:.1f},{pzz:.1f}) "
                  f"pocket=({cx},{feet},{cz}) yaw={env.obs['yaw']:.1f}",
                  flush=True)
        if not select_hotbar(env, 274) and not select_hotbar(env, 270):
            return None
        mine_block(env, cx, feet, cz)
        mine_block(env, cx, feet + 1, cz)
        before = hotbar_count(env, item_id)
        if before < 1 or not select_hotbar(env, item_id):
            return None
        for pitch in (40.0, 50.0, 32.0, 60.0, 25.0, 70.0):
            for _ in range(12):
                if hotbar_count(env, item_id) < before:
                    if IRON_DBG:
                        print(f"    [place] item={item_id} placed "
                              f"at pitch={pitch:.1f}", flush=True)
                    return (cx, feet, cz)
                rp = pitch - env.obs["pitch"]
                a = {"dpitch": float(np.clip(rp, -25, 25))}
                if abs(rp) < 10:
                    a["use"] = 1
                step(env, a)
    if IRON_DBG:
        print(f"    [place] item={item_id} failed", flush=True)
    return None


def stage_iron_kit(env):
    """Underground base: consolidate wood, stack both needed tables before
    the hotbar fills, place one, then craft the stone picks."""
    while inv(env, IX_LOG) > 0:
        step(env, {"craft": 0})         # free a hotbar slot on overshoot seeds
    while inv(env, IX_STICK) < 8 and inv(env, IX_PLANK) >= 2:
        step(env, {"craft": 1})     # 2 spicks + ipick need 6 sticks total
    while inv(env, IX_TABLE) < 2 and inv(env, IX_PLANK) >= 4:
        step(env, {"craft": 2})         # kit table + carried final table
    if inv(env, IX_STICK) < 4 or inv(env, IX_TABLE) < 2:
        return False
    pos = place_from_hotbar(env, 58)
    if pos is None:
        return False
    for _ in range(40):
        if env.obs["container"] == 1:
            break
        step(env, {"interact": 1})
    else:
        return False
    step(env, {"craft": 4})
    step(env, {"craft": 4})             # spare spick may overflow - fine
    # one table stays behind; the other shares its hotbar stack until the
    # smelting site, where placing it frees the furnace-output slot
    return inv(env, IX_SPICK) >= 1 and hotbar_count(env, 58) >= 1 \
        and select_hotbar(env, 274)


def load_iron_oracle(seed):
    """Static iron-ore world positions from the seed's t0 snapshot region
    (worldgen is deterministic, so the t0 dump stays valid). The scripted
    executive may use snapshot oracles - same standing as the env coal scan
    the coal stage navigates by; the LEARNED policy never sees this."""
    import struct
    path = os.path.join(OUT, "snaps", f"s{seed}_t0.bsnp")
    if not os.path.exists(path):
        return []
    data = open(path, "rb").read()
    # RlSnapHead is 752 bytes packed; tail = n_items u32 + rx0 ry0 rz0
    # rnx rny rnz (blaze_snapshot.h); items are 76 bytes each; region cells
    # follow as u16 (id<<4)|meta in [x][y][z] order (rl_snapshot_write).
    n_items, rx0, ry0, rz0, rnx, rny, rnz = struct.unpack_from(
        "<Iiiiiii", data, 752 - 28)
    cells = np.frombuffer(data, "<u2", rnx * rny * rnz, 752 + n_items * 76)
    ids = (cells >> 4).reshape(rnx, rny, rnz)
    return [[rx0 + int(x), ry0 + int(y), rz0 + int(z)]
            for x, y, z in np.argwhere(ids == IRON_ORE)]


def probe_cell(env, tx, ty, tz):
    """Aim at the cell center and classify it with the semantic camera:
    'ore' (center pixel is iron ore), 'gone' (unobstructed ray passes beyond
    the cell - it is air), 'blocked' (a nearer block obstructs the ray).
    Depth units are dist*4 (obs_camera.h)."""
    for _ in range(10):
        ry, rp, dist = aim_error(env.obs, tx, ty, tz)
        if abs(ry) < 4 and abs(rp) < 4:
            cid, d = center_probe(env.obs)
            if cid == IRON_ORE:
                return "ore"
            # a block directly BEHIND the cell reads (dist+1)*4 - the ray
            # passing the cell center by half a block already proves air
            return "gone" if d > (dist + 0.5) * 4 else "blocked"
        step(env, turn_act(ry, rp))
    return "blocked"


def stage_iron_mine(env, want=3, budget=9000):
    """Tunnel to the nearest oracle iron ore with the stone pick - the same
    staircase burrow that reaches coal, pointed at snapshot-oracle targets
    (no in-env iron scan exists)."""
    if not select_hotbar(env, 274):
        return False
    ores = load_iron_oracle(env.seed)
    if not ores:
        return False
    spent = 0
    attempts = {}
    stall = {}                          # key -> (best_dist, no-progress count)
    while spent < budget:
        if iron_count(env, IRON_ORE) >= want:
            return True
        if not ores:
            return False
        select_hotbar(env, 274)     # re-arm the spare when a pick breaks
        px, py, pz = env.obs["x"], env.obs["y"], env.obs["z"]

        def okey(c):
            dxx = c[0] + 0.5 - px
            dyy = c[1] + 0.5 - py - EYE
            dzz = c[2] + 0.5 - pz
            up = max(0.0, dyy)      # ores overhead are hard to stair up to:
            return dxx * dxx + dyy * dyy + dzz * dzz + 25.0 * up * up
        ores.sort(key=okey)
        tx, ty, tz = ores[0]
        key = (tx, ty, tz)
        _, _, dist = aim_error(env.obs, tx, ty, tz)
        if IRON_DBG:
            print(f"    [im] t={env.obs['t']} pos=({px:.1f},{py:.1f},{pz:.1f})"
                  f" tgt={key} d={dist:.1f} spent={spent}", flush=True)
        # generic anti-stall: a target we fail to close on (cave gap, ledge
        # above, unwalkable diagonal) gets retired - 4k+ others remain
        best, miss = stall.get(key, (1e9, 0))
        miss = miss + 1 if dist > best - 0.3 else 0
        stall[key] = (min(best, dist), miss)
        if miss >= 5:
            ores.pop(0)
            continue
        if dist <= 3.2:
            # ghost pruning: the t0 oracle is static, but earlier stages (and
            # this stage's own tunnel churn - wooden-pick breaks DESTROY iron
            # ore, no drop) remove ore cells. Standing inside the cell, or an
            # unobstructed crosshair ray that passes beyond it, proves air.
            if dist < 1.1:
                ores.pop(0)
                continue
            state = probe_cell(env, tx, ty, tz)
            spent += 10
            if state == "gone":
                ores.pop(0)
                continue
            # close range: mine along the crosshair ray (each attempt breaks
            # the first solid on the ray - obstructing corners first, then
            # the ore), steer onto the cell to collect. PICKUP is the only
            # trusted break confirmation: the depth heuristic reports "broke"
            # for any block on the ray, not necessarily the ore.
            n0 = iron_count(env, IRON_ORE)
            mine_block(env, tx, ty, tz, budget=160)
            # the drop lands in the ore cell; collection means STANDING there.
            # Open headroom above the ore, then walk in with face-adjacent
            # steps - a diagonal push freezes against the corner blocks (the
            # frozen-pos walkover was why range-mined drops never collected).
            mine_block(env, tx, ty + 1, tz, budget=60)
            spent += 230
            for _ in range(3):
                if iron_count(env, IRON_ORE) > n0:
                    break
                px, py, pz = env.obs["x"], env.obs["y"], env.obs["z"]
                if math.floor(px) == tx and math.floor(pz) == tz:
                    break
                ddx, ddz = tx + 0.5 - px, tz + 0.5 - pz
                if abs(ddx) >= abs(ddz):
                    cx = math.floor(px) + (1 if ddx > 0 else -1)
                    cz = math.floor(pz)
                else:
                    cx = math.floor(px)
                    cz = math.floor(pz) + (1 if ddz > 0 else -1)
                feet = math.floor(py)
                mine_block(env, cx, feet, cz, budget=60)
                mine_block(env, cx, feet + 1, cz, budget=60)
                ry, _, _ = aim_error(env.obs, cx, feet, cz)
                step(env, turn_act(ry, 0))
                for _ in range(10):
                    step(env, {"forward": 1, "jump": 1})
                spent += 135
            attempts[key] = attempts.get(key, 0) + 1
            if iron_count(env, IRON_ORE) > n0 or attempts[key] >= 6:
                ores.pop(0)
            continue
        # clear the next tunnel cell toward the ore (stage_coal recipe)
        dx, dz = tx + 0.5 - px, tz + 0.5 - pz
        if ty < py - 1.5 and math.hypot(dx, dz) < 1.8:
            # ore almost straight below: the horizontal step degenerates to
            # the player's own cell - shaft straight down instead
            bx, bz = math.floor(px), math.floor(pz)
            y0 = py
            mine_block(env, bx, math.floor(py) - 1, bz)
            spent += 90
            if env.obs["y"] > y0 - 0.5:
                step(env, {"forward": 0.4})
                spent += 1
            continue
        # face-adjacent step along the dominant axis: a diagonal next-cell
        # gets mined but stays unwalkable (the two face-adjacent corner
        # blocks still block the diagonal), freezing the walk
        if abs(dx) >= abs(dz):
            cx = math.floor(px) + (1 if dx > 0 else -1)
            cz = math.floor(pz)
        else:
            cx = math.floor(px)
            cz = math.floor(pz) + (1 if dz > 0 else -1)
        feet = math.floor(py)
        if ty > py + 1.5:
            mine_block(env, cx, feet + 1, cz)
            mine_block(env, cx, feet + 2, cz)
            spent += 180
            ry, _, _ = aim_error(env.obs, cx, feet + 1, cz)
            step(env, {"dyaw": float(np.clip(ry, -25, 25))})
            for _ in range(8):
                step(env, {"forward": 1, "jump": 1})
            spent += 9
            continue
        mine_block(env, cx, feet + 1, cz)
        mine_block(env, cx, feet, cz)
        spent += 180
        if ty < py - 1.5:
            mine_block(env, cx, feet - 1, cz)
            spent += 90
        y0, x0, z0 = env.obs["y"], env.obs["x"], env.obs["z"]
        ry, _, _ = aim_error(env.obs, cx, feet + 1, cz)
        step(env, {"dyaw": float(np.clip(ry, -25, 25))})
        for _ in range(6):
            step(env, {"forward": 1})
        spent += 7
        if (abs(env.obs["x"] - x0) + abs(env.obs["z"] - z0) < 0.2
                and abs(env.obs["y"] - y0) < 0.2):
            step(env, {"forward": 1, "jump": 1})
            spent += 1
    return False


def stage_smelt(env, want=3, budget=2400):
    """At the ore site, place the carried final table, craft/place a furnace,
    load it (smelt:1 = ore + coal), and wait out the cook."""
    table_pos = place_from_hotbar(env, 58)
    if table_pos is None:
        return False
    for _ in range(40):
        if env.obs["container"] == 1:
            break
        step(env, {"interact": 1})
    else:
        return False
    step(env, {"craft": 6})
    if hotbar_count(env, FURNACE_ITEM) < 1:
        return False
    step(env, {"dyaw": 90.0})
    step(env, {"dyaw": 90.0})           # furnace opposite the table
    furnace_pos = place_from_hotbar(env, FURNACE_ITEM)
    if furnace_pos is None:
        return False
    # Move toward the furnace so nearest-block interaction cannot relatch the
    # table placed on the other side of the pocket.
    for _ in range(3):
        ry, _, _ = aim_error(env.obs, *furnace_pos)
        a = turn_act(ry, 0)
        if abs(ry) < 12:
            a["forward"] = 1
        step(env, a)
    for _ in range(40):
        if env.obs["container"] == 2:
            break
        step(env, {"interact": 1})
    else:
        return False
    step(env, {"smelt": 1})
    if iron_count(env, IRON_ORE) > 0:
        return False                    # load failed
    for t in range(budget):
        step(env, {"smelt": 1} if (t + 1) % 100 == 0 else {})
        if iron_count(env, INGOT) >= want:
            env.final_table_pos = table_pos
            return True
    return False


def stage_ipick(env):
    """Return to the table beside the furnace and craft:7."""
    table_pos = getattr(env, "final_table_pos", None)
    if table_pos is None:
        return False
    for _ in range(40):
        if env.obs["container"] == 1:
            break
        step(env, {"interact": 1})
        if env.obs["container"] != 1:
            ry, _, _ = aim_error(env.obs, *table_pos)
            a = turn_act(ry, 0)
            if abs(ry) < 12:
                a["forward"] = 1
            step(env, a)
    else:
        return False
    step(env, {"craft": 7})
    return iron_count(env, IPICK) >= 1


IRON_STAGES = [("cobble_bank", stage_cobble_bank), ("iron_kit", stage_iron_kit),
               ("iron_mine", stage_iron_mine), ("smelt", stage_smelt),
               ("iron_pick", stage_ipick)]

BASE_STAGES = [("chop", stage_chop), ("craft_kit", stage_craft_kit),
               ("place_table", stage_place_table), ("wooden_pick", stage_pick),
               ("dig", stage_dig), ("mine_coal", stage_coal),
               ("torches", stage_torch)]
# Active stage list; reconfigured by configure_iron() when --iron is set.
STAGES = list(BASE_STAGES)


def configure_iron(iron=False, iron_dbg=False):
    """Apply --iron / --iron-dbg knobs (module-level, read by stage fns)."""
    global IRON, IRON_DBG, LOG_TARGET, STAGES
    IRON = bool(iron)
    IRON_DBG = bool(iron_dbg)
    LOG_TARGET = 6 if IRON else 4
    STAGES = list(BASE_STAGES) + (list(IRON_STAGES) if IRON else [])


def run_seed(seed, verbose=True):
    # IRON runs need the JSON obs (inv_iron field); the base chain keeps the
    # fast binary protocol
    env = MagmaEnv(seed, bin=not IRON)
    results = []
    try:
        for name, fn in STAGES:
            t0 = env.obs["t"]
            ok = fn(env)
            results.append((name, ok, env.obs["t"] - t0))
            if verbose:
                counts = {INV_IDS[i]: c
                          for i, c in enumerate(env.obs["inv_counts"]) if c}
                print(f"  seed {seed} {name:12s} "
                      f"{'OK  ' if ok else 'FAIL'} +{env.obs['t'] - t0:4d}t "
                      f"y={env.obs['y']:.1f} inv={counts}", flush=True)
                if IRON_DBG:
                    hotbar = [(item, count) for item, count in zip(
                        env.obs["hotbar_ids"], env.obs["hotbar_counts"])
                        if count]
                    print(f"    hotbar={hotbar} inv_iron="
                          f"{env.obs.get('inv_iron')}", flush=True)
            if not ok:
                return results, env.actions
        return results, env.actions
    finally:
        env.close()


def run_prefix(seed):
    """Stages chop..dig only. Returns the action stream if every stage passed
    AND coal is visible in the final scan - the training start state for the
    learned mine-coal skill. None otherwise."""
    env = MagmaEnv(seed)
    try:
        for name, fn in STAGES[:5]:
            if not fn(env):
                return None
        # scripted burrow until the ore is on the last mile: the learned
        # phase (EP_LEN 600) must be able to finish from the handoff state
        if not stage_coal(env, budget=2400, stop_dist=6.0):
            return None
        coal = [c for c in env.obs["coal"] if c != [0, 0, 0]]
        return list(env.actions) if coal else None
    finally:
        env.close()


def screen(seeds):
    """--prefix mode: save per-seed prefix action streams for training."""
    from concurrent.futures import ThreadPoolExecutor
    os.makedirs(OUT, exist_ok=True)
    with ThreadPoolExecutor(max_workers=16) as pool:
        results = list(pool.map(run_prefix, seeds))
    good = {}
    for seed, acts in zip(seeds, results):
        if acts:
            good[str(seed)] = acts
        print(f"  seed {seed}: {'OK ' + str(len(acts)) + 't' if acts else 'no'}",
              flush=True)
    path = os.path.join(OUT, "coal_prefixes.json")
    with open(path, "w") as f:
        json.dump(good, f)
    print(f"{len(good)}/{len(seeds)} seeds usable -> {path}", flush=True)
    return 0 if good else 1


def main():
    global PREFIX
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iron", action="store_true",
                    help="extend chain through iron pick (was IRON=1)")
    ap.add_argument("--iron-dbg", action="store_true",
                    help="verbose iron-stage diagnostics (was IRON_DBG=1)")
    ap.add_argument("--prefix", action="store_true",
                    help="screen prefixes into coal_prefixes.json (was PREFIX=1)")
    ap.add_argument("seeds", nargs="*", type=int,
                    help="seeds to run (defaults depend on mode)")
    args = ap.parse_args()
    PREFIX = args.prefix
    configure_iron(iron=args.iron, iron_dbg=args.iron_dbg)
    os.makedirs(OUT, exist_ok=True)
    if PREFIX:
        return screen(args.seeds or list(range(48)))
    seeds = args.seeds or [0, 2, 3, 10, 11, 12]
    for seed in seeds:
        print(f"seed {seed}:", flush=True)
        results, actions = run_seed(seed)
        if all(ok for _, ok, _ in results):
            path = os.path.join(OUT, f"chain_actions_s{seed}.json")
            with open(path, "w") as f:
                json.dump(actions, f)
            total = sum(t for _, _, t in results)
            print(f"FULL CHAIN COMPLETE seed {seed}: {total} ticks, "
                  f"actions -> {path}", flush=True)
            return 0
    print("no seed completed the chain", flush=True)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
