#!/usr/bin/env python3
"""Recover a continuous ordinary-input tape with a state-aware search policy."""
import argparse
import json
import math
import os
import pathlib
import subprocess


ACTION_DEFAULTS = {
    "forward": 0, "strafe": 0, "dyaw": 0, "dpitch": 0,
    "jump": 0, "sneak": 0, "sprint": 0, "attack": 0, "use": 0,
    "do_break": 0, "do_place": 0, "close_container": 0,
    "hotbar": -1, "death_click": 0,
    "death_button": 0,
}
OPTIONAL_ACTION_FIELDS = {"inv_slot", "inv_button", "inv_type"}


def angle_delta(target, current):
    return (target - current + 180.0) % 360.0 - 180.0


def look_at(obs, x, y, z):
    dx = x - obs["x"]
    dy = y - (obs["y"] + 1.62)
    dz = z - obs["z"]
    yaw = math.degrees(math.atan2(-dx, dz))
    pitch = math.degrees(math.atan2(-dy, math.hypot(dx, dz)))
    return angle_delta(yaw, obs["yaw"]), pitch - obs["pitch"]


class Episode:
    def __init__(self, args):
        self.can_save = bool(args.save_root)
        command = [
            str(args.game), "--seed", str(args.seed), "--world", "default",
            "--view-distance", str(args.view_distance), "--mobs", "on",
            "--rl", "--set", "vanilla_spawn=1",
            "--set", f"spawn_offset_x={args.spawn_offset_x}",
            "--set", f"spawn_offset_z={args.spawn_offset_z}",
        ]
        env = os.environ.copy()
        if args.save_root:
            env["MAGMA_NATIVE_WORLD_ROOT"] = str(args.save_root.resolve())
        if args.load_slot:
            env["MAGMA_RL_LOAD_SLOT"] = args.load_slot
        self.proc = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, bufsize=1, env=env)
        first = self.proc.stdout.readline()
        if not first:
            raise RuntimeError(self.proc.stderr.read().strip() or
                               "RL process failed before its initial observation")
        self.obs = json.loads(first)
        self.rows = []
        if args.resume_tape:
            with args.resume_tape.open(encoding="utf-8") as stream:
                self.rows = [json.loads(line) for line in stream if line.strip()]
            if self.obs["t"] != len(self.rows):
                raise RuntimeError(
                    f"resume tick {self.obs['t']} != tape length {len(self.rows)}")

    def step(self, **values):
        wire = dict(values)
        wire.setdefault("cam", 0)
        self.proc.stdin.write(json.dumps(wire, separators=(",", ":")) + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError(self.proc.stderr.read().strip() or "RL process ended")
        row = dict(ACTION_DEFAULTS)
        for key, value in values.items():
            if key in row or key in OPTIONAL_ACTION_FIELDS:
                row[key] = value
        row.update({"tick": len(self.rows), "type": "action"})
        self.rows.append(row)
        self.obs = json.loads(line)
        return self.obs

    def idle_ticks(self, ticks):
        """Advance exact no-input ticks while requesting only the final obs."""
        if ticks <= 0:
            return self.obs
        self.proc.stdin.write(json.dumps(
            {"idle_ticks": ticks, "cam": 0}, separators=(",", ":")) + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError(self.proc.stderr.read().strip() or
                               "RL process ended during bulk idle")
        for _ in range(ticks):
            row = dict(ACTION_DEFAULTS)
            row.update({"tick": len(self.rows), "type": "action"})
            self.rows.append(row)
        self.obs = json.loads(line)
        if self.obs["t"] != len(self.rows):
            raise RuntimeError(
                f"bulk idle tick {self.obs['t']} != tape length "
                f"{len(self.rows)}")
        return self.obs

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=30)
        stderr = self.proc.stderr.read()
        if self.proc.returncode:
            raise RuntimeError(stderr.strip())
        return stderr


def nearest_log(obs):
    logs = [value for value in obs.get("logs", []) if value != [0, 0, 0]]
    if not logs:
        return None
    columns = {}
    for value in logs:
        key = value[0], value[2]
        if key not in columns or value[1] < columns[key][1]:
            columns[key] = value
    return min(columns.values(),
               key=lambda p: ((p[0] + 0.5 - obs["x"]) ** 2 +
                              (p[1] + 0.5 - obs["y"] - 1.0) ** 2 +
                              (p[2] + 0.5 - obs["z"]) ** 2))


def opening(ep):
    for tick in range(160):
        action = {}
        if tick == 0:
            action["dyaw"] = 85.0
        if 40 <= tick < 155:
            action.update(forward=1, sprint=1, jump=1)
        if tick == 159:
            action["dyaw"] = -85.0
        ep.step(**action)


def approach_and_mine_log(ep):
    target = nearest_log(ep.obs)
    if target is None:
        raise RuntimeError("opening endpoint has no visible log")
    for _ in range(100):
        dx = target[0] + 0.5 - ep.obs["x"]
        dz = target[2] + 0.5 - ep.obs["z"]
        if math.hypot(dx, dz) < 3.4:
            break
        dyaw, dpitch = look_at(ep.obs, target[0] + 0.5,
                              target[1] + 0.5, target[2] + 0.5)
        ep.step(forward=1, jump=1, dyaw=dyaw, dpitch=dpitch)
    else:
        raise RuntimeError("failed to approach first log")
    initial_logs = ep.obs["inv_counts"][0]
    for _ in range(160):
        dyaw, dpitch = look_at(ep.obs, target[0] + 0.5,
                              target[1] + 0.5, target[2] + 0.5)
        ep.step(attack=1, dyaw=dyaw, dpitch=dpitch, cam=1)
        if ep.obs["inv_counts"][0] > initial_logs:
            return target
        if target not in ep.obs.get("logs", []):
            for _ in range(60):
                dyaw, dpitch = look_at(ep.obs, target[0] + 0.5,
                                      target[1] + 0.5, target[2] + 0.5)
                ep.step(forward=1, jump=1, dyaw=dyaw, dpitch=dpitch)
                if ep.obs["inv_counts"][0] > initial_logs:
                    return target
    raise RuntimeError(
        f"first log {target} did not break or enter inventory; "
        f"end=({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
        f"look=({ep.obs['yaw']:.2f},{ep.obs['pitch']:.2f}) "
        f"visible={target in ep.obs.get('logs', [])}")


def collect_logs(ep, count):
    targets = []
    while ep.obs["inv_counts"][0] < count:
        targets.append(approach_and_mine_log(ep))
    return targets


def collect_logs_probed(ep, count):
    """Collect exact visible logs with the cross-stack world probe."""
    while item_count(ep.obs, 17) < count:
        target = nearest_log(ep.obs)
        if target is None:
            raise RuntimeError("log collection has no visible target")
        navigate(ep, target[0] + 0.5, target[2] + 0.5,
                 max_ticks=240, sprint=False, tolerance=2.5,
                 arrival_idle=0, combat=False)
        evade_creepers(ep, radius=10.0)
        kill_nearby_hostiles(ep, radius=8.0, hostile_types=(5,))
        before = item_count(ep.obs, 17)
        mine_probed_coordinate(ep, target[0], target[1], target[2],
                               available_break_tool(ep.obs), max_ticks=240)
        navigate(ep, target[0] + 0.5, target[2] + 0.5,
                 max_ticks=120, sprint=False, tolerance=1.0,
                 arrival_idle=12, combat=False)
        if item_count(ep.obs, 17) <= before:
            for _ in range(80):
                dyaw, dpitch = look_at(
                    ep.obs, target[0] + 0.5, target[1] + 0.5,
                    target[2] + 0.5)
                ep.step(forward=1, jump=1, dyaw=dyaw, dpitch=dpitch)
                if item_count(ep.obs, 17) > before:
                    break
        if item_count(ep.obs, 17) <= before:
            raise RuntimeError(f"mined log {target} was not collected")


def click(ep, slot, button=0, click_type=0):
    ep.step(inv_slot=slot, inv_button=button, inv_type=click_type)


def hotbar_slot(ep, item):
    for slot, value in enumerate(ep.obs["hotbar_ids"]):
        if value == item:
            return slot
    raise RuntimeError(f"item {item} is not in the hotbar: {ep.obs['hotbar_ids']}")


def inventory_slot(ep, item):
    for slot, value in enumerate(ep.obs["inventory_ids"]):
        if value == item:
            return slot
    raise RuntimeError(f"item {item} is not in the inventory")


def item_count(obs, item):
    return sum(count for value, count in zip(
        obs["inventory_ids"], obs["inventory_counts"]) if value == item)


def available_pick(obs):
    return next((item for item in (257, 274, 270)
                 if item in obs["inventory_ids"]), None)


def available_break_tool(obs):
    return next((item for item in (273, 256, 284, 277, 269)
                 if item in obs["inventory_ids"]), None) or available_pick(obs) or next(
        (item for item in (259, 280, 263, 5, 35)
         if item in obs["inventory_ids"]), None) or next(
        (item for item, count in zip(obs["inventory_ids"],
                                     obs["inventory_counts"])
         if item != 0 and count > 0), None)


def ensure_hotbar(ep, item, replace=1):
    if item in ep.obs["hotbar_ids"]:
        return hotbar_slot(ep, item)
    click(ep, inventory_slot(ep, item), button=replace, click_type=2)
    return hotbar_slot(ep, item)


def craft_planks(ep):
    before_planks = ep.obs["inv_counts"][1]
    log_slot = hotbar_slot(ep, 17)
    dst = hotbar_slot(ep, 5) if 5 in ep.obs["hotbar_ids"] else next(
        slot for slot, value in enumerate(ep.obs["hotbar_ids"]) if value == 0)
    click(ep, log_slot)
    click(ep, 36, button=1)
    click(ep, log_slot)
    click(ep, 45)
    click(ep, dst)
    if ep.obs["inv_counts"][1] < before_planks + 4:
        raise RuntimeError(
            "ordinary inventory clicks failed to craft planks: "
            f"counts={ep.obs['inv_counts']} hotbar={ep.obs['hotbar_ids']}/"
            f"{ep.obs['hotbar_counts']} container={ep.obs['container']}")


def craft_opening_items(ep):
    """Use only ContainerPlayer clicks to make planks and a table."""
    craft_planks(ep)
    plank_slot = hotbar_slot(ep, 5)
    click(ep, plank_slot)
    for slot in (36, 37, 39, 40):
        click(ep, slot, button=1)
    click(ep, plank_slot)
    click(ep, 45)
    table_slot = next(slot for slot, value in enumerate(ep.obs["inventory_ids"])
                      if value == 0)
    click(ep, table_slot)
    if ep.obs["inv_counts"][4] < 1:
        raise RuntimeError("ordinary inventory clicks failed to craft table")


def craft_sticks(ep):
    plank_slot = hotbar_slot(ep, 5)
    before = ep.obs["inv_counts"][2]
    click(ep, plank_slot)
    for slot in (36, 39):
        click(ep, slot, button=1)
    click(ep, plank_slot)
    click(ep, 45)
    dst = next(slot for slot, value in enumerate(ep.obs["hotbar_ids"])
               if value == 0)
    click(ep, dst)
    if ep.obs["inv_counts"][2] < before + 4:
        raise RuntimeError("ordinary inventory clicks failed to craft sticks")


def craft_torches(ep):
    """Craft one coal-and-stick batch in the player's 2x2 grid."""
    before = item_count(ep.obs, 50)
    coal_slot = inventory_slot(ep, 263)
    stick_slot = inventory_slot(ep, 280)
    click(ep, coal_slot)
    click(ep, 36, button=1)
    click(ep, coal_slot)
    click(ep, stick_slot)
    click(ep, 39, button=1)
    click(ep, stick_slot)
    click(ep, 45)
    destination = next(slot for slot, value in enumerate(
        ep.obs["inventory_ids"]) if value == 0)
    click(ep, destination)
    if item_count(ep.obs, 50) < before + 4:
        raise RuntimeError("ordinary inventory clicks failed to craft torches")


def craft_sandstone(ep, batches):
    """Compress four falling sand blocks into one stable 2x2 sandstone."""
    for _ in range(batches):
        before_sand = item_count(ep.obs, 12)
        before_stone = item_count(ep.obs, 24)
        if before_sand < 4:
            raise RuntimeError("sandstone craft exhausted sand")
        sand_slot = inventory_slot(ep, 12)
        click(ep, sand_slot)
        for grid_slot in (36, 37, 39, 40):
            click(ep, grid_slot, button=1)
        click(ep, sand_slot)
        click(ep, 45)
        destination = next(
            (slot for slot, value in enumerate(ep.obs["inventory_ids"])
             if value in (0, 24)), None)
        if destination is None:
            raise RuntimeError("sandstone craft has no destination slot")
        click(ep, destination)
        if (item_count(ep.obs, 12) != before_sand - 4 or
                item_count(ep.obs, 24) != before_stone + 1):
            raise RuntimeError(
                f"ordinary sandstone craft failed; sand="
                f"{before_sand}->{item_count(ep.obs, 12)} sandstone="
                f"{before_stone}->{item_count(ep.obs, 24)}")


def craft_stone_shovel(ep):
    """Craft a stone shovel in an already-open crafting table."""
    cobble_slot = inventory_slot(ep, 4)
    stick_slot = inventory_slot(ep, 280)
    click(ep, cobble_slot)
    click(ep, 37, button=1)
    click(ep, cobble_slot)
    click(ep, stick_slot)
    for slot in (40, 43):
        click(ep, slot, button=1)
    click(ep, stick_slot)
    click(ep, 45)
    destination = next(slot for slot, value in enumerate(
        ep.obs["inventory_ids"]) if value == 0)
    click(ep, destination)
    if 273 not in ep.obs["inventory_ids"]:
        raise RuntimeError("ordinary table clicks failed to craft stone shovel")
    ep.step(close_container=1)


def craft_wooden_shovel(ep):
    """Craft a wooden shovel in an already-open crafting table."""
    plank_slot = inventory_slot(ep, 5)
    stick_slot = inventory_slot(ep, 280)
    click(ep, plank_slot)
    click(ep, 37, button=1)
    click(ep, plank_slot)
    click(ep, stick_slot)
    for slot in (40, 43):
        click(ep, slot, button=1)
    click(ep, stick_slot)
    click(ep, 45)
    destination = next(slot for slot, value in enumerate(
        ep.obs["inventory_ids"]) if value == 0)
    click(ep, destination)
    if 269 not in ep.obs["inventory_ids"]:
        raise RuntimeError("ordinary table clicks failed to craft wooden shovel")
    ep.step(close_container=1)


def prepare_portal_shelf_shovel(ep):
    """Recover one scaffold plank and one dead-bush stick for a shovel."""
    if 269 in ep.obs["inventory_ids"]:
        return
    stick_before = item_count(ep.obs, 280)
    navigate(ep, 52.5, 36.5, max_ticks=300, sprint=True,
             tolerance=0.3, arrival_idle=0, combat="creeper")
    for _ in range(int(os.environ.get("NETHERITE_BUSH_WAIT", "0"))):
        ep.step()
    ep.step(probe_x=52, probe_y=65, probe_z=37)
    if ep.obs["probe"][0] == 32:
        mine_probed_coordinate(ep, 52, 65, 37,
                               available_break_tool(ep.obs), max_ticks=20)
        navigate(ep, 52.5, 37.5, max_ticks=80, sprint=False,
                 tolerance=0.25, arrival_idle=12, combat="creeper")
    tried = {(52, 65, 37)}
    for _ in range(12):
        if item_count(ep.obs, 280) > stick_before:
            break
        bushes = [b for b in ep.obs["blocks"]
                  if b[0] == 32 and tuple(b[1:]) not in tried]
        if not bushes:
            navigate(ep, ep.obs["x"] + 6.0, ep.obs["z"], max_ticks=180,
                     sprint=True, tolerance=0.5, arrival_idle=0,
                     combat="creeper")
            continue
        bush = min(bushes, key=lambda b:
                   (b[1] + 0.5 - ep.obs["x"]) ** 2 +
                   (b[3] + 0.5 - ep.obs["z"]) ** 2)
        tried.add(tuple(bush[1:]))
        distance = math.hypot(
            bush[1] + 0.5 - ep.obs["x"], bush[3] + 0.5 - ep.obs["z"])
        if distance > 3.5:
            if bush[3] > 42:
                bypass_x = min(ep.obs["x"], bush[1] + 0.5) - 10.0
                navigate(ep, bypass_x, ep.obs["z"], max_ticks=300,
                         sprint=True, tolerance=0.8, arrival_idle=0,
                         combat="creeper")
                navigate(ep, bypass_x, bush[3] + 0.5, max_ticks=300,
                         sprint=True, tolerance=0.8, arrival_idle=0,
                         combat="creeper")
            navigate(ep, bush[1] + 0.5, bush[3] + 0.5,
                     max_ticks=600, sprint=True, tolerance=1.2,
                     arrival_idle=0, combat=False)
        mine_probed_coordinate(ep, bush[1], bush[2], bush[3],
                               available_break_tool(ep.obs), max_ticks=20)
        navigate(ep, bush[1] + 0.5, bush[3] + 0.5,
                 max_ticks=120, sprint=False, tolerance=1.0,
                 arrival_idle=12, combat=False)
    if item_count(ep.obs, 280) <= stick_before:
        raise RuntimeError("portal dead bush yielded no shovel stick")
    navigate(ep, 51.5, 31.5, max_ticks=400, sprint=True,
             tolerance=0.5, arrival_idle=0, combat=False)
    ground_y = ep.obs["y"]
    pillar_up_mixed(ep, 70.0, items=(12,), centered=True)
    if os.environ.get("NETHERITE_DEBUG_PLANK_PERCH"):
        raise RuntimeError(
            f"plank perch pose=({ep.obs['x']},{ep.obs['y']},{ep.obs['z']}) "
            f"mobs={ep.obs['mobs'][:20]} blocks={ep.obs['blocks'][:40]}")
    for _ in range(24):
        if (math.hypot(ep.obs["x"] - 51.5, ep.obs["z"] - 29.5) < 0.45
                and ep.obs["y"] >= 69.5):
            break
        dyaw, dpitch = look_at(ep.obs, 51.5, 70.0, 29.5)
        ep.step(forward=1, jump=1, sprint=1,
                dyaw=dyaw, dpitch=dpitch)
    else:
        raise RuntimeError(
            f"plank-perch leap missed portal cap; pose="
            f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f})")
    navigate(ep, 50.5, 26.5, max_ticks=400, sprint=False,
             tolerance=0.35, arrival_idle=0, combat=False)
    plank_before = item_count(ep.obs, 5)
    mine_probed_coordinate(ep, 50, 69, 25,
                           available_break_tool(ep.obs), max_ticks=100)
    for _ in range(12):
        ep.step()
    if item_count(ep.obs, 5) <= plank_before:
        plank_drops = [item for item in ep.obs.get("items", [])
                       if item[0] == 5]
        if plank_drops:
            drop = min(plank_drops, key=lambda item:
                       (item[3] - ep.obs["x"]) ** 2 +
                       (item[5] - ep.obs["z"]) ** 2)
            navigate(ep, 50.5, 25.5, max_ticks=100, sprint=False,
                     tolerance=0.08, arrival_idle=12, combat=False)
            settle(ep)
    if item_count(ep.obs, 5) <= plank_before:
        raise RuntimeError(
            f"portal scaffold plank was not collected; pose="
            f"({ep.obs['x']},{ep.obs['y']},{ep.obs['z']}) "
            f"items={ep.obs.get('items', [])[:20]}")
    if ep.obs["y"] >= 69.5:
        navigate(ep, 51.5, 29.5, max_ticks=300, sprint=False,
                 tolerance=0.35, arrival_idle=0, combat=False)
        for _ in range(24):
            if (math.hypot(ep.obs["x"] - 51.5,
                           ep.obs["z"] - 31.5) < 0.45
                    and ep.obs["y"] >= 69.5):
                break
            dyaw, dpitch = look_at(ep.obs, 51.5, 70.0, 31.5)
            ep.step(forward=1, jump=1, sprint=1,
                    dyaw=dyaw, dpitch=dpitch)
        else:
            raise RuntimeError("plank-perch return leap missed sand pillar")
        descend_mixed_pillar(ep, ground_y)
    navigate(ep, 49.5, 33.5, max_ticks=300, sprint=True,
             tolerance=0.8, arrival_idle=0, combat=False)
    table = next((b for b in ep.obs["blocks"] if b[0] == 58), None)
    if table is None:
        raise RuntimeError("portal shelf crafting table is not visible")
    open_table(ep, table)
    craft_wooden_shovel(ep)


def pillar_kill_route_zombie(ep):
    """Use a two-block sand perch to clear the food-starved route safely."""
    if ep.obs["food"] >= 6:
        return
    navigate(ep, 53.5, 34.5, max_ticks=260, sprint=True,
             tolerance=0.25, arrival_idle=0, combat="creeper")
    ground_y = ep.obs["y"]
    pillar_up_mixed(ep, ground_y + 2.0, items=(12,), centered=True)
    target_eid = None
    for tick in range(900):
        zombies = [m for m in ep.obs["mobs"] if m[0] == 2]
        mob = min(zombies, key=lambda m:
                  (m[2] - ep.obs["x"]) ** 2 +
                  (m[4] - ep.obs["z"]) ** 2) if zombies else None
        if mob is None:
            if target_eid is not None:
                break
            ep.step()
            continue
        target_eid = mob[1]
        distance = math.sqrt(
            (mob[2] - ep.obs["x"]) ** 2 +
            (mob[3] + 0.9 - ep.obs["y"] - 1.62) ** 2 +
            (mob[4] - ep.obs["z"]) ** 2)
        dyaw, dpitch = look_at(ep.obs, mob[2], mob[3] + 0.9, mob[4])
        if distance <= 4.5 and tick % 13 == 0:
            slot = ensure_hotbar(ep, available_break_tool(ep.obs))
            ep.step(attack=1, do_break=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
        else:
            ep.step(dyaw=dyaw, dpitch=dpitch)
        if ep.obs["dead"]:
            raise RuntimeError("route-zombie pillar combat was lethal")
    else:
        raise RuntimeError("route zombie did not clear from sand pillar")
    drops = [item for item in ep.obs.get("items", []) if item[0] == 367]
    descend_mixed_pillar(ep, ground_y)
    if drops:
        drop = min(drops, key=lambda item:
                   (item[3] - ep.obs["x"]) ** 2 +
                   (item[5] - ep.obs["z"]) ** 2)
        navigate(ep, drop[3], drop[5], max_ticks=180, sprint=True,
                 tolerance=0.5, arrival_idle=12, combat="creeper")
    if item_count(ep.obs, 367) > 0:
        eat_available_meat(ep)


def pillar_kill_fortress_wither(ep):
    """Trap the entrance wither skeleton behind the repaired deck gap."""
    rows = [m for m in ep.obs["mobs"] if m[0] == 32 and
            abs(m[3] - ep.obs["y"]) <= 2.0]
    if not rows:
        return
    target = min(rows, key=lambda m:
                 (m[2] - ep.obs["x"]) ** 2 +
                 (m[4] - ep.obs["z"]) ** 2)
    if abs(target[4] + 4.5) > 6.0:
        return
    target_eid = target[1]
    navigate(ep, 3.5, -4.5, max_ticks=80, sprint=False,
             tolerance=0.15, arrival_idle=0, combat=False)
    barrier_slot = ensure_hotbar(ep, 24)
    barrier = []
    for y in (69, 70):
        for target_z in (-3, -4, -5):
            for _ in range(12):
                dyaw, dpitch = look_at(
                    ep.obs, 1.5, y + 0.5, target_z + 0.99)
                ep.step(use=1, do_place=1, hotbar=barrier_slot,
                        dyaw=dyaw, dpitch=dpitch, cam=1)
                ep.step(probe_x=1, probe_y=y, probe_z=target_z)
                if ep.obs["probe"][0] == 24:
                    barrier.append((1, y, target_z))
                    break
            else:
                raise RuntimeError(
                    f"fortress bulkhead did not place at 1,{y},{target_z}; "
                    f"probe={ep.obs.get('probe')} ray={ep.obs.get('ray')}")
    sword = next((item for item in (268, 272, 267, 276)
                  if item in ep.obs["inventory_ids"]), None)
    if sword is None:
        raise RuntimeError("fortress trap requires a sword")
    sword_slot = ensure_hotbar(ep, sword)
    # The deterministic entrance skeleton is just beyond melee reach of this
    # wall. Back beyond its 32-block random-despawn radius while the barrier
    # absorbs blaze fire, then return to dismantle it.
    navigate(ep, 18.5, -3.5, max_ticks=120, sprint=True,
             tolerance=0.4, arrival_idle=0, combat=False)
    ep.idle_ticks(800)
    navigate(ep, 3.5, -4.5, max_ticks=120, sprint=True,
             tolerance=0.2, arrival_idle=0, combat=False)
    seen = False
    trap_hits = 0
    for tick in range(1200):
        mob = next((m for m in ep.obs["mobs"]
                    if m[1] == target_eid and m[5] > 0.0), None)
        if mob is None:
            if seen:
                break
            ep.step()
            continue
        seen = True
        distance = math.sqrt(
            (mob[2] - ep.obs["x"]) ** 2 +
            (mob[3] + 1.2 - ep.obs["y"] - 1.62) ** 2 +
            (mob[4] - ep.obs["z"]) ** 2)
        dyaw, dpitch = look_at(ep.obs, mob[2], mob[3] + 1.2, mob[4])
        if distance <= 4.6 and tick % 11 == 0:
            ep.step(attack=1, do_break=1, jump=1, hotbar=sword_slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
            ep.step()
            trap_hits += 1
        else:
            ep.step(dyaw=dyaw, dpitch=dpitch)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"fortress wither-skeleton window trap was lethal; pose="
                f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                f"{ep.obs['z']:.3f}) target={mob} blocks="
                f"{ep.obs['blocks'][:20]}")
        if tick >= 120 and trap_hits == 0:
            break
    else:
        pass
    for x, y, z in reversed(barrier):
        mine_probed_coordinate(ep, x, y, z,
                               available_break_tool(ep.obs), max_ticks=120)
    navigate(ep, 7.5, -4.5, max_ticks=180, sprint=True,
             tolerance=0.5, arrival_idle=0, combat=False)


def seal_fortress_entrance(ep):
    """Leave the late-spawned east-side fortress pack behind a full wall."""
    navigate_serpentine(ep, 7.5, -4.5, max_ticks=140, tolerance=0.4)
    for target_z in (-4, -3, -5):
        navigate(ep, 7.5, target_z + 0.5, max_ticks=80,
                 sprint=False, tolerance=0.22, arrival_idle=0,
                 combat=False)
        for target_y in (69, 70):
            item = next((value for value in (12, 24, 35, 5)
                         if item_count(ep.obs, value) > 0), None)
            if item is None:
                raise RuntimeError("fortress entrance seal exhausted blocks")
            place_item_on(ep, 10, target_y - 1, target_z, item)
            ep.step(probe_x=10, probe_y=target_y, probe_z=target_z)
            if ep.obs["probe"][0] == 0:
                raise RuntimeError(
                    f"fortress entrance seal missed 10,{target_y},"
                    f"{target_z}")


def clear_route_blazes_behind_gate(ep, wall_x):
    """Build a low-slit fortress gate and clear blazes from its far side."""
    if nearby_mob(ep, (32,), radius=8.0, max_vertical=4.0) is not None:
        # The fortress bridge has no supporting floor outside its narrow
        # rails, so an offset jump-pillar cannot be placed reliably here.
        # Fight along the bridge axis instead; the completed previous gate
        # prevents a knockback retreat from walking us into the east pack.
        kill_nearby_hostiles(ep, radius=8.0, hostile_types=(32,),
                             corridor_target=(wall_x - 4.5, -4.5))
    stand_x = wall_x + 2.5
    # The native side rails close the lower side cells. Build one full center
    # column, then extend its stable top course sideways in two fast clicks.
    navigate(ep, stand_x, -4.5, max_ticks=100,
             sprint=False, tolerance=0.22, arrival_idle=0,
             combat=False)
    for target_y, preferred in ((69, (12, 24)), (70, (24, 12))):
        item = next((value for value in preferred
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError("fortress route gate exhausted center blocks")
        place_item_on(ep, wall_x, target_y - 1, -5, item)
    for target_z in (-4, -3):
        navigate(ep, stand_x, target_z + 0.5, max_ticks=80,
                 sprint=False, tolerance=0.25, arrival_idle=0,
                 combat=False)
        for target_y in (69, 70):
            item = next((value for value in (12, 24)
                         if item_count(ep.obs, value) > 0), None)
            if item is None:
                raise RuntimeError("fortress route gate exhausted side blocks")
            place_item_on(ep, wall_x, target_y - 1, target_z, item)
    navigate(ep, stand_x, -4.5, max_ticks=100,
             sprint=False, tolerance=0.2, arrival_idle=0,
             combat=False)
    recovery_entry = (ep.obs["health"], ep.obs["food"],
                      ep.obs["fire_ticks"], ep.obs["last_damage_source"])
    eat_available_meat(ep)
    for _ in range(360):
        if ep.obs["health"] >= 19.0 and ep.obs["fire_ticks"] <= 0:
            break
        ep.step(sneak=1)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"fortress route gate recovery was lethal; wall={wall_x} "
                f"entry={recovery_entry} health={ep.obs['health']} "
                f"food={ep.obs['food']} fire="
                f"{ep.obs['fire_ticks']} source="
                f"{ep.obs['last_damage_source']} mobs={ep.obs['mobs'][:16]}")
    # Combat and knockback may move us well outside block reach. Reacquire
    # the safe east-side working position before opening the gate.
    navigate(ep, stand_x, -4.5, max_ticks=160,
             sprint=False, tolerance=0.25, arrival_idle=0,
             combat=False)
    hand_item = available_pick(ep.obs) or next(
        (item for item in (280, 319, 363, 334, 12, 24)
         if item in ep.obs["inventory_ids"]), None)
    mine_probed_coordinate(ep, wall_x, 69, -4, hand_item, max_ticks=120)
    mine_probed_coordinate(ep, wall_x, 69, -5, hand_item, max_ticks=120)
    if nearby_mob(ep, (32,), radius=8.0, max_vertical=4.0) is not None:
        kill_wither_through_gate(ep, wall_x)
        navigate(ep, stand_x, -4.5, max_ticks=160,
                 sprint=False, tolerance=0.25, arrival_idle=0,
                 combat=False)
    # A standing player spans both y=69 and y=70.  Leaving the cap over the
    # nominal "slit" made the first gate impassable even though the floor
    # probes were clear.  Open one complete two-block doorway; keep the
    # neighbouring column intact as projectile cover.
    mine_probed_coordinate(ep, wall_x, 70, -5, hand_item, max_ticks=120)
    settle(ep)


def kill_wither_through_gate(ep, wall_x):
    """Kill west-side wither skeletons without entering their melee reach."""
    # Stay beyond the skeleton's roughly two-block melee envelope while the
    # low aim ray remains inside the player's longer interaction reach.
    stand_x = wall_x + 2.2
    navigate(ep, stand_x, -4.5, max_ticks=120,
             sprint=False, tolerance=0.2, arrival_idle=0,
             combat=False)
    eat_available_meat(ep)
    for _ in range(8):
        candidates = [mob for mob in ep.obs["mobs"]
                      if mob[0] == 32 and mob[2] < wall_x - 0.2 and
                      abs(mob[4] + 4.5) <= 1.5 and
                      mob[5] > 0.0 and
                      abs(mob[3] - ep.obs["y"]) <= 3.0 and
                      math.hypot(mob[2] - ep.obs["x"],
                                 mob[4] - ep.obs["z"]) <= 12.0]
        target = (min(candidates, key=lambda mob: math.hypot(
            mob[2] - ep.obs["x"], mob[4] - ep.obs["z"]))
                  if candidates else None)
        if target is None:
            return
        eid = target[1]
        fight_z = max(-4.7, min(-3.3, target[4]))
        navigate(ep, stand_x, fight_z, max_ticks=80,
                 sprint=False, tolerance=0.12, arrival_idle=0,
                 combat=False)
        for tick in range(600):
            current = next((mob for mob in ep.obs["mobs"]
                            if mob[1] == eid and mob[5] > 0.0), None)
            if current is None:
                break
            if abs(current[4] + 4.5) > 2.5:
                break
            # The y=70 gate cap prevents the 2.4-block mob from crossing.
            # Holding this position leaves a narrow sword-only reach band.
            aim_y = current[3] + 0.05
            distance = math.sqrt(
                (current[2] - ep.obs["x"]) ** 2 +
                (aim_y - ep.obs["y"] - 1.62) ** 2 +
                (current[4] - ep.obs["z"]) ** 2)
            dyaw, dpitch = look_at(
                ep.obs, current[2], aim_y, current[4])
            if distance <= 3.7 and tick % 13 == 0:
                sword = next((item for item in (268, 272, 267, 276)
                              if item in ep.obs["inventory_ids"]), None)
                if sword is None:
                    raise RuntimeError(
                        f"wither gate combat exhausted swords; target="
                        f"{current} tick={tick} distance={distance:.3f} "
                        f"ray={ep.obs.get('ray')} pose=({ep.obs['x']:.3f},"
                        f"{ep.obs['y']:.3f},{ep.obs['z']:.3f})")
                ep.step(attack=1, do_break=1,
                        hotbar=ensure_hotbar(ep, sword),
                        dyaw=dyaw, dpitch=dpitch, cam=1, sneak=1)
                ep.step(sneak=1)
            else:
                ep.step(dyaw=dyaw, dpitch=dpitch, sneak=1)
            if ep.obs["dead"]:
                raise RuntimeError(
                    f"wither gate combat was lethal; wall={wall_x} "
                    f"target={current} health={ep.obs['health']} "
                    f"source={ep.obs['last_damage_source']}")
        else:
            raise RuntimeError(
                f"wither {eid} never entered gate reach; wall={wall_x} "
                f"target={current} pose=({ep.obs['x']:.3f},"
                f"{ep.obs['y']:.3f},{ep.obs['z']:.3f})")
    raise RuntimeError(f"wither gate at {wall_x} did not stay clear")


def seal_fortress_route_behind(ep, wall_x, recover=True):
    """Seal a completed east-west bridge leg from its safe west side."""
    stand_x = wall_x - 2.5
    for target_z in (-5, -4, -3):
        navigate(ep, stand_x, target_z + 0.5, max_ticks=100,
                 sprint=False, tolerance=0.22, arrival_idle=0,
                 combat=False)
        for target_y in (69, 70):
            ep.step(probe_x=wall_x, probe_y=target_y,
                    probe_z=target_z)
            if ep.obs["probe"][0] != 0:
                continue
            item = next((value for value in (12, 24)
                         if item_count(ep.obs, value) > 0), None)
            if item is None:
                raise RuntimeError("fortress rear seal exhausted blocks")
            place_item_on(ep, wall_x, target_y - 1, target_z, item)
    if not recover:
        return
    navigate(ep, stand_x, -4.5, max_ticks=100,
             sprint=False, tolerance=0.2, arrival_idle=0,
             combat=False)
    recovery_entry = (ep.obs["health"], ep.obs["food"],
                      ep.obs["fire_ticks"], ep.obs["last_damage_source"])
    eat_available_meat(ep)
    for _ in range(360):
        if ep.obs["health"] >= 19.0 and ep.obs["fire_ticks"] <= 0:
            return
        ep.step(sneak=1)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"fortress rear-seal recovery was lethal; entry="
                f"{recovery_entry} mobs={ep.obs['mobs'][:16]}")
    # Raw meat can refill the hunger bar without enough saturation to heal
    # all the way to 19.  The completed wall is the actual safety invariant;
    # do not reject a live, extinguished route merely for failing an
    # over-conservative health target.
    if ep.obs["fire_ticks"] > 0 or ep.obs["health"] <= 4.0:
        raise RuntimeError(
            f"fortress rear seal left an unsafe recovery state; health="
            f"{ep.obs['health']} food={ep.obs['food']} fire="
            f"{ep.obs['fire_ticks']}")


def pillar_kill_route_creeper(ep, radius=16.0, ignored=()):
    """Kill the trench creeper above swell range but within player reach."""
    rows = [m for m in ep.obs["mobs"] if m[0] == 4
            and m[1] not in ignored
            and math.hypot(m[2] - ep.obs["x"],
                           m[4] - ep.obs["z"]) <= radius
            and abs(m[3] - ep.obs["y"]) <= 6.0]
    creeper = min(rows, key=lambda m:
                  (m[2] - ep.obs["x"]) ** 2 +
                  (m[4] - ep.obs["z"]) ** 2) if rows else None
    if creeper is None:
        return None, True
    dx = creeper[2] - ep.obs["x"]
    dz = creeper[4] - ep.obs["z"]
    length = max(0.001, math.hypot(dx, dz))
    navigate(ep, ep.obs["x"] + dx / length,
             ep.obs["z"] + dz / length, max_ticks=100,
             sprint=True, tolerance=0.6, arrival_idle=0, combat=False)
    ground_y = ep.obs["y"]
    pillar_up_mixed(ep, max(ground_y + 1.0, creeper[3] + 2.0),
                    items=(12,), centered=True)
    eid = creeper[1]
    initial_health = creeper[5]
    for _ in range(1):
        mob = next((m for m in ep.obs["mobs"] if m[1] == eid), creeper)
        dyaw, dpitch = look_at(
            ep.obs, mob[2], ep.obs["y"] + 1.62, mob[4])
        ep.step(forward=1, sneak=1, dyaw=dyaw, dpitch=dpitch)
    for tick in range(1400):
        mob = next((m for m in ep.obs["mobs"] if m[1] == eid), None)
        if mob is None:
            break
        distance = math.sqrt(
            (mob[2] - ep.obs["x"]) ** 2 +
            (mob[3] + 0.9 - ep.obs["y"] - 1.62) ** 2 +
            (mob[4] - ep.obs["z"]) ** 2)
        dyaw, dpitch = look_at(ep.obs, mob[2], mob[3] + 0.9, mob[4])
        if distance <= 5.05 and tick % 13 == 0:
            tool = available_break_tool(ep.obs)
            slot = ensure_hotbar(ep, tool)
            ep.step(attack=1, do_break=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
        else:
            ep.step(dyaw=dyaw, dpitch=dpitch)
        if ep.obs["dead"]:
            raise RuntimeError("route-creeper pillar combat was lethal")
        if tick >= 40 and mob[5] >= initial_health and ep.obs.get(
                "ray", [0])[0] != 0:
            break
    remaining = next((m for m in ep.obs["mobs"] if m[1] == eid), None)
    descend_mixed_pillar(ep, ground_y)
    return eid, remaining is None


def place_table(ep):
    table_slot = hotbar_slot(ep, 58)
    py = math.floor(ep.obs["y"]) - 1
    candidates = [b for b in ep.obs["blocks"]
                  if b != [0, 0, 0, 0] and b[2] == py and
                  1.1 < math.hypot(b[1] + 0.5 - ep.obs["x"],
                                   b[3] + 0.5 - ep.obs["z"]) < 3.2]
    if not candidates:
        raise RuntimeError(
            "no adjacent surface for the crafting table at "
            f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f}); "
            f"blocks={ep.obs['blocks'][:12]}")
    support = min(candidates, key=lambda b: math.hypot(
        b[1] + 0.5 - ep.obs["x"], b[3] + 0.5 - ep.obs["z"]))
    dyaw, dpitch = look_at(ep.obs, support[1] + 0.5,
                           support[2] + 0.95, support[3] + 0.5)
    ep.step(use=1, do_place=1, hotbar=table_slot,
            dyaw=dyaw, dpitch=dpitch, cam=1)
    if ep.obs["inv_counts"][4] != 0:
        raise RuntimeError(f"table placement failed against {support}")
    tables = [b for b in ep.obs["blocks"] if b[0] == 58]
    if not tables:
        raise RuntimeError("placed table is absent from observation")
    return min(tables, key=lambda b: math.hypot(
        b[1] + 0.5 - ep.obs["x"], b[3] + 0.5 - ep.obs["z"]))


def open_table(ep, table):
    dyaw, dpitch = look_at(ep.obs, table[1] + 0.5,
                           table[2] + 0.9, table[3] + 0.5)
    ep.step(use=1, do_place=1, dyaw=dyaw, dpitch=dpitch)
    for _ in range(3):
        if ep.obs["container"] == 1:
            break
        ep.step()
    if ep.obs["container"] != 1:
        raise RuntimeError(f"failed to open crafting table at {table}")


def craft_wooden_pick(ep):
    before = item_count(ep.obs, 270)
    plank_slot = hotbar_slot(ep, 5)
    stick_slot = next(
        slot for slot, (item, count) in enumerate(
            zip(ep.obs["hotbar_ids"], ep.obs["hotbar_counts"]))
        if item == 280 and count >= 2)
    click(ep, plank_slot)
    for slot in (36, 37, 38):
        click(ep, slot, button=1)
    click(ep, plank_slot)
    click(ep, stick_slot)
    for slot in (40, 43):
        click(ep, slot, button=1)
    click(ep, stick_slot)
    click(ep, 45)
    dst = next(slot for slot, value in enumerate(ep.obs["hotbar_ids"])
               if value == 0)
    click(ep, dst)
    if item_count(ep.obs, 270) != before + 1:
        raise RuntimeError("ordinary table clicks failed to craft wooden pick")
    ep.step(close_container=1)


def craft_wooden_sword(ep):
    """Craft a wooden sword in an already-open crafting table."""
    before_swords = item_count(ep.obs, 268)
    plank_slot = inventory_slot(ep, 5)
    stick_slot = inventory_slot(ep, 280)
    click(ep, plank_slot)
    for slot in (37, 40):
        click(ep, slot, button=1)
    click(ep, plank_slot)
    click(ep, stick_slot)
    click(ep, 43, button=1)
    click(ep, stick_slot)
    click(ep, 45)
    destination = next(slot for slot, item in
                       enumerate(ep.obs["inventory_ids"]) if item == 0)
    click(ep, destination)
    if item_count(ep.obs, 268) != before_swords + 1:
        raise RuntimeError("ordinary table clicks failed to craft wooden sword")
    ep.step(close_container=1)


def craft_stone_pick_and_furnace(ep):
    cobble_slot = hotbar_slot(ep, 4)
    stick_slot = hotbar_slot(ep, 280)
    click(ep, cobble_slot)
    for slot in (36, 37, 38):
        click(ep, slot, button=1)
    click(ep, cobble_slot)
    click(ep, stick_slot)
    for slot in (40, 43):
        click(ep, slot, button=1)
    click(ep, stick_slot)
    click(ep, 45)
    stone_pick_slot = next(slot for slot, value in enumerate(ep.obs["hotbar_ids"])
                           if value == 0)
    click(ep, stone_pick_slot)
    if ep.obs["inv_counts"][6] != 1:
        raise RuntimeError("ordinary table clicks failed to craft stone pick")
    cobble_before_furnace = ep.obs["inv_counts"][3]
    cobble_slot = hotbar_slot(ep, 4)
    click(ep, cobble_slot)
    for slot in (36, 37, 38, 39, 41, 42, 43, 44):
        click(ep, slot, button=1)
    click(ep, cobble_slot)
    ep.step()
    click(ep, 45)
    furnace_slot = next(slot for slot, value in enumerate(ep.obs["hotbar_ids"])
                        if value == 0)
    click(ep, furnace_slot)
    if (ep.obs["inv_counts"][3] != cobble_before_furnace - 8
            or 61 not in ep.obs["hotbar_ids"]):
        raise RuntimeError(
            "ordinary table clicks failed to craft furnace: "
            f"before={cobble_before_furnace} after={ep.obs['inv_counts'][3]} "
            f"hotbar={ep.obs['hotbar_ids']}/{ep.obs['hotbar_counts']}")
    ep.step(close_container=1)


def craft_furnace(ep):
    before = item_count(ep.obs, 4)
    cobble_slot = inventory_slot(ep, 4)
    click(ep, cobble_slot)
    for slot in (36, 37, 38, 39, 41, 42, 43, 44):
        click(ep, slot, button=1)
    click(ep, cobble_slot)
    ep.step()
    click(ep, 45)
    click(ep, next(slot for slot, value in enumerate(ep.obs["inventory_ids"])
                   if value == 0))
    if item_count(ep.obs, 4) != before - 8 or 61 not in ep.obs["inventory_ids"]:
        raise RuntimeError("ordinary table clicks failed to craft route furnace")
    ep.step(close_container=1)


def cook_route_mutton(ep):
    if item_count(ep.obs, 423) == 0:
        return
    table = place_table(ep)
    open_table(ep, table)
    craft_furnace(ep)
    furnace = place_furnace_on(ep, 49, 64, 32)
    open_furnace(ep, furnace)
    raw = inventory_slot(ep, 423)
    coal = inventory_slot(ep, 263)
    click(ep, raw)
    click(ep, 46)
    click(ep, coal)
    click(ep, 47, button=1)
    click(ep, coal)
    for _ in range(420):
        ep.step()
    click(ep, 48, click_type=1)
    ep.step(close_container=1)
    eat_available_meat(ep)
    for _ in range(120):
        ep.step()


def wait_for_daylight(ep):
    """Wait out the first night in an ordinary-input three-deep hide."""
    navigate(ep, 46.5, 35.5, max_ticks=180,
             sprint=False, tolerance=0.25)
    settle(ep)
    cx, cy, cz = (math.floor(ep.obs["x"]), math.floor(ep.obs["y"]),
                  math.floor(ep.obs["z"]))
    tool = ensure_hotbar(ep, 257)
    for depth in range(7):
        start_y = ep.obs["y"]
        target_y = cy - 1 - depth
        for _ in range(100):
            dyaw, dpitch = look_at(
                ep.obs, cx + 0.5, target_y + 0.5, cz + 0.5)
            ep.step(attack=1, hotbar=tool, dyaw=dyaw,
                    dpitch=dpitch, cam=1)
            if ep.obs["y"] < start_y - 0.55:
                break
        else:
            raise RuntimeError(f"failed to dig daylight hide to y={target_y}")
        settle(ep)

    # The adjacent natural ground block supplies the face for a roof over the
    # original column.  Seven blocks of vertical separation is beyond a
    # creeper explosion's six-block entity query; a shallower hide correctly
    # took the explosion's mandatory one-point, zero-density edge damage.
    roof_item = next(item for item in (3, 4, 35, 5)
                     if item_count(ep.obs, item) > 0)
    before = item_count(ep.obs, roof_item)
    slot = ensure_hotbar(ep, roof_item)
    for _ in range(12):
        dyaw, dpitch = look_at(ep.obs, cx + 1.02, cy - 0.5, cz + 0.5)
        ep.step(use=1, do_place=1, hotbar=slot,
                dyaw=dyaw, dpitch=dpitch, cam=1)
        if item_count(ep.obs, roof_item) < before:
            break
        ep.step()
    else:
        raise RuntimeError("failed to close daylight shelter roof")
    wait = (24000 - ep.obs["t"] % 24000) + 1200
    last_health = ep.obs["health"]
    previous_projectiles = ep.obs["projectiles"]
    for elapsed in range(wait):
        ep.step()
        if ep.obs["health"] < last_health:
            mobs = sorted(ep.obs["mobs"], key=lambda m:
                          (m[2] - ep.obs["x"]) ** 2 +
                          (m[3] - ep.obs["y"]) ** 2 +
                          (m[4] - ep.obs["z"]) ** 2)
            previous_projectiles = sorted(
                previous_projectiles, key=lambda p:
                (p[2] - ep.obs["x"]) ** 2 +
                (p[3] - ep.obs["y"] - 0.9) ** 2 +
                (p[4] - ep.obs["z"]) ** 2)[:8]
            raise RuntimeError(
                f"daylight hide first damage after {elapsed} ticks; "
                f"pose=({ep.obs['x']},{ep.obs['y']},{ep.obs['z']}) "
                f"health={ep.obs['health']} air={ep.obs['air']} "
                f"fall={ep.obs['fall_distance']} "
                f"source={ep.obs['last_damage_source']} "
                f"fire={ep.obs['fire_ticks']} mobs={mobs[:8]} "
                f"previous_projectiles={previous_projectiles} "
                f"projectiles={ep.obs['projectiles'][:8]} "
                f"blocks={ep.obs['blocks'][:24]}")
        last_health = ep.obs["health"]
        previous_projectiles = ep.obs["projectiles"]
        if ep.obs["dead"]:
            raise RuntimeError(
                f"daylight shelter failed after {elapsed} ticks; "
                f"health={ep.obs['health']} food={ep.obs['food']} "
                f"mobs={ep.obs['mobs'][:20]}")
    ensure_hotbar(ep, 257)
    mine_coordinate(ep, cx, cy - 1, cz, None, 257,
                    max_ticks=80, require_pickup=False, detect_depth=True)
    pillar_up(ep, float(cy), item_id=12)


def navigate(ep, target_x, target_z, max_ticks=400, sprint=True, tolerance=1.3,
             arrival_idle=8, combat=False, swim=False):
    stall = 0
    previous = ep.obs["x"], ep.obs["z"]
    for _ in range(max_ticks):
        distance = math.hypot(target_x - ep.obs["x"],
                              target_z - ep.obs["z"])
        if distance < tolerance and ep.obs.get("dead", 0) == 0:
            for _ in range(arrival_idle):
                ep.step()
            return
        dyaw, dpitch = look_at(ep.obs, target_x, ep.obs["y"] + 1.62, target_z)
        if stall > 8:
            leaves = nearest_block(ep, 18)
            leaves_reach = (math.sqrt(
                (leaves[1] + 0.5 - ep.obs["x"]) ** 2 +
                (leaves[2] + 0.5 - ep.obs["y"] - 1.62) ** 2 +
                (leaves[3] + 0.5 - ep.obs["z"]) ** 2)
                            if leaves is not None else math.inf)
            if leaves_reach <= 3.8:
                mine_block(ep, 18, None,
                           available_break_tool(ep.obs), max_ticks=80)
                stall = 0
                previous = ep.obs["x"], ep.obs["z"]
                continue
            table = nearest_block(ep, 58)
            table_reach = (math.sqrt(
                (table[1] + 0.5 - ep.obs["x"]) ** 2 +
                (table[2] + 0.5 - ep.obs["y"] - 1.62) ** 2 +
                (table[3] + 0.5 - ep.obs["z"]) ** 2)
                           if table is not None else math.inf)
            if table_reach <= 3.8:
                mine_block(ep, 58, None,
                           available_break_tool(ep.obs), max_ticks=120)
                stall = 0
                previous = ep.obs["x"], ep.obs["z"]
                continue
            ep.step(strafe=1, jump=1, dyaw=dyaw, dpitch=dpitch, cam=1)
        else:
            ep.step(forward=1, jump=int(swim or stall > 2), sprint=int(sprint), dyaw=dyaw,
                    dpitch=dpitch)
        fireball = (nearby_mob(ep, (27,), radius=12.0, max_vertical=8.0)
                    if combat is True else None)
        if fireball is not None:
            fyaw, fpitch = look_at(
                ep.obs, fireball[2], fireball[3] + 0.3, fireball[4])
            reflector = next((item for item in (268, 272, 267, 276, 270)
                              if item in ep.obs["inventory_ids"]), None)
            reflector_slot = (ensure_hotbar(ep, reflector)
                              if reflector is not None else
                              next((index for index, item in enumerate(
                                  ep.obs["hotbar_ids"]) if item == 0),
                                   ep.obs["hotbar_sel"]))
            ep.step(attack=1, do_break=1,
                    hotbar=reflector_slot,
                    dyaw=fyaw, dpitch=fpitch, cam=1)
            continue
        if combat == "creeper" and nearby_mob(ep, (4,), radius=20.0,
                                                max_vertical=6.0):
            evade_creepers(ep, radius=18.0)
        elif (combat is True and 268 in ep.obs["inventory_ids"] and
              nearby_mob(ep, (32,), radius=10.0)):
            pillar_kill_nearby_wither(ep, radius=10.0)
        elif (combat is True and 268 in ep.obs["inventory_ids"] and
              nearby_mob(ep, (2, 3, 5), radius=8.0)):
            kill_nearby_hostiles(ep, radius=10.0,
                                 hostile_types=(2, 3, 5))
        elif combat is True and nearby_mob(
                ep, (2, 3, 4, 5, 7) if 268 in ep.obs["inventory_ids"]
                else (2, 3, 4, 5, 7, 32), radius=8.0):
            evade_hostiles(ep, radius=10.0)
        current = ep.obs["x"], ep.obs["z"]
        if math.hypot(current[0] - previous[0], current[1] - previous[1]) < 0.003:
            stall += 1
        else:
            stall = 0
        previous = current
        if ep.obs.get("dead", 0):
            mobs = sorted(ep.obs["mobs"], key=lambda m:
                          (m[2] - ep.obs["x"]) ** 2 +
                          (m[3] - ep.obs["y"]) ** 2 +
                          (m[4] - ep.obs["z"]) ** 2)
            raise RuntimeError(
                f"navigation to {target_x},{target_z} was lethal; "
                f"pose=({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                f"{ep.obs['z']:.3f}) source={ep.obs['last_damage_source']} "
                f"fire={ep.obs['fire_ticks']} mobs={mobs[:8]}")
    raise RuntimeError(
        f"navigation did not reach {target_x},{target_z}; "
        f"end=({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
        f"container={ep.obs['container']} dead={ep.obs['dead']} "
        f"blocks={ep.obs['blocks'][:12]}")


def kill_nearby_wither_aggressive(ep, radius=14.0):
    """Rush one route-blocking wither skeleton with ordinary sword swings."""
    mob = nearby_mob(ep, (32,), radius=radius, max_vertical=3.0)
    if mob is None:
        return
    sword = next((item for item in (268, 272, 267, 276)
                  if item in ep.obs["inventory_ids"]), None)
    if sword is None:
        raise RuntimeError("wither-skeleton rush has no sword")
    slot = ensure_hotbar(ep, sword)
    eid = mob[1]
    for _ in range(360):
        current = next((row for row in ep.obs["mobs"]
                        if row[1] == eid and row[5] > 0.0), None)
        if current is None:
            return
        distance = math.sqrt(
            (current[2] - ep.obs["x"]) ** 2 +
            (current[3] + 1.0 - ep.obs["y"] - 1.62) ** 2 +
            (current[4] - ep.obs["z"]) ** 2)
        dyaw, dpitch = look_at(
            ep.obs, current[2], current[3] + 1.0, current[4])
        if distance <= 3.55:
            ep.step(attack=1, do_break=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
        else:
            ep.step(forward=1, sprint=1, jump=int(distance > 5.0),
                    dyaw=dyaw, dpitch=dpitch)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"wither-skeleton rush {eid} was lethal; target={current} "
                f"source={ep.obs['last_damage_source']} mobs="
                f"{ep.obs['mobs'][:12]}")
    raise RuntimeError(f"wither-skeleton rush did not kill {eid}")


def navigate_serpentine(ep, target_x, target_z, max_ticks=500,
                        tolerance=1.0):
    """Cross a straight hostile corridor while varying projectile lead."""
    for tick in range(max_ticks):
        if math.hypot(target_x - ep.obs["x"],
                      target_z - ep.obs["z"]) < tolerance:
            return
        if (268 in ep.obs["inventory_ids"] and
                nearby_mob(ep, (32,), radius=12.0) is not None):
            kill_nearby_wither_aggressive(ep, radius=14.0)
            continue
        fireballs = [m for m in ep.obs["mobs"] if m[0] == 27
                     and math.sqrt((m[2] - ep.obs["x"]) ** 2 +
                                   (m[3] + 0.3 - ep.obs["y"] - 1.62) ** 2 +
                                   (m[4] - ep.obs["z"]) ** 2) <= 2.5]
        if fireballs:
            fireball = min(fireballs, key=lambda m:
                           (m[2] - ep.obs["x"]) ** 2 +
                           (m[4] - ep.obs["z"]) ** 2)
            dyaw, dpitch = look_at(
                ep.obs, fireball[2], fireball[3] + 0.3, fireball[4])
            ep.step(attack=1, do_break=1,
                    hotbar=ensure_hotbar(ep, available_break_tool(ep.obs)),
                    strafe=1 if (tick // 7) % 2 == 0 else -1,
                    jump=1, dyaw=dyaw, dpitch=dpitch, cam=1)
            if ep.obs["dead"]:
                raise RuntimeError("fireball reflection was lethal")
            continue
        dyaw, dpitch = look_at(
            ep.obs, target_x, ep.obs["y"] + 1.62, target_z)
        ep.step(forward=1, strafe=1 if (tick // 7) % 2 == 0 else -1,
                jump=1, sprint=1, dyaw=dyaw, dpitch=dpitch)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"serpentine route was lethal at "
                f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                f"{ep.obs['z']:.3f}); source="
                f"{ep.obs['last_damage_source']} health="
                f"{ep.obs['health']} fire={ep.obs['fire_ticks']} mobs="
                f"{ep.obs['mobs'][:16]} projectiles="
                f"{ep.obs.get('projectiles', [])[:12]}")
    raise RuntimeError(
        f"serpentine route did not reach {target_x},{target_z}; pose="
        f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
        f"dead={ep.obs['dead']} health={ep.obs['health']} source="
        f"{ep.obs['last_damage_source']} fire={ep.obs['fire_ticks']} "
        f"mobs={ep.obs['mobs'][:12]}")


def navigate_carving(ep, target_x, target_z, max_ticks=800,
                     tolerance=1.0):
    """Walk a route while cutting two-high cells through a steep dune."""
    previous = ep.obs["x"], ep.obs["z"]
    stall = 0
    for _ in range(max_ticks):
        dx = target_x - ep.obs["x"]
        dz = target_z - ep.obs["z"]
        distance = math.hypot(dx, dz)
        if distance < tolerance:
            return
        ux, uz = dx / distance, dz / distance
        cut = False
        if stall >= 3:
            cell_x = math.floor(ep.obs["x"])
            cell_z = math.floor(ep.obs["z"])
            step_x = 1 if ux > 0.05 else -1 if ux < -0.05 else 0
            step_z = 1 if uz > 0.05 else -1 if uz < -0.05 else 0
            front_cells = {(cell_x + step_x, cell_z),
                           (cell_x, cell_z + step_z),
                           (cell_x + step_x, cell_z + step_z)}
            base_y = math.floor(ep.obs["y"])
            for front_x, front_z in front_cells:
                for target_y in (base_y, base_y + 1):
                    ep.step(probe_x=front_x, probe_y=target_y,
                            probe_z=front_z)
                    block = ep.obs["probe"][0]
                    if block in (0, 8, 9, 10, 11):
                        continue
                    tool = (next((item for item in
                                  (280, 319, 363, 365, 423)
                                  if item in ep.obs["inventory_ids"]), None)
                            if block == 12 else available_pick(ep.obs) or
                            available_break_tool(ep.obs))
                    if mine_probed_coordinate(
                            ep, front_x, target_y, front_z, tool,
                            max_ticks=300, allow_disengage=True):
                        cut = True
            stall = 0
        dyaw, dpitch = look_at(
            ep.obs, target_x, ep.obs["y"] + 1.62, target_z)
        ep.step(forward=1, sprint=int(not cut), jump=1,
                dyaw=dyaw, dpitch=dpitch)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"dune escape was lethal; pose=({ep.obs['x']:.3f},"
                f"{ep.obs['y']:.3f},{ep.obs['z']:.3f}) source="
                f"{ep.obs['last_damage_source']} mobs={ep.obs['mobs'][:12]}")
        current = ep.obs["x"], ep.obs["z"]
        if math.hypot(current[0] - previous[0],
                      current[1] - previous[1]) < 0.003:
            stall += 1
        else:
            stall = 0
        previous = current
    raise RuntimeError(
        f"dune escape did not reach {target_x},{target_z}; pose=("
        f"{ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f})")


def settle(ep, max_ticks=80):
    stable = 0
    previous_y = ep.obs["y"]
    for _ in range(max_ticks):
        ep.step()
        if ep.obs["dead"]:
            raise RuntimeError(
                f"settling was lethal; pose=({ep.obs['x']:.3f},"
                f"{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
                f"health={ep.obs['health']} food={ep.obs['food']} "
                f"air={ep.obs['air']} fall={ep.obs['fall_distance']} "
                f"source={ep.obs['last_damage_source']} "
                f"local={[b for b in ep.obs['blocks'] if b != [0, 0, 0, 0] and abs(b[1] - math.floor(ep.obs['x'])) <= 1 and abs(b[2] - math.floor(ep.obs['y'])) <= 2 and abs(b[3] - math.floor(ep.obs['z'])) <= 1]} "
                f"mobs={ep.obs['mobs'][:12]}")
        if abs(ep.obs["y"] - previous_y) < 1e-5:
            stable += 1
            if stable >= 4:
                return
        else:
            stable = 0
        previous_y = ep.obs["y"]
    raise RuntimeError("player did not settle")


def nearest_block(ep, block_id, max_vertical=4):
    candidates = [b for b in ep.obs["blocks"]
                  if b[0] == block_id and
                  abs((b[2] + 0.5) - (ep.obs["y"] + 1.0)) <= max_vertical]
    if not candidates:
        return None
    return min(candidates, key=lambda b: (
        (b[1] + 0.5 - ep.obs["x"]) ** 2 +
        (b[2] + 0.5 - ep.obs["y"] - 1.62) ** 2 +
        (b[3] + 0.5 - ep.obs["z"]) ** 2))


def mine_block(ep, block_id, drop_count_index, tool_item, max_ticks=180):
    target = nearest_block(ep, block_id)
    if target is None:
        raise RuntimeError(
            f"no visible block {block_id} to mine at "
            f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f}); "
            f"blocks={ep.obs['blocks'][:16]}")
    for _ in range(120):
        distance = math.sqrt((target[1] + 0.5 - ep.obs["x"]) ** 2 +
                             (target[2] + 0.5 - ep.obs["y"] - 1.62) ** 2 +
                             (target[3] + 0.5 - ep.obs["z"]) ** 2)
        if distance < 4.0:
            break
        dyaw, dpitch = look_at(ep.obs, target[1] + 0.5,
                              target[2] + 0.5, target[3] + 0.5)
        ep.step(forward=1, jump=1, dyaw=dyaw, dpitch=dpitch)
    before = (ep.obs["inv_counts"][drop_count_index]
              if drop_count_index is not None else 0)
    tool_slot = hotbar_slot(ep, tool_item)
    for _ in range(max_ticks):
        dyaw, dpitch = look_at(ep.obs, target[1] + 0.5,
                              target[2] + 0.5, target[3] + 0.5)
        ep.step(attack=1, hotbar=tool_slot, dyaw=dyaw, dpitch=dpitch, cam=1)
        if target not in ep.obs["blocks"]:
            if drop_count_index is None:
                return target
            for _ in range(50):
                dyaw, dpitch = look_at(ep.obs, target[1] + 0.5,
                                      target[2] + 0.5, target[3] + 0.5)
                ep.step(forward=1, jump=1, dyaw=dyaw, dpitch=dpitch)
                if ep.obs["inv_counts"][drop_count_index] > before:
                    return target
            break
    raise RuntimeError(
        f"block {target} did not yield its expected drop; "
        f"end=({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
        f"counts={ep.obs['inv_counts']} visible={target in ep.obs['blocks']}")


def mine_block_item(ep, block_id, item_id, tool_item, max_ticks=180):
    candidates = [b for b in ep.obs["blocks"] if b[0] == block_id and
                  1.2 < math.hypot(b[1] + 0.5 - ep.obs["x"],
                                   b[3] + 0.5 - ep.obs["z"]) < 4.2]
    if not candidates:
        raise RuntimeError(f"no safe visible block {block_id} to mine")
    target = min(candidates, key=lambda b: (
        (b[1] + 0.5 - ep.obs["x"]) ** 2 +
        (b[2] + 0.5 - ep.obs["y"] - 1.62) ** 2 +
        (b[3] + 0.5 - ep.obs["z"]) ** 2))
    before = item_count(ep.obs, item_id)
    tool_slot = ensure_hotbar(ep, tool_item)
    for _ in range(max_ticks):
        dyaw, dpitch = look_at(ep.obs, target[1] + 0.5,
                              target[2] + 0.5, target[3] + 0.5)
        ep.step(attack=1, hotbar=tool_slot, dyaw=dyaw,
                dpitch=dpitch, cam=1)
        if target not in ep.obs["blocks"]:
            for _ in range(60):
                dyaw, dpitch = look_at(ep.obs, target[1] + 0.5,
                                      target[2] + 0.5, target[3] + 0.5)
                ep.step(forward=1, jump=1, dyaw=dyaw, dpitch=dpitch)
                if item_count(ep.obs, item_id) > before:
                    return target
            break
    raise RuntimeError(f"block {target} did not yield item {item_id}")


def mine_coordinate(ep, x, y, z, drop_count_index, tool_item, max_ticks=180,
                    require_pickup=True, aim_y=None, detect_depth=True):
    before = (ep.obs["inv_counts"][drop_count_index]
              if require_pickup else 0)
    tool_slot = hotbar_slot(ep, tool_item)
    initial_depth = None
    initially_visible = any(
        b != [0, 0, 0, 0] and b[1:] == [x, y, z]
        for b in ep.obs["blocks"])
    for _ in range(max_ticks):
        dyaw, dpitch = look_at(ep.obs, x + 0.5,
                              y + 0.5 if aim_y is None else aim_y, z + 0.5)
        ep.step(attack=1, hotbar=tool_slot, dyaw=dyaw, dpitch=dpitch, cam=1)
        depth = ep.obs["depth"][18 * 64 + 32]
        if initial_depth is None:
            initial_depth = depth
        if (require_pickup and
                ep.obs["inv_counts"][drop_count_index] > before):
            return
        if (not require_pickup and initially_visible and _ >= 1 and
                not any(b != [0, 0, 0, 0] and b[1:] == [x, y, z]
                        for b in ep.obs["blocks"])):
            return
        if (not require_pickup and detect_depth and _ >= 5 and
                not initially_visible and depth > initial_depth + 2):
            return
        if not require_pickup and not initially_visible and _ >= max_ticks - 1:
            return
    raise RuntimeError(f"failed to mine coordinate {x},{y},{z}")


def mine_probed_coordinate(ep, x, y, z, tool_item, max_ticks=180,
                           allow_disengage=False):
    """Mine one exact coordinate using the RL harness's direct world probe."""
    slot = (ensure_hotbar(ep, tool_item) if tool_item is not None else
            next((index for index, item in enumerate(ep.obs["hotbar_ids"])
                  if item == 0), ep.obs["hotbar_sel"]))
    for _ in range(max_ticks):
        reach = math.sqrt(
            (x + 0.5 - ep.obs["x"]) ** 2 +
            (y + 0.5 - ep.obs["y"] - 1.62) ** 2 +
            (z + 0.5 - ep.obs["z"]) ** 2)
        if allow_disengage and reach > 4.5:
            return False
        dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.5, z + 0.5)
        ep.step(attack=1, hotbar=slot, dyaw=dyaw, dpitch=dpitch, cam=1,
                probe_x=x, probe_y=y, probe_z=z)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"probed mining was lethal at {x},{y},{z}; "
                f"pose=({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                f"{ep.obs['z']:.3f}) health={ep.obs['health']} "
                f"source={ep.obs['last_damage_source']} "
                f"mobs={ep.obs['mobs'][:12]}")
        # Gravity and fluid updates can replace a successfully broken block
        # in the same tick. The requested solid is gone in either case.
        if ep.obs.get("probe", [1])[0] in (0, 8, 9, 10, 11):
            return True
    raise RuntimeError(
        f"probed block {x},{y},{z} did not break; "
        f"probe={ep.obs.get('probe')} pose=({ep.obs['x']:.3f},"
        f"{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
        f"look=({ep.obs['yaw']:.3f},{ep.obs['pitch']:.3f}) "
        f"tool={tool_item} hotbar={ep.obs['hotbar_ids']}/"
        f"{ep.obs['hotbar_counts']} metas={ep.obs.get('hotbar_metas')} "
        f"ray={ep.obs.get('ray')} dig={ep.obs.get('dig')}")


def gather_portal_shelf_sand(ep, target_count):
    """Cut a connected one-block trench through the portal's sand shelf."""
    if item_count(ep.obs, 12) >= target_count:
        return
    # Sand receives no mining-speed benefit from a pickaxe. Use an empty hand
    # so the route pick survives for fortress brick quarrying.
    tool = None
    if ep.obs["y"] > 65.5:
        navigate(ep, 51.5, 30.5, max_ticks=500, sprint=False,
                 tolerance=0.3, arrival_idle=0, combat=False)
        settle(ep)
    # Enter the trench beside the portal. Then depth-first search the connected
    # sand layer, rejecting old construction cells before the camera ray can
    # strike their one-block-above obstruction.
    start = (51, 30)
    ep.step(probe_x=start[0], probe_y=64, probe_z=start[1])
    if ep.obs["probe"][0] == 12:
        mine_probed_coordinate(ep, start[0], 64, start[1], tool,
                               max_ticks=80)
        settle(ep)
    stack = [start]
    visited = {start}
    ignored_creepers = set()
    while stack and item_count(ep.obs, 12) < target_count:
        x, z = stack[-1]
        if ep.obs["y"] < 62.5:
            pillar_up_mixed(ep, 64.0, items=(12,), centered=True)
        for mob in ep.obs["mobs"]:
            if (mob[0] == 4 and mob[1] in ignored_creepers
                    and math.hypot(mob[2] - ep.obs["x"],
                                   mob[4] - ep.obs["z"]) <= 10.0):
                ignored_creepers.discard(mob[1])
        nearby_unhandled = [m for m in ep.obs["mobs"]
                            if m[0] == 4 and m[1] not in ignored_creepers
                            and math.hypot(m[2] - ep.obs["x"],
                                           m[4] - ep.obs["z"]) <= 16.0
                            and abs(m[3] - ep.obs["y"]) <= 6.0]
        if nearby_unhandled:
            eid, cleared = pillar_kill_route_creeper(
                ep, radius=16.0, ignored=ignored_creepers)
            if eid is not None and not cleared:
                ignored_creepers.add(eid)
            navigate(ep, x + 0.5, z + 0.5, max_ticks=500,
                     sprint=True, tolerance=0.25, arrival_idle=0,
                     combat=False)
            continue
        destination = None
        for nx, nz in ((x - 1, z), (x, z + 1),
                       (x + 1, z), (x, z - 1)):
            if ((nx, nz) in visited or not 40 <= nx <= 62 or
                    not 20 <= nz <= 40):
                continue
            visited.add((nx, nz))
            ep.step(probe_x=nx, probe_y=65, probe_z=nz)
            if ep.obs["probe"][0] != 0:
                continue
            ep.step(probe_x=nx, probe_y=66, probe_z=nz)
            if ep.obs["probe"][0] != 0:
                continue
            ep.step(probe_x=nx, probe_y=64, probe_z=nz)
            if ep.obs["probe"][0] == 12:
                destination = nx, nz
                break
        if destination is None:
            stack.pop()
            if stack:
                bx, bz = stack[-1]
                try:
                    navigate(ep, bx + 0.5, bz + 0.5, max_ticks=220,
                             sprint=False, tolerance=0.18, arrival_idle=0,
                             combat=False)
                except RuntimeError:
                    if ep.obs["dead"] or ep.obs["y"] >= 62.5:
                        raise
                    pillar_up_mixed(ep, 64.0, items=(12,), centered=True)
                    navigate(ep, bx + 0.5, bz + 0.5, max_ticks=300,
                             sprint=False, tolerance=0.25, arrival_idle=0,
                             combat=False)
            continue
        nx, nz = destination
        before = item_count(ep.obs, 12)
        carried = tuple(value for value, count in zip(
            ep.obs["inventory_ids"], ep.obs["inventory_counts"])
                        if value and count)
        try:
            mine_probed_coordinate(ep, nx, 64, nz, tool, max_ticks=80)
            if ep.obs["y"] < 62.5:
                pillar_up_mixed(ep, 64.0, items=(12,), centered=True)
            navigate(ep, nx + 0.5, nz + 0.5, max_ticks=220,
                     sprint=False, tolerance=0.18, arrival_idle=3,
                     combat=False)
        except RuntimeError:
            if not ep.obs["dead"] and ep.obs["y"] < 62.5:
                pillar_up_mixed(ep, 64.0, items=(12,), centered=True)
                navigate(ep, nx + 0.5, nz + 0.5, max_ticks=300,
                         sprint=False, tolerance=0.25, arrival_idle=3,
                         combat=False)
                stack.append(destination)
                continue
            if not ep.obs["dead"]:
                raise
            death_x, death_z = ep.obs["x"], ep.obs["z"]
            respawn_and_recover_drops(
                ep, death_x, death_z, required=carried, lives=4)
            tool = None
            ep.step(probe_x=nx, probe_y=64, probe_z=nz)
            if ep.obs["probe"][0] == 12:
                mine_probed_coordinate(ep, nx, 64, nz, tool,
                                       max_ticks=80)
            navigate(ep, nx + 0.5, nz + 0.5, max_ticks=100,
                     sprint=False, tolerance=0.25, arrival_idle=3,
                     combat=False)
        stack.append(destination)
        if item_count(ep.obs, 12) <= before:
            for _ in range(20):
                ep.step()
                if item_count(ep.obs, 12) > before:
                    break
        if ep.obs["dead"]:
            raise RuntimeError("portal shelf sand gathering was lethal")
    if item_count(ep.obs, 12) >= target_count:
        return
    raise RuntimeError(
        f"portal shelf exhausted at {item_count(ep.obs, 12)}/"
        f"{target_count} sand")


def wait_portal_shelf_daylight(ep):
    """Pass one night in a sealed two-block cell under the sand shelf."""
    if ep.obs["world_time"] % 24000 < 9000:
        return
    gather_portal_shelf_sand(ep, 4)
    craft_sandstone(ep, 1)
    cx = math.floor(ep.obs["x"])
    cz = math.floor(ep.obs["z"])
    surface_y = math.floor(ep.obs["y"])
    for target_y in (surface_y - 1, surface_y - 2, surface_y - 3):
        mine_probed_coordinate(
            ep, cx, target_y, cz, available_break_tool(ep.obs),
            max_ticks=300)
        settle(ep)
    roof_slot = ensure_hotbar(ep, 24)
    for _ in range(20):
        dyaw, dpitch = look_at(
            ep.obs, cx + 1.02, surface_y - 0.5, cz + 0.5)
        ep.step(use=1, do_place=1, hotbar=roof_slot,
                dyaw=dyaw, dpitch=dpitch, cam=1,
                probe_x=cx, probe_y=surface_y - 1, probe_z=cz)
        if ep.obs["probe"][0] == 24:
            break
    else:
        raise RuntimeError("failed to seal portal-shelf daylight cell")
    phase = ep.obs["world_time"] % 24000
    ep.idle_ticks(24000 - phase + 1200)
    if ep.obs["dead"] or ep.obs["health"] < 20.0:
        raise RuntimeError(
            f"portal-shelf daylight cell was unsafe; health="
            f"{ep.obs['health']} source={ep.obs['last_damage_source']}")
    mine_probed_coordinate(
        ep, cx, surface_y - 1, cz, available_break_tool(ep.obs),
        max_ticks=500)
    for _ in range(20):
        ep.step()
        if item_count(ep.obs, 24) >= 1:
            break
    pillar_up(ep, float(surface_y), item_id=12)


def place_table_on(ep, x, y, z):
    table_slot = hotbar_slot(ep, 58)
    dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.95, z + 0.5)
    ep.step(use=1, do_place=1, hotbar=table_slot,
            dyaw=dyaw, dpitch=dpitch, cam=1)
    if ep.obs["inv_counts"][4] != 0:
        raise RuntimeError(f"failed to place table on {x},{y},{z}")
    return [58, x, y + 1, z]


def place_furnace_on(ep, x, y, z):
    furnace_slot = hotbar_slot(ep, 61)
    dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.95, z + 0.5)
    ep.step(use=1, do_place=1, hotbar=furnace_slot,
            dyaw=dyaw, dpitch=dpitch, cam=1)
    if ep.obs["inv_iron"][0] != 0:
        raise RuntimeError(f"failed to place furnace on {x},{y},{z}")
    return [61, x, y + 1, z]


def place_item_on(ep, x, y, z, item_id):
    """Place one inventory block on the top face of an exact support."""
    before = item_count(ep.obs, item_id)
    slot = ensure_hotbar(ep, item_id)
    for _ in range(12):
        dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.98, z + 0.5)
        ep.step(use=1, do_place=1, hotbar=slot,
                dyaw=dyaw, dpitch=dpitch, cam=1)
        if item_count(ep.obs, item_id) < before:
            return
        ep.step()
    ep.step(probe_x=x, probe_y=y, probe_z=z)
    support = ep.obs["probe"]
    raise RuntimeError(
        f"failed to place item {item_id} on {x},{y},{z}; "
        f"pose=({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
        f"support={support} health={ep.obs['health']} mobs="
        f"{ep.obs['mobs'][:12]}")


def place_wall_top_shifted_x(ep, x, y, z, item_id):
    """Place atop a narrow wall whose live ray coordinates are x-1 shifted."""
    before = item_count(ep.obs, item_id)
    slot = ensure_hotbar(ep, item_id)
    for _ in range(12):
        dyaw, dpitch = look_at(ep.obs, x - 0.5, y + 0.98, z + 0.5)
        ep.step(use=1, do_place=1, hotbar=slot,
                dyaw=dyaw, dpitch=dpitch, cam=1)
        ep.step(probe_x=x, probe_y=y + 1, probe_z=z)
        if ep.obs["probe"][0] != 0:
            return
        if item_count(ep.obs, item_id) < before:
            probes = {}
            for probe_x in range(x - 2, x + 2):
                ep.step(probe_x=probe_x, probe_y=y + 1, probe_z=z)
                probes[probe_x] = ep.obs["probe"]
            raise RuntimeError(
                f"shifted wall-top placement missed {x},{y + 1},{z}; "
                f"ray={ep.obs.get('ray')} probes={probes}")
    raise RuntimeError(
        f"shifted wall-top placement failed at {x},{y + 1},{z}; "
        f"ray={ep.obs.get('ray')}")


def solidify_adjacent_shallow_lava(ep, item_id=12):
    """Replace one supported neighboring lava cell with a walkable block."""
    y = math.floor(ep.obs["y"])
    x = math.floor(ep.obs["x"])
    z = math.floor(ep.obs["z"])
    ep.step(probe_x=x, probe_y=y, probe_z=z)
    if ep.obs["probe"][0] not in (10, 11):
        return False
    probes = []
    for nx, nz in ((x + 1, z), (x, z - 1),
                   (x - 1, z), (x, z + 1)):
        ep.step(probe_x=nx, probe_y=y, probe_z=nz)
        fluid = ep.obs["probe"][0]
        ep.step(probe_x=nx, probe_y=y - 1, probe_z=nz)
        support = ep.obs["probe"][0]
        probes.append((nx, nz, fluid, support))
        if fluid in (10, 11) and support not in (0, 8, 9, 10, 11):
            place_item_on(ep, nx, y - 1, nz, item_id)
            navigate(ep, nx + 0.5, nz + 0.5, max_ticks=120,
                     sprint=False, tolerance=0.12,
                     arrival_idle=0, combat=False)
            settle(ep)
            return True
    raise RuntimeError(
        f"no supported adjacent lava escape at {x},{y},{z}: {probes}")


def bridge_positive_z(ep, target_z, item_id=4):
    """Extend and walk a one-wide bridge over a drop or fluid."""
    slot = ensure_hotbar(ep, item_id)
    while ep.obs["z"] < target_z - 0.25:
        x = math.floor(ep.obs["x"])
        y = math.floor(ep.obs["y"]) - 1
        z = math.floor(ep.obs["z"])
        before = item_count(ep.obs, item_id)
        for _ in range(10):
            dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.5, z + 0.98)
            ep.step(use=1, do_place=1, sneak=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
            if item_count(ep.obs, item_id) < before:
                break
            ep.step()
        navigate(ep, x + 0.5, z + 1.5, max_ticks=80,
                 sprint=False, tolerance=0.2)


def bridge_positive_axis_mixed(ep, axis, target):
    """Build a level +X or +Z bridge, consuming only stable blocks."""
    if axis not in ("x", "z"):
        raise ValueError(axis)
    while ep.obs[axis] < target - 0.25:
        item = next((value for value in (112, 24, 4, 3, 35, 5, 1)
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            if ep.can_save:
                ep.step(save_slot="portal-bridge-front")
            raise RuntimeError(
                f"elevated {axis}-bridge exhausted stable blocks at "
                f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f})")
        slot = ensure_hotbar(ep, item)
        x = math.floor(ep.obs["x"])
        y = math.floor(ep.obs["y"]) - 1
        z = math.floor(ep.obs["z"])
        next_x = x + (1 if axis == "x" else 0)
        next_z = z + (1 if axis == "z" else 0)
        ep.step(probe_x=next_x, probe_y=y, probe_z=next_z)
        if ep.obs["probe"][0] != 0:
            navigate(ep, next_x + 0.5, next_z + 0.5, max_ticks=80,
                     sprint=False, tolerance=0.2, arrival_idle=0)
            continue
        for _ in range(20):
            edge_x = x + (2.0 if axis == "x" else 0.5)
            edge_z = z + (2.0 if axis == "z" else 0.5)
            dyaw, dpitch = look_at(
                ep.obs, edge_x, ep.obs["y"] + 1.62, edge_z)
            ep.step(forward=1, sneak=1, dyaw=dyaw, dpitch=dpitch)
            coordinate = ep.obs[axis]
            cell = x if axis == "x" else z
            if coordinate >= cell + 1.26:
                break
        before = item_count(ep.obs, item)
        for _ in range(12):
            aim_x = x + (1.0 if axis == "x" else 0.5)
            aim_z = z + (1.0 if axis == "z" else 0.5)
            dyaw, dpitch = look_at(ep.obs, aim_x, y + 0.5, aim_z)
            ep.step(use=1, do_place=1, sneak=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
            if item_count(ep.obs, item) < before:
                break
        else:
            raise RuntimeError(
                f"elevated {axis}-bridge placement failed at "
                f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                f"{ep.obs['z']:.3f}); support={x},{y},{z} item={item} "
                f"look=({ep.obs['yaw']:.2f},{ep.obs['pitch']:.2f}) "
                f"blocks={ep.obs['blocks'][:20]}")
        navigate(ep, x + (1.5 if axis == "x" else 0.5),
                 z + (1.5 if axis == "z" else 0.5), max_ticks=80,
                 sprint=False, tolerance=0.2, arrival_idle=0)


def bridge_negative_z_mixed(ep, target_z):
    """Repair and cross a level bridge toward decreasing Z."""
    while ep.obs["z"] > target_z + 0.25:
        item = next((value for value in (24, 4, 3, 35, 5, 1)
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError("negative-z bridge exhausted stable blocks")
        x = math.floor(ep.obs["x"])
        y = math.floor(ep.obs["y"]) - 1
        z = math.floor(ep.obs["z"])
        for _ in range(24):
            dyaw, dpitch = look_at(
                ep.obs, x + 0.5, ep.obs["y"] + 1.62, z - 1.0)
            ep.step(forward=1, sneak=1, dyaw=dyaw, dpitch=dpitch)
            if ep.obs["z"] <= z - 0.26:
                break
        before = item_count(ep.obs, item)
        slot = ensure_hotbar(ep, item)
        for _ in range(16):
            dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.5, z)
            ep.step(use=1, do_place=1, sneak=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
            if item_count(ep.obs, item) < before:
                break
        else:
            raise RuntimeError(
                f"negative-z bridge placement failed at "
                f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                f"{ep.obs['z']:.3f})")
        navigate(ep, x + 0.5, z - 0.5, max_ticks=80,
                 sprint=False, tolerance=0.2, arrival_idle=0)


def bridge_negative_x_mixed(ep, target_x):
    """Repair and cross a level bridge toward decreasing X."""
    while ep.obs["x"] > target_x + 0.25:
        item = next((value for value in (112, 24, 4, 3, 35, 5, 1)
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError("negative-x bridge exhausted stable blocks")
        x = math.floor(ep.obs["x"])
        y = math.floor(ep.obs["y"]) - 1
        z = math.floor(ep.obs["z"])
        ep.step(probe_x=x - 1, probe_y=y, probe_z=z)
        if ep.obs["probe"][0] != 0:
            navigate(ep, x - 0.5, z + 0.5, max_ticks=80,
                     sprint=False, tolerance=0.2, arrival_idle=0)
            continue
        for _ in range(24):
            dyaw, dpitch = look_at(
                ep.obs, x - 1.0, ep.obs["y"] + 1.62, z + 0.5)
            ep.step(forward=1, sneak=1, dyaw=dyaw, dpitch=dpitch)
            if ep.obs["x"] <= x - 0.05:
                break
        before = item_count(ep.obs, item)
        slot = ensure_hotbar(ep, item)
        for _ in range(16):
            dyaw, dpitch = look_at(ep.obs, x + 0.0, y + 0.5, z + 0.5)
            ep.step(use=1, do_place=1, sneak=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
            if item_count(ep.obs, item) < before:
                break
        else:
            raise RuntimeError(
                f"negative-x bridge placement failed at "
                f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                f"{ep.obs['z']:.3f}); support={x},{y},{z} "
                f"ray={ep.obs.get('ray')} blocks={ep.obs['blocks'][:20]}")
        navigate(ep, x - 0.5, z + 0.5, max_ticks=80,
                 sprint=False, tolerance=0.2, arrival_idle=0)


def descending_sand_stair_positive_z(ep):
    """Use 4+3+2+1 falling sand blocks as a no-damage bridge descent."""
    for height in (4, 3, 2, 1):
        x = math.floor(ep.obs["x"])
        y = math.floor(ep.obs["y"]) - 1
        z = math.floor(ep.obs["z"])
        for _ in range(24):
            dyaw, dpitch = look_at(
                ep.obs, x + 0.5, ep.obs["y"] + 1.62, z + 2.0)
            ep.step(forward=1, sneak=1, dyaw=dyaw, dpitch=dpitch)
            if ep.obs["y"] < 68.0:
                # This runtime does not edge-clamp crouching on an isolated
                # bridge block.  The resulting surface landing is still a
                # valid ordinary-input descent; continue from it.
                settle(ep)
                return
            if ep.obs["z"] >= z + 1.26:
                break
        slot = ensure_hotbar(ep, 12)
        for _ in range(height):
            before = item_count(ep.obs, 12)
            for _ in range(20):
                dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.5,
                                      z + 1.0)
                ep.step(use=1, do_place=1, sneak=1, hotbar=slot,
                        dyaw=dyaw, dpitch=dpitch, cam=1)
                if ep.obs["y"] < 68.0:
                    settle(ep)
                    return
                if item_count(ep.obs, 12) < before:
                    break
            else:
                raise RuntimeError(
                    f"falling-sand stair placement failed; height={height} "
                    f"support={x},{y},{z} pose=({ep.obs['x']:.3f},"
                    f"{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
                    f"look=({ep.obs['yaw']:.2f},{ep.obs['pitch']:.2f}) "
                    f"blocks={ep.obs['blocks'][:20]}")
            for _ in range(8):
                ep.step(sneak=1)
        navigate(ep, x + 0.5, z + 1.5, max_ticks=100,
                 sprint=False, tolerance=0.2, arrival_idle=0)
        settle(ep)


def open_furnace(ep, furnace):
    dyaw, dpitch = look_at(ep.obs, furnace[1] + 0.5,
                           furnace[2] + 0.8, furnace[3] + 0.5)
    ep.step(use=1, do_place=1, dyaw=dyaw, dpitch=dpitch)
    for _ in range(3):
        if ep.obs["container"] == 2:
            break
        ep.step()
    if ep.obs["container"] != 2:
        raise RuntimeError(f"failed to open furnace at {furnace}")


def smelt_iron(ep, count):
    before_ingots = ep.obs["inv_iron"][2]
    ore_slot = inventory_slot(ep, 15)
    coal_slot = inventory_slot(ep, 263)
    click(ep, ore_slot)
    click(ep, 46)
    click(ep, coal_slot)
    click(ep, 47, button=1)
    click(ep, coal_slot)
    for _ in range(count * 200 + 8):
        ep.step()
    click(ep, 48, click_type=1)
    if ep.obs["inv_iron"][2] < before_ingots + count:
        raise RuntimeError(
            f"furnace produced {ep.obs['inv_iron'][2] - before_ingots} "
            f"of {count} ingots")
    ep.step(close_container=1)


def craft_iron_route_gear(ep):
    ingot_slot = inventory_slot(ep, 265)
    stick_slot = inventory_slot(ep, 280)
    click(ep, ingot_slot)
    for slot in (36, 37, 38):
        click(ep, slot, button=1)
    click(ep, ingot_slot)
    click(ep, stick_slot)
    for slot in (40, 43):
        click(ep, slot, button=1)
    click(ep, stick_slot)
    click(ep, 45)
    dst = next(slot for slot, value in enumerate(ep.obs["inventory_ids"])
               if value == 0)
    click(ep, dst)
    if ep.obs["inv_iron"][3] != 1:
        raise RuntimeError("ordinary table clicks failed to craft iron pick")

    ingot_slot = inventory_slot(ep, 265)
    click(ep, ingot_slot)
    for slot in (36, 38, 40):
        click(ep, slot, button=1)
    click(ep, ingot_slot)
    click(ep, 45)
    dst = next(slot for slot, value in enumerate(ep.obs["inventory_ids"])
               if value == 0)
    click(ep, dst)
    if 325 not in ep.obs["inventory_ids"]:
        raise RuntimeError("ordinary table clicks failed to craft bucket")

    ingot_slot = inventory_slot(ep, 265)
    flint_slot = inventory_slot(ep, 318)
    click(ep, ingot_slot)
    click(ep, 36, button=1)
    click(ep, flint_slot)
    click(ep, 37, button=1)
    click(ep, 45)
    dst = next(slot for slot, value in enumerate(ep.obs["inventory_ids"])
               if value == 0)
    click(ep, dst)
    if 259 not in ep.obs["inventory_ids"]:
        raise RuntimeError("ordinary table clicks failed to craft flint and steel")
    ep.step(close_container=1)


def pillar_up(ep, target_y, item_id=4, settle_result=True):
    """Nerd-pole with ordinary jump/use actions until the requested foot Y."""
    block_slot = ensure_hotbar(ep, item_id)
    stalled = 0
    previous_y = ep.obs["y"]
    for _ in range(1000):
        if ep.obs["y"] >= target_y:
            if settle_result:
                settle(ep)
            return
        _, dpitch = look_at(ep.obs, ep.obs["x"], ep.obs["y"] - 1.0,
                            ep.obs["z"])
        ep.step(jump=1, use=1, do_place=1, hotbar=block_slot,
                dpitch=dpitch, cam=1)
        stalled = 0 if ep.obs["y"] > previous_y + 0.02 else stalled + 1
        previous_y = ep.obs["y"]
        if stalled > 80:
            raise RuntimeError(
                f"pillar stalled at y={ep.obs['y']:.3f}; "
                f"blocks={item_count(ep.obs, item_id)}")
    raise RuntimeError(f"pillar did not reach y={target_y}")


def pillar_one_staggered(ep, item_id, settle_after=True):
    """Jump clear of the player AABB, then place one block underneath."""
    start_y = ep.obs["y"]
    block_x = math.floor(ep.obs["x"])
    block_y = math.floor(start_y)
    block_z = math.floor(ep.obs["z"])
    slot = ensure_hotbar(ep, item_id)
    before = item_count(ep.obs, item_id)
    max_y = start_y
    # A Nether route may begin this primitive while swimming in lava, whose
    # jump impulse is far slower than the 12-tick dry-land arc.
    for _ in range(80):
        _, dpitch = look_at(ep.obs, ep.obs["x"], start_y - 0.5,
                            ep.obs["z"])
        ep.step(jump=1, hotbar=slot, dpitch=dpitch, cam=1)
        max_y = max(max_y, ep.obs["y"])
        if ep.obs["y"] > start_y + 1.05:
            break
    else:
        column = [b for b in ep.obs["blocks"]
                  if b != [0, 0, 0, 0]
                  and b[1] == math.floor(ep.obs["x"])
                  and b[3] == math.floor(ep.obs["z"])
                  and math.floor(ep.obs["y"]) - 1 <= b[2]
                  <= math.floor(ep.obs["y"]) + 5]
        raise RuntimeError(
            f"staggered pillar could not jump from y={start_y}; "
            f"pose=({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
            f"{ep.obs['z']:.3f}) max_y={max_y:.3f} "
            f"on_ground={ep.obs['on_ground']} column={column} nearby="
            f"{[b for b in ep.obs['blocks'] if b != [0, 0, 0, 0] and abs(b[1] - ep.obs['x']) < 3 and 64 <= b[2] <= 68 and abs(b[3] - ep.obs['z']) < 3]}")
    # Existing portal water can move the player across a cell boundary during
    # the jump. Placement is aimed from the apex, so validate that apex cell,
    # not the stale takeoff cell.
    block_x = math.floor(ep.obs["x"])
    block_z = math.floor(ep.obs["z"])
    for _ in range(8):
        _, dpitch = look_at(ep.obs, ep.obs["x"], start_y - 0.5,
                            ep.obs["z"])
        ep.step(jump=1, use=1, do_place=1, hotbar=slot,
                dpitch=dpitch, cam=1,
                probe_x=block_x, probe_y=block_y, probe_z=block_z)
        if ep.obs.get("probe", [0])[0] not in (0, 8, 9, 10, 11):
            break
        before = item_count(ep.obs, item_id)
    else:
        raise RuntimeError(
            f"staggered pillar did not place item {item_id}; "
            f"pose=({ep.obs['x']},{ep.obs['y']},{ep.obs['z']}) "
            f"health={ep.obs['health']} source="
            f"{ep.obs['last_damage_source']} "
            f"look=({ep.obs['yaw']},{ep.obs['pitch']}) "
            f"probe={ep.obs.get('probe')}")
    if not settle_after:
        return
    settle(ep)
    if ep.obs["y"] < start_y + 0.8:
        raise RuntimeError(
            f"staggered pillar fell from {start_y} to {ep.obs['y']}; "
            f"cell={block_x},{block_y},{block_z} "
            f"health={ep.obs['health']} source="
            f"{ep.obs['last_damage_source']} "
            f"probe={ep.obs.get('probe')} item={item_id} "
            f"before={before} after={item_count(ep.obs, item_id)} "
            f"local={[b for b in ep.obs['blocks'] if b != [0, 0, 0, 0] and abs(b[1] - block_x) <= 2 and abs(b[2] - block_y) <= 2 and abs(b[3] - block_z) <= 2]} "
            f"mobs={ep.obs['mobs'][:16]}")


def pillar_up_mixed(ep, target_y, items=(4, 12, 3, 35, 5), centered=False):
    """Nerd-pole across several finite block stacks."""
    for item in items:
        while item_count(ep.obs, item) > 0 and ep.obs["y"] < target_y:
            try:
                if centered:
                    navigate(ep, math.floor(ep.obs["x"]) + 0.5,
                             math.floor(ep.obs["z"]) + 0.5,
                             max_ticks=60, sprint=False, tolerance=0.04,
                             arrival_idle=0)
                pillar_one_staggered(ep, item)
            except RuntimeError:
                if item_count(ep.obs, item) == 0:
                    break
                raise


def pillar_up_clearing_mixed(ep, target_y, items=(12, 13)):
    """Nerd-pole while mining only the head cells that block the jump."""
    while ep.obs["y"] < target_y:
        x = math.floor(ep.obs["x"])
        z = math.floor(ep.obs["z"])
        navigate(ep, x + 0.5, z + 0.5, max_ticks=60,
                 sprint=False, tolerance=0.08, arrival_idle=0,
                 combat=False)
        # Recompute the touched columns after every mined block.  Hostile
        # collision can push the player across a cell boundary while the
        # camera is held on a long fist-mine, changing which neighboring
        # ceiling column clips the 0.6-wide AABB.
        for _ in range(24):
            center_x = math.floor(ep.obs["x"])
            center_z = math.floor(ep.obs["z"])
            x_cells = range(center_x - 1, center_x + 2)
            z_cells = range(center_z - 1, center_z + 2)
            target = None
            for y in range(math.floor(ep.obs["y"]) + 1,
                           math.floor(ep.obs["y"]) + 4):
                for clear_x in x_cells:
                    for clear_z in z_cells:
                        ep.step(probe_x=clear_x, probe_y=y,
                                probe_z=clear_z)
                        if ep.obs["probe"][0] != 0:
                            target = clear_x, y, clear_z
                            break
                    if target is not None:
                        break
                if target is not None:
                    break
            if target is None:
                break
            mine_probed_coordinate(
                ep, *target, available_break_tool(ep.obs), max_ticks=240)
        else:
            raise RuntimeError("clearing pillar could not open its head cells")
        item = next((value for value in items
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError("clearing pillar exhausted blocks")
        if (ep.can_save and ep.obs["y"] >= 63.9 and
                os.environ.get("NETHERITE_DEBUG_PILLAR_SAVE")):
            ep.step(save_slot="debug-pillar-cap")
        pillar_one_staggered(ep, item)
        if ep.obs["y"] >= target_y:
            return
    if ep.obs["y"] >= target_y - 1.01:
        return
    raise RuntimeError(
        f"mixed pillar did not reach y={target_y}; y={ep.obs['y']} "
        f"health={ep.obs['health']} dead={ep.obs['dead']} "
        f"source={ep.obs['last_damage_source']} "
        f"inventory={list(zip(ep.obs['inventory_ids'], ep.obs['inventory_counts']))} "
        f"mobs={ep.obs['mobs'][:12]}")


def mine_and_pillar_shaft(ep, target_y):
    """Clear a one-cell shaft and ordinary-input pillar through it."""
    # The portal water curtain has reached several cells sideways by y=62.
    # Move the return shaft outside that spread while still in the dry y=52
    # gallery; climbing next to the portal and trying to drain it costs more
    # air than the player has.
    if ep.obs["y"] < 60.0 and math.floor(ep.obs["x"]) > 40:
        foot_y = math.floor(ep.obs["y"])
        z = math.floor(ep.obs["z"])
        for tunnel_x in range(math.floor(ep.obs["x"]) - 1, 39, -1):
            tool = available_break_tool(ep.obs)
            if tool is None:
                raise RuntimeError("dry shaft approach exhausted every tool")
            for tunnel_y in (foot_y, foot_y + 1):
                ep.step(probe_x=tunnel_x, probe_y=tunnel_y, probe_z=z)
                if ep.obs["probe"][0] != 0:
                    mine_probed_coordinate(
                        ep, tunnel_x, tunnel_y, z, tool, max_ticks=2400)
            navigate(ep, tunnel_x + 0.5, z + 0.5, max_ticks=120,
                     sprint=False, tolerance=0.15, arrival_idle=0)
        x = math.floor(ep.obs["x"])
        for tunnel_z in range(math.floor(ep.obs["z"]) - 1, 19, -1):
            tool = available_break_tool(ep.obs)
            if tool is None:
                raise RuntimeError("dry shaft north leg exhausted every tool")
            for tunnel_y in (foot_y, foot_y + 1):
                ep.step(probe_x=x, probe_y=tunnel_y, probe_z=tunnel_z)
                if ep.obs["probe"][0] != 0:
                    mine_probed_coordinate(
                        ep, x, tunnel_y, tunnel_z, tool, max_ticks=2400)
            navigate(ep, x + 0.5, tunnel_z + 0.5, max_ticks=120,
                     sprint=False, tolerance=0.15, arrival_idle=0)
        z = math.floor(ep.obs["z"])
        for tunnel_x in range(math.floor(ep.obs["x"]) - 1, 9, -1):
            tool = available_break_tool(ep.obs)
            if tool is None:
                raise RuntimeError("deep despawn leg exhausted every tool")
            for tunnel_y in (foot_y, foot_y + 1):
                ep.step(probe_x=tunnel_x, probe_y=tunnel_y, probe_z=z)
                if ep.obs["probe"][0] != 0:
                    mine_probed_coordinate(
                        ep, tunnel_x, tunnel_y, z, tool, max_ticks=2400)
            navigate(ep, tunnel_x + 0.5, z + 0.5, max_ticks=120,
                     sprint=False, tolerance=0.15, arrival_idle=0)
            if tunnel_x in (32, 24, 16):
                seal_item = next((item for item in (4, 3, 35, 5, 12)
                                  if item_count(ep.obs, item) >= 2), None)
                if seal_item is None:
                    raise RuntimeError("deep despawn leg lacks bulkhead blocks")
                place_item_on(ep, tunnel_x + 1, foot_y - 1, z, seal_item)
                place_item_on(ep, tunnel_x + 1, foot_y, z, seal_item)
        # Mine a dead-end stock spur and return to the ascent cell.  Its
        # twenty stone blocks fund a causeway all the way over the portal,
        # removing the last creeper-exposed ground dash.
        for spur_x in range(9, -1, -1):
            tool = available_break_tool(ep.obs)
            if tool is None:
                raise RuntimeError("bridge-stock spur exhausted every tool")
            for tunnel_y in (foot_y, foot_y + 1):
                ep.step(probe_x=spur_x, probe_y=tunnel_y, probe_z=z)
                if ep.obs["probe"][0] != 0:
                    mine_probed_coordinate(
                        ep, spur_x, tunnel_y, z, tool, max_ticks=2400)
            navigate(ep, spur_x + 0.5, z + 0.5, max_ticks=120,
                     sprint=False, tolerance=0.15, arrival_idle=0)
    day_tick = ep.obs.get("world_time", ep.obs["t"]) % 24000
    if ep.obs["y"] < 60.0 and day_tick >= 3000:
        for _ in range(24000 - day_tick + 1000):
            ep.step()
            if ep.obs["dead"]:
                raise RuntimeError("deep portal-return wait was breached")
    if ep.obs["y"] < 60.0 and math.floor(ep.obs["x"]) <= 0:
        z = math.floor(ep.obs["z"])
        for bulkhead_x in (17, 25, 33):
            navigate(ep, bulkhead_x - 0.5, z + 0.5, max_ticks=500,
                     sprint=False, tolerance=0.2, arrival_idle=0)
            tool = available_break_tool(ep.obs)
            for tunnel_y in (math.floor(ep.obs["y"]),
                             math.floor(ep.obs["y"]) + 1):
                ep.step(probe_x=bulkhead_x, probe_y=tunnel_y, probe_z=z)
                if ep.obs["probe"][0] != 0:
                    mine_probed_coordinate(
                        ep, bulkhead_x, tunnel_y, z, tool, max_ticks=2400)
            navigate(ep, bulkhead_x + 0.5, z + 0.5, max_ticks=120,
                     sprint=False, tolerance=0.2, arrival_idle=0)
        navigate(ep, 40.5, z + 0.5, max_ticks=500, sprint=False,
                 tolerance=0.2, arrival_idle=0)
    upper_saved = False
    for _ in range(40):
        if ep.obs["y"] >= target_y - 0.2:
            return
        x, z = math.floor(ep.obs["x"]), math.floor(ep.obs["z"])
        if ep.obs["y"] >= 61.8 and ep.can_save and not upper_saved:
            ep.step(save_slot="portal-upper-shaft")
            upper_saved = True
        if x in (10, 15, 40) and z == 20 and 62.8 <= ep.obs["y"] < 63.5:
            ep.step(probe_x=x, probe_y=65, probe_z=z)
            if ep.obs["probe"][0] == 0:
                # A resumed post-cascade checkpoint can continue directly.
                if ep.can_save:
                    ep.step(save_slot="portal-surface-ready")
            else:
            # Clear the desert cap from beside the shaft.  Mining a vertical
            # sand column while standing under it inflicts repeated
            # suffocation damage even though every individual block is
            # reachable.  First empty an adjacent refuge, then put a torch at
            # the main column's landing cell so the whole cascade breaks.
                side_x = x - 1
                for side_y in (63, 64, 65):
                    stable = 0
                    tool = available_break_tool(ep.obs)
                    slot = ensure_hotbar(ep, tool)
                    for _ in range(2400):
                        dyaw, dpitch = look_at(
                            ep.obs, side_x + 0.5, side_y + 0.5, z + 0.5)
                        ep.step(attack=1, hotbar=slot, dyaw=dyaw,
                                dpitch=dpitch, cam=1,
                                probe_x=side_x, probe_y=side_y, probe_z=z)
                        if ep.obs["dead"]:
                            raise RuntimeError("sand refuge clearing was lethal")
                        stable = stable + 1 if ep.obs["probe"][0] == 0 else 0
                        if stable >= 30:
                            break
                    else:
                        raise RuntimeError("sand refuge did not clear")
                navigate(ep, side_x + 0.5, z + 0.5, max_ticks=120,
                         sprint=False, tolerance=0.15, arrival_idle=0)
                torch_slot = ensure_hotbar(ep, 50)
                before_torches = item_count(ep.obs, 50)
                for _ in range(16):
                    dyaw, dpitch = look_at(ep.obs, x + 0.5, 63.98, z + 0.5)
                    ep.step(use=1, do_place=1, hotbar=torch_slot,
                            dyaw=dyaw, dpitch=dpitch, cam=1,
                            probe_x=x, probe_y=64, probe_z=z)
                    if item_count(ep.obs, 50) < before_torches:
                        break
                else:
                    raise RuntimeError("failed to place sand-column torch")
                stable = 0
                tool = available_break_tool(ep.obs)
                slot = ensure_hotbar(ep, tool)
                for _ in range(4000):
                    dyaw, dpitch = look_at(ep.obs, x + 0.5, 65.5, z + 0.5)
                    ep.step(attack=1, hotbar=slot, dyaw=dyaw, dpitch=dpitch,
                            cam=1, probe_x=x, probe_y=65, probe_z=z)
                    if ep.obs["dead"]:
                        raise RuntimeError("main sand cascade was lethal")
                    stable = stable + 1 if ep.obs["probe"][0] == 0 else 0
                    if stable >= 40:
                        break
                else:
                    raise RuntimeError("main sand column did not clear")
                for torch_y in (63, 64):
                    ep.step(probe_x=x, probe_y=torch_y, probe_z=z)
                    if ep.obs["probe"][0] == 50:
                        mine_probed_coordinate(ep, x, torch_y, z,
                                               available_break_tool(ep.obs),
                                               max_ticks=240)
                navigate(ep, x + 0.5, z + 0.5, max_ticks=120,
                         sprint=False, tolerance=0.15, arrival_idle=0)
                if ep.can_save:
                    ep.step(save_slot="portal-surface-ready")
        if x == 50 and 61.8 <= ep.obs["y"] < 62.5:
            foot_y = math.floor(ep.obs["y"])
            side_x = x - 1
            ep.step(probe_x=side_x, probe_y=foot_y - 1, probe_z=z)
            if ep.obs["probe"][0] == 0:
                support_item = next((value for value in (4, 3, 35, 5, 12)
                                     if item_count(ep.obs, value) > 0), None)
                if support_item is None:
                    raise RuntimeError("side-drain scaffold exhausted blocks")
                place_item_on(ep, side_x, foot_y - 2, z, support_item)
            for drain_x in (side_x, x):
                if drain_x == x:
                    roof_item = next((value for value in (4, 3, 35, 5, 12)
                                      if item_count(ep.obs, value) >= 1), None)
                    if roof_item is None:
                        raise RuntimeError("side-drain roof exhausted blocks")
                    roof_y = foot_y + 2
                    roof_slot = ensure_hotbar(ep, roof_item)
                    for _ in range(16):
                        dyaw, dpitch = look_at(
                            ep.obs, side_x + 0.5, roof_y + 0.5, z + 1.02)
                        ep.step(use=1, do_place=1, hotbar=roof_slot,
                                dyaw=dyaw, dpitch=dpitch, cam=1,
                                probe_x=side_x, probe_y=roof_y, probe_z=z)
                        if ep.obs["probe"][0] not in (0, 8, 9):
                            break
                    else:
                        raise RuntimeError("failed to install side-drain roof")
                    for _ in range(16):
                        dyaw, dpitch = look_at(
                            ep.obs, side_x + 0.98, roof_y + 0.5, z + 0.5)
                        ep.step(use=1, do_place=1, hotbar=roof_slot,
                                dyaw=dyaw, dpitch=dpitch, cam=1,
                                probe_x=x, probe_y=roof_y, probe_z=z)
                        if ep.obs["probe"][0] not in (0, 8, 9):
                            break
                    else:
                        raise RuntimeError("failed to cap main portal shaft")
                    for support_y in (foot_y - 1, foot_y):
                        ep.step(probe_x=side_x, probe_y=support_y + 1,
                                probe_z=z)
                        if ep.obs["probe"][0] in (0, 8, 9):
                            place_item_on(ep, side_x, support_y, z,
                                          roof_item)
                    clear_tool = available_break_tool(ep.obs)
                    for clear_y in (foot_y, foot_y + 1):
                        mine_probed_coordinate(
                            ep, side_x, clear_y, z, clear_tool,
                            max_ticks=2400)
                    navigate(ep, side_x + 0.5, z + 0.5, max_ticks=120,
                             sprint=False, tolerance=0.15, arrival_idle=0)
                    # Do not reopen the flooded portal column.  Seal its two
                    # occupied cells from this dry alcove, then move the
                    # ascent three blocks west.  The old route spent several
                    # thousand ordinary attack ticks waiting for flowing
                    # water to settle and could drown before y=66 cleared.
                    for support_y in (foot_y - 1, foot_y):
                        ep.step(probe_x=x, probe_y=support_y + 1,
                                probe_z=z)
                        if ep.obs["probe"][0] in (0, 8, 9):
                            place_item_on(ep, x, support_y, z, roof_item)
                    for tunnel_x in (side_x - 1, side_x - 2):
                        tool = available_break_tool(ep.obs)
                        if tool is None:
                            raise RuntimeError("dry bypass exhausted every tool")
                        for tunnel_y in (foot_y, foot_y + 1):
                            ep.step(probe_x=tunnel_x, probe_y=tunnel_y,
                                    probe_z=z)
                            if ep.obs["probe"][0] != 0:
                                mine_probed_coordinate(
                                    ep, tunnel_x, tunnel_y, z, tool,
                                    max_ticks=2400)
                        navigate(ep, tunnel_x + 0.5, z + 0.5,
                                 max_ticks=120, sprint=False,
                                 tolerance=0.15, arrival_idle=0)
                    x = side_x - 2
                    break
        for _clearance_round in range(24):
            blocked = False
            for y in range(math.floor(ep.obs["y"]) + 1,
                           math.floor(ep.obs["y"]) + 4):
                ep.step(probe_x=x, probe_y=y, probe_z=z)
                if ep.obs["probe"][0] == 0:
                    continue
                blocked = True
                tool = available_break_tool(ep.obs)
                if tool is None:
                    raise RuntimeError("mined shaft exhausted every tool")
                mine_probed_coordinate(ep, x, y, z, tool, max_ticks=2400)
            if not blocked:
                break
        else:
            raise RuntimeError("mined shaft falling column did not settle")
        item = next((value for value in (4, 3, 35, 5, 12)
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError(
                f"mined shaft exhausted pillar blocks at "
                f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                f"{ep.obs['z']:.3f}); dead={ep.obs['dead']} "
                f"health={ep.obs['health']} source="
                f"{ep.obs['last_damage_source']} air={ep.obs['air']} "
                f"inventory="
                f"{list(zip(ep.obs['inventory_ids'], ep.obs['inventory_counts']))}")
        if target_y <= 65.1 and ep.obs["y"] >= 64.0:
            pillar_one_staggered(ep, item, settle_after=False)
            return
        pillar_one_staggered(ep, item)
    raise RuntimeError(f"mined shaft did not reach y={target_y}")


def descend_pillar(ep, ground_y, item_id=12):
    """Dismantle the top of a scaffold one block at a time while descending."""
    tool = available_pick(ep.obs)
    if tool is None:
        raise RuntimeError("no pick remains to descend scaffold")
    ensure_hotbar(ep, tool)
    while ep.obs["y"] > ground_y + 0.2:
        x = math.floor(ep.obs["x"])
        y = math.floor(ep.obs["y"]) - 1
        z = math.floor(ep.obs["z"])
        target = [item_id, x, y, z]
        for _ in range(30):
            dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.5, z + 0.5)
            ep.step(attack=1, hotbar=hotbar_slot(ep, tool),
                    dyaw=dyaw, dpitch=dpitch, cam=1)
            if target not in ep.obs["blocks"]:
                break
        else:
            raise RuntimeError(f"pillar block {target} did not break")
        settle(ep)


def descend_mixed_pillar(ep, ground_y):
    """Dismantle a mixed cobble/sand/dirt scaffold from its top."""
    while ep.obs["y"] > ground_y + 0.2:
        x = math.floor(ep.obs["x"])
        y = math.floor(ep.obs["y"]) - 1
        z = math.floor(ep.obs["z"])
        target = next((b for b in ep.obs["blocks"]
                       if b != [0, 0, 0, 0] and b[1:] == [x, y, z]), None)
        if target is not None and target[0] in (8, 9):
            for _ in range(100):
                ep.step(sneak=1)
                if ep.obs["y"] <= ground_y + 0.2:
                    return
                if math.floor(ep.obs["y"]) - 1 != y:
                    break
            continue
        if target is None:
            start_y = ep.obs["y"]
            tool = available_break_tool(ep.obs)
            if tool is None:
                for _ in range(100):
                    ep.step(sneak=1)
                    if ep.obs["y"] < start_y - 0.2:
                        settle(ep)
                        break
                else:
                    raise RuntimeError(
                        "no pick remains for unlisted solid shaft block")
                continue
            slot = ensure_hotbar(ep, tool)
            for _ in range(800):
                dyaw, dpitch = look_at(
                    ep.obs, x + 0.5, y + 0.5, z + 0.5)
                ep.step(attack=1, hotbar=slot, dyaw=dyaw,
                        dpitch=dpitch, cam=1,
                        probe_x=x, probe_y=y, probe_z=z)
                if ep.obs.get("probe", [0])[0] != 0:
                    mine_probed_coordinate(
                        ep, x, y, z, available_break_tool(ep.obs),
                        max_ticks=2400)
                    settle(ep)
                if ep.obs["y"] < start_y - 0.2:
                    break
            else:
                raise RuntimeError(f"mixed pillar air gap did not descend at {x},{y},{z}")
            if ep.obs["dead"]:
                raise RuntimeError("mixed pillar air-gap fall was lethal")
            continue
        tool = available_break_tool(ep.obs)
        slot = ensure_hotbar(ep, tool) if tool is not None else ep.obs["hotbar_sel"]
        for _ in range(800):
            dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.5, z + 0.5)
            ep.step(attack=1, hotbar=slot, dyaw=dyaw,
                    dpitch=dpitch, cam=1)
            if not any(b != [0, 0, 0, 0] and b[1:] == [x, y, z]
                       for b in ep.obs["blocks"]):
                break
        else:
            raise RuntimeError(f"mixed pillar block {target} did not break")
        settle(ep)


def descend_portal_bridge_column(ep, ground_y):
    """Open the upper shaft from its neighboring bridge, then descend it."""
    if ep.obs["y"] >= 69.0:
        column_x = math.floor(ep.obs["x"])
        column_z = math.floor(ep.obs["z"])
        # A creeper lives directly below the bridge in this saved state.  Kite
        # its fuse along the existing +X bridge before committing to the slow
        # bare-hand stone break; otherwise it removes the floor underfoot.
        for _ in range(3):
            creeper = nearby_mob(ep, (4,), radius=8.0, max_vertical=7.0)
            if creeper is None:
                break
            navigate(ep, column_x + 2.5, column_z + 0.5, max_ticks=100,
                     sprint=False, tolerance=0.2, arrival_idle=8)
            navigate(ep, 50.5, column_z + 0.5, max_ticks=140,
                     sprint=True, tolerance=0.3, arrival_idle=0)
            for _ in range(50):
                ep.step()
                if ep.obs["dead"]:
                    raise RuntimeError("bridge creeper kite was lethal")
            navigate(ep, column_x + 0.5, column_z + 0.5,
                     max_ticks=180, sprint=True, tolerance=0.2,
                     arrival_idle=0)
        if item_count(ep.obs, 24) < 3 and item_count(ep.obs, 12) >= 12:
            craft_sandstone(ep, 3)
        navigate(ep, column_x + 1.5, column_z + 0.5, max_ticks=120,
                 sprint=False, tolerance=0.12, arrival_idle=0)
        # The saved surface contains a skeleton firing north across this
        # bridge.  Extend one floor cell south and raise a two-block shield;
        # otherwise the deliberately slow bare-hand stone break is lethal.
        if item_count(ep.obs, 24) >= 3:
            bridge_positive_axis_mixed(ep, "z", column_z + 1.5)
            navigate(ep, column_x + 1.5, column_z + 0.5, max_ticks=120,
                     sprint=False, tolerance=0.12, arrival_idle=0)
            place_item_on(ep, column_x + 1, 69, column_z + 1, 24)
            place_item_on(ep, column_x + 1, 70, column_z + 1, 24)
        for block_y in range(69, 64, -1):
            ep.step(probe_x=column_x, probe_y=block_y,
                    probe_z=column_z)
            if ep.obs["probe"][0] != 0:
                mine_probed_coordinate(
                    ep, column_x, block_y, column_z,
                    available_break_tool(ep.obs), max_ticks=2400)
        death_x = column_x + 0.5
        death_z = column_z + 0.5
        try:
            navigate(ep, death_x, death_z, max_ticks=120,
                     sprint=False, tolerance=0.12, arrival_idle=0)
            settle(ep)
        except RuntimeError:
            if not ep.obs["dead"]:
                raise
            for _ in range(24):
                ep.step()
            for _ in range(20):
                ep.step(death_click=1, death_button=0)
                if not ep.obs["dead"]:
                    break
            else:
                raise RuntimeError("portal shaft death did not respawn")
            navigate(ep, death_x, death_z, max_ticks=1600,
                     sprint=True, tolerance=0.7, arrival_idle=0,
                     combat=True)
            for _ in range(80):
                ep.step()
            skipped_drops = set()
            wanted_drops = {12, 13, 24, 1, 259, 280, 325, 263, 50,
                            262, 288, 289, 334, 352}
            for _ in range(24):
                drops = [item for item in ep.obs.get("items", [])
                         if item[0] in wanted_drops
                         and item[2] not in skipped_drops
                         and math.hypot(item[3] - death_x,
                                        item[5] - death_z) <= 12.0]
                if not drops:
                    break
                drop = min(drops, key=lambda item:
                           (item[3] - ep.obs["x"]) ** 2 +
                           (item[5] - ep.obs["z"]) ** 2)
                try:
                    navigate(ep, drop[3], drop[5], max_ticks=180,
                             sprint=True, tolerance=0.5, arrival_idle=0,
                             combat=True)
                    for _ in range(12):
                        ep.step()
                except RuntimeError:
                    if ep.obs["dead"]:
                        raise
                    skipped_drops.add(drop[2])
            if (325 not in ep.obs["inventory_ids"] or
                    259 not in ep.obs["inventory_ids"]):
                raise RuntimeError(
                    "respawn route did not recover bucket and flint steel")
            navigate(ep, death_x, death_z, max_ticks=160,
                     sprint=False, tolerance=0.12, arrival_idle=0)
            settle(ep)
    descend_mixed_pillar(ep, ground_y)


def respawn_and_recover_drops(ep, death_x, death_z, required=(), lives=4):
    """Respawn and reclaim the ordinary inventory dropped at one death site."""
    if not ep.obs["dead"]:
        raise RuntimeError("drop recovery requested while player is alive")
    for _ in range(24):
        ep.step()
    for _ in range(20):
        ep.step(death_click=1, death_button=0)
        if not ep.obs["dead"]:
            break
    else:
        raise RuntimeError("drop recovery did not respawn")
    try:
        navigate(ep, death_x, death_z, max_ticks=1600,
                 sprint=True, tolerance=0.8, arrival_idle=0, combat=False)
    except RuntimeError:
        if not ep.obs["dead"] or lives <= 1:
            raise
        return respawn_and_recover_drops(
            ep, death_x, death_z, required=required, lives=lives - 1)
    for _ in range(20):
        ep.step()
    skipped = set()
    # Walk every visible drop from the death burst, not an item allowlist.
    # The run's inventory evolves continuously, so filtering silently lost
    # newly acquired tools and made later recovery depend on stale assumptions.
    for _ in range(48):
        if required and all(item in ep.obs["inventory_ids"]
                            for item in required):
            return
        drops = [item for item in ep.obs.get("items", [])
                 if item[2] not in skipped
                 and math.hypot(item[3] - death_x,
                                item[5] - death_z) <= 12.0]
        if not drops:
            break
        drop = min(drops, key=lambda item:
                   (item[3] - ep.obs["x"]) ** 2
                   + (item[5] - ep.obs["z"]) ** 2)
        try:
            navigate(ep, drop[3], drop[5], max_ticks=220,
                     sprint=True, tolerance=0.55, arrival_idle=0,
                     combat=False, swim=True)
            for _ in range(12):
                ep.step(jump=int(ep.obs["y"] <= 63.2))
        except RuntimeError:
            if ep.obs["dead"]:
                if lives <= 1:
                    raise RuntimeError(
                        f"drop recovery exhausted lives; required={required} "
                        f"inventory={ep.obs['inventory_ids']}")
                return respawn_and_recover_drops(
                    ep, death_x, death_z, required=required,
                    lives=lives - 1)
            skipped.add(drop[2])
    missing = [item for item in required
               if item not in ep.obs["inventory_ids"]]
    if missing:
        raise RuntimeError(
            f"drop recovery is missing required items {missing}; "
            f"inventory={ep.obs['inventory_ids']} items={ep.obs.get('items', [])}")


def descend_shaft(ep, target_y):
    """Mine a one-wide ordinary-input shaft, settling after every block."""
    ensure_hotbar(ep, 257)
    for _ in range(20):
        if ep.obs["y"] <= target_y:
            return
        x = math.floor(ep.obs["x"])
        y = math.floor(ep.obs["y"]) - 1
        z = math.floor(ep.obs["z"])
        mine_coordinate(ep, x, y, z, None, 257, max_ticks=35,
                        require_pickup=False, detect_depth=True)
        settle(ep)
    raise RuntimeError(f"shaft did not reach y={target_y}: y={ep.obs['y']}")


def pickup_fluid(ep, x, y, z, full_bucket_item):
    bucket_slot = ensure_hotbar(ep, 325)
    offsets = ((0, 0), (-4, 0), (4, 0), (0, -4), (0, 4),
               (-8, 0), (8, 0), (0, -8), (0, 8),
               (-4, -4), (-4, 4), (4, -4), (4, 4))
    for yaw_offset, pitch_offset in offsets:
        dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.5, z + 0.5)
        ep.step(use=1, do_place=1, hotbar=bucket_slot,
                dyaw=dyaw + yaw_offset, dpitch=dpitch + pitch_offset, cam=1)
        if full_bucket_item in ep.obs["inventory_ids"]:
            return
        ep.step()
    raise RuntimeError(
        f"failed to bucket fluid source at {x},{y},{z}; "
        f"pose=({ep.obs['x']},{ep.obs['y']},{ep.obs['z']}) "
        f"look=({ep.obs['yaw']},{ep.obs['pitch']}) "
        f"sources={ep.obs.get('water_sources', [])}")


def pickup_any_lava(ep, sources):
    for source in sources:
        try:
            pickup_fluid(ep, *source, 327)
            return source
        except RuntimeError:
            if 327 in ep.obs["inventory_ids"]:
                return source
    raise RuntimeError("no reachable lava source remained in the ravine")


def prime_portal_cast_with_water(ep, target, prefer_curtain=False):
    """Put a water source in the next frame cell using the shared bucket."""
    if prefer_curtain:
        navigate(ep, 55.5, 30.5, max_ticks=160,
                 sprint=False, tolerance=0.2)
        pillar_up(ep, 68.0, item_id=12)
        navigate(ep, 55.5, 30.5, max_ticks=120,
                 sprint=False, tolerance=0.18, arrival_idle=0)
        pickup_fluid(ep, 54, 68, 30, 326)
        descend_pillar(ep, 65.0, item_id=12)
        navigate(ep, 52.5, 31.5, max_ticks=160,
                 sprint=False, tolerance=0.4)
        x, y, z = target
        place_fluid_on(ep, x, y - 1, z, 326)
        for _ in range(8):
            ep.step()
        return
    sources = []
    sources.extend((
        (25, 62, 39), (25, 62, 40), (24, 62, 37),
        (24, 62, 38), (24, 62, 39), (24, 62, 40),
        (23, 62, 36), (23, 62, 37), (23, 62, 38),
    ))
    have_water = 326 in ep.obs["inventory_ids"]
    local_sources = [tuple(value) for value in ep.obs.get("water_sources", [])
                     if math.hypot(value[0] - 52.0, value[2] - 30.0) < 9.0]
    local_sources.sort(key=lambda p: math.hypot(
        p[0] + 0.5 - ep.obs["x"], p[2] + 0.5 - ep.obs["z"]))
    for source in (() if have_water else local_sources):
        try:
            navigate(ep, source[0] + 0.5, source[2] + 1.5,
                     max_ticks=220, sprint=False, tolerance=0.45)
            pickup_fluid(ep, *source, 326)
            have_water = True
            break
        except RuntimeError:
            if 326 in ep.obs["inventory_ids"]:
                have_water = True
                break
    if not have_water:
        # Approach north of the five-column sand mold.  A straight diagonal
        # from the frame wedges against its northwest corner at z=21.3.
        fetch_route = ((44.5, 25.5), (40.5, 25.5), (40.5, 40.5),
                       (30.5, 50.5))
        for waypoint in fetch_route:
            navigate(ep, *waypoint, max_ticks=300, sprint=False,
                     tolerance=0.7, arrival_idle=0, combat=True)
        observed = ep.obs.get("water_sources", [])
        if observed:
            observed.sort(key=lambda p: math.hypot(
                p[0] + 0.5 - ep.obs["x"], p[2] + 0.5 - ep.obs["z"]))
            sources = [tuple(value) for value in observed] + sources
        for source in sources:
            try:
                navigate(ep, source[0] + 0.5, source[2] + 1.5,
                         max_ticks=160, sprint=False, tolerance=0.45,
                         combat=True)
                pickup_fluid(ep, *source, 326)
                break
            except RuntimeError:
                if 326 in ep.obs["inventory_ids"]:
                    break
        else:
            raise RuntimeError("no reachable water source for portal cast")
        for waypoint in tuple(reversed(fetch_route[:-1])) + ((52.5, 31.5),):
            navigate(ep, *waypoint, max_ticks=300, sprint=False,
                     tolerance=0.7, arrival_idle=0, combat=True)
        if ep.can_save:
            ep.step(save_slot="portal-side9-water-return")
    settle(ep)
    x, y, z = target
    # The upper frame support is already within bucket reach from the mold's
    # south ledge.  Do not spend scarce construction blocks climbing when an
    # ordinary right click on that support is valid.
    reach = math.sqrt((x + 0.5 - ep.obs["x"]) ** 2 +
                      (y - 0.02 - (ep.obs["y"] + 1.62)) ** 2 +
                      (z + 0.5 - ep.obs["z"]) ** 2)
    if reach <= 4.5:
        if (x, y, z) == (53, 68, 29):
            # Preserve a same-level source beside the cast cell.  Putting the
            # source in the target itself worked only until the lava bucket
            # replaced it, leaving all residual flow one level too low.
            support_item = next(
                item for item in (1, 4, 3, 35, 5)
                if item_count(ep.obs, item) > 0)
            slot = ensure_hotbar(ep, support_item)
            before = item_count(ep.obs, support_item)
            for _ in range(12):
                # Click the west face of the existing 54,67,30 scaffold so
                # the new support lands exactly at 53,67,30.
                dyaw, dpitch = look_at(ep.obs, 54.01, 67.5, 30.5)
                ep.step(use=1, do_place=1, hotbar=slot,
                        dyaw=dyaw, dpitch=dpitch, cam=1,
                        probe_x=53, probe_y=67, probe_z=30)
                if ep.obs["probe"][0] != 0:
                    break
                ep.step()
            else:
                raise RuntimeError("failed to build adjacent water support")
            if item_count(ep.obs, support_item) >= before:
                raise RuntimeError("adjacent water support consumed no block")
            if ep.can_save:
                ep.step(save_slot="portal-side9-water-pedestal")
            pillar_item = next(
                item for item in (1, 4, 3, 35, 5)
                if item_count(ep.obs, item) > 0)
            pillar_one_staggered(ep, pillar_item)
            bucket_slot = ensure_hotbar(ep, 326)
            for _ in range(12):
                ep.step(jump=1, hotbar=bucket_slot)
                if ep.obs["y"] > 66.55:
                    break
            dyaw, dpitch = look_at(ep.obs, 53.5, 67.99, 30.5)
            ep.step(use=1, do_place=1, hotbar=bucket_slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1,
                    probe_x=53, probe_y=68, probe_z=30)
            if ep.obs["probe"][0] not in (8, 9):
                raise RuntimeError(
                    f"adjacent water source landed in wrong cell; "
                    f"probe={ep.obs['probe']} pose="
                    f"({ep.obs['x']},{ep.obs['y']},{ep.obs['z']})")
        else:
            place_fluid_on(ep, x, y - 1, z, 326)
        for _ in range(8):
            ep.step()
        return
    navigate(ep, x + 0.5, 34.5, max_ticks=800, sprint=False,
             tolerance=0.3, arrival_idle=0)
    ep.step(probe_x=x, probe_y=66, probe_z=33)
    if ep.obs["probe"][0] not in (0, 8, 9):
        navigate(ep, x + 0.5, 35.5, max_ticks=120, sprint=False,
                 tolerance=0.25, arrival_idle=0)
        ep.step(probe_x=x, probe_y=65, probe_z=34)
        if ep.obs["probe"][0] in (0, 8, 9):
            stair_item = next((item for item in (4, 3, 35, 5, 12)
                               if item_count(ep.obs, item) > 0), None)
            if stair_item is None:
                raise RuntimeError("water cast alignment exhausted stair blocks")
            place_item_on(ep, x, 64, 34, stair_item)
        navigate(ep, x + 0.5, 34.5, max_ticks=160, sprint=False,
                 tolerance=0.22, arrival_idle=0)
        settle(ep)
    ep.step(probe_x=x, probe_y=65, probe_z=33)
    if ep.obs["probe"][0] in (0, 8, 9):
        step_item = next((item for item in (4, 3, 35, 5)
                          if item_count(ep.obs, item) > 0), None)
        if step_item is None:
            raise RuntimeError("water cast alignment exhausted step blocks")
        place_item_on(ep, x, 64, 33, step_item)
    navigate(ep, x + 0.5, 33.5, max_ticks=160, sprint=False,
             tolerance=0.22, arrival_idle=0)
    settle(ep)
    while ep.obs["y"] < y:
        pillar_item = next((item for item in (4, 3, 35, 5)
                            if item_count(ep.obs, item) > 0), None)
        if pillar_item is None:
            raise RuntimeError("water cast alignment exhausted pillar blocks")
        pillar_one_staggered(ep, pillar_item)
    place_fluid_on(ep, x, y - 1, z, 326)
    for _ in range(8):
        ep.step()


def portal_lava_trip(ep, sources, target, descent_y):
    navigate(ep, 56.7, 41.7, max_ticks=260,
             sprint=False, tolerance=0.18)
    settle(ep)
    descend_pillar(ep, 53.0, item_id=12)
    if descent_y < 53.0:
        descend_shaft(ep, descent_y)
    if ep.obs["y"] < 49.0:
        pillar_up(ep, 49.0, item_id=4)
        bridge_positive_z(ep, 44.5, item_id=4)
    pickup_any_lava(ep, sources)
    pillar_up(ep, 66.0, item_id=12)
    for x, z in ((55.5, 40.5), (55.5, 35.5), (52.5, 31.5)):
        navigate(ep, x, z, max_ticks=240,
                 sprint=False, tolerance=0.45)
        settle(ep)
    x, y, z = target
    place_fluid_on(ep, x, y - 1, z, 327)
    for _ in range(8):
        ep.step()
    if not any(b[0] == 49 and b[1:] == [x, y, z]
               for b in ep.obs["blocks"]):
        raise RuntimeError(f"portal cast at {target} did not make obsidian")


def finish_east_portal_cast(ep, target, east_path, fresh_return=False,
                            direct_tunnel=False):
    return_path = ((112.5, 20.5), (90.5, 20.5),
                   (65.5, 20.5), (40.5, 20.5)) if fresh_return else \
                  tuple(reversed(east_path[:-2]))
    if not direct_tunnel:
        for x, z in return_path:
            navigate(ep, x, z, max_ticks=400, sprint=True, tolerance=0.7,
                     arrival_idle=0)
        for x, z in ((40.5, 15.5), (65.5, 15.5), (65.5, 35.5),
                     (59.5, 35.5), (55.5, 33.5), (55.5, 31.5)):
            navigate(ep, x, z, max_ticks=400, sprint=True, tolerance=0.55,
                     arrival_idle=0, combat=True)
    if target[1] >= 68:
        x = target[0]
        far_exit = direct_tunnel and ep.obs["x"] < 41.0
        bridge_complete = (direct_tunnel and ep.obs["y"] >= 69.0
                           and ep.obs["x"] >= 50.0
                           and ep.obs["z"] >= 35.0)
        bridge_front = (direct_tunnel and ep.obs["y"] >= 69.0
                        and 40.0 < ep.obs["x"] < 50.0
                        and ep.obs["z"] < 25.0)
        if bridge_complete:
            navigate(ep, 50.5, 37.5, max_ticks=180, sprint=True,
                     tolerance=0.6, arrival_idle=0)
            settle(ep)
        elif bridge_front:
            back_x = math.floor(ep.obs["x"]) - 1
            back_y = math.floor(ep.obs["y"]) - 1
            ep.step(probe_x=back_x, probe_y=back_y,
                    probe_z=math.floor(ep.obs["z"]))
            if ep.obs["probe"][0] != 0:
                mine_probed_coordinate(
                    ep, back_x, back_y, math.floor(ep.obs["z"]),
                    available_break_tool(ep.obs), max_ticks=2400)
            day_tick = ep.obs.get("world_time", ep.obs["t"]) % 24000
            if day_tick >= 12000:
                for _ in range(24000 - day_tick + 1000):
                    ep.step()
                    if ep.obs["dead"]:
                        raise RuntimeError(
                            f"isolated bridge wait was breached; "
                            f"source={ep.obs['last_damage_source']} "
                            f"health={ep.obs['health']} food={ep.obs['food']} "
                            f"pose=({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                            f"{ep.obs['z']:.3f}) mobs={ep.obs['mobs'][:12]}")
            descending_sand_stair_positive_z(ep)
        elif direct_tunnel and ep.obs["z"] < 25.0:
            if far_exit:
                settle(ep)
                pillar_up_mixed(ep, 70.0, items=(12, 13, 4, 3, 35, 5))
                bridge_positive_axis_mixed(ep, "x", 50.5)
                bridge_positive_axis_mixed(ep, "z", 35.5)
                if ep.can_save:
                    ep.step(save_slot="portal-bridge-complete")
                navigate(ep, 50.5, 37.5, max_ticks=180, sprint=True,
                         tolerance=0.6, arrival_idle=0)
                settle(ep)
                escape_route = ()
            else:
                escape_route = ((25.5, 10.5), (15.5, 10.5),
                                (15.5, 35.5), (25.5, 35.5),
                                (40.5, 35.5))
            for route_x, route_z in escape_route:
                navigate(ep, route_x, route_z, max_ticks=500, sprint=True,
                         tolerance=0.7, arrival_idle=0,
                         combat=False if far_exit else "creeper")
        navigate(ep, x + 0.5, 35.5, max_ticks=800,
                 sprint=far_exit or bridge_front or bridge_complete,
                 tolerance=0.3, arrival_idle=0,
                 combat=False if far_exit or bridge_front or bridge_complete
                 else "creeper")
        ep.step(probe_x=x, probe_y=65, probe_z=34)
        if ep.obs["probe"][0] in (0, 8, 9):
            stair_item = next((item for item in (4, 3, 35, 5)
                               if item_count(ep.obs, item) > 0), None)
            if stair_item is None:
                raise RuntimeError("lava cast alignment exhausted stair blocks")
            place_item_on(ep, x, 64, 34, stair_item)
        for stair_z in (34.5, 33.5, 32.5):
            destination_z = math.floor(stair_z)
            if bridge_complete:
                for roof_z in (math.floor(ep.obs["z"]), destination_z):
                    ep.step(probe_x=x, probe_y=69, probe_z=roof_z)
                    if ep.obs["probe"][0] != 0:
                        mine_probed_coordinate(
                            ep, x, 69, roof_z,
                            available_break_tool(ep.obs), max_ticks=2400)
            ep.step(probe_x=x, probe_y=math.floor(ep.obs["y"]) + 1,
                    probe_z=destination_z)
            if ep.obs["probe"][0] not in (0, 8, 9):
                # A previous water-prime pillar can leave the next tread two
                # blocks above our feet. Raise the current tread once before
                # asking ordinary jump navigation to cross it.
                lift_item = next((item for item in (4, 3, 35, 5, 12)
                                  if item_count(ep.obs, item) > 0), None)
                if lift_item is None:
                    raise RuntimeError("lava cast staircase exhausted blocks")
                pillar_one_staggered(ep, lift_item)
            navigate(ep, x + 0.5, stair_z, max_ticks=240,
                     sprint=False, tolerance=0.22, arrival_idle=0,
                     swim=not bridge_complete)
            settle(ep)
    else:
        navigate(ep, target[0] + 0.5, 32.5, max_ticks=800,
                 sprint=False, tolerance=0.3, arrival_idle=0)
    # The high-row staircase already puts the eye comfortably within reach of
    # the target support. An extra pillar at its water-covered last tread has
    # no attachable face and is unnecessary.
    cast_foot_y = target[1] if target[1] >= 68 else target[1] + 1.0
    while ep.obs["y"] < cast_foot_y:
        pillar_item = next((item for item in (4, 3, 35, 5, 12)
                            if item_count(ep.obs, item) > 0), None)
        if pillar_item is None:
            raise RuntimeError("lava cast alignment exhausted pillar blocks")
        pillar_one_staggered(ep, pillar_item)
    if bridge_complete:
        for roof_z in (31, 30, 29):
            ep.step(probe_x=target[0], probe_y=69, probe_z=roof_z)
            if ep.obs["probe"][0] != 0:
                mine_probed_coordinate(
                    ep, target[0], 69, roof_z,
                    available_break_tool(ep.obs), max_ticks=2400)
    settle(ep)
    x, y, z = target
    place_fluid_on(ep, x, y - 1, z, 327)
    for _ in range(8):
        ep.step()
    if not any(b[0] == 49 and b[1:] == [x, y, z]
               for b in ep.obs["blocks"]):
        local = [b for b in ep.obs["blocks"]
                 if b != [0, 0, 0, 0] and 48 <= b[1] <= 55
                 and 63 <= b[2] <= 70 and 27 <= b[3] <= 31]
        raise RuntimeError(
            f"east-pool portal cast at {target} failed; "
            f"bucket={[(i, c) for i, c in zip(ep.obs['inventory_ids'], ep.obs['inventory_counts']) if i in (325, 326, 327)]} "
            f"local={local}")


def east_pool_lava_trip(ep, sources, target, first_visit, safe_exit=False):
    """Fetch one source from the large sealed east pool and cast it."""
    if ep.obs["food"] < 18 and meat_count(ep.obs) > 0:
        eat_available_meat(ep)
    for _ in range(240):
        if ep.obs["health"] >= 8.0:
            break
        ep.step()
        if ep.obs["food"] < 20 and meat_count(ep.obs) > 0:
            eat_available_meat(ep)
    if ep.obs["health"] < 8.0:
        raise RuntimeError(
            f"east-pool trip lacks health for the open-shaft drop; "
            f"health={ep.obs['health']} food={ep.obs['food']} "
            f"meat={meat_count(ep.obs)} pose=({ep.obs['x']:.3f},"
            f"{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
            f"fall={ep.obs['fall_distance']} fire={ep.obs['fire_ticks']} "
            f"source={ep.obs['last_damage_source']} "
            f"mobs={ep.obs['mobs'][:12]}")
    east_path = (
        (40.5, 20.5), (40.5, 5.5), (40.5, -5.5),
        (60.5, -5.5), (90.5, -5.5), (112.5, -5.5),
        (112.5, 20.5), (112.5, 43.5),
    )
    for x, z in east_path:
        navigate(ep, x, z, max_ticks=400, sprint=True, tolerance=0.7,
                 arrival_idle=0, combat=True)
    settle(ep)
    # Dig a short vertical access shaft, then a four-block gallery into the
    # pool's west wall.  The first version attempted a descending staircase;
    # on flat sandstone it walked west without losing Y and spent every pick
    # on an 80-block tunnel.
    shaft_x = math.floor(ep.obs["x"])
    shaft_z = math.floor(ep.obs["z"])
    # On resumed visits the open one-wide shaft drops the player before this
    # pose is sampled; the mapped plateau standing height is 69.
    surface_y = max(ep.obs["y"], 69.0)
    navigate(ep, shaft_x + 0.5, shaft_z + 0.5, max_ticks=80,
             sprint=False, tolerance=0.08, arrival_idle=0)
    settle(ep)
    if first_visit:
        while ep.obs["y"] > 60.2:
            start_y = ep.obs["y"]
            target_y = math.floor(start_y) - 1
            pick = available_pick(ep.obs)
            if pick is None:
                raise RuntimeError("pool shaft exhausted every pick")
            slot = ensure_hotbar(ep, pick)
            for _ in range(120):
                dyaw, dpitch = look_at(
                    ep.obs, shaft_x + 0.5, target_y + 0.5,
                    shaft_z + 0.5)
                ep.step(attack=1, hotbar=slot, dyaw=dyaw,
                        dpitch=dpitch, cam=1)
                if ep.obs["y"] < start_y - 0.55:
                    break
            else:
                raise RuntimeError(f"pool shaft did not descend at y={target_y}")
            settle(ep)
    else:
        descend_mixed_pillar(ep, 60.0)
    gallery_start = shaft_x
    gallery_y = math.floor(ep.obs["y"])
    for x in range(gallery_start + 1, 117):
        if first_visit:
            for y in (gallery_y, gallery_y + 1):
                pick = available_pick(ep.obs)
                if pick is None:
                    raise RuntimeError("pool gallery exhausted every pick")
                ensure_hotbar(ep, pick)
                mine_coordinate(ep, x, y, shaft_z, None, pick,
                                max_ticks=120, require_pickup=False,
                                detect_depth=True)
        navigate(ep, x + 0.5, shaft_z + 0.5, max_ticks=100,
                 sprint=False, tolerance=0.22)
    pickup_any_lava(ep, sources)
    day_tick = ep.obs.get("world_time", ep.obs["t"]) % 24000
    # The long alternate exit remains sealed until its far end, so waiting at
    # the lava gallery as well only burns a second night of hunger. The short
    # legacy exit still needs this shelter before it returns to the surface.
    if day_tick >= 6000 and not safe_exit:
        navigate(ep, 115.5, shaft_z + 0.5, max_ticks=100,
                 sprint=False, tolerance=0.25, arrival_idle=0)
        shelter_item = next(item for item in (4, 12, 3, 35, 5)
                            if item_count(ep.obs, item) >= 2)
        for support_y in (gallery_y - 1, gallery_y):
            place_item_on(ep, 114, support_y, shaft_z, shelter_item)
        for _ in range(24000 - day_tick + 1200):
            ep.step()
            if ep.obs["dead"]:
                raise RuntimeError("east-pool gallery shelter was breached")
        tool = available_break_tool(ep.obs)
        if tool is None:
            raise RuntimeError("east-pool gallery shelter has no break item")
        for block_y in (gallery_y + 1, gallery_y):
            mine_probed_coordinate(ep, 114, block_y, shaft_z, tool,
                                   max_ticks=800)
    # Re-open the whole return gallery. Earlier casts may have sheltered in
    # it, and their two-block plug is persistent world state in this replay.
    for x in range(115, gallery_start - 1, -1):
        for y in (gallery_y, gallery_y + 1):
            ep.step(probe_x=x, probe_y=y, probe_z=shaft_z)
            if ep.obs["probe"][0] != 0:
                tool = available_break_tool(ep.obs)
                if tool is None:
                    raise RuntimeError("pool return exhausted every tool")
                mine_probed_coordinate(ep, x, y, shaft_z, tool,
                                       max_ticks=800)
        navigate(ep, x + 0.5, shaft_z + 0.5, max_ticks=100,
                 sprint=False, tolerance=0.9, arrival_idle=0)
    if safe_exit and target[1] >= 68:
        # A direct underground return is both shorter and deterministic. Mine
        # west to the portal column, turn north under it, then surface inside
        # the already-watered mold without exposing a starved player to mobs.
        navigate(ep, gallery_start + 0.5, shaft_z + 0.5,
                 max_ticks=120, sprint=False, tolerance=0.12,
                 arrival_idle=0)
        settle(ep)
        while ep.obs["y"] > 52.2:
            x = math.floor(ep.obs["x"])
            y = math.floor(ep.obs["y"]) - 1
            z = math.floor(ep.obs["z"])
            tool = available_break_tool(ep.obs)
            if tool is None:
                raise RuntimeError("deep portal return exhausted every tool")
            mine_probed_coordinate(ep, x, y, z, tool, max_ticks=2400)
            settle(ep)
        tunnel_y = math.floor(ep.obs["y"])
        portal_z = 32
        # Turn north first. The straight west line at z=43 intersects the
        # original lava reservoir around x=56; z=32 stays in dry sandstone.
        for step_index, z in enumerate(
                range(shaft_z - 1, portal_z - 1, -1), start=1):
            for y in (tunnel_y, tunnel_y + 1):
                ep.step(probe_x=gallery_start, probe_y=y, probe_z=z)
                if ep.obs["probe"][0] != 0:
                    tool = available_break_tool(ep.obs)
                    if tool is None:
                        raise RuntimeError("portal turn exhausted every tool")
                    mine_probed_coordinate(ep, gallery_start, y, z, tool,
                                           max_ticks=1600)
            navigate(ep, gallery_start + 0.5, z + 0.5, max_ticks=100,
                     sprint=False, tolerance=0.3, arrival_idle=0)
            if step_index % 8 == 0:
                seal_item = next((value for value in (4, 3, 35, 5, 12)
                                  if item_count(ep.obs, value) >= 2), None)
                if seal_item is None:
                    raise RuntimeError("portal turn has no bulkhead blocks")
                place_item_on(ep, gallery_start, tunnel_y - 1, z + 1,
                              seal_item)
                place_item_on(ep, gallery_start, tunnel_y, z + 1,
                              seal_item)
        for step_index, x in enumerate(
                range(gallery_start - 1, 49, -1), start=1):
            for y in (tunnel_y, tunnel_y + 1):
                ep.step(probe_x=x, probe_y=y, probe_z=portal_z)
                if ep.obs["probe"][0] != 0:
                    tool = available_break_tool(ep.obs)
                    if tool is None:
                        raise RuntimeError("portal return exhausted every tool")
                    mine_probed_coordinate(ep, x, y, portal_z, tool,
                                           max_ticks=1600)
            navigate(ep, x + 0.5, portal_z + 0.5, max_ticks=100,
                     sprint=False, tolerance=0.3, arrival_idle=0)
            if step_index % 8 == 0:
                seal_item = next((value for value in (4, 3, 35, 5, 12)
                                  if item_count(ep.obs, value) >= 2), None)
                if seal_item is None:
                    raise RuntimeError("portal return has no bulkhead blocks")
                place_item_on(ep, x + 1, tunnel_y - 1, portal_z,
                              seal_item)
                place_item_on(ep, x + 1, tunnel_y, portal_z, seal_item)
        # Drain the desert's falling column from a side alcove. Doing this
        # while standing in the shaft lets sand occupy the eye cell and deal
        # suffocation damage, which is lethal at the starvation floor.
        for y in (tunnel_y, tunnel_y + 1):
            ep.step(probe_x=49, probe_y=y, probe_z=portal_z)
            if ep.obs["probe"][0] != 0:
                tool = available_break_tool(ep.obs)
                if tool is None:
                    raise RuntimeError("portal alcove exhausted every tool")
                mine_probed_coordinate(ep, 49, y, portal_z, tool,
                                       max_ticks=2400)
        navigate(ep, 49.5, portal_z + 0.5, max_ticks=120,
                 sprint=False, tolerance=0.2, arrival_idle=0)
        if item_count(ep.obs, 50) == 0:
            craft_torches(ep)
        place_item_on(ep, 50, tunnel_y - 1, portal_z, 50)
        drain_y = tunnel_y + 1
        tool = available_break_tool(ep.obs)
        if tool is None:
            raise RuntimeError("portal sand drain exhausted every tool")
        slot = ensure_hotbar(ep, tool)
        clear_ticks = 0
        for _ in range(12000):
            dyaw, dpitch = look_at(
                ep.obs, 50.5, drain_y + 0.5, portal_z + 0.5)
            ep.step(attack=1, hotbar=slot, dyaw=dyaw, dpitch=dpitch,
                    cam=1, probe_x=50, probe_y=drain_y,
                    probe_z=portal_z)
            if ep.obs["dead"]:
                raise RuntimeError("portal sand drain was lethal")
            clear_ticks = clear_ticks + 1 if ep.obs["probe"][0] == 0 else 0
            if clear_ticks >= 40:
                break
        else:
            raise RuntimeError("portal sand column did not drain")
        navigate(ep, 50.5, portal_z + 0.5, max_ticks=120,
                 sprint=False, tolerance=0.2, arrival_idle=0)
        if ep.can_save:
            ep.step(save_slot="portal-direct-shaft")
        navigate(ep, 50.5, portal_z + 0.5, max_ticks=160,
                 sprint=False, tolerance=0.05, arrival_idle=0)
        settle(ep)
        ep.step(probe_x=50, probe_y=math.floor(ep.obs["y"]),
                probe_z=portal_z)
        if ep.obs["probe"][0] == 50:
            mine_probed_coordinate(
                ep, 50, math.floor(ep.obs["y"]), portal_z,
                available_break_tool(ep.obs), max_ticks=80)
        mine_and_pillar_shaft(ep, target[1])
        finish_east_portal_cast(ep, target, east_path, direct_tunnel=True)
        return
    if not safe_exit:
        navigate(ep, shaft_x + 0.5, shaft_z + 0.5, max_ticks=100,
                 sprint=False, tolerance=0.15, arrival_idle=0)
        pillar_up_mixed(ep, surface_y)
        finish_east_portal_cast(ep, target, east_path)
        return
    # Leave by a second shaft well west of the exposed entrance. Non-burning
    # mobs can camp the original opening. The far end of this sealed tunnel
    # performs the one required daylight wait immediately before surfacing.
    exit_x = shaft_x
    # Go beyond 32 blocks so the entrance campers enter vanilla random-
    # despawn range while we remain protected underground.
    exit_z = shaft_z - 52
    for z in range(shaft_z, exit_z - 1, -1):
        tunnel_y = math.floor(ep.obs["y"])
        for y in (tunnel_y, tunnel_y + 1):
            ep.step(probe_x=exit_x, probe_y=y, probe_z=z)
            if ep.obs["probe"][0] != 0:
                tool = available_break_tool(ep.obs)
                if tool is None:
                    raise RuntimeError("alternate exit exhausted every tool")
                mine_probed_coordinate(ep, exit_x, y, z, tool,
                                       max_ticks=800)
        navigate(ep, exit_x + 0.5, z + 0.5, max_ticks=100,
                 sprint=False, tolerance=0.22, arrival_idle=0)
    settle(ep)
    for _ in range(800):
        ep.step()
        if ep.obs["dead"]:
            raise RuntimeError("alternate-exit despawn wait was breached")
    # Do not wait out the night here. On a foodless long-horizon tape that
    # deliberately reduces health to the NORMAL starvation floor, making any
    # later contact fatal. The fresh shaft and eastward surface detour provide
    # the safety margin while retaining the health carried into the trip.
    if target[1] >= 68:
        fresh_exit_x = exit_x + 24
        for tunnel_x in range(exit_x + 1, fresh_exit_x + 1):
            tunnel_y = math.floor(ep.obs["y"])
            for block_y in (tunnel_y, tunnel_y + 1):
                ep.step(probe_x=tunnel_x, probe_y=block_y,
                        probe_z=exit_z)
                if ep.obs["probe"][0] != 0:
                    tool = available_break_tool(ep.obs)
                    if tool is None:
                        raise RuntimeError("fresh exit exhausted every tool")
                    mine_probed_coordinate(ep, tunnel_x, block_y, exit_z,
                                           tool, max_ticks=800)
            navigate(ep, tunnel_x + 0.5, exit_z + 0.5,
                     max_ticks=100, sprint=False, tolerance=0.22,
                     arrival_idle=0)
        exit_x = fresh_exit_x
    exit_surface_y = 90.0 if target[1] >= 68 else surface_y + 1.0
    # Some sandstone columns open into shallow cavities. Anchor a solid floor
    # to the untouched east wall before nerd-poling; a downward click has no
    # face to attach to in that case.
    floor_item = next((item for item in (4, 3, 35, 5)
                       if item_count(ep.obs, item) > 0), None)
    if floor_item is None:
        raise RuntimeError("alternate shaft has no stable floor block")
    floor_y = math.floor(ep.obs["y"]) - 1
    ep.step(probe_x=exit_x, probe_y=floor_y, probe_z=exit_z)
    if ep.obs["probe"][0] == 0:
        before_floor = item_count(ep.obs, floor_item)
        floor_slot = ensure_hotbar(ep, floor_item)
        for _ in range(12):
            dyaw, dpitch = look_at(
                ep.obs, exit_x + 1.02, floor_y + 0.5, exit_z + 0.5)
            ep.step(use=1, do_place=1, hotbar=floor_slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1,
                    probe_x=exit_x, probe_y=floor_y, probe_z=exit_z)
            if ep.obs["probe"][0] != 0:
                break
            ep.step()
        else:
            raise RuntimeError("failed to anchor alternate-shaft floor")
    settle(ep)
    for _ in range(32):
        if ep.obs["y"] >= exit_surface_y - 0.2:
            break
        # Clear only the three cells the jump and standing player can occupy.
        # A desert column can refill a lower cell while a higher one is being
        # mined, so sweep until all three are simultaneously clear.
        for _clearance_round in range(24):
            blocked = False
            for ceiling_y in range(math.floor(ep.obs["y"]) + 1,
                                   math.floor(ep.obs["y"]) + 4):
                if ceiling_y > math.floor(exit_surface_y) + 1:
                    continue
                ep.step(probe_x=exit_x, probe_y=ceiling_y, probe_z=exit_z)
                if ep.obs["probe"][0] == 0:
                    continue
                blocked = True
                tool = available_break_tool(ep.obs)
                if tool is None:
                    raise RuntimeError("alternate shaft exhausted every tool")
                mine_probed_coordinate(ep, exit_x, ceiling_y, exit_z, tool,
                                       max_ticks=2400)
            if not blocked:
                break
        else:
            raise RuntimeError("alternate shaft sand column did not settle")
        pillar_item = next((item for item in (4, 3, 35, 5)
                            if item_count(ep.obs, item) > 0), None)
        if pillar_item is None:
            raise RuntimeError("alternate shaft exhausted stable pillar blocks")
        pillar_one_staggered(ep, pillar_item)
    else:
        raise RuntimeError(
            f"alternate shaft exceeded 32 levels at y={ep.obs['y']}")
    fresh_return = target[1] >= 68
    if fresh_return:
        fresh_surface_route = (
            (136.5, -8.5), (125.5, -8.5), (125.5, -7.5),
            (124.5, -7.5), (124.5, -3.5), (123.5, -3.5),
            (123.5, -1.5), (122.5, -1.5), (122.5, -0.5),
            (121.5, -0.5), (121.5, 0.5), (120.5, 0.5),
            (120.5, 1.5), (119.5, 1.5), (119.5, 2.5),
            (117.5, 2.5), (117.5, 3.5), (113.5, 3.5),
            (113.5, 4.5), (125.5, 4.5), (125.5, 20.5),
            (112.5, 20.5),
        )
        for route_x, route_z in fresh_surface_route:
            navigate(ep, route_x, route_z, max_ticks=300,
                     sprint=True, tolerance=0.65, arrival_idle=0,
                     combat=route_x >= 124.0 or route_z >= 20.0)
    finish_east_portal_cast(ep, target, east_path,
                            fresh_return=fresh_return)


def place_fluid_on(ep, x, y, z, full_bucket_item):
    slot = ensure_hotbar(ep, full_bucket_item)
    dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.98, z + 0.5)
    ep.step(use=1, do_place=1, hotbar=slot,
            dyaw=dyaw, dpitch=dpitch, cam=1)
    if 325 not in ep.obs["inventory_ids"]:
        raise RuntimeError(
            f"failed to place bucket fluid on {x},{y},{z}; "
            f"pose=({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
            f"{ep.obs['z']:.3f}) look=({ep.obs['yaw']:.2f},"
            f"{ep.obs['pitch']:.2f}) hotbar={ep.obs['hotbar_ids']}/"
            f"{ep.obs['hotbar_counts']} probe={ep.obs.get('probe')}")


def place_fluid_against(ep, x, y, z, full_bucket_item):
    """Click the south face of a support; fluid is placed at z+1."""
    slot = ensure_hotbar(ep, full_bucket_item)
    navigate(ep, x + 0.5, z + 2.5, max_ticks=180,
             sprint=False, tolerance=0.45)
    dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.5, z + 0.98)
    ep.step(use=1, do_place=1, hotbar=slot,
            dyaw=dyaw, dpitch=dpitch, cam=1)
    if 325 not in ep.obs["inventory_ids"]:
        support = [b for b in ep.obs["blocks"]
                   if b[1] == x and b[3] == z]
        raise RuntimeError(
            f"failed to place bucket fluid against {x},{y},{z}; "
            f"pos=({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
            f"look=({ep.obs['yaw']:.2f},{ep.obs['pitch']:.2f}) "
            f"support={support} hotbar={ep.obs['hotbar_ids']}")


def place_fluid_against_current(ep, x, y, z, full_bucket_item):
    slot = ensure_hotbar(ep, full_bucket_item)
    for _ in range(12):
        dyaw, dpitch = look_at(ep.obs, x + 0.5, y + 0.5, z + 0.98)
        ep.step(use=1, do_place=1, hotbar=slot,
                dyaw=dyaw, dpitch=dpitch, cam=1)
        if 325 in ep.obs["inventory_ids"]:
            return
        ep.step()
    raise RuntimeError(
        f"failed elevated bucket placement against {x},{y},{z}; "
        f"pos=({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f})")


def nearby_mob(ep, types, radius=48.0, max_vertical=None):
    rows = [m for m in ep.obs["mobs"] if m[0] in types and m[5] > 0.0 and
            math.hypot(m[2] - ep.obs["x"], m[4] - ep.obs["z"]) <= radius and
            (max_vertical is None
             or abs(m[3] - ep.obs["y"]) <= max_vertical)]
    return min(rows, key=lambda m: math.hypot(
        m[2] - ep.obs["x"], m[4] - ep.obs["z"])) if rows else None


def nearby_oasis_passive(ep, radius=64.0, center_x=80.5, center_z=-80.5):
    """Return a reachable passive without letting successive hunts drift."""
    rows = [m for m in ep.obs["mobs"]
            if m[0] in (10, 11, 12, 13)
            and m[5] > 0.0
            and math.hypot(m[2] - center_x, m[4] - center_z) <= 24.0
            and (center_z != -80.5 or m[2] <= center_x + 2.0)
            and math.hypot(m[2] - ep.obs["x"], m[4] - ep.obs["z"]) <= radius
            and abs(m[3] - ep.obs["y"]) <= 6.0]
    # Chickens are fast, light targets around this oasis and repeatedly flee
    # into its submerged ravine. Prefer a nearby large passive, then use a
    # chicken only when it is the remaining food source.
    return min(rows, key=lambda m: (m[0] == 13, math.hypot(
        m[2] - ep.obs["x"], m[4] - ep.obs["z"]), m[5])) if rows else None


RAW_MEAT = (319, 423, 363, 365)
ALL_MEAT = (320, 424, 364, 366, 367) + RAW_MEAT


def meat_count(obs):
    return sum(item_count(obs, item) for item in ALL_MEAT)


def hunt_passive(ep, wanted=3, center_x=80.5, center_z=-80.5):
    before = meat_count(ep.obs)
    attempts = []
    for _ in range(12):
        mob = nearby_oasis_passive(
            ep, center_x=center_x, center_z=center_z)
        if mob is None:
            oasis_rows = [m for m in ep.obs["mobs"]
                          if m[0] in (10, 11, 12, 13)
                          and (center_z != -80.5 or
                               m[2] <= center_x + 2.0)
                          and math.hypot(m[2] - center_x,
                                         m[4] - center_z) <= 24.0]
            if oasis_rows:
                navigate(ep, center_x, center_z, max_ticks=300,
                         sprint=False, tolerance=0.5, arrival_idle=0,
                         swim=True)
                settle(ep)
                mob = nearby_oasis_passive(
                    ep, center_x=center_x, center_z=center_z)
            if mob is None:
                break
        eid = mob[1]
        selected = list(mob)
        attacks = 0
        best_distance = float("inf")
        chase_stall = 0
        for _ in range(1200):
            if ep.obs["dead"]:
                break
            if nearby_mob(ep, (4,), radius=16.0) is not None:
                evade_creepers(ep, radius=8.0)
                best_distance = float("inf")
                chase_stall = 0
                continue
            if ep.obs["air"] < 180:
                dyaw, dpitch = look_at(
                    ep.obs, center_x, ep.obs["y"] + 3.0,
                    center_z + 6.0)
                ep.step(forward=1, jump=1, sprint=1,
                        dyaw=dyaw, dpitch=dpitch)
                if ep.obs["dead"]:
                    break
                continue
            current = next((m for m in ep.obs["mobs"]
                            if m[1] == eid and m[5] > 0.0), None)
            if (current is not None and center_z == -80.5
                    and math.hypot(current[2] - center_x,
                                   current[4] - center_z)
                    > (36.0 if current[5] <= 4.0 else 26.0)):
                break
            if current is None:
                drops = [item for item in ep.obs.get("items", [])
                         if item[0] in ALL_MEAT]
                if drops:
                    drop = min(drops, key=lambda item:
                               (item[3] - ep.obs["x"]) ** 2
                               + (item[5] - ep.obs["z"]) ** 2)
                    pickup_x, pickup_z = drop[3], drop[5]
                else:
                    pickup_x, pickup_z = mob[2], mob[4]
                if os.environ.get("NETHERITE_DEBUG_FOOD_PICKUP"):
                    raise RuntimeError(
                        f"food pickup debug selected={selected} "
                        f"attacks={attacks} pose=({ep.obs['x']:.3f},"
                        f"{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
                        f"health={ep.obs['health']} food={ep.obs['food']} "
                        f"drops={ep.obs.get('items', [])[:12]} "
                        f"mobs={ep.obs['mobs'][:20]}")
                evade_creepers(ep, radius=8.0)
                if (center_x == 80.5 and center_z == -80.5 and
                        pickup_x > 90.0 and ep.obs["x"] < 90.0):
                    # The direct east bank crosses a persistent creeper's
                    # fuse radius. Collect eastern drops around the dry north
                    # rim instead of walking through that deterministic blast.
                    navigate(ep, ep.obs["x"], -63.5, max_ticks=300,
                             sprint=True, tolerance=0.8, arrival_idle=0,
                             combat=True)
                    navigate(ep, pickup_x, -63.5, max_ticks=300,
                             sprint=True, tolerance=0.8, arrival_idle=0,
                             combat=True)
                navigate(ep, pickup_x, pickup_z, max_ticks=300,
                         sprint=False, tolerance=0.8, arrival_idle=0,
                         swim=ep.obs["y"] <= 63.2, combat=True)
                for _ in range(20):
                    ep.step(jump=int(ep.obs["y"] <= 63.2))
                break
            mob = current
            distance = math.hypot(mob[2] - ep.obs["x"], mob[4] - ep.obs["z"])
            vertical = mob[3] - ep.obs["y"]
            if distance > 3.0 or abs(vertical) > 2.5:
                dyaw, dpitch = look_at(ep.obs, mob[2], mob[3] + 0.8, mob[4])
                if distance < best_distance - 0.02:
                    best_distance = distance
                    chase_stall = 0
                else:
                    chase_stall += 1
                if chase_stall > 8:
                    # Keep pressure toward the target while climbing/swimming.
                    # Lateral unsticking drifted east across the ravine and
                    # directly over a trapped creeper below the bank.
                    ep.step(forward=1, sprint=1, jump=1,
                            dyaw=dyaw, dpitch=dpitch)
                    chase_stall = 0
                else:
                    ep.step(forward=1, sprint=1,
                            jump=int(vertical > 0.5
                                     or ep.obs["y"] <= 63.2),
                            dyaw=dyaw, dpitch=dpitch)
                continue
            weapon = next((item for item in (268, 257, 274, 270, 259)
                           if item in ep.obs["inventory_ids"]), None)
            if weapon is None:
                slot = next((slot for slot, item in
                             enumerate(ep.obs["hotbar_ids"])
                             if item == 0), ep.obs["hotbar_sel"])
            else:
                slot = ensure_hotbar(ep, weapon)
            dyaw, dpitch = look_at(ep.obs, mob[2], mob[3] + 0.8, mob[4])
            ep.step(attack=1, do_break=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
            attacks += 1
            for _ in range(11):
                current = next(
                    (m for m in ep.obs["mobs"]
                     if m[1] == eid and m[5] > 0.0), None)
                if current is None:
                    break
                # Stay alongside the knockback target instead of driving it
                # toward the oasis ravine during every cooldown.
                dyaw, dpitch = look_at(
                    ep.obs, current[2], current[3] + 0.8, current[4])
                ep.step(strafe=1, sprint=1,
                        dyaw=dyaw, dpitch=dpitch)
        if meat_count(ep.obs) >= before + wanted:
            return
        attempts.append({"selected": selected, "attacks": attacks,
                         "final": next((m for m in ep.obs["mobs"]
                                        if m[1] == eid), None)})
    if meat_count(ep.obs) <= before:
        raise RuntimeError(
            f"passive hunt produced no meat at ({ep.obs['x']:.2f},"
            f"{ep.obs['y']:.2f},{ep.obs['z']:.2f}); attempts={attempts} "
            f"dead={ep.obs['dead']} health={ep.obs['health']} "
            f"air={ep.obs['air']} "
            f"mobs={ep.obs['mobs'][:12]}")


def pillar_kill_nearby_wither(ep, radius=10.0):
    """Kill one wither skeleton from a two-block ordinary scaffold."""
    mob = nearby_mob(ep, (32,), radius=radius)
    if mob is None:
        return
    ground_y = ep.obs["y"]
    eid = mob[1]
    sword = ensure_hotbar(ep, 268)
    # Build before making contact. At this range the two jump/place arcs
    # complete before the skeleton can enter melee reach.
    pillar_up_mixed(ep, math.floor(ground_y) + 3.0,
                    items=(24, 12), centered=False)
    for tick in range(500):
        current = next((m for m in ep.obs["mobs"] if m[1] == eid), None)
        if current is None:
            descend_mixed_pillar(ep, ground_y)
            return
        if (current[3] < ground_y - 4.0 or
                math.hypot(current[2] - ep.obs["x"],
                           current[4] - ep.obs["z"]) > radius + 4.0):
            # Knockback into the fortress ravine is as route-safe as a kill;
            # do not wait indefinitely for an unreachable four-health mob.
            descend_mixed_pillar(ep, ground_y)
            return
        dyaw, dpitch = look_at(
            ep.obs, current[2], current[3] + 2.2, current[4])
        distance = math.sqrt(
            (current[2] - ep.obs["x"]) ** 2 +
            (current[3] + 2.2 - ep.obs["y"] - 1.62) ** 2 +
            (current[4] - ep.obs["z"]) ** 2)
        if distance <= 3.6 and tick % 12 == 0:
            ep.step(attack=1, do_break=1, hotbar=sword,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
        else:
            ep.step(dyaw=dyaw, dpitch=dpitch)
        if ep.obs["dead"]:
            raise RuntimeError("wither scaffold combat was lethal")
    raise RuntimeError(
        f"wither {eid} did not die from scaffold combat; target={current} "
        f"distance={distance:.3f} pose=({ep.obs['x']:.3f},"
        f"{ep.obs['y']:.3f},{ep.obs['z']:.3f}) blocks="
        f"{ep.obs['blocks'][:20]}")


def kill_nearby_hostiles(ep, radius=12.0, hostile_types=None,
                         corridor_target=None):
    """Clear ordinary route threats with the iron pick before a long leg."""
    # Daytime spiders and unprovoked Endermen are neutral; attacking either
    # creates a route hazard that ordinary avoidance would not have.
    if hostile_types is None:
        hostile_types = (2, 3, 4, 5, 7, 32)
    max_targets = 1 if hostile_types == (7,) else 8
    for _ in range(max_targets):
        mob = nearby_mob(ep, hostile_types, radius=radius,
                         max_vertical=3.0)
        if mob is None:
            return
        eid = mob[1]
        for attack_round in range(10):
            current = next((m for m in ep.obs["mobs"]
                            if m[1] == eid and m[5] > 0.0), None)
            if current is None:
                if hostile_types in ((32,), (7,)):
                    # A new fortress mob can enter range while the selected
                    # target is dying. Do not stand still to heal in front of
                    # it; let the outer target loop acquire it first.
                    if nearby_mob(ep, hostile_types, radius=radius,
                                  max_vertical=3.0) is not None:
                        break
                    eat_available_meat(ep)
                    for _ in range(360):
                        if (ep.obs["health"] >= 19.0 and
                                ep.obs["fire_ticks"] <= 0):
                            break
                        fireball = nearby_mob(
                            ep, (27,), radius=12.0, max_vertical=8.0)
                        if fireball is not None:
                            fyaw, fpitch = look_at(
                                ep.obs, fireball[2], fireball[3] + 0.3,
                                fireball[4])
                            ep.step(
                                attack=1, do_break=1,
                                hotbar=ensure_hotbar(
                                    ep, available_break_tool(ep.obs)),
                                dyaw=fyaw, dpitch=fpitch, cam=1)
                        else:
                            ep.step(sneak=1)
                        if ep.obs["dead"]:
                            raise RuntimeError(
                                "post-hostile route recovery was lethal")
                break
            for _ in range(180):
                current = next(
                    (m for m in ep.obs["mobs"]
                     if m[1] == eid and m[5] > 0.0), None)
                if current is None:
                    break
                if hostile_types in ((7,), (32,)):
                    fireball = nearby_mob(ep, (27,), radius=12.0,
                                          max_vertical=8.0)
                    if fireball is not None:
                        fyaw, fpitch = look_at(
                            ep.obs, fireball[2], fireball[3] + 0.3,
                            fireball[4])
                        ep.step(attack=1, do_break=1,
                                hotbar=ensure_hotbar(
                                    ep, available_break_tool(ep.obs)),
                                dyaw=fyaw, dpitch=fpitch, cam=1)
                        continue
                aim_y = (current[3] + 0.05 if hostile_types == (7,)
                         else current[3] + 0.9)
                distance = math.sqrt(
                    (current[2] - ep.obs["x"]) ** 2 +
                    (aim_y - ep.obs["y"] - 1.62) ** 2 +
                    (current[4] - ep.obs["z"]) ** 2)
                dyaw, dpitch = look_at(
                    ep.obs, current[2], aim_y, current[4])
                if distance <= (3.5 if hostile_types == (7,) else 2.75):
                    weapon = next((item for item in (268, 257, 274, 270, 259)
                                   if item in ep.obs["inventory_ids"]), None)
                    slot = (ensure_hotbar(ep, weapon) if weapon is not None
                            else next((index for index, item in enumerate(
                                ep.obs["hotbar_ids"]) if item == 0),
                                      ep.obs["hotbar_sel"]))
                    ep.step(attack=1, do_break=1, hotbar=slot,
                            dyaw=dyaw, dpitch=dpitch, cam=1)
                    # CPacketUseEntity is consumed on the following locked
                    # server tick. Moving on that tick changes the server
                    # reach origin and can turn a visually valid queued hit
                    # into a miss, especially against a knockback-moving
                    # blaze. Hold the aimed pose for that one tick.
                    ep.step()
                    break
                ep.step(forward=1, sprint=1, jump=int(distance > 5.0),
                        dyaw=dyaw, dpitch=dpitch)
            if current is None:
                if hostile_types in ((32,), (7,)):
                    eat_available_meat(ep)
                    for _ in range(360):
                        if (ep.obs["health"] >= 19.0 and
                                ep.obs["fire_ticks"] <= 0):
                            break
                        fireball = nearby_mob(
                            ep, (27,), radius=12.0, max_vertical=8.0)
                        if fireball is not None:
                            fyaw, fpitch = look_at(
                                ep.obs, fireball[2], fireball[3] + 0.3,
                                fireball[4])
                            ep.step(
                                attack=1, do_break=1,
                                hotbar=ensure_hotbar(
                                    ep, available_break_tool(ep.obs)),
                                dyaw=fyaw, dpitch=fpitch, cam=1)
                        else:
                            ep.step(sneak=1)
                        if ep.obs["dead"]:
                            raise RuntimeError(
                                "post-wither route recovery was lethal")
                break
            cooldown_ticks = (6 if hostile_types == (7,) else
                              8 if hostile_types == (32,) else 28)
            for _ in range(cooldown_ticks):
                current = next(
                    (m for m in ep.obs["mobs"]
                     if m[1] == eid and m[5] > 0.0), None)
                if current is None:
                    break
                dyaw, dpitch = look_at(
                    ep.obs, current[2], current[3] + 0.9, current[4])
                if hostile_types == (32,):
                    fireball = nearby_mob(ep, (27,), radius=12.0,
                                          max_vertical=8.0)
                    if fireball is not None:
                        fyaw, fpitch = look_at(
                            ep.obs, fireball[2], fireball[3] + 0.3,
                            fireball[4])
                        ep.step(attack=1, do_break=1,
                                hotbar=ensure_hotbar(
                                    ep, available_break_tool(ep.obs)),
                                dyaw=fyaw, dpitch=fpitch, cam=1)
                        continue
                if hostile_types == (7,):
                    fireball = nearby_mob(ep, (27,), radius=5.0,
                                          max_vertical=4.0)
                    if fireball is not None:
                        fyaw, fpitch = look_at(
                            ep.obs, fireball[2], fireball[3] + 0.3,
                            fireball[4])
                        ep.step(attack=1, do_break=1,
                                hotbar=ensure_hotbar(
                                    ep, available_break_tool(ep.obs)),
                                dyaw=fyaw, dpitch=fpitch, cam=1)
                        continue
                    # Circle at sword reach. Backpedalling compounds sword
                    # knockback, turns each cooldown into another long chase,
                    # and gives the blaze time to complete its fireball burst.
                    ep.step(strafe=1 if attack_round % 2 == 0 else -1,
                            sprint=1,
                            dyaw=dyaw, dpitch=dpitch)
                    if ep.obs["dead"]:
                        raise RuntimeError(
                            f"blaze circle {eid} was lethal; health="
                            f"{ep.obs['health']} fire={ep.obs['fire_ticks']} "
                            f"source={ep.obs['last_damage_source']} mobs="
                            f"{ep.obs['mobs'][:12]}")
                    continue
                if hostile_types == (32,) and corridor_target is not None:
                    route_dx = corridor_target[0] - ep.obs["x"]
                    route_dz = corridor_target[1] - ep.obs["z"]
                    if abs(route_dx) >= abs(route_dz):
                        retreat_x = (ep.obs["x"] -
                                     math.copysign(12.0, route_dx))
                        retreat_z = corridor_target[1]
                    else:
                        retreat_x = corridor_target[0]
                        retreat_z = (ep.obs["z"] -
                                     math.copysign(12.0, route_dz))
                    away, _ = look_at(
                        ep.obs, retreat_x, ep.obs["y"] + 1.62,
                        retreat_z)
                else:
                    away = angle_delta(ep.obs["yaw"] + dyaw + 180.0,
                                       ep.obs["yaw"])
                ep.step(forward=1, sprint=1, jump=1,
                        dyaw=away, dpitch=-ep.obs["pitch"])
                if ep.obs["dead"]:
                    raise RuntimeError(
                        f"hostile kite {eid} was lethal; target={current} "
                        f"health={ep.obs['health']} fire="
                        f"{ep.obs['fire_ticks']} source="
                        f"{ep.obs['last_damage_source']} mobs="
                        f"{ep.obs['mobs'][:12]} projectiles="
                        f"{ep.obs.get('projectiles', [])[:12]}")


def kill_crossed_blaze_aggressive(ep, eid, displace_west=False):
    """Finish one blaze that crossed cover before it gets a second melee."""
    sword = next((item for item in (268, 272, 267, 276)
                  if item in ep.obs["inventory_ids"]), None)
    if sword is None:
        raise RuntimeError("crossed blaze combat exhausted swords")
    slot = ensure_hotbar(ep, sword)
    high_hover_ticks = 0
    for _ in range(1200):
        mob = next((m for m in ep.obs["mobs"]
                    if m[1] == eid and m[5] > 0.0), None)
        if mob is None:
            return "dead"
        if displace_west and mob[2] <= -147.0 and ep.obs["x"] >= -145.7:
            return "displaced"
        fireball = nearby_mob(ep, (27,), radius=6.0, max_vertical=5.0)
        if fireball is not None:
            fyaw, fpitch = look_at(
                ep.obs, fireball[2], fireball[3] + 0.3, fireball[4])
            ep.step(attack=1, do_break=1, hotbar=slot,
                    dyaw=fyaw, dpitch=fpitch, cam=1)
            continue
        dyaw, dpitch = look_at(ep.obs, mob[2], mob[3] + 0.5, mob[4])
        distance = math.sqrt(
            (mob[2] - ep.obs["x"]) ** 2 +
            (mob[3] + 0.5 - ep.obs["y"] - 1.62) ** 2 +
            (mob[4] - ep.obs["z"]) ** 2)
        if ep.obs["x"] > -144.2:
            ep.step(forward=-1, sprint=1, jump=1,
                    dyaw=dyaw, dpitch=dpitch)
        elif distance <= 3.8:
            ep.step(attack=1, do_break=1, hotbar=slot,
                    strafe=1 if (_ // 7) % 2 == 0 else -1,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
        elif abs(mob[3] - ep.obs["y"]) <= 4.5:
            high_hover_ticks = 0
            ep.step(forward=1, sprint=1, jump=int(distance > 5.0),
                    dyaw=dyaw, dpitch=dpitch)
        else:
            high_hover_ticks += 1
            ep.step(sneak=1, dyaw=dyaw, dpitch=dpitch)
            if high_hover_ticks >= 120:
                return "displaced"
        if ep.obs["health"] < 15.0 and ep.obs["food"] < 20 and meat_count(
                ep.obs) > 0:
            eat_available_meat(ep)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"crossed blaze {eid} was lethal; target={mob} health="
                f"{ep.obs['health']} fire={ep.obs['fire_ticks']}")
    raise RuntimeError(f"crossed blaze {eid} survived aggressive combat")


def acquire_blaze_rods(ep, target=5):
    """Farm the fortress spawner with ordinary sword combat and pickups."""
    for _cycle in range(40):
        kill_nearby_hostiles(ep, radius=18.0, hostile_types=(7,))
        if ep.can_save:
            ep.step(save_slot=f"blaze-wave-{_cycle}")
        for _ in range(24):
            drops = [item for item in ep.obs.get("items", [])
                     if item[0] == 369]
            if not drops:
                break
            drop = min(drops, key=lambda item:
                       (item[3] - ep.obs["x"]) ** 2 +
                       (item[5] - ep.obs["z"]) ** 2)
            navigate(ep, drop[3], drop[5], max_ticks=180,
                     sprint=True, tolerance=0.5, arrival_idle=8,
                     combat=True)
        if item_count(ep.obs, 369) >= target:
            return
        for wait_tick in range(240):
            if nearby_mob(ep, (7,), radius=18.0,
                          max_vertical=3.0) is not None:
                break
            airborne = nearby_mob(ep, (7,), radius=18.0)
            if airborne is None:
                ep.step()
            else:
                dyaw, dpitch = look_at(
                    ep.obs, airborne[2], airborne[3] + 0.9, airborne[4])
                ep.step(strafe=1 if (wait_tick // 20) % 2 == 0 else -1,
                        sneak=1, dyaw=dyaw, dpitch=dpitch)
            if ep.obs["dead"]:
                raise RuntimeError("blaze-spawner wait was lethal")
    raise RuntimeError(
        f"blaze farm ended at {item_count(ep.obs, 369)}/{target} rods; "
        f"health={ep.obs['health']} food={ep.obs['food']} "
        f"mobs={ep.obs['mobs'][:16]} items={ep.obs.get('items', [])[:16]}")


def kill_blaze_through_bulkhead(ep):
    """Kill one blaze through a low slit while its fire line hits the cap."""
    ep.step(probe_x=-140, probe_y=69, probe_z=-172)
    if ep.obs["probe"][0] == 0:
        # Close the one-wide bridge behind the farm. Natural fortress spawns
        # can otherwise appear around x=-129 inside the long "safe" retreat
        # corridor and repeatedly reset the player's burn timer.
        navigate(ep, -142.5, -171.5, max_ticks=120,
                 sprint=False, tolerance=0.25, arrival_idle=0,
                 combat=False)
        for target_y in (69, 70):
            block = next((item for item in (112, 24, 12)
                          if item_count(ep.obs, item) > 0), None)
            if block is None:
                raise RuntimeError("blaze retreat partition exhausted blocks")
            place_item_on(ep, -140, target_y - 1, -172, block)
        navigate(ep, -142.5, -169.5, max_ticks=100,
                 sprint=False, tolerance=0.25, arrival_idle=0,
                 combat=False)
        for target_z in (-171, -170):
            ep.step(probe_x=-140, probe_y=69, probe_z=target_z)
            if ep.obs["probe"][0] == 0:
                block = next((item for item in (112, 24, 12)
                              if item_count(ep.obs, item) > 0), None)
                if block is None:
                    raise RuntimeError(
                        "blaze retreat partition exhausted side blocks")
                before = item_count(ep.obs, block)
                slot = ensure_hotbar(ep, block)
                for _ in range(16):
                    dyaw, dpitch = look_at(
                        ep.obs, -139.5, 69.5, target_z - 0.001)
                    ep.step(use=1, do_place=1, hotbar=slot,
                            dyaw=dyaw, dpitch=dpitch, cam=1)
                    ep.step(probe_x=-140, probe_y=69,
                            probe_z=target_z)
                    if ep.obs["probe"][0] != 0:
                        break
                    if item_count(ep.obs, block) < before:
                        raise RuntimeError(
                            f"blaze partition side missed -140,69,"
                            f"{target_z}; ray={ep.obs.get('ray')}")
                else:
                    raise RuntimeError(
                        f"blaze partition side did not place at "
                        f"-140,69,{target_z}")
            block = next((item for item in (112, 24, 12)
                          if item_count(ep.obs, item) > 0), None)
            if block is None:
                raise RuntimeError(
                    "blaze retreat partition exhausted top blocks")
            place_item_on(ep, -140, 69, target_z, block)
    # Complete the south edge and the upper side course even when resuming
    # from an older farm checkpoint. A blaze around z=-169 can otherwise
    # shoot diagonally around the nominal three-column partition and reset a
    # burn after the front shutter is already closed.
    ep.step(probe_x=-140, probe_y=71, probe_z=-169)
    if ep.obs["probe"][0] == 0:
        for target_z in (-171, -170, -169):
            navigate(ep, -142.5, target_z + 0.5, max_ticks=100,
                     sprint=False, tolerance=0.25, arrival_idle=0,
                     combat=False)
            for target_y in (69, 70, 71):
                ep.step(probe_x=-140, probe_y=target_y, probe_z=target_z)
                if ep.obs["probe"][0] != 0:
                    continue
                block = next((item for item in (112, 24, 12)
                              if item_count(ep.obs, item) > 0), None)
                if block is None:
                    raise RuntimeError(
                        "blaze retreat partition exhausted flank blocks")
                try:
                    place_item_on(ep, -140, target_y - 1, target_z, block)
                except RuntimeError:
                    ep.step(probe_x=-140, probe_y=target_y,
                            probe_z=target_z)
                    if ep.obs["probe"][0] == 0:
                        raise
    navigate(ep, -145.5, -170.5, max_ticks=120,
             sprint=False, tolerance=0.18, arrival_idle=0,
             combat=False)
    for scaffold_z in (-170, -171, -172):
        for scaffold_y in (70, 69):
            ep.step(probe_x=-148, probe_y=scaffold_y,
                    probe_z=scaffold_z)
            if ep.obs["probe"][0] not in (0, 112):
                mine_probed_coordinate(ep, -148, scaffold_y, scaffold_z,
                                       available_break_tool(ep.obs),
                                       max_ticks=220)
                settle(ep)
    for roof_x, roof_z in (
            (-148, -172), (-148, -171), (-148, -170),
            (-147, -171), (-147, -170)):
        navigate(ep, -145.5, roof_z + 0.5, max_ticks=100,
                 sprint=False, tolerance=0.25, arrival_idle=0,
                 combat=False)
        ep.step(probe_x=roof_x, probe_y=72, probe_z=roof_z)
        if ep.obs["probe"][0] != 0:
            continue
        item = next((value for value in (5, 24)
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            break
        before = item_count(ep.obs, item)
        slot = ensure_hotbar(ep, item)
        for _ in range(16):
            dyaw, dpitch = look_at(
                ep.obs, roof_x - 0.001, 72.5, roof_z + 0.5)
            ep.step(use=1, do_place=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1)
            ep.step(probe_x=roof_x, probe_y=72, probe_z=roof_z)
            if ep.obs["probe"][0] != 0:
                break
            if item_count(ep.obs, item) < before:
                raise RuntimeError(
                    f"blaze roof misplaced block for "
                    f"{roof_x},72,{roof_z}")
        else:
            raise RuntimeError(
                f"blaze roof did not place at {roof_x},72,{roof_z}")
    # Fight diagonally across the center slit. Sword reach still crosses the
    # opening, while a blaze aiming at the offset eye line strikes the solid
    # neighbouring wall column instead of travelling straight through it.
    navigate(ep, -147.5, -169.5, max_ticks=180,
             sprint=False, tolerance=0.20, arrival_idle=0,
             combat=False)
    ep.step(probe_x=-149, probe_y=70, probe_z=-171)
    if ep.obs["probe"][0] != 0:
        mine_probed_coordinate(ep, -149, 70, -171,
                               available_break_tool(ep.obs), max_ticks=220)
        settle(ep)
    if ep.can_save:
        ep.step(save_slot="blaze-slit")
    target = nearby_mob(ep, (7,), radius=20.0, max_vertical=6.0)
    for wait_tick in range(500):
        if target is not None:
            break
        ep.step(sneak=1)
        target = nearby_mob(ep, (7,), radius=20.0, max_vertical=6.0)
    if target is None:
        raise RuntimeError("blaze bulkhead has no live target")
    eid = target[1]
    hits = 0
    last_hit_tick = 0
    for tick in range(1600):
        current = next((m for m in ep.obs["mobs"]
                        if m[0] == 7 and m[1] == eid and m[5] > 0.0), None)
        if current is None:
            return eid
        fireball = nearby_mob(ep, (27,), radius=9.0, max_vertical=7.0)
        if (fireball is not None and ep.obs["fire_ticks"] <= 0 and
                ep.obs["health"] >= 18.0):
            fyaw, fpitch = look_at(
                ep.obs, fireball[2], fireball[3] + 0.3, fireball[4])
            reflector = next((item for item in (268, 272, 267, 276, 270)
                              if item in ep.obs["inventory_ids"]), None)
            reflector_slot = (ensure_hotbar(ep, reflector)
                              if reflector is not None else
                              next((index for index, item in enumerate(
                                  ep.obs["hotbar_ids"]) if item == 0),
                                   ep.obs["hotbar_sel"]))
            ep.step(attack=1, do_break=1,
                    hotbar=reflector_slot,
                    dyaw=fyaw, dpitch=fpitch, cam=1, sneak=1)
            continue
        if ep.obs["fire_ticks"] > 0 or ep.obs["health"] < 18.0:
            if ep.can_save:
                ep.step(save_slot="blaze-recovery-entry")
            recovery_entry = (ep.obs["health"], ep.obs["food"],
                              ep.obs["fire_ticks"])
            shutter = next((item for item in (112, 24, 12)
                            if item_count(ep.obs, item) > 0), None)
            if shutter is None:
                raise RuntimeError("blaze slit recovery exhausted shutters")
            # Finish the already-launched burst before turning away to close
            # the slit. Otherwise a projectile that crossed the wall plane
            # one tick before the shutter can hit during the retreat and
            # reset the burn timer to its full duration.
            handled_fireballs = set()
            for _ in range(3):
                candidates = [mob for mob in ep.obs["mobs"]
                              if mob[0] == 27 and
                              mob[1] not in handled_fireballs and
                              math.hypot(mob[2] - ep.obs["x"],
                                         mob[4] - ep.obs["z"]) <= 8.0]
                if not candidates:
                    break
                inbound = min(candidates, key=lambda mob: math.hypot(
                    mob[2] - ep.obs["x"], mob[4] - ep.obs["z"]))
                handled_fireballs.add(inbound[1])
                fyaw, fpitch = look_at(
                    ep.obs, inbound[2], inbound[3] + 0.3, inbound[4])
                reflector = next((item for item in (268, 272, 267, 276, 270)
                                  if item in ep.obs["inventory_ids"]), None)
                slot = (ensure_hotbar(ep, reflector) if reflector is not None
                        else ep.obs["hotbar_sel"])
                ep.step(attack=1, do_break=1, hotbar=slot,
                        dyaw=fyaw, dpitch=fpitch, cam=1, sneak=1)
                ep.step(sneak=1)
            # The center cell has a native lower support at y=69. Unlike the
            # former off-by-one y=71 attempt, its top face is below the eye
            # and can be clicked reliably from the fighting position.
            navigate(ep, -147.5, -170.5, max_ticks=60,
                     sprint=True, tolerance=0.3, arrival_idle=0,
                     combat=False)
            place_item_on(ep, -149, 69, -171, shutter)
            if ep.can_save:
                ep.step(save_slot="blaze-recovery-shutter")
            navigate(ep, -142.7, -170.5, max_ticks=140,
                     sprint=True, tolerance=0.3, arrival_idle=0,
                     combat=False)
            if ep.can_save:
                ep.step(save_slot="blaze-recovery-safe")
            eat_available_meat(ep)
            for _ in range(360):
                if (ep.obs["fire_ticks"] <= 0 and
                        (ep.obs["health"] >= 16.0 or
                         (ep.obs["food"] < 18 and
                          ep.obs["health"] >= 12.0))):
                    break
                ep.step(sneak=1)
                if ep.obs["dead"]:
                    raise RuntimeError(
                        f"blaze slit retreat did not prevent lethal burn; "
                        f"entry={recovery_entry} current=("
                        f"{ep.obs['health']},{ep.obs['food']},"
                        f"{ep.obs['fire_ticks']}) source="
                        f"{ep.obs['last_damage_source']} mobs="
                        f"{ep.obs['mobs'][:12]}")
            navigate(ep, -145.5, -170.5, max_ticks=180,
                     sprint=False, tolerance=0.25, arrival_idle=0,
                     combat=False)
            mine_probed_coordinate(
                ep, -149, 70, -171, available_break_tool(ep.obs),
                max_ticks=300)
            settle(ep)
            navigate(ep, -147.5, -169.5, max_ticks=180,
                     sprint=False, tolerance=0.25, arrival_idle=0,
                     combat=False)
            replacement = nearby_mob(ep, (7,), radius=20.0,
                                     max_vertical=6.0)
            if replacement is not None:
                eid = replacement[1]
            last_hit_tick = tick
            continue
        crossed = [m for m in ep.obs["mobs"]
                   if (m[0] == 7 and m[5] > 0.0 and
                       -148.5 < m[2] < -140.0)]
        if crossed:
            crossed_mob = min(
                crossed, key=lambda m:
                (m[2] - ep.obs["x"]) ** 2 +
                (m[4] - ep.obs["z"]) ** 2)
            shutter = next((item for item in (112, 24, 12)
                            if item_count(ep.obs, item) > 0), None)
            if shutter is None:
                raise RuntimeError("crossed-blaze response exhausted shutters")
            navigate(ep, -147.5, -170.5, max_ticks=60,
                     sprint=True, tolerance=0.3, arrival_idle=0,
                     combat=False)
            place_item_on(ep, -149, 69, -171, shutter)
            kill_crossed_blaze_aggressive(ep, crossed_mob[1])
            navigate(ep, -145.5, -170.5, max_ticks=180,
                     sprint=False, tolerance=0.25, arrival_idle=0,
                     combat=False)
            mine_probed_coordinate(
                ep, -149, 70, -171, available_break_tool(ep.obs),
                max_ticks=300)
            settle(ep)
            navigate(ep, -147.5, -169.5, max_ticks=180,
                     sprint=False, tolerance=0.25, arrival_idle=0,
                     combat=False)
            replacement = nearby_mob(ep, (7,), radius=20.0,
                                     max_vertical=6.0)
            if replacement is not None:
                eid = replacement[1]
            last_hit_tick = tick
            continue
        # The player's eye is in wall course y=71, which remains solid. Aim
        # at the blaze's feet through the y=70 slit; its return fire continues
        # to target the eye and therefore strikes the block above the slit.
        dyaw, dpitch = look_at(ep.obs, current[2], current[3] + 0.05,
                              current[4])
        distance = math.sqrt(
            (current[2] - ep.obs["x"]) ** 2 +
            (current[3] + 0.05 - ep.obs["y"] - 1.62) ** 2 +
            (current[4] - ep.obs["z"]) ** 2)
        if distance <= 5.30 and tick % 13 == 0:
            weapon = next((item for item in (268, 272, 267, 276, 270, 280,
                                              334, 319, 363)
                           if item in ep.obs["inventory_ids"]), None)
            if weapon is None:
                raise RuntimeError("blaze slit combat exhausted held items")
            before_health = current[5]
            ep.step(attack=1, do_break=1, hotbar=ensure_hotbar(ep, weapon),
                    dyaw=dyaw, dpitch=dpitch, cam=1, sneak=1)
            ep.step(sneak=1)
            after = next((m for m in ep.obs["mobs"]
                          if m[1] == eid and m[5] > 0.0), None)
            if after is None or after[5] < before_health:
                hits += 1
                last_hit_tick = tick
        else:
            ep.step(sneak=1, dyaw=dyaw, dpitch=dpitch)
        if tick > 0 and tick - last_hit_tick >= 300:
            # Ranged blazes sometimes hold outside survival reach. The wall's
            # solid eye-level course already intercepts their return fire, so
            # back away and let their hover phase change without attempting a
            # brittle top-face shutter placement beneath the low roof.
            shutter = next((item for item in (112, 24, 12)
                            if item_count(ep.obs, item) > 0), None)
            if shutter is None:
                raise RuntimeError("blaze hover wait exhausted shutters")
            navigate(ep, -147.5, -170.5, max_ticks=60,
                     sprint=True, tolerance=0.3, arrival_idle=0,
                     combat=False)
            place_item_on(ep, -149, 69, -171, shutter)
            crossed_wither = next(
                (mob for mob in ep.obs["mobs"]
                 if mob[0] == 32 and mob[2] < -140.5 and mob[5] > 0.0 and
                 math.hypot(mob[2] - ep.obs["x"],
                            mob[4] - ep.obs["z"]) <= 10.0 and
                 abs(mob[3] - ep.obs["y"]) <= 3.0), None)
            if crossed_wither is not None:
                kill_nearby_hostiles(ep, radius=10.0,
                                     hostile_types=(32,))
            navigate(ep, -142.7, -170.5, max_ticks=140,
                     sprint=False, tolerance=0.3, arrival_idle=0,
                     combat=False)
            eat_available_meat(ep)
            ep.idle_ticks(120)
            navigate(ep, -145.5, -170.5, max_ticks=140,
                     sprint=False, tolerance=0.25, arrival_idle=0,
                     combat=False)
            mine_probed_coordinate(
                ep, -149, 70, -171, available_break_tool(ep.obs),
                max_ticks=300)
            settle(ep)
            navigate(ep, -147.5, -169.5, max_ticks=180,
                     sprint=False, tolerance=0.25, arrival_idle=0,
                     combat=False)
            replacement = nearby_mob(ep, (7,), radius=20.0,
                                     max_vertical=6.0)
            if replacement is not None:
                eid = replacement[1]
            hits = 0
            last_hit_tick = tick
        if ep.obs["health"] < 15.0 and ep.obs["food"] < 20 and meat_count(
                ep.obs) > 0:
            eat_available_meat(ep)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"blaze slit combat was lethal; target={current} hits={hits} "
                f"health={ep.obs['health']} fire={ep.obs['fire_ticks']} "
                f"source={ep.obs['last_damage_source']} projectiles="
                f"{ep.obs.get('projectiles', [])[:12]}")
    raise RuntimeError(
        f"blaze {eid} never entered slit reach; target={current} hits={hits} "
        f"pose=({ep.obs['x']},{ep.obs['y']},{ep.obs['z']}) health="
        f"{ep.obs['health']} fire={ep.obs['fire_ticks']}")


def acquire_blaze_rods_protected(ep, target=5):
    """Farm rod entities behind the wall, then collect them in one rush."""
    for cycle in range(30):
        existing_rods = [item for item in ep.obs.get("items", [])
                         if item[0] == 369]
        if sum(item[1] for item in existing_rods) >= target:
            break
        kill_blaze_through_bulkhead(ep)
        if ep.can_save:
            ep.step(save_slot=f"blaze-slit-kill-{cycle}")
        rod_drops = [item for item in ep.obs.get("items", [])
                     if item[0] == 369]
        rod_total = sum(item[1] for item in rod_drops)
        if rod_total >= target:
            # Five deterministic drops are already on the other side. A
            # short doorway pickup traverse costs far less exposure than
            # clearing an unbounded live spawner population and preserves a
            # sword for the return route.
            break
    else:
        raise RuntimeError(
            f"protected blaze farm produced fewer than {target} dropped "
            f"rods; items={ep.obs.get('items', [])[:24]}")
    # The player stands at y=69, so its doorway occupies y=70..71.  Open the
    # head course first while y=70 still shields the body, then remove y=70
    # and rush immediately.  The old bottom-up order spent a full block-break
    # interval in an open firing lane and was lethal with the accumulated
    # wave.  y=69 is the floor and must stay intact.
    for doorway_y in (71, 70):
        ep.step(probe_x=-149, probe_y=doorway_y, probe_z=-171)
        if ep.obs["probe"][0] != 0:
            mine_probed_coordinate(
                ep, -149, doorway_y, -171,
                available_break_tool(ep.obs), max_ticks=240)
            if doorway_y == 71:
                settle(ep)
    # Move only far enough through the doorway for the 1.5-block item pickup
    # radius, then retreat behind the eye-level course.  Chasing each entity
    # into the spawner room needlessly exposes the player to the live wave.
    navigate(ep, -149.15, -170.5, max_ticks=80,
             sprint=True, tolerance=0.18, arrival_idle=3,
             combat=False)
    navigate(ep, -147.5, -170.5, max_ticks=80,
             sprint=True, tolerance=0.25, arrival_idle=0,
             combat=False)
    for _ in range(3):
        rod_drops = [item for item in ep.obs.get("items", [])
                     if item[0] == 369]
        if not rod_drops:
            break
        drop = min(rod_drops, key=lambda item:
                   (item[3] - ep.obs["x"]) ** 2 +
                   (item[5] - ep.obs["z"]) ** 2)
        navigate(ep, drop[3], drop[5], max_ticks=240,
                 sprint=True, tolerance=0.45, arrival_idle=8,
                 combat=False)
    if item_count(ep.obs, 369) < target:
        raise RuntimeError(
            f"blaze doorway collected {item_count(ep.obs, 369)}/{target} "
            f"rods; pose=({ep.obs['x']},{ep.obs['y']},{ep.obs['z']}) "
            f"items={ep.obs.get('items', [])[:24]} mobs="
            f"{ep.obs['mobs'][:20]}")


def build_quarry_screen(ep):
    """Screen the fortress quarry from the east-side blaze sightline."""
    wall_x = -128
    stand_x = -130.5
    for target_z in (-172,):
        navigate(ep, stand_x, target_z + 0.5, max_ticks=100,
                 sprint=False, tolerance=0.25, arrival_idle=0,
                 combat=False)
        for target_y in (69, 70):
            item = next((value for value in (12, 24)
                         if item_count(ep.obs, value) > 0), None)
            if item is None:
                raise RuntimeError("fortress quarry screen exhausted blocks")
            place_item_on(ep, wall_x, target_y - 1, target_z, item)
    navigate(ep, stand_x, -169.5, max_ticks=100,
             sprint=False, tolerance=0.25, arrival_idle=0,
             combat=False)
    item = next((value for value in (12, 24)
                 if item_count(ep.obs, value) > 0), None)
    if item is None:
        raise RuntimeError("fortress quarry screen exhausted center block")
    slot = ensure_hotbar(ep, item)
    for _ in range(16):
        dyaw, dpitch = look_at(ep.obs, wall_x + 0.5, 70.5, -171.001)
        ep.step(use=1, do_place=1, hotbar=slot,
                dyaw=dyaw, dpitch=dpitch, cam=1)
        ep.step(probe_x=wall_x, probe_y=70, probe_z=-171)
        if ep.obs["probe"][0] != 0:
            break
    else:
        raise RuntimeError("fortress quarry screen missed center block")
    navigate(ep, stand_x, -169.5, max_ticks=80,
             sprint=False, tolerance=0.25, arrival_idle=0,
             combat=False)
    for target_y in (69, 70):
        item = next((value for value in (12, 24)
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError(
                f"fortress quarry screen exhausted south block at y="
                f"{target_y}; inventory={ep.obs['inventory_ids']}/"
                f"{ep.obs['inventory_counts']} blocks="
                f"{ep.obs['blocks'][:30]}")
        place_item_on(ep, wall_x, target_y - 1, -170, item)


def build_blaze_bulkhead(ep):
    """Seal the spawner bridge with a four-high ordinary-input wall."""
    wall_x = -149
    stand_x = -147.5
    scaffold_x = -148
    stable_stock = sum(item_count(ep.obs, item)
                       for item in (112, 24, 5))
    if 270 in ep.obs["inventory_ids"] and stable_stock < 20:
        build_quarry_screen(ep)
        pick_slot = ensure_hotbar(ep, 270)
        before_bricks = item_count(ep.obs, 112)
        # The parapet begins at x=-132 after the stair/bridge junction;
        # x=-129..-131 are open transition cells, not quarry material.
        for brick_x in range(-132, -142, -1):
            navigate(ep, brick_x + 0.5, -171.5, max_ticks=100,
                     sprint=False, tolerance=0.22, arrival_idle=0,
                     combat=False)
            # Quarry the solid south parapet, not the bridge floor.  The old
            # coordinate was air at z=-170, so the probe helper legitimately
            # returned without producing a drop.  Parapet drops land on the
            # intact one-wide deck and enter pickup range immediately.
            mine_probed_coordinate(ep, brick_x, 69, -173, 270,
                                   max_ticks=100)
            # EntityItem has a ten-tick pickup delay after a block break.
            # Remain beside the intact deck until that delay has expired so
            # the item cannot be left behind between quarry cells.
            ep.idle_ticks(14)
        if ep.can_save:
            ep.step(save_slot="fortress-quarry-end")
        if item_count(ep.obs, 112) < before_bricks + 10:
            raise RuntimeError(
                f"fortress quarry collected "
                f"{item_count(ep.obs, 112) - before_bricks}/10 bricks; "
                f"pose=({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                f"{ep.obs['z']:.3f}) items={ep.obs.get('items', [])[:20]}")
        del pick_slot
    navigate_serpentine(ep, stand_x, -171.5, max_ticks=220,
                        tolerance=0.6)
    # Seal the east side of the work pocket before exposing the player to the
    # spawner wall build. Without this partition a bridge blaze around x=-129
    # can keep resetting fireTicks even after the x=-149 bulkhead is complete.
    for rear_z in (-172, -171, -170, -169):
        navigate(ep, -142.5, rear_z + 0.5, max_ticks=120,
                 sprint=True, tolerance=0.3, arrival_idle=0, combat=False)
        for rear_y in (69, 70, 71):
            ep.step(probe_x=-140, probe_y=rear_y, probe_z=rear_z)
            if ep.obs["probe"][0] != 0:
                continue
            item = next((value for value in (12, 24, 112, 5)
                         if item_count(ep.obs, value) > 0), None)
            if item is None:
                raise RuntimeError("blaze rear partition exhausted blocks")
            place_item_on(ep, -140, rear_y - 1, rear_z, item)
    navigate(ep, stand_x, -171.5, max_ticks=140,
             sprint=True, tolerance=0.4, arrival_idle=0, combat=False)
    # The stair has reached the y=68 bridge here. Nether-brick side rails at
    # z=-169/-173 already close the corridor, so three columns seal its full
    # walkable width. Build the lower courses from bridge level first.
    for target_z in (-172,):
        navigate(ep, stand_x, target_z + 0.5, max_ticks=100,
                 sprint=False, tolerance=0.25, arrival_idle=0,
                 combat=False)
        for target_y in (69, 70):
            ep.step(probe_x=wall_x, probe_y=target_y, probe_z=target_z)
            if ep.obs["probe"][0] != 0:
                continue
            preferred = ((12, 24, 112, 5, 58) if target_y == 69
                         else (24, 112, 5, 12, 58))
            item = next((value for value in preferred
                         if item_count(ep.obs, value) > 0), None)
            if item is None:
                raise RuntimeError("blaze bulkhead exhausted blocks")
            place_item_on(ep, wall_x, target_y - 1, target_z, item)
            ep.step(probe_x=wall_x, probe_y=target_y, probe_z=target_z)
            if ep.obs["probe"][0] == 0:
                raise RuntimeError(
                    f"blaze bulkhead misplaced block at "
                    f"{wall_x},{target_y},{target_z}")
    # Close the central y=70 cell from the south face of the north column.
    # Its native fence already blocks the lower 1.5 blocks, so no sacrificial
    # y=69 support is needed. Keep the south column absent until this ray has
    # crossed its cell.
    navigate(ep, -147.5, -169.5, max_ticks=120,
             sprint=False, tolerance=0.22, arrival_idle=0,
             combat=False)
    item = next((value for value in (24, 112, 5, 12, 58)
                 if item_count(ep.obs, value) > 0), None)
    if item is None:
        raise RuntimeError("blaze bulkhead exhausted side-face block")
    before = item_count(ep.obs, item)
    slot = ensure_hotbar(ep, item)
    for _ in range(16):
        dyaw, dpitch = look_at(ep.obs, wall_x + 0.5, 70.5, -171.001)
        ep.step(use=1, do_place=1, hotbar=slot,
                dyaw=dyaw, dpitch=dpitch, cam=1)
        ep.step(probe_x=wall_x, probe_y=70, probe_z=-171)
        if ep.obs["probe"][0] != 0:
            break
        if item_count(ep.obs, item) < before:
            raise RuntimeError(
                f"blaze side-face placement hit wrong cell; pose="
                f"({ep.obs['x']},{ep.obs['y']},{ep.obs['z']}) ray="
                f"{ep.obs.get('ray')} blocks={ep.obs['blocks'][:30]}")
    else:
        raise RuntimeError("blaze side-face placement did not close wall")
    target_z = -170
    navigate(ep, stand_x, target_z + 0.5, max_ticks=100,
             sprint=False, tolerance=0.25, arrival_idle=0,
             combat=False)
    for target_y in (69, 70):
        preferred = ((12, 24, 112, 5, 58) if target_y == 69
                     else (24, 112, 5, 12, 58))
        item = next((value for value in preferred
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError("blaze bulkhead exhausted south column")
        place_item_on(ep, wall_x, target_y - 1, target_z, item)
        ep.step(probe_x=wall_x, probe_y=target_y, probe_z=target_z)
        if ep.obs["probe"][0] == 0:
            raise RuntimeError(
                f"blaze bulkhead misplaced south block at "
                f"{wall_x},{target_y},{target_z}")
    # A three-block work platform keeps the camera aligned with each column
    # for the upper courses. It avoids both out-of-reach placement from the
    # bridge and ray interception by an already-completed neighboring column.
    navigate(ep, stand_x, -170.5, max_ticks=80,
             sprint=False, tolerance=0.16, arrival_idle=0,
             combat=False)
    for scaffold_z in (-170, -172):
        item = next((value for value in (12, 24, 112, 5, 58)
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError("blaze bulkhead exhausted scaffold blocks")
        place_item_on(ep, scaffold_x, 67, scaffold_z, item)
    item = next((value for value in (12, 112, 5, 24, 58)
                 if item_count(ep.obs, value) > 0), None)
    if item is None:
        raise RuntimeError("blaze bulkhead exhausted scaffold blocks")
    pillar_one_staggered(ep, item)
    for target_z in (-171, -170, -172):
        navigate(ep, stand_x, target_z + 0.5, max_ticks=80,
                 sprint=False, tolerance=0.20, arrival_idle=0,
                 combat=False)
        item = next((value for value in (112, 5, 24, 12, 58)
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError("blaze bulkhead exhausted upper blocks")
        place_item_on(ep, wall_x, 70, target_z, item)
        ep.step(probe_x=wall_x, probe_y=71, probe_z=target_z)
        if ep.obs["probe"][0] == 0:
            raise RuntimeError(
                f"blaze bulkhead misplaced upper block at {wall_x},71,"
                f"{target_z}; pose=({ep.obs['x']},{ep.obs['y']},"
                f"{ep.obs['z']}) ray={ep.obs.get('ray')} inventory="
                f"{ep.obs['inventory_ids']}/{ep.obs['inventory_counts']} "
                f"blocks={ep.obs['blocks'][:40]}")
    navigate(ep, stand_x, -170.5, max_ticks=80,
             sprint=False, tolerance=0.16, arrival_idle=0,
             combat=False)
    for scaffold_z in (-170, -172):
        item = next((value for value in (12, 24, 112, 5, 58)
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError("blaze bulkhead exhausted high scaffold")
        place_item_on(ep, scaffold_x, 68, scaffold_z, item)
    item = next((value for value in (12, 24, 112, 5, 58)
                 if item_count(ep.obs, value) > 0), None)
    if item is None:
            raise RuntimeError("blaze bulkhead exhausted high scaffold")
    pillar_one_staggered(ep, item)
    for target_z in (-171, -170, -172):
        navigate(ep, stand_x, target_z + 0.5, max_ticks=80,
                 sprint=False, tolerance=0.20, arrival_idle=0,
                 combat=False)
        item = next((value for value in (24, 112, 5, 12, 58)
                     if item_count(ep.obs, value) > 0), None)
        if item is None:
            raise RuntimeError("blaze bulkhead exhausted cap blocks")
        place_item_on(ep, wall_x, 71, target_z, item)
        ep.step(probe_x=wall_x, probe_y=72, probe_z=target_z)
        if ep.obs["probe"][0] == 0:
            raise RuntimeError(
                f"blaze bulkhead misplaced cap at {wall_x},72,{target_z}")
    if ep.can_save:
        ep.step(save_slot="blaze-bulkhead-base")
    navigate_serpentine(ep, -144.5, -169.5, max_ticks=100,
                        tolerance=0.4)
    navigate(ep, -142.5, -169.5, max_ticks=100, sprint=True,
             tolerance=0.25, arrival_idle=0, combat=False)
    settle(ep, max_ticks=20)
    recover_while_reflecting(ep)
    if ep.can_save:
        ep.step(save_slot="blaze-bulkhead")
    if os.environ.get("NETHERITE_DEBUG_BLAZE_BULKHEAD"):
        raise RuntimeError(
            f"blaze bulkhead debug pose=({ep.obs['x']},{ep.obs['y']},"
            f"{ep.obs['z']}) health={ep.obs['health']} food="
            f"{ep.obs['food']} fire={ep.obs['fire_ticks']} inventory="
            f"{ep.obs['inventory_ids']}/{ep.obs['inventory_counts']} "
            f"mobs={ep.obs['mobs'][:16]} blocks={ep.obs['blocks'][:40]}")


def recover_between_blaze_waves(ep):
    """Break line of sight at the fortress bend, heal, then return."""
    for retreat_index, (x, z) in enumerate((
            (-139.5, -171.5), (-120.5, -171.5),
            (-101.5, -171.5), (-101.5, -152.5))):
        navigate_serpentine(ep, x, z, max_ticks=500, tolerance=1.0)
        if ep.can_save:
            ep.step(save_slot=f"blaze-retreat-{retreat_index}")
    eat_available_meat(ep)
    for tick in range(600):
        if (ep.obs["fire_ticks"] <= 0 and ep.obs["health"] >= 19.0):
            break
        ep.step(strafe=1 if (tick // 20) % 2 == 0 else -1,
                sneak=1)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"blaze-wave recovery was lethal; health={ep.obs['health']} "
                f"food={ep.obs['food']} fire={ep.obs['fire_ticks']} "
                f"source={ep.obs['last_damage_source']} mobs="
                f"{ep.obs['mobs'][:12]} projectiles="
                f"{ep.obs.get('projectiles', [])[:12]}")
    else:
        raise RuntimeError(
            f"blaze-wave recovery did not heal; health={ep.obs['health']} "
            f"food={ep.obs['food']} fire={ep.obs['fire_ticks']}")
    for x, z in ((-101.5, -171.5), (-120.5, -171.5),
                 (-139.5, -171.5), (-152.5, -171.5)):
        navigate_serpentine(ep, x, z, max_ticks=500, tolerance=1.0)


def kill_nearby_creepers(ep, radius=18.0):
    """Kill visible creepers with hit-and-retreat ordinary combat."""
    for _ in range(8):
        mob = nearby_mob(ep, (4,), radius=radius)
        if mob is None:
            return
        eid = mob[1]
        for _ in range(8):
            current = next((m for m in ep.obs["mobs"] if m[1] == eid), None)
            if current is None:
                break
            for _ in range(160):
                current = next(
                    (m for m in ep.obs["mobs"] if m[1] == eid), None)
                if current is None:
                    break
                distance = math.hypot(
                    current[2] - ep.obs["x"], current[4] - ep.obs["z"])
                dyaw, dpitch = look_at(
                    ep.obs, current[2], current[3] + 0.9, current[4])
                if distance <= 2.75:
                    tool = next((item for item in (257, 274, 270, 259)
                                 if item in ep.obs["inventory_ids"]), None)
                    if tool is None:
                        raise RuntimeError("no durable creeper-kite weapon")
                    slot = ensure_hotbar(ep, tool)
                    ep.step(attack=1, do_break=1, hotbar=slot,
                            dyaw=dyaw, dpitch=dpitch, cam=1)
                    break
                ep.step(forward=1, sprint=1, jump=int(distance > 5.0),
                        dyaw=dyaw, dpitch=dpitch)
            if current is None:
                break
            # Reset EntityAICreeperSwell outside its close range while the
            # player's attack cooldown refills.
            for _ in range(28):
                current = next(
                    (m for m in ep.obs["mobs"] if m[1] == eid), None)
                if current is None:
                    break
                dyaw, dpitch = look_at(
                    ep.obs, current[2], current[3] + 0.9, current[4])
                away = angle_delta(ep.obs["yaw"] + dyaw + 180.0,
                                   ep.obs["yaw"])
                ep.step(forward=1, sprint=1, jump=1,
                        dyaw=away, dpitch=-ep.obs["pitch"])
            if ep.obs["dead"]:
                raise RuntimeError(
                    f"creeper kite {eid} was lethal at "
                    f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                    f"{ep.obs['z']:.3f}); target={current} "
                    f"mobs={ep.obs['mobs'][:12]}")


def evade_creepers(ep, radius=8.0):
    """Sprint away until nearby creepers leave both swell and blast range."""
    for _ in range(100):
        mob = nearby_mob(ep, (4,), radius=radius)
        if mob is None:
            return
        away_x = ep.obs["x"] * 2.0 - mob[2]
        away_z = ep.obs["z"] * 2.0 - mob[4]
        away, away_pitch = look_at(
            ep.obs, away_x, ep.obs["y"] + 1.62, away_z)
        ep.step(forward=1, sprint=1, jump=1,
                dyaw=away, dpitch=away_pitch)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"creeper evasion {mob[1]} was lethal; target={mob} "
                f"pose=({ep.obs['x']:.3f},{ep.obs['y']:.3f},"
                f"{ep.obs['z']:.3f}) source="
                f"{ep.obs['last_damage_source']}")
    # A low-health recovery must not escalate a failed disengagement into
    # melee.  Yield to the route loop, which will choose another away vector
    # on its next step if the creeper remains in detection range.
    return


def evade_hostiles(ep, radius=10.0):
    """Disengage from route threats when fighting is resource-negative."""
    for _ in range(160):
        mob = nearby_mob(ep, (2, 3, 4, 5, 7, 32), radius=radius)
        if mob is None:
            return
        dyaw, _ = look_at(ep.obs, mob[2], mob[3] + 0.9, mob[4])
        away = angle_delta(ep.obs["yaw"] + dyaw + 180.0, ep.obs["yaw"])
        ep.step(forward=1, sprint=1, jump=1,
                dyaw=away, dpitch=-ep.obs["pitch"])
        if ep.obs["dead"]:
            raise RuntimeError(f"hostile evasion {mob[1]} was lethal")
    raise RuntimeError("hostile evasion did not clear route")


def eat_available_meat(ep):
    for item in ALL_MEAT:
        while item_count(ep.obs, item) > 0 and ep.obs["food"] < 20:
            slot = ensure_hotbar(ep, item)
            before = item_count(ep.obs, item)
            for _ in range(40):
                ep.step(use=1, hotbar=slot)
                if item_count(ep.obs, item) < before:
                    break


def collect_oasis_food_drops(ep):
    """Collect edible dawn drops within the bounded oasis work area."""
    skipped = set()
    for _ in range(16):
        drops = [item for item in ep.obs.get("items", [])
                 if item[0] in ALL_MEAT and item[2] not in skipped
                 and math.hypot(item[3] - 80.5, item[5] + 80.5) <= 32.0]
        if not drops:
            return
        drop = min(drops, key=lambda item:
                   (item[3] - ep.obs["x"]) ** 2
                   + (item[5] - ep.obs["z"]) ** 2)
        try:
            navigate(ep, drop[3], drop[5], max_ticks=400,
                     sprint=False, tolerance=0.8, arrival_idle=0,
                     swim=ep.obs["y"] <= 63.2)
            for _ in range(20):
                ep.step(jump=int(ep.obs["y"] <= 63.2))
        except RuntimeError:
            skipped.add(drop[2])


def wait_local_daylight(ep):
    """Dig, seal, wait, and leave a seven-deep ordinary-input shelter."""
    settle(ep)
    surface_y = ep.obs["y"]
    x, z = math.floor(ep.obs["x"]), math.floor(ep.obs["z"])
    for _ in range(7):
        y = math.floor(ep.obs["y"]) - 1
        pick = available_break_tool(ep.obs)
        if pick is None:
            raise RuntimeError("local daylight shelter exhausted every tool")
        ensure_hotbar(ep, pick)
        mine_probed_coordinate(ep, x, y, z, pick, max_ticks=400)
        settle(ep)
    ceiling_y = math.floor(ep.obs["y"]) + 2
    roof_item = next((item for item in (4, 3, 35, 5, 12)
                      if item_count(ep.obs, item) > 0), None)
    if roof_item is None:
        raise RuntimeError("local daylight shelter has no roof block")
    before = item_count(ep.obs, roof_item)
    slot = ensure_hotbar(ep, roof_item)
    for _ in range(12):
        dyaw, dpitch = look_at(
            ep.obs, x + 1.02, ceiling_y + 0.5, z + 0.5)
        ep.step(use=1, do_place=1, hotbar=slot,
                dyaw=dyaw, dpitch=dpitch, cam=1,
                probe_x=x, probe_y=ceiling_y, probe_z=z)
        if (item_count(ep.obs, roof_item) < before
                or ep.obs["probe"][0] != 0):
            break
        ep.step()
    else:
        raise RuntimeError("local daylight shelter did not seal")
    day_tick = ep.obs.get("world_time", ep.obs["t"]) % 24000
    wait = 24000 - day_tick + 1200
    for _ in range(wait):
        ep.step()
        if ep.obs["dead"]:
            raise RuntimeError("local daylight shelter was breached")
    pick = available_break_tool(ep.obs)
    if pick is None:
        raise RuntimeError("local daylight shelter has no exit pick")
    mine_probed_coordinate(ep, x, ceiling_y, z, pick, max_ticks=800)
    for _ in range(16):
        if ep.obs["y"] >= surface_y - 0.2:
            return
        overhead_y = math.floor(ep.obs["y"]) + 2
        ep.step(probe_x=x, probe_y=overhead_y, probe_z=z)
        if ep.obs["probe"][0] != 0:
            pick = available_break_tool(ep.obs)
            if pick is None:
                raise RuntimeError("local shelter exit exhausted every pick")
            mine_probed_coordinate(ep, x, overhead_y, z, pick,
                                   max_ticks=800)
        pillar_item = next((item for item in (4, 3, 35, 5, 12)
                            if item_count(ep.obs, item) > 0), None)
        if pillar_item is None:
            raise RuntimeError("local shelter exit exhausted pillar blocks")
        pillar_one_staggered(ep, pillar_item)
    raise RuntimeError("local daylight shelter exit exceeded 16 levels")


def recover_portal_food(ep):
    """Walk to the known grass band, respawn food, and return to the mold."""
    if ep.obs["food"] < 14 and meat_count(ep.obs) > 0:
        eat_available_meat(ep)
    if ep.obs["food"] >= 14:
        for _ in range(400):
            if ep.obs["health"] >= 19.0 or ep.obs["food"] < 18:
                break
            ep.step()
        return
    # The five-column portal mold blocks the direct north line near z=25.
    # Reuse the already-proven western leg of the east-pool route.
    route = ((40.5, 20.5), (40.5, 5.5), (40.5, -5.5),
             (40.5, -25.5), (40.5, -45.5), (40.5, -63.5),
             (55.5, -63.5), (65.5, -67.5), (65.5, -80.5),
             (80.5, -80.5))
    for x, z in route:
        navigate(ep, x, z, max_ticks=500, sprint=False,
                 tolerance=0.65, arrival_idle=0)
    settle(ep)
    if ep.obs.get("world_time", ep.obs["t"]) % 24000 >= 2000:
        # Wait outside the hostile random-despawn radius, then re-enter the
        # oasis in daylight. Sheltering on either oasis bank preserved every
        # creeper beside the passives for the entire night.
        for x, z in ((65.5, -67.5), (55.5, -63.5),
                     (40.5, -63.5), (40.5, -45.5)):
            navigate(ep, x, z, max_ticks=400, sprint=False,
                     tolerance=0.9, arrival_idle=0)
        settle(ep)
        wait_local_daylight(ep)
        for x, z in ((40.5, -63.5), (55.5, -63.5),
                     (65.5, -67.5), (65.5, -80.5), (80.5, -80.5)):
            navigate(ep, x, z, max_ticks=400, sprint=True,
                     tolerance=0.9, arrival_idle=0, combat="creeper")
        settle(ep)
    collect_oasis_food_drops(ep)
    eat_available_meat(ep)
    for _ in range(1200):
        if nearby_oasis_passive(ep) is not None:
            break
        ep.step()
    else:
        raise RuntimeError(
            f"no passive spawned on grass; living={ep.obs.get('living_count')} "
            f"mobs={ep.obs['mobs'][:12]}")
    daylight_waits = 0
    while ep.obs["food"] < 20:
        hunt_passive(ep, wanted=2)
        eat_available_meat(ep)
        if nearby_oasis_passive(ep) is None:
            navigate(ep, 80.5, -79.5, max_ticks=500, sprint=False,
                     tolerance=0.4, arrival_idle=0, swim=True)
            settle(ep)
            if ep.obs.get("world_time", ep.obs["t"]) % 24000 >= 10000:
                wait_local_daylight(ep)
                daylight_waits += 1
                continue
            for _ in range(800):
                ep.step()
                if nearby_oasis_passive(ep) is not None:
                    break
            else:
                if daylight_waits >= 2:
                    raise RuntimeError(
                        f"food recovery exhausted passives at food={ep.obs['food']}")
                wait_local_daylight(ep)
                daylight_waits += 1
                continue
    for _ in range(2400):
        ep.step()
        if ep.obs["health"] >= 19.0:
            break
        if ep.obs["food"] < 18 and meat_count(ep.obs) > 0:
            eat_available_meat(ep)
    if ep.obs["health"] < 18.0:
        raise RuntimeError(
            f"food recovery did not heal player; health={ep.obs['health']} "
            f"food={ep.obs['food']}")
    if ep.obs.get("world_time", ep.obs["t"]) % 24000 >= 10000:
        wait_local_daylight(ep)
    # Creepers survive the dawn wait. Clear any one close enough to begin
    # swelling while the bucket route crosses the adjacent shallows.
    kill_nearby_creepers(ep, radius=24.0)
    if 325 in ep.obs["inventory_ids"]:
        # The surface observer can omit the oasis when the shelter exits on
        # the dune above it. This source is metadata-zero in the pinned CRWS
        # world and is reachable from the grass cell immediately to its west.
        try:
            navigate(ep, 80.5, -80.5, max_ticks=300, sprint=False,
                     tolerance=0.35, arrival_idle=0, swim=True)
            pickup_fluid(ep, 81, 62, -80, 326)
        except RuntimeError:
            if 326 not in ep.obs["inventory_ids"]:
                pass
        water = sorted(ep.obs.get("water_sources", []), key=lambda p:
                       (p[0] + 0.5 - ep.obs["x"]) ** 2
                       + (p[2] + 0.5 - ep.obs["z"]) ** 2)
        for source in (() if 326 in ep.obs["inventory_ids"] else water):
            try:
                navigate(ep, source[0] + 0.5, source[2] + 1.5,
                         max_ticks=240, sprint=False, tolerance=0.45)
                pickup_fluid(ep, *source, 326)
                break
            except RuntimeError:
                if 326 in ep.obs["inventory_ids"]:
                    break
    return_route = (
        (80.5, -89.5), (80.5, -99.5), (80.5, -104.5),
        (80.5, -76.5), (78.5, -76.5), (78.5, -75.5),
        (76.5, -75.5), (76.5, -72.5), (74.5, -72.5),
        (74.5, -70.5), (73.5, -70.5), (73.5, -69.5),
        (71.5, -69.5), (71.5, -68.5), (70.5, -68.5),
        (70.5, -67.5), (69.5, -67.5), (69.5, -66.5),
        (62.5, -66.5), (62.5, -65.5), (47.5, -65.5),
        (47.5, -53.5), (46.5, -53.5), (46.5, -51.5),
        (44.5, -51.5), (44.5, -49.5), (43.5, -49.5),
        (43.5, -48.5), (42.5, -48.5), (42.5, -46.5),
        (41.5, -46.5), (41.5, -45.5), (40.5, -45.5),
        (40.5, -44.5),
        (40.5, -25.5), (40.5, -5.5), (40.5, 5.5), (40.5, 20.5),
    )
    for route_index, (x, z) in enumerate(return_route):
        navigate(ep, x, z, max_ticks=500, sprint=False,
                 tolerance=0.65, arrival_idle=0,
                 swim=route_index == 0)
    navigate(ep, 52.5, 31.5, max_ticks=300, sprint=True,
             tolerance=0.7, arrival_idle=0)
    settle(ep)


def write_jsonl(path, rows):
    with path.open("w", encoding="utf-8") as stream:
        for row in rows:
            stream.write(json.dumps(row, separators=(",", ":")) + "\n")


def open_fortress_doorway(ep, wall_x, wall_z=-5):
    """Open a standing-height doorway in one of this tape's route seals."""
    if wall_z == -172:
        stand_x = wall_x - 3.4
        stand_z = wall_z - 1.1
    else:
        stand_x = wall_x - 3.0 if ep.obs["x"] < wall_x else wall_x + 3.0
        stand_z = wall_z + 0.5
    navigate(ep, stand_x, stand_z, max_ticks=160,
             sprint=False, tolerance=0.25, arrival_idle=0,
             combat=False)
    target_ys = (70, 69) if wall_x == -140 else (69, 70)
    for target_y in target_ys:
        ep.step(probe_x=wall_x, probe_y=target_y, probe_z=wall_z)
        if ep.obs["probe"][0] != 0:
            mine_probed_coordinate(
                ep, wall_x, target_y, wall_z,
                available_break_tool(ep.obs), max_ticks=500)
            if not (wall_x == -140 and target_y == 69):
                settle(ep)


def clear_wither_through_return_partition(ep, wall_x=-140, slit_z=-170):
    """Clear the east-side return wither through a capped one-block slit."""
    initial = [mob for mob in ep.obs["mobs"]
               if mob[0] == 32 and
               wall_x < mob[2] < wall_x + 14.0 and mob[5] > 0.0]
    initial.sort(key=lambda mob: mob[2])
    tracked_eid = initial[0][1] if initial else None
    if initial:
        slit_z = max(-172, min(-169,
                              math.floor(initial[0][4] + 0.5)))
    # The native west parapet stops the player at x=-142.3 on this bridge.
    stand_x = wall_x - 2.3
    navigate(ep, stand_x, slit_z + 0.5, max_ticks=140,
             sprint=False, tolerance=0.18, arrival_idle=0, combat=False)
    hand_item = available_pick(ep.obs) or next(
        (item for item in (280, 319, 363, 334, 12, 24)
         if item in ep.obs["inventory_ids"]), None)
    # Convert both slow nether-brick doorway cells while the other course
    # remains closed, then use a body-height y=70 window. A y=71 cap plus the
    # closed y=69 course prevents the 2.4-block wither from crossing.
    for target_y in (69, 70):
        ep.step(probe_x=wall_x, probe_y=target_y, probe_z=slit_z)
        if ep.obs["probe"][0] == 112:
            mine_probed_coordinate(ep, wall_x, target_y, slit_z, hand_item,
                                   max_ticks=300)
            fast_block = next((item for item in (3, 24)
                               if item_count(ep.obs, item) > 0), None)
            if fast_block is None:
                raise RuntimeError(
                    "return doorway has no stable fast replacement block")
            place_item_on(ep, wall_x, target_y - 1, slit_z, fast_block)
    ep.step(probe_x=wall_x, probe_y=71, probe_z=slit_z)
    if ep.obs["probe"][0] == 0:
        cap = next((item for item in (112, 3, 24)
                    if item_count(ep.obs, item) > 0), None)
        if cap is None:
            raise RuntimeError("return doorway has no high-slit cap")
        place_item_on(ep, wall_x, 70, slit_z, cap)
    mine_probed_coordinate(ep, wall_x, 70, slit_z, hand_item,
                           max_ticks=180)
    mine_probed_coordinate(ep, wall_x - 2, 70, slit_z, hand_item,
                           max_ticks=300)
    navigate(ep, wall_x - 1.9, slit_z + 0.5, max_ticks=100,
             sprint=False, tolerance=0.12, arrival_idle=0, combat=False)
    weapon = next((item for item in (268, 272, 267, 276, 270, 280, 334,
                                     319, 363)
                   if item in ep.obs["inventory_ids"]), None)
    if weapon is None:
        raise RuntimeError("return partition wither combat has no held item")
    slot = ensure_hotbar(ep, weapon)
    min_distance = float("inf")
    missing_ticks = 0

    def close_low_slit():
        ep.step(probe_x=wall_x, probe_y=70, probe_z=slit_z)
        if ep.obs["probe"][0] not in (0, 51):
            return
        block = next((item for item in (3, 24, 112, 12)
                      if item_count(ep.obs, item) > 0), None)
        if block is None:
            raise RuntimeError("return wither slit exhausted closure blocks")
        place_item_on(ep, wall_x, 69, slit_z, block)

    for tick in range(2200):
        if ep.obs["fire_ticks"] > 0 or ep.obs["health"] < 15.0:
            close_low_slit()
            recover_while_reflecting(ep, min_health=18.0)
            mine_probed_coordinate(ep, wall_x, 70, slit_z,
                                   available_break_tool(ep.obs),
                                   max_ticks=180)
            navigate(ep, wall_x - 1.9, slit_z + 0.5, max_ticks=100,
                     sprint=False, tolerance=0.12, arrival_idle=0,
                     combat=False)
            continue
        candidates = [mob for mob in ep.obs["mobs"]
                      if mob[0] == 32 and mob[2] > wall_x + 0.1 and
                      abs(mob[3] - ep.obs["y"]) <= 3.0 and mob[5] > 0.0 and
                      math.hypot(mob[2] - wall_x,
                                 mob[4] - (slit_z + 0.5)) <= 14.0]
        if not candidates:
            tracked = next((mob for mob in ep.obs["mobs"]
                            if mob[1] == tracked_eid and mob[5] > 0.0), None)
            if tracked is not None:
                dyaw, dpitch = look_at(
                    ep.obs, tracked[2], tracked[3] + 0.5, tracked[4])
                ep.step(sneak=1, dyaw=dyaw, dpitch=dpitch)
                missing_ticks = 0
                continue
            missing_ticks += 1
            if missing_ticks >= 500:
                close_low_slit()
                return slit_z, False
            ep.step(sneak=1)
            continue
        missing_ticks = 0
        target = min(candidates, key=lambda mob:
                     (mob[2] - ep.obs["x"]) ** 2 +
                     (mob[4] - ep.obs["z"]) ** 2)
        if target[2] >= wall_x + 7.0:
            close_low_slit()
            return slit_z, False
        # Stay beneath the y=70 cap. The south deck drops the player to y=68
        # and needs a raised lower-body ray; the level deck uses a foot ray.
        aim_y = target[3] + 0.8
        distance = math.sqrt(
            (target[2] - ep.obs["x"]) ** 2 +
            (aim_y - ep.obs["y"] - 1.62) ** 2 +
            (target[4] - ep.obs["z"]) ** 2)
        min_distance = min(min_distance, distance)
        dyaw, dpitch = look_at(ep.obs, target[2], aim_y, target[4])
        fireballs = [mob for mob in ep.obs["mobs"] if mob[0] == 27 and
                     math.sqrt((mob[2] - ep.obs["x"]) ** 2 +
                               (mob[3] + 0.3 - ep.obs["y"] - 1.62) ** 2 +
                               (mob[4] - ep.obs["z"]) ** 2) <= 3.0]
        if fireballs:
            fireball = min(fireballs, key=lambda mob:
                           (mob[2] - ep.obs["x"]) ** 2 +
                           (mob[4] - ep.obs["z"]) ** 2)
            fyaw, fpitch = look_at(
                ep.obs, fireball[2], fireball[3] + 0.3, fireball[4])
            ep.step(attack=1, do_break=1, hotbar=slot,
                    dyaw=fyaw, dpitch=fpitch, cam=1, sneak=1)
            continue
        aligned = abs(target[4] - (slit_z + 0.5)) <= 1.35
        if aligned and distance <= 3.9 and tick % 13 == 0:
            ep.step(attack=1, do_break=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1, sneak=1)
        else:
            ep.step(sneak=1, dyaw=dyaw, dpitch=dpitch)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"return partition wither combat was lethal; target={target} "
                f"distance={distance:.3f} food={ep.obs['food']} fire="
                f"{ep.obs['fire_ticks']} source="
                f"{ep.obs['last_damage_source']}")
    current = next((mob for mob in ep.obs["mobs"]
                    if mob[1] == tracked_eid and mob[5] > 0.0), None)
    if current is not None and current[5] < 15.0:
        # Knockback can leave the damaged skeleton outside reach while the cap
        # also prevents it pathing back. Open the cap only after halving it,
        # then backpedal through the recovery cell for the short finish.
        mine_probed_coordinate(ep, wall_x, 70, slit_z,
                               available_pick(ep.obs), max_ticks=300)
        eid = current[1]
        for tick in range(500):
            current = next((mob for mob in ep.obs["mobs"]
                            if mob[1] == eid and mob[5] > 0.0), None)
            if current is None:
                return slit_z, True
            fireballs = [mob for mob in ep.obs["mobs"] if mob[0] == 27 and
                         math.sqrt((mob[2] - ep.obs["x"]) ** 2 +
                                   (mob[3] + 0.3 - ep.obs["y"] - 1.62) ** 2 +
                                   (mob[4] - ep.obs["z"]) ** 2) <= 3.0]
            if fireballs:
                fireball = min(fireballs, key=lambda mob:
                               (mob[2] - ep.obs["x"]) ** 2 +
                               (mob[4] - ep.obs["z"]) ** 2)
                dyaw, dpitch = look_at(
                    ep.obs, fireball[2], fireball[3] + 0.3, fireball[4])
                ep.step(attack=1, do_break=1, hotbar=slot,
                        dyaw=dyaw, dpitch=dpitch, cam=1)
                continue
            aim_y = current[3] + 0.8
            distance = math.sqrt(
                (current[2] - ep.obs["x"]) ** 2 +
                (aim_y - ep.obs["y"] - 1.62) ** 2 +
                (current[4] - ep.obs["z"]) ** 2)
            dyaw, dpitch = look_at(
                ep.obs, current[2], aim_y, current[4])
            if (distance <= 3.7 and
                    (current[5] <= 2.1 or tick % 13 == 0)):
                ep.step(attack=1, do_break=1, hotbar=slot,
                        forward=-1, dyaw=dyaw, dpitch=dpitch, cam=1)
            else:
                ep.step(forward=-1 if distance < 2.7 else 0,
                        dyaw=dyaw, dpitch=dpitch)
            if ep.obs["dead"]:
                raise RuntimeError(
                    f"weakened return wither finish was lethal; target="
                    f"{current} source={ep.obs['last_damage_source']}")
    raise RuntimeError(
        f"return partition wither never entered slit reach; pose="
        f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f}) "
        f"min_distance={min_distance:.3f} ray={ep.obs.get('ray')} "
        f"mobs={ep.obs['mobs'][:16]}")


def build_post_farm_fire_screen(ep):
    """Complete the farm bulkhead before the slow partition mining."""
    wall_x = -149
    # The saved-state census shows one relevant eye-level hole. Fill only that
    # measured cell; the remaining bulkhead columns are already solid.
    navigate(ep, -145.5, -169.5, max_ticks=100,
             sprint=True, tolerance=0.35, arrival_idle=0, combat=False)
    ep.step(probe_x=wall_x, probe_y=70, probe_z=-170)
    if ep.obs["probe"][0] in (0, 51):
        block = next((item for item in (112, 24, 12, 3)
                      if item_count(ep.obs, item) > 0), None)
        if block is None:
            raise RuntimeError("post-farm fire screen exhausted blocks")
        place_item_on(ep, wall_x, 69, -170, block)


def cage_crossed_blaze_west(ep):
    """Close a two-high wall after knocking the recovery-cell blaze west."""
    wall_x = -146
    for wall_z in (-172, -171, -170, -169, -173):
        navigate(ep, -144.2, wall_z + 0.5, max_ticks=100,
                 sprint=True, tolerance=0.3, arrival_idle=0, combat=False)
        ep.step(probe_x=wall_x, probe_y=68, probe_z=wall_z)
        if ep.obs["probe"][0] == 0:
            continue
        for target_y in (69, 70):
            ep.step(probe_x=wall_x, probe_y=target_y, probe_z=wall_z)
            if ep.obs["probe"][0] not in (0, 51):
                continue
            block = next((item for item in (112, 24, 12, 3)
                          if item_count(ep.obs, item) > 0), None)
            if block is None:
                raise RuntimeError("crossed-blaze cage exhausted blocks")
            place_item_on(ep, wall_x, target_y - 1, wall_z, block)


def recover_while_reflecting(ep, min_health=19.0, max_ticks=700):
    """Regenerate in a hostile cell without accepting trailing fireballs."""
    # A blaze impact can leave block 51 under the player. Remaining in that
    # cell resets fireTicks to 100 every tick, so first step onto a measured
    # clear deck cell and let the finite burn expire there.
    for target_x, target_z in (
            (-142.5, -169.5), (-142.5, -170.5),
            (-144.5, -170.5), (-144.5, -171.5),
            (-145.5, -171.5), (-143.5, -170.5)):
        bx, bz = math.floor(target_x), math.floor(target_z)
        ep.step(probe_x=bx, probe_y=68, probe_z=bz)
        ground = ep.obs["probe"][0]
        ep.step(probe_x=bx, probe_y=69, probe_z=bz)
        feet = ep.obs["probe"][0]
        ep.step(probe_x=bx, probe_y=70, probe_z=bz)
        head = ep.obs["probe"][0]
        if ground != 0 and feet == 0 and head == 0:
            navigate(ep, target_x, target_z, max_ticks=100,
                     sprint=True, tolerance=0.25, arrival_idle=0,
                     combat=False)
            break
    eat_available_meat(ep)
    for tick in range(max_ticks):
        if (ep.obs["health"] >= min_health and ep.obs["fire_ticks"] <= 0 and
                tick >= 120):
            return
        fireballs = [mob for mob in ep.obs["mobs"] if mob[0] == 27 and
                     math.sqrt((mob[2] - ep.obs["x"]) ** 2 +
                               (mob[3] + 0.3 - ep.obs["y"] - 1.62) ** 2 +
                               (mob[4] - ep.obs["z"]) ** 2) <= 3.0]
        if fireballs:
            fireball = min(fireballs, key=lambda mob:
                           (mob[2] - ep.obs["x"]) ** 2 +
                           (mob[4] - ep.obs["z"]) ** 2)
            dyaw, dpitch = look_at(
                ep.obs, fireball[2], fireball[3] + 0.3, fireball[4])
            held = available_break_tool(ep.obs)
            slot = (ensure_hotbar(ep, held) if held is not None else
                    ep.obs["hotbar_sel"])
            ep.step(attack=1, do_break=1, hotbar=slot,
                    dyaw=dyaw, dpitch=dpitch, cam=1, sneak=1)
        else:
            ep.step(sneak=1)
        if ep.obs["food"] < 18 and meat_count(ep.obs) > 0:
            eat_available_meat(ep)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"reflecting recovery was lethal; health={ep.obs['health']} "
                f"food={ep.obs['food']} fire={ep.obs['fire_ticks']} "
                f"source={ep.obs['last_damage_source']} mobs="
                f"{ep.obs['mobs'][:20]}")
    raise RuntimeError(
        f"reflecting recovery stalled; health={ep.obs['health']} food="
        f"{ep.obs['food']} fire={ep.obs['fire_ticks']} mobs="
        f"{ep.obs['mobs'][:20]}")


def return_from_blaze_farm(ep):
    """Recover the blaze pickup rush and walk the built route back to portal."""
    # Cross the still-open farm doorway before the existing burn finishes,
    # then close its lower slit. The rear partition makes this a sealed cell.
    navigate(ep, -147.5, -169.5, max_ticks=100, sprint=True,
             tolerance=0.35, arrival_idle=0, combat=False)
    navigate(ep, -147.5, -170.5, max_ticks=60, sprint=True,
             tolerance=0.3, arrival_idle=0, combat=False)
    shutter = next((item for item in (112, 24, 12)
                    if item_count(ep.obs, item) > 0), None)
    if shutter is None:
        raise RuntimeError("post-rod recovery has no shutter block")
    ep.step(probe_x=-149, probe_y=69, probe_z=-171)
    if ep.obs["probe"][0] == 0:
        place_item_on(ep, -149, 68, -171, shutter)
    shutter = next((item for item in (112, 24, 12)
                    if item_count(ep.obs, item) > 0), None)
    if shutter is None:
        raise RuntimeError("post-rod recovery exhausted upper shutter")
    place_item_on(ep, -149, 69, -171, shutter)
    navigate(ep, -145.5, -170.5, max_ticks=120, sprint=True,
             tolerance=0.3, arrival_idle=0, combat=False)
    eat_available_meat(ep)
    for _ in range(600):
        if ep.obs["fire_ticks"] <= 0 and ep.obs["health"] >= 19.5:
            break
        if ep.obs["food"] < 18 and any(
                item_count(ep.obs, item) > 0 for item in (319, 320, 363, 364)):
            eat_available_meat(ep)
        ep.step(sneak=1)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"post-rod sealed recovery was lethal; health="
                f"{ep.obs['health']} food={ep.obs['food']} fire="
                f"{ep.obs['fire_ticks']} source="
                f"{ep.obs['last_damage_source']} mobs="
                f"{ep.obs['mobs'][:16]} blocks={ep.obs['blocks'][:24]}")
    else:
        raise RuntimeError(
            f"post-rod recovery stalled; health={ep.obs['health']} "
            f"food={ep.obs['food']} fire={ep.obs['fire_ticks']}")
    # A wither skeleton can remain just east of the partition after the farm
    # fight. Keep the eye-level cap intact, expose one foot-level slit, and
    # clear it before opening the standing-height return doorway.
    crossed_blaze_caged = False
    for _ in range(4):
        crossed = next(
            (mob for mob in ep.obs["mobs"]
             if mob[0] == 7 and -149.0 < mob[2] < -140.0 and
             abs(mob[3] - ep.obs["y"]) <= 4.5 and mob[5] > 0.0), None)
        if crossed is None:
            break
        outcome = kill_crossed_blaze_aggressive(
            ep, crossed[1], displace_west=True)
        if outcome == "displaced":
            cage_crossed_blaze_west(ep)
            crossed_blaze_caged = True
            break
        eat_available_meat(ep)
        recover_while_reflecting(ep, min_health=18.0)
    if not crossed_blaze_caged:
        build_post_farm_fire_screen(ep)
    recover_while_reflecting(ep)
    doorway_z = -171
    wither_killed = False
    for _ in range(6):
        blockers = [mob for mob in ep.obs["mobs"]
                    if mob[0] == 32 and
                    -140.0 < mob[2] < -133.0 and mob[5] > 0.0 and
                    abs(mob[3] - ep.obs["y"]) <= 4.5]
        if not blockers:
            break
        doorway_z, killed = clear_wither_through_return_partition(ep)
        wither_killed = wither_killed or killed
        recover_while_reflecting(ep, min_health=18.0)
    else:
        raise RuntimeError(
            f"return partition still has a near-side hostile pack: "
            f"{ep.obs['mobs'][:24]}")
    if wither_killed:
        recover_while_reflecting(ep, min_health=18.0)
    open_fortress_doorway(ep, -140, wall_z=doorway_z)
    escape_weapon = next((item for item in (268, 272, 267, 276, 270, 280)
                          if item in ep.obs["inventory_ids"]), None)
    escape_slot = (ensure_hotbar(ep, escape_weapon)
                   if escape_weapon is not None else ep.obs["hotbar_sel"])
    for _ in range(30):
        wither = nearby_mob(ep, (32,), radius=5.0, max_vertical=3.0)
        if wither is None or ep.obs["x"] >= -139.0:
            break
        dyaw, dpitch = look_at(
            ep.obs, wither[2], wither[3] + 0.8, wither[4])
        ep.step(attack=1, do_break=1, hotbar=escape_slot,
                forward=1 if wither[2] >= ep.obs["x"] else -1,
                sprint=1, jump=1, dyaw=dyaw, dpitch=dpitch, cam=1)
        if ep.obs["dead"]:
            raise RuntimeError(
                f"return doorway escape was lethal; wither={wither} "
                f"source={ep.obs['last_damage_source']}")
    navigate(ep, -138.5, doorway_z + 0.5, max_ticks=160, sprint=True,
             tolerance=0.25, arrival_idle=0, combat=False)
    navigate(ep, -138.5, -171.5, max_ticks=100, sprint=True,
             tolerance=0.7, arrival_idle=0, combat=False)
    for target_x in (-120.5, -101.5):
        # This connector is a one-block-wide bridge. Serpentine projectile
        # evasion steps off its edge; keep the centerline and use the regular
        # fireball reflector instead.
        navigate(ep, target_x, -171.5, max_ticks=700, sprint=True,
                 tolerance=0.65, arrival_idle=1, combat=False, swim=True)
        if target_x == -120.5:
            # The naturally spawned bridge blaze remains behind us. Seal the
            # one-wide connector immediately after passing it so its first
            # trailing fireball cannot finish the low-health return sprint.
            for target_y in (69, 70):
                block = next((item for item in (112, 24, 12)
                              if item_count(ep.obs, item) > 0), None)
                if block is None:
                    raise RuntimeError(
                        "post-blaze connector seal exhausted blocks")
                place_item_on(ep, -123, target_y - 1, -172, block)
            # Let any projectile that crossed the wall plane before the last
            # placement continue east past us; the player is slower than a
            # small fireball and must not chase that in-flight shot.
            ep.idle_ticks(100)
    for target_z in (-152.5, -133.5, -114.5, -101.5, -88.5,
                     -69.5, -50.5, -37.5, -24.5, -11.5, -4.5):
        navigate(ep, -101.5, target_z, max_ticks=900, sprint=True,
                 tolerance=1.0, arrival_idle=1, combat=True,
                 swim=target_z <= -11.5)
    for wall_x, west_x, east_x in (
            (-63, -65.5, -61.5), (-44, -46.5, -42.5),
            (-25, -27.5, -23.5)):
        navigate_serpentine(ep, west_x, -4.5,
                            max_ticks=400, tolerance=0.8)
        open_fortress_doorway(ep, wall_x)
        navigate_serpentine(ep, east_x, -4.5,
                            max_ticks=180, tolerance=0.8)
    for target_x in (-12.5, -5.5, -1.5, 7.5):
        navigate_serpentine(ep, target_x, -4.5,
                            max_ticks=500, tolerance=0.9)
    # The original entrance seal at x=10 remains complete.
    open_fortress_doorway(ep, 10)
    navigate(ep, 13.5, -3.5, max_ticks=300, sprint=True,
             tolerance=0.5, arrival_idle=0, combat=False)
    descend_mixed_pillar(ep, 59.0)
    navigate(ep, 6.5, 0.5, max_ticks=300, sprint=True,
             tolerance=0.2, arrival_idle=0, combat=False)
    for _ in range(140):
        ep.step()
        if ep.obs["dimension"] == 0:
            return
        if ep.obs["dead"]:
            raise RuntimeError("post-rod portal return was lethal")
    raise RuntimeError(
        f"post-rod portal did not return Overworld; pose="
        f"({ep.obs['x']:.3f},{ep.obs['y']:.3f},{ep.obs['z']:.3f})")


def continue_food_stocked_to_fortress_edge(ep):
    """Resume the resupply return without replaying the completed hunt."""
    navigate_carving(ep, 40.5, -120.5, max_ticks=900, tolerance=1.0)
    navigate_carving(ep, 80.5, -120.5, max_ticks=1600, tolerance=1.0)
    navigate(ep, 51.5, 31.5, max_ticks=2200, sprint=True,
             tolerance=0.5, combat=True)
    if ep.can_save:
        ep.step(save_slot="portal-shelf-start")
    gather_portal_shelf_sand(ep, 96)
    if item_count(ep.obs, 268) < 1:
        raise RuntimeError("wooden sword was lost before portal return")
    craft_sandstone(ep, 10)
    for x, z, tolerance in ((55.5, 34.5, 0.3),
                            (51.5, 34.5, 0.3),
                            (51.5, 30.4, 0.2)):
        navigate(ep, x, z, max_ticks=500, sprint=False,
                 tolerance=tolerance, arrival_idle=0, combat=False)
    ep.idle_ticks(12)
    if ep.obs["y"] < 65.8:
        pillar_up(ep, 66.0, item_id=12)
    for _ in range(40):
        dyaw, dpitch = look_at(
            ep.obs, 51.5, ep.obs["y"] + 1.62, 29.5)
        ep.step(forward=1, jump=1, dyaw=dyaw, dpitch=dpitch)
    for _ in range(240):
        ep.step()
        if ep.obs["dimension"] == -1:
            break
    else:
        raise RuntimeError("food-stocked return did not enter Nether")
    if item_count(ep.obs, 12) < 11:
        raise RuntimeError("food-stocked return lost pillar stock")
    if ep.can_save:
        ep.step(save_slot="nether-refreshed")
    navigate(ep, 13.5, -3.5, max_ticks=500, sprint=True,
             tolerance=0.8, arrival_idle=8, combat=True)
    if ep.can_save:
        ep.step(save_slot="fortress-pillar-base")
    solidify_adjacent_shallow_lava(ep, item_id=12)
    pillar_up_clearing_mixed(ep, 68.0, items=(12, 13))
    bridge_positive_axis_mixed(ep, "x", 18.5)
    navigate(ep, 18.5, -3.5, max_ticks=500, sprint=True,
             tolerance=0.8, arrival_idle=8, combat=True)
    if ep.can_save:
        ep.step(save_slot="fortress-edge")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", type=pathlib.Path,
                        default=pathlib.Path("magma/magma_game"))
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--spawn-offset-x", type=int, default=-7)
    parser.add_argument("--spawn-offset-z", type=int, default=-10)
    parser.add_argument("--view-distance", type=int, default=2)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    stages = ("opening", "first_log", "opening_craft", "wooden_pick",
              "cobblestone", "stone_tools", "iron_ore", "smelted_iron",
              "iron_gear", "cave_exit", "water_bucket", "lava_pool",
              "portal_backing", "water_curtain", "lava_access",
              "lava_descent", "portal_one", "portal_daylight",
              "portal_sides")
    parser.add_argument("--stop-after", choices=stages,
                        default="iron_gear")
    parser.add_argument("--start-at", choices=stages, default="opening")
    parser.add_argument("--resume-tape", type=pathlib.Path)
    parser.add_argument("--load-slot")
    parser.add_argument("--save-root", type=pathlib.Path)
    parser.add_argument("--save-slot")
    parser.add_argument("--portal-side-start", type=int, default=0)
    parser.add_argument("--skip-portal-food", action="store_true")
    parser.add_argument("--resume-direct-shaft", action="store_true")
    parser.add_argument("--resume-side-nine", action="store_true")
    parser.add_argument("--resume-side-nine-pool", action="store_true")
    parser.add_argument("--resume-side-nine-return", action="store_true")
    parser.add_argument("--prime-side-nine-water", action="store_true")
    parser.add_argument("--stop-side-nine-deep", action="store_true")
    parser.add_argument("--stop-side-nine-lava", action="store_true")
    parser.add_argument("--stop-side-nine-stock", action="store_true")
    parser.add_argument("--wait-ticks", type=int, default=0)
    parser.add_argument("--resume-portal-top", action="store_true")
    parser.add_argument("--portal-top-index", type=int, choices=(0, 1),
                        default=0)
    parser.add_argument("--stop-portal-top-deep", action="store_true")
    parser.add_argument("--stop-portal-top-lava", action="store_true")
    parser.add_argument("--stop-portal-top-return", action="store_true")
    parser.add_argument("--resume-portal-ignite", action="store_true")
    parser.add_argument("--stop-portal-lit", action="store_true")
    parser.add_argument("--resume-nether-route", action="store_true")
    parser.add_argument("--stop-overworld-stock-return", action="store_true")
    parser.add_argument("--stop-nether-stocked", action="store_true")
    parser.add_argument("--try-fortress-direct", action="store_true")
    parser.add_argument("--stop-fortress-spawner", action="store_true")
    parser.add_argument("--refresh-vitals", action="store_true")
    args = parser.parse_args()
    stage = stages.index(args.stop_after)
    start = stages.index(args.start_at)
    if start and (not args.resume_tape or not args.load_slot):
        parser.error("--start-at after opening requires --resume-tape and --load-slot")
    if bool(args.load_slot) != bool(args.save_root):
        parser.error("--load-slot and --save-root must be supplied together")
    if start > stage:
        parser.error("--start-at must not follow --stop-after")
    run = lambda name: start <= stages.index(name) <= stage
    args.game = args.game.resolve()
    episode = Episode(args)
    try:
        if args.resume_nether_route:
            if args.load_slot == "weapon-prepared":
                food_centers = (
                    (110.5, -232.5), (110.5, -232.5),
                    (134.5, -233.5), (147.5, -205.5),
                    (102.5, -198.5), (69.5, -250.5),
                    (66.5, -146.5), (115.0, -189.0),
                    (145.5, -170.5), (175.5, -190.5),
                    (175.5, -230.5), (150.5, -260.5),
                    (110.5, -275.5), (75.5, -275.5),
                    (45.5, -240.5), (45.5, -200.5),
                    (55.5, -165.5), (90.5, -150.5),
                    (125.5, -150.5), (160.5, -145.5),
                    (195.5, -165.5), (205.5, -205.5),
                    (205.5, -245.5), (180.5, -280.5),
                    (135.5, -305.5), (90.5, -305.5),
                )
                for center_x, center_z in food_centers:
                    if meat_count(episode.obs) >= 15:
                        break
                    try:
                        navigate(episode, center_x, center_z,
                                 max_ticks=700, sprint=False,
                                 tolerance=1.0, arrival_idle=0,
                                 combat="creeper", swim=True)
                        hunt_passive(episode, wanted=3,
                                     center_x=center_x,
                                     center_z=center_z)
                    except RuntimeError:
                        if episode.obs["dead"]:
                            raise
                if meat_count(episode.obs) < 15:
                    raise RuntimeError(
                        f"resupply collected only "
                        f"{meat_count(episode.obs)}/15 route meat")
                eat_available_meat(episode)
                if episode.can_save:
                    episode.step(save_slot="food-stocked")
                write_jsonl(args.out, episode.rows)
                print(json.dumps({
                    "food_stocked": True,
                    "tick": len(episode.rows),
                    "meat": meat_count(episode.obs),
                }), flush=True)
                return
            if args.load_slot == "food-stocked":
                continue_food_stocked_to_fortress_edge(episode)
                write_jsonl(args.out, episode.rows)
                print(json.dumps({
                    "fortress_direct": True,
                    "tick": len(episode.rows),
                    "pose": [episode.obs["x"], episode.obs["y"],
                             episode.obs["z"]],
                    "health": episode.obs["health"],
                    "food": episode.obs["food"],
                }), flush=True)
                return
            if args.load_slot == "nether-stocked":
                navigate(episode, 13.5, -3.5, max_ticks=500,
                         sprint=True, tolerance=0.8, arrival_idle=8,
                         combat=True)
                if episode.can_save:
                    episode.step(save_slot="fortress-pillar-base")
                solidify_adjacent_shallow_lava(episode, item_id=12)
                pillar_up_clearing_mixed(
                    episode, 68.0, items=(12, 13))
                bridge_positive_axis_mixed(episode, "x", 18.5)
                navigate(episode, 18.5, -3.5, max_ticks=500,
                         sprint=True, tolerance=0.8, arrival_idle=8,
                         combat=True)
                if episode.can_save:
                    episode.step(save_slot="fortress-edge")
                write_jsonl(args.out, episode.rows)
                print(json.dumps({
                    "fortress_direct": True,
                    "tick": len(episode.rows),
                    "pose": [episode.obs["x"], episode.obs["y"],
                             episode.obs["z"]],
                    "health": episode.obs["health"],
                    "food": episode.obs["food"],
                    "mobs": episode.obs["mobs"][:12],
                    "blocks": episode.obs["blocks"][:20],
                }), flush=True)
                return
            if args.load_slot in ("nether-entry", "nether-refreshed"):
                if args.try_fortress_direct:
                    if (args.load_slot == "nether-entry" and
                            args.refresh_vitals and
                            episode.obs["health"] < 15.0):
                        # Start the long Overworld resupply in early morning.
                        # Waiting after arrival populates the sand worksite
                        # with persistent creepers; this enclosed Nether
                        # pocket advances the same server clock safely before
                        # the intentional fire death and respawn.
                        phase = episode.obs["world_time"] % 24000
                        delayed_for_daylight = phase > 0
                        if delayed_for_daylight:
                            episode.idle_ticks(24000 - phase)
                        navigate(episode, 6.5, 1.5, max_ticks=100,
                                 sprint=False, tolerance=0.15,
                                 arrival_idle=0, combat=False)
                        flint_slot = ensure_hotbar(episode, 259)
                        dyaw, dpitch = look_at(
                            episode.obs, 6.5, 58.98, 1.5)
                        episode.step(use=1, do_place=1, hotbar=flint_slot,
                                     dyaw=dyaw, dpitch=dpitch, cam=1,
                                     probe_x=6, probe_y=59, probe_z=1)
                        for _ in range(500):
                            episode.step()
                            if episode.obs["dead"]:
                                break
                        else:
                            raise RuntimeError(
                                f"Nether portal fire was not lethal; pose="
                                f"({episode.obs['x']},{episode.obs['y']},"
                                f"{episode.obs['z']}) health="
                                f"{episode.obs['health']} fire="
                                f"{episode.obs['fire_ticks']} probe="
                                f"{episode.obs.get('probe')}")
                        for _ in range(24):
                            episode.step()
                        for _ in range(20):
                            episode.step(death_click=1, death_button=0)
                            if not episode.obs["dead"]:
                                break
                        else:
                            raise RuntimeError("vitals refresh did not respawn")
                        respawn_phase = episode.obs["world_time"] % 24000
                        if respawn_phase >= 13000:
                            episode.idle_ticks(
                                24000 - respawn_phase + 7000)
                            if episode.obs["dead"]:
                                for _ in range(20):
                                    episode.step(
                                        death_click=1, death_button=0)
                                    if not episode.obs["dead"]:
                                        break
                                else:
                                    raise RuntimeError(
                                        "daylight refresh did not respawn")
                        if episode.can_save:
                            episode.step(save_slot="overworld-refreshed-spawn")
                        if os.environ.get("NETHERITE_DEBUG_REFRESH_SPAWN"):
                            raise RuntimeError(
                                f"refresh spawn pose=({episode.obs['x']},"
                                f"{episode.obs['y']},{episode.obs['z']}) "
                                f"health={episode.obs['health']} food="
                                f"{episode.obs['food']} blocks="
                                f"{episode.obs['blocks'][:40]}")
                        if not delayed_for_daylight:
                            # Two wounded zombies occupy the narrow south face
                            # of this deterministic spawn. Lure them into sun
                            # when the old dusk-aligned checkpoint is used.
                            for tick in range(240):
                                episode.step(
                                    forward=1, jump=1, sprint=1,
                                    dyaw=-45.0 if tick == 0 else 0.0)
                            if episode.obs["dead"]:
                                for _ in range(20):
                                    episode.step()
                                for _ in range(20):
                                    episode.step(
                                        death_click=1, death_button=0)
                                    if not episode.obs["dead"]:
                                        break
                                else:
                                    raise RuntimeError(
                                        "post-lure respawn did not complete")
                            episode.idle_ticks(800)
                            for tick in range(100):
                                episode.step(
                                    forward=1, jump=1, sprint=1,
                                    dyaw=-45.0 if tick == 0 else 0.0)
                        else:
                            # At early-morning respawn the old zombies are
                            # already burning. Exit on the same open southeast
                            # diagonal, which bypasses the creeper west of the
                            # direct first-tree line.
                            episode.idle_ticks(240)
                            for tick in range(100):
                                episode.step(
                                    forward=1, jump=1, sprint=1,
                                    dyaw=-45.0 if tick == 0 else 0.0)
                        for tree_x, tree_z in ((100.5, -120.5),
                                               (120.5, -180.5),
                                               (119.5, -235.5)):
                            navigate(episode, tree_x, tree_z, max_ticks=900,
                                     sprint=False, tolerance=1.0,
                                     arrival_idle=0, combat=True)
                        collect_logs_probed(episode, 2)
                        craft_opening_items(episode)
                        craft_planks(episode)
                        craft_sticks(episode)
                        navigate(episode, 117.5, -234.5, max_ticks=180,
                                 sprint=False, tolerance=0.5,
                                 arrival_idle=0, combat=False)
                        table = place_table(episode)
                        open_table(episode, table)
                        craft_wooden_sword(episode)
                        collect_logs_probed(episode, 3)
                        for _ in range(3):
                            craft_planks(episode)
                        navigate(episode, table[1] + 0.5,
                                 table[3] + 0.5, max_ticks=180,
                                 sprint=False, tolerance=2.0,
                                 arrival_idle=0, combat=False)
                        open_table(episode, table)
                        craft_wooden_sword(episode)
                        craft_sticks(episode)
                        open_table(episode, table)
                        craft_wooden_sword(episode)
                        open_table(episode, table)
                        if episode.can_save:
                            episode.step(save_slot="pre-pick")
                        craft_wooden_pick(episode)
                        open_table(episode, table)
                        craft_wooden_pick(episode)
                        if episode.can_save:
                            episode.step(save_slot="weapon-prepared")
                        food_centers = (
                            (110.5, -232.5), (110.5, -232.5),
                            (134.5, -233.5), (147.5, -205.5),
                            (102.5, -198.5), (69.5, -250.5),
                            (66.5, -146.5), (115.0, -189.0),
                            (145.5, -170.5), (175.5, -190.5),
                            (175.5, -230.5), (150.5, -260.5),
                            (110.5, -275.5), (75.5, -275.5),
                            (45.5, -240.5), (45.5, -200.5),
                            (55.5, -165.5), (90.5, -150.5),
                            (125.5, -150.5), (160.5, -145.5),
                            (195.5, -165.5), (205.5, -205.5),
                            (205.5, -245.5), (180.5, -280.5),
                            (135.5, -305.5), (90.5, -305.5),
                        )
                        for center_x, center_z in food_centers:
                            if meat_count(episode.obs) >= 15:
                                break
                            try:
                                navigate(episode, center_x, center_z,
                                         max_ticks=700, sprint=False,
                                         tolerance=1.0, arrival_idle=0,
                                         combat="creeper", swim=True)
                                hunt_passive(episode, wanted=3,
                                             center_x=center_x,
                                             center_z=center_z)
                            except RuntimeError as error:
                                if episode.obs["dead"]:
                                    raise
                        if meat_count(episode.obs) < 15:
                            raise RuntimeError(
                                f"resupply collected only "
                                f"{meat_count(episode.obs)}/15 route meat")
                        eat_available_meat(episode)
                        if episode.can_save:
                            episode.step(save_slot="food-stocked")
                        navigate_carving(episode, 40.5, -120.5,
                                         max_ticks=900, tolerance=1.0)
                        navigate_carving(episode, 80.5, -120.5,
                                         max_ticks=1600, tolerance=1.0)
                        navigate(
                            episode, 51.5, 31.5, max_ticks=2200,
                            sprint=True, tolerance=0.5, combat=True)
                        if episode.can_save:
                            episode.step(save_slot="portal-shelf-start")
                        # Carry enough ordinary gravity-safe material for the
                        # fortress rear seals and the full blaze bulkhead.
                        # The quarry supplements this stock but its exposed
                        # parapet drops can fall outside the one-wide bridge.
                        gather_portal_shelf_sand(episode, 96)
                        if item_count(episode.obs, 268) < 1:
                            raise RuntimeError(
                                "wooden sword was lost before portal return; "
                                f"inventory={episode.obs['inventory_ids']}/"
                                f"{episode.obs['inventory_counts']}")
                        craft_sandstone(episode, 10)
                        navigate(episode, 55.5, 34.5, max_ticks=500,
                                 sprint=False, tolerance=0.3,
                                 arrival_idle=0, combat=False)
                        navigate(episode, 51.5, 34.5, max_ticks=500,
                                 sprint=False, tolerance=0.3,
                                 arrival_idle=0, combat=False)
                        navigate(episode, 51.5, 30.4, max_ticks=500,
                                 sprint=False, tolerance=0.2,
                                 arrival_idle=0, combat=False)
                        for _ in range(12):
                            episode.step()
                        if episode.obs["y"] < 65.8:
                            pillar_up(episode, 66.0, item_id=12)
                        for _ in range(40):
                            dyaw, dpitch = look_at(
                                episode.obs, 51.5,
                                episode.obs["y"] + 1.62, 29.5)
                            episode.step(forward=1, jump=1,
                                         dyaw=dyaw, dpitch=dpitch)
                        for _ in range(240):
                            episode.step()
                            if episode.obs["dimension"] == -1:
                                break
                        else:
                            raise RuntimeError(
                                f"vitals refresh did not return Nether; pose="
                                f"({episode.obs['x']:.3f},"
                                f"{episode.obs['y']:.3f},"
                                f"{episode.obs['z']:.3f}) portal="
                                f"{episode.obs.get('portal')} blocks="
                                f"{episode.obs['blocks'][:20]}")
                        if item_count(episode.obs, 12) < 11:
                            raise RuntimeError(
                                f"vitals refresh lost pillar stock; inventory="
                                f"{episode.obs['inventory_ids']}")
                        if episode.can_save:
                            episode.step(save_slot="nether-refreshed")
                    navigate(episode, 13.5, -3.5, max_ticks=500,
                             sprint=True, tolerance=0.8, arrival_idle=8,
                             combat=True)
                    if episode.can_save:
                        episode.step(save_slot="fortress-pillar-base")
                    solidify_adjacent_shallow_lava(episode, item_id=12)
                    pillar_up_clearing_mixed(
                        episode, 68.0, items=(12, 13))
                    bridge_positive_axis_mixed(episode, "x", 18.5)
                    navigate(episode, 18.5, -3.5, max_ticks=500,
                             sprint=True, tolerance=0.8, arrival_idle=8,
                             combat=True)
                    if episode.can_save:
                        episode.step(save_slot="fortress-edge")
                    write_jsonl(args.out, episode.rows)
                    print(json.dumps({
                        "fortress_direct": True,
                        "tick": len(episode.rows),
                        "pose": [episode.obs["x"], episode.obs["y"],
                                 episode.obs["z"]],
                        "health": episode.obs["health"],
                        "food": episode.obs["food"],
                        "mobs": episode.obs["mobs"][:12],
                        "blocks": episode.obs["blocks"][:20],
                    }), flush=True)
                    return
                navigate(episode, 6.5, 1.5, max_ticks=100,
                         sprint=False, tolerance=0.15, arrival_idle=12)
                navigate(episode, 6.5, 0.5, max_ticks=200,
                         sprint=False, tolerance=0.15, arrival_idle=0)
                for _ in range(110):
                    episode.step()
                    if episode.obs["dimension"] == 0:
                        break
                    if episode.obs["dead"]:
                        raise RuntimeError("Nether stock return was lethal")
                else:
                    raise RuntimeError(
                        f"Nether portal did not return to Overworld; "
                        f"pose=({episode.obs['x']:.3f},"
                        f"{episode.obs['y']:.3f},{episode.obs['z']:.3f})")
                if episode.can_save:
                    episode.step(save_slot="overworld-stock-return")
                if args.stop_overworld_stock_return:
                    write_jsonl(args.out, episode.rows)
                    print(json.dumps({
                        "overworld_stock_return": True,
                        "tick": len(episode.rows),
                        "pose": [episode.obs["x"], episode.obs["y"],
                                 episode.obs["z"]],
                        "time": episode.obs["world_time"],
                        "health": episode.obs["health"],
                        "food": episode.obs["food"],
                        "mobs": episode.obs["mobs"][:12],
                    }), flush=True)
                    return
            elif args.load_slot == "blaze-rods":
                return_from_blaze_farm(episode)
                if episode.can_save:
                    episode.step(save_slot="overworld-after-rods")
                write_jsonl(args.out, episode.rows)
                print(json.dumps({
                    "overworld_after_rods": True,
                    "tick": len(episode.rows),
                    "pose": [episode.obs["x"], episode.obs["y"],
                             episode.obs["z"]],
                    "health": episode.obs["health"],
                    "food": episode.obs["food"],
                    "blaze_rods": item_count(episode.obs, 369),
                }), flush=True)
                return
            elif (args.load_slot in ("fortress-edge", "fortress-spawner",
                                     "blaze-bulkhead") or
                  args.load_slot.startswith("blaze-wave-") or
                  args.load_slot.startswith("blaze-slit-kill-") or
                  args.load_slot.startswith("fortress-leg-")):
                fortress_waypoints = [
                    (7.5, -4.5), (-12.5, -4.5), (-31.5, -4.5),
                    (-50.5, -4.5), (-69.5, -4.5), (-88.5, -4.5),
                    (-101.5, -4.5), (-101.5, -11.5),
                    (-101.5, -24.5), (-101.5, -37.5),
                    (-101.5, -50.5), (-101.5, -69.5),
                    (-101.5, -88.5), (-101.5, -101.5),
                    (-101.5, -114.5), (-101.5, -133.5),
                    (-101.5, -152.5), (-101.5, -171.5),
                    (-120.5, -171.5), (-139.5, -171.5),
                    (-152.5, -171.5),
                ]
                start_index = (0 if args.load_slot == "fortress-edge" else
                               len(fortress_waypoints)
                               if (args.load_slot == "fortress-spawner" or
                                   args.load_slot == "blaze-bulkhead" or
                                   args.load_slot.startswith("blaze-wave-") or
                                   args.load_slot.startswith(
                                       "blaze-slit-kill-")) else
                               int(args.load_slot.rsplit("-", 1)[1]) + 1)
                if start_index == 0:
                    bridge_negative_x_mixed(episode, 7.5)
                    seal_fortress_entrance(episode)
                for index in range(start_index, len(fortress_waypoints)):
                    # Build the spawner bulkhead from the safe east side of
                    # the bridge.  Advancing to the final waypoint first
                    # spawns the blazes with the player west of the wall and
                    # turns construction into an unavoidable damage race.
                    if index == 20:
                        build_blaze_bulkhead(episode)
                        break
                    if index == 1:
                        clear_route_blazes_behind_gate(episode, -4)
                        navigate(episode, -4.6, -4.5, max_ticks=100,
                                 sprint=True, tolerance=0.2,
                                 arrival_idle=0, combat=False)
                        shutter = next((item for item in (12, 24)
                                        if item_count(episode.obs, item) > 0),
                                       None)
                        if shutter is None:
                            raise RuntimeError(
                                "first fortress airlock exhausted blocks")
                        place_item_on(episode, -4, 68, -5, shutter)
                        navigate(episode, -6.5, -4.5, max_ticks=160,
                                 sprint=True, tolerance=0.35,
                                 arrival_idle=0, combat=False)
                        seal_fortress_route_behind(episode, -4,
                                                   recover=False)
                        clear_route_blazes_behind_gate(episode, -8)
                    if index == 5:
                        clear_route_blazes_behind_gate(episode, -70)
                    target_x, target_z = fortress_waypoints[index]
                    if index in (19, 20):
                        eat_available_meat(episode)
                        navigate_serpentine(
                            episode, target_x, target_z,
                            max_ticks=500, tolerance=1.0)
                    else:
                        navigate(episode, target_x, target_z, max_ticks=900,
                                 sprint=True, tolerance=1.0, arrival_idle=2,
                                 combat=index not in (5, 6),
                                 swim=7 <= index <= 17)
                    if index == 2:
                        rear_x = -25
                        seal_fortress_route_behind(
                            episode, rear_x, recover=False)
                        eat_available_meat(episode)
                    if episode.can_save:
                        episode.step(save_slot=f"fortress-leg-{index}")
                if (episode.can_save and
                        start_index < len(fortress_waypoints) and
                        index == len(fortress_waypoints) - 1 and
                        args.load_slot != "fortress-spawner" and
                        not args.load_slot.startswith("blaze-wave-")):
                    episode.step(save_slot="fortress-spawner")
                if args.stop_fortress_spawner:
                    write_jsonl(args.out, episode.rows)
                    print(json.dumps({
                        "fortress_spawner": True,
                        "tick": len(episode.rows),
                        "pose": [episode.obs["x"], episode.obs["y"],
                                 episode.obs["z"]],
                        "health": episode.obs["health"],
                        "food": episode.obs["food"],
                        "mobs": episode.obs["mobs"][:16],
                    }), flush=True)
                    return
                if args.load_slot.startswith("blaze-wave-"):
                    recover_between_blaze_waves(episode)
                elif args.load_slot == "fortress-spawner":
                    build_blaze_bulkhead(episode)
                if os.environ.get("NETHERITE_DEBUG_BLAZE_SLIT"):
                    eid = kill_blaze_through_bulkhead(episode)
                    raise RuntimeError(
                        f"blaze slit debug killed={eid} health="
                        f"{episode.obs['health']} food={episode.obs['food']} "
                        f"fire={episode.obs['fire_ticks']} mobs="
                        f"{episode.obs['mobs'][:16]} items="
                        f"{episode.obs.get('items', [])[:16]}")
                acquire_blaze_rods_protected(episode, target=5)
                if episode.can_save:
                    episode.step(save_slot="blaze-rods")
                write_jsonl(args.out, episode.rows)
                print(json.dumps({
                    "blaze_rods": item_count(episode.obs, 369),
                    "tick": len(episode.rows),
                    "pose": [episode.obs["x"], episode.obs["y"],
                             episode.obs["z"]],
                    "health": episode.obs["health"],
                    "food": episode.obs["food"],
                }), flush=True)
                return
            elif args.load_slot != "overworld-stock-return":
                raise RuntimeError(
                    "Nether route resume requires nether-entry or "
                    "overworld-stock-return or fortress-edge")
            day_phase = episode.obs["world_time"] % 24000
            if day_phase >= 13000:
                # The return checkpoint is exposed and food-starved. Spend
                # the unsafe night in the enclosed Nether portal pocket. Bulk
                # idle is still expanded into exact no-input tape rows.
                navigate(episode, 51.5, 31.5, max_ticks=120,
                         sprint=False, tolerance=0.18, arrival_idle=12,
                         combat=True)
                navigate(episode, 51.5, 29.5, max_ticks=180,
                         sprint=False, tolerance=0.15, arrival_idle=0,
                         combat=True)
                for _ in range(110):
                    episode.step()
                    if episode.obs["dimension"] == -1:
                        break
                else:
                    raise RuntimeError(
                        f"night wait did not enter Nether; pose="
                        f"({episode.obs['x']:.3f},{episode.obs['y']:.3f},"
                        f"{episode.obs['z']:.3f}) time="
                        f"{episode.obs['world_time']} mobs="
                        f"{episode.obs['mobs'][:8]} blocks="
                        f"{episode.obs['blocks'][:16]} portal="
                        f"{episode.obs.get('portal_time')}/"
                        f"{episode.obs.get('portal_cooldown')}/"
                        f"{episode.obs.get('portal_last_valid')} cache="
                        f"{episode.obs.get('portal_cache')}")
                episode.idle_ticks(24000 - day_phase + 2200)
                if episode.obs["dead"]:
                    raise RuntimeError("Nether night wait was lethal")
                navigate(episode, 6.5, 1.5, max_ticks=100,
                         sprint=False, tolerance=0.15, arrival_idle=12)
                navigate(episode, 6.5, 0.5, max_ticks=200,
                         sprint=False, tolerance=0.15, arrival_idle=0)
                for _ in range(110):
                    episode.step()
                    if episode.obs["dimension"] == 0:
                        break
                else:
                    raise RuntimeError("night wait did not return Overworld")
                kill_nearby_hostiles(episode, radius=10.0)
            pillar_kill_route_zombie(episode)
            kill_nearby_creepers(episode, radius=12.0)
            prepare_portal_shelf_shovel(episode)
            gather_portal_shelf_sand(episode, 88)
            craft_sandstone(episode, 22)
            navigate(episode, 51.5, 29.5, max_ticks=700,
                     sprint=False, tolerance=0.15, arrival_idle=0,
                     combat=True)
            for _ in range(120):
                episode.step()
                if episode.obs["dimension"] == -1:
                    break
                if episode.obs["dead"]:
                    raise RuntimeError("stocked Nether entry was lethal")
            else:
                raise RuntimeError(
                    f"stocked player did not enter Nether; "
                    f"pose=({episode.obs['x']:.3f},"
                    f"{episode.obs['y']:.3f},{episode.obs['z']:.3f})")
            if episode.can_save:
                episode.step(save_slot="nether-stocked")
            if args.stop_nether_stocked:
                write_jsonl(args.out, episode.rows)
                print(json.dumps({
                    "nether_stocked": True,
                    "tick": len(episode.rows),
                    "pose": [episode.obs["x"], episode.obs["y"],
                             episode.obs["z"]],
                    "health": episode.obs["health"],
                    "food": episode.obs["food"],
                    "sandstone": item_count(episode.obs, 24),
                    "mobs": episode.obs["mobs"][:12],
                }), flush=True)
                return
            raise RuntimeError("Nether fortress traversal is not yet complete")
        if args.resume_portal_ignite:
            if args.load_slot == "portal-top-1":
                # Remove the casting source while it is still within reach of
                # the elevated return position. Flowing water in x52,y66 is a
                # valid empty-for-casting cell but invalid for BlockPortal.
                pickup_fluid(episode, 53, 69, 29, 326)
                for _ in range(120):
                    episode.step()
                navigate(episode, 52.5, 31.5, max_ticks=500,
                         sprint=True, tolerance=0.4, arrival_idle=0,
                         combat="creeper", swim=False)
                settle(episode)
                navigate(episode, 51.5, 29.5, max_ticks=300,
                         sprint=False, tolerance=0.15, arrival_idle=0,
                         combat="creeper", swim=False)
                flint_slot = ensure_hotbar(episode, 259)
                for x in (51, 52):
                    for y in (66, 67, 68):
                        episode.step(probe_x=x, probe_y=y, probe_z=29)
                        if episode.obs["probe"][0] != 0:
                            raise RuntimeError(
                                f"portal ignition cell is occupied: "
                                f"probe={episode.obs['probe']}")
                frame = ([(x, 65, 29) for x in (51, 52)]
                         + [(x, 69, 29) for x in (51, 52)]
                         + [(x, y, 29) for x in (50, 53)
                            for y in (66, 67, 68)])
                for x, y, z in frame:
                    episode.step(probe_x=x, probe_y=y, probe_z=z)
                    if episode.obs["probe"][0] != 49:
                        raise RuntimeError(
                            f"portal frame is incomplete: "
                            f"probe={episode.obs['probe']}")
                for _ in range(12):
                    # From inside the empty frame, click the east face of the
                    # left upright. Its adjacent cell is the portal interior.
                    dyaw, dpitch = look_at(episode.obs, 50.99, 67.5, 29.5)
                    episode.step(use=1, do_place=1, hotbar=flint_slot,
                                 dyaw=dyaw, dpitch=dpitch, cam=1,
                                 probe_x=51, probe_y=66, probe_z=29)
                    if episode.obs["probe"][0] == 90:
                        break
                    episode.step()
                else:
                    raise RuntimeError(
                        f"completed portal did not ignite; "
                        f"probe={episode.obs['probe']} "
                        f"ray={episode.obs.get('ray')} "
                        f"hotbar={episode.obs['hotbar_ids']}/"
                        f"{episode.obs.get('hotbar_metas')} "
                        f"pose=({episode.obs['x']:.3f},"
                        f"{episode.obs['y']:.3f},{episode.obs['z']:.3f}) "
                        f"blocks={episode.obs['blocks'][:24]}")
                for x in (51, 52):
                    for y in (66, 67, 68):
                        episode.step(probe_x=x, probe_y=y, probe_z=29)
                        if episode.obs["probe"][0] != 90:
                            raise RuntimeError(
                                f"portal ignition missed interior {x},{y},29; "
                                f"probe={episode.obs['probe']}")
                if episode.can_save:
                    episode.step(save_slot="portal-lit")
                if args.stop_portal_lit:
                    write_jsonl(args.out, episode.rows)
                    print(json.dumps({"portal_lit": True,
                                      "tick": len(episode.rows)}), flush=True)
                    return
            elif args.load_slot != "portal-lit":
                raise RuntimeError(
                    "portal ignition resume requires portal-top-1 or portal-lit")
            navigate(episode, 51.5, 29.5, max_ticks=300,
                     sprint=False, tolerance=0.2, arrival_idle=0,
                     combat="creeper", swim=True)
            for _ in range(100):
                episode.step()
                if episode.obs["dimension"] == -1:
                    break
                if episode.obs["dead"]:
                    raise RuntimeError("portal entry was lethal")
            else:
                raise RuntimeError(
                    f"portal did not transfer after 100 contacts; "
                    f"dimension={episode.obs['dimension']} "
                    f"pose=({episode.obs['x']:.3f},{episode.obs['y']:.3f},"
                    f"{episode.obs['z']:.3f})")
            if episode.can_save:
                episode.step(save_slot="nether-entry")
            write_jsonl(args.out, episode.rows)
            print(json.dumps({"nether_entry": True,
                              "tick": len(episode.rows),
                              "pose": [episode.obs["x"], episode.obs["y"],
                                       episode.obs["z"]]}), flush=True)
            return
        if args.resume_portal_top:
            top_index = args.portal_top_index
            target_x = 51 + top_index
            deep_slot = f"portal-top-{top_index}-deep"
            lava_slot = f"portal-top-{top_index}-lava"
            return_slot = f"portal-top-{top_index}-return-high"
            if args.load_slot == return_slot:
                try:
                    navigate(episode, 50.5, 20.5, max_ticks=600,
                             sprint=False, tolerance=0.25, arrival_idle=0)
                    navigate(episode, 50.5, 28.5, max_ticks=600,
                             sprint=False, tolerance=0.25, arrival_idle=0)
                except RuntimeError:
                    if episode.obs["dead"] or episode.obs["y"] >= 67.0:
                        raise
                    ground_route = ((54.5, 25.5), (54.5, 35.5))
                    ground_route += (((52.5, 35.5), (52.5, 31.5))
                                     if top_index == 0 else
                                     ())
                    for route_x, route_z in ground_route:
                        navigate(episode, route_x, route_z, max_ticks=500,
                                 sprint=False, tolerance=0.45,
                                 arrival_idle=0, combat=True, swim=True)
                    if top_index == 1:
                        # Stay east of the sandstone ridge. Hand-mining it at
                        # dusk took long enough for the first night pack to
                        # spawn and path into the open trench.
                        for route_x, route_z in ((59.5, 35.5),
                                                 (59.5, 28.5),
                                                 (56.5, 28.5)):
                            navigate(episode, route_x, route_z,
                                     max_ticks=500, sprint=True,
                                     tolerance=0.5, arrival_idle=0,
                                     combat="creeper", swim=True)
                if top_index == 0:
                    corner_item = next(
                        item for item in (24, 1, 4, 3, 35, 5, 12, 13)
                        if item_count(episode.obs, item) > 0)
                    pillar_one_staggered(episode, corner_item)
                    corner_slot = ensure_hotbar(episode, corner_item)
                    before_corner = item_count(episode.obs, corner_item)
                    for _ in range(12):
                        dyaw, dpitch = look_at(
                            episode.obs, 50.5, 69.5, 28.99)
                        episode.step(
                            use=1, do_place=1, hotbar=corner_slot,
                            dyaw=dyaw, dpitch=dpitch, cam=1,
                            probe_x=50, probe_y=69, probe_z=29)
                        if episode.obs["probe"][0] != 0:
                            break
                        episode.step()
                    else:
                        raise RuntimeError("top-left mold corner did not place")
                    if item_count(episode.obs, corner_item) >= before_corner:
                        raise RuntimeError("top-left mold consumed no block")
                navigate(episode, target_x + 1.5, 29.5, max_ticks=240,
                         sprint=False, tolerance=0.25, arrival_idle=0,
                         swim=True)
                if episode.can_save:
                    episode.step(
                        save_slot=f"portal-top-{top_index}-ground-ready")
                lava_slot_index = ensure_hotbar(episode, 327)
                # Click the east face of the already-cast block immediately
                # west of the target.  Cast x=51 first from side x=50; its
                # obsidian then becomes the placement face for x=52.
                dyaw, dpitch = look_at(
                    episode.obs, target_x - 0.01, 69.5, 29.5)
                episode.step(use=1, do_place=1, hotbar=lava_slot_index,
                             dyaw=dyaw, dpitch=dpitch, cam=1,
                             probe_x=target_x, probe_y=69, probe_z=29)
                if 325 not in episode.obs["inventory_ids"]:
                    raise RuntimeError("top-cast jump did not empty lava bucket")
                for _ in range(8):
                    episode.step()
                if not any(b[0] == 49 and
                           b[1:] == [target_x, 69, 29]
                           for b in episode.obs["blocks"]):
                    local = [b for b in episode.obs["blocks"]
                             if b != [0, 0, 0, 0]
                             and 49 <= b[1] <= 54
                             and 67 <= b[2] <= 70
                             and 28 <= b[3] <= 31]
                    raise RuntimeError(
                        f"portal top {top_index} did not cast from return; "
                        f"local={local}")
                if episode.can_save:
                    episode.step(save_slot=f"portal-top-{top_index}")
                write_jsonl(args.out, episode.rows)
                print(json.dumps({"portal_top_cast": top_index,
                                  "tick": len(episode.rows)}), flush=True)
                return
            if args.load_slot not in (deep_slot, lava_slot):
                if top_index == 0:
                    pickup_fluid(episode, 53, 68, 30, 326)
                    place_fluid_on(episode, 53, 68, 29, 326)
                    for _ in range(8):
                        episode.step()
                    navigate(episode, 50.5, 20.5, max_ticks=600,
                             sprint=False, tolerance=0.25, arrival_idle=0)
                    navigate(episode, 40.5, 20.5, max_ticks=500,
                             sprint=False, tolerance=0.25, arrival_idle=0)
                    if episode.can_save:
                        episode.step(
                            save_slot=f"portal-top-{top_index}-high")
                    descend_portal_bridge_column(episode, 52.0)
                else:
                    for route_x, route_z in (
                            (52.5, 35.5), (40.5, 40.5),
                            (40.5, 25.5), (41.5, 20.5)):
                        navigate(episode, route_x, route_z, max_ticks=600,
                                 sprint=False, tolerance=0.4,
                                 arrival_idle=0, combat=True, swim=True)
                    for block_y in range(69, 60, -1):
                        episode.step(probe_x=40, probe_y=block_y,
                                     probe_z=20)
                        if episode.obs["probe"][0] != 0:
                            mine_probed_coordinate(
                                episode, 40, block_y, 20,
                                available_break_tool(episode.obs),
                                max_ticks=2400)
                    navigate(episode, 40.4, 20.5, max_ticks=200,
                             sprint=False, tolerance=0.03, arrival_idle=0)
                    descend_mixed_pillar(episode, 52.0)
                if episode.can_save:
                    episode.step(save_slot=deep_slot)
                if args.stop_portal_top_deep:
                    write_jsonl(args.out, episode.rows)
                    print(json.dumps({"portal_top_deep": top_index,
                                      "tick": len(episode.rows)}),
                          flush=True)
                    return
            if args.load_slot != lava_slot:
                navigate(episode, 40.5, 32.5, max_ticks=500,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                navigate(episode, 112.5, 32.5, max_ticks=1000,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                navigate(episode, 112.5, 43.5, max_ticks=500,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                pillar_up_mixed(
                    episode, 60.0,
                    items=(24, 1, 4, 3, 35, 5, 12, 13), centered=True)
                navigate(episode, 115.5, 43.5, max_ticks=300,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                for dry_z in (42, 41):
                    for dry_y in (60, 61):
                        stable = 0
                        for _ in range(1200):
                            episode.step(probe_x=115, probe_y=dry_y,
                                         probe_z=dry_z)
                            if episode.obs["probe"][0] == 0:
                                stable += 1
                                if stable >= 12:
                                    break
                                continue
                            stable = 0
                            mine_probed_coordinate(
                                episode, 115, dry_y, dry_z,
                                available_break_tool(episode.obs),
                                max_ticks=2400)
                        else:
                            raise RuntimeError(
                                "top-cast pool entrance did not stabilize")
                navigate(episode, 115.5, 41.5, max_ticks=180,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                for dry_x in range(116, 121):
                    for dry_y in (60, 61):
                        stable = 0
                        for _ in range(1200):
                            episode.step(probe_x=dry_x, probe_y=dry_y,
                                         probe_z=41)
                            if episode.obs["probe"][0] == 0:
                                stable += 1
                                if stable >= 12:
                                    break
                                continue
                            stable = 0
                            mine_probed_coordinate(
                                episode, dry_x, dry_y, 41,
                                available_break_tool(episode.obs),
                                max_ticks=2400)
                        else:
                            raise RuntimeError(
                                "top-cast pool corridor did not stabilize")
                    navigate(episode, dry_x + 0.5, 41.5,
                             max_ticks=140, sprint=False, tolerance=0.25,
                             arrival_idle=0)
                navigate(episode, 120.5, 41.5, max_ticks=300,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                candidates = (
                    (117, 59, 43), (118, 59, 42), (118, 59, 43),
                    (118, 59, 44), (119, 59, 42), (119, 59, 43),
                    (119, 59, 44), (119, 59, 45), (120, 59, 42),
                    (120, 59, 43), (120, 59, 44), (120, 59, 45),
                    (121, 59, 43), (121, 59, 44), (121, 59, 45),
                    (123, 59, 44), (123, 59, 45), (123, 59, 46),
                )
                source = None
                for candidate in candidates:
                    episode.step(probe_x=candidate[0],
                                 probe_y=candidate[1],
                                 probe_z=candidate[2])
                    if episode.obs["probe"][:2] in ([10, 0], [11, 0]):
                        source = candidate
                        break
                if source is None:
                    raise RuntimeError("top cast found no east-pool source")
                pickup_fluid(episode, *source, 327)
                if episode.can_save:
                    episode.step(save_slot=lava_slot)
                if args.stop_portal_top_lava:
                    write_jsonl(args.out, episode.rows)
                    print(json.dumps({"portal_top_lava": top_index,
                                      "source": source,
                                      "tick": len(episode.rows)}),
                          flush=True)
                    return
            for _ in range(args.wait_ticks):
                episode.step()
            if args.load_slot == lava_slot:
                def top_blocks():
                    return sum(item_count(episode.obs, item)
                               for item in (24, 1, 4, 3, 35, 5, 12, 13))
                for stock_x in range(120, 109, -1):
                    navigate(episode, stock_x + 0.5, 41.5,
                             max_ticks=180, sprint=False, tolerance=0.25,
                             arrival_idle=0)
                    for stock_y in (62, 63):
                        for _ in range(240):
                            episode.step(probe_x=stock_x,
                                         probe_y=stock_y, probe_z=41)
                            if episode.obs["probe"][0] not in (12, 13, 24):
                                break
                            mine_probed_coordinate(
                                episode, stock_x, stock_y, 41,
                                available_break_tool(episode.obs),
                                max_ticks=1600)
                            for _ in range(20):
                                episode.step()
                            if top_blocks() >= 10:
                                break
                        if top_blocks() >= 10:
                            break
                    if top_blocks() >= 10:
                        break
                if top_blocks() < 10:
                    raise RuntimeError(
                        f"east ceiling did not yield enough return blocks; "
                        f"inventory={list(zip(episode.obs['inventory_ids'], episode.obs['inventory_counts']))}")
                if episode.can_save:
                    episode.step(
                        save_slot=f"portal-top-{top_index}-lava-stock")
            navigate(episode, 115.5, 41.5, max_ticks=300,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 115.5, 43.5, max_ticks=180,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 112.5, 43.5, max_ticks=300,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            descend_mixed_pillar(episode, 52.0)
            navigate(episode, 112.5, 32.5, max_ticks=500,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 40.5, 32.5, max_ticks=1000,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 40.5, 20.5, max_ticks=500,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            pillar_up_mixed(
                episode, 70.0,
                items=(24, 1, 4, 3, 35, 5, 12, 13), centered=True)
            if 68.8 <= episode.obs["y"] < 69.8:
                for _ in range(60):
                    dyaw, dpitch = look_at(
                        episode.obs, 41.5,
                        episode.obs["y"] + 1.62, 20.5)
                    episode.step(forward=1, jump=1, dyaw=dyaw,
                                 dpitch=dpitch)
                    if episode.obs["y"] >= 69.8:
                        break
            if episode.obs["y"] < 69.8:
                raise RuntimeError(
                    f"top-cast return pillar stopped at {episode.obs['y']}")
            if episode.can_save:
                episode.step(
                    save_slot=return_slot)
            if args.stop_portal_top_return:
                write_jsonl(args.out, episode.rows)
                print(json.dumps({"portal_top_return": top_index,
                                  "tick": len(episode.rows)}), flush=True)
                return
            navigate(episode, 50.5, 20.5, max_ticks=600,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 50.5, 28.5, max_ticks=600,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            place_fluid_on(episode, target_x, 68, 29, 327)
            for _ in range(8):
                episode.step()
            if not any(b[0] == 49 and b[1:] == [target_x, 69, 29]
                       for b in episode.obs["blocks"]):
                raise RuntimeError(
                    f"portal top {top_index} did not cast at x={target_x}")
            if episode.can_save:
                episode.step(save_slot=f"portal-top-{top_index}")
            write_jsonl(args.out, episode.rows)
            print(json.dumps({"portal_top_cast": top_index,
                              "tick": len(episode.rows)}), flush=True)
            return
        if args.prime_side_nine_water:
            prime_portal_cast_with_water(episode, (53, 68, 29))
            if episode.can_save:
                episode.step(save_slot="portal-side9-water")
            write_jsonl(args.out, episode.rows)
            print(json.dumps({"portal_side9_water": {
                key: episode.obs[key] for key in
                ("t", "x", "y", "z", "health", "food")}}), flush=True)
            return
        if args.resume_side_nine_return:
            for _ in range(args.wait_ticks):
                episode.step()
            at_sand_stock = args.load_slot == "portal-side9-sand-stock"
            dawn_restock = False
            if at_sand_stock and episode.obs.get(
                    "world_time", episode.obs["t"]) % 24000 >= 13000:
                # The stock checkpoint is exposed to the east at night.  A
                # zombie can land eleven hits during the deliberately slow
                # mixed-material pillar.  Reclaim the column, wait in the
                # sealed y=52 gallery, and climb after undead burn at dawn.
                descend_mixed_pillar(episode, 52.0)
                day_tick = episode.obs.get(
                    "world_time", episode.obs["t"]) % 24000
                for _ in range(24000 - day_tick + 1200):
                    episode.step()
                    if episode.obs["dead"]:
                        raise RuntimeError(
                            "side9 daylight wait was breached")
                if episode.can_save:
                    episode.step(save_slot="portal-side9-dawn-deep")
                dawn_restock = True
            if dawn_restock:
                pillar_up_mixed(
                    episode, 63.0, items=(12, 13, 24, 4, 3, 35, 5, 1),
                    centered=True)
                for sand_x in range(39, 19, -1):
                    for sand_y in (63, 64):
                        stable = 0
                        tool = available_break_tool(episode.obs)
                        slot = ensure_hotbar(episode, tool)
                        for _ in range(2400):
                            dyaw, dpitch = look_at(
                                episode.obs, sand_x + 0.5,
                                sand_y + 0.5, 20.5)
                            episode.step(
                                attack=1, hotbar=slot, dyaw=dyaw,
                                dpitch=dpitch, cam=1, probe_x=sand_x,
                                probe_y=sand_y, probe_z=20)
                            stable = (stable + 1 if
                                      episode.obs["probe"][0] == 0 else 0)
                            if stable >= 20:
                                break
                        else:
                            raise RuntimeError(
                                f"dawn stock cell {sand_x},{sand_y} "
                                "did not stabilize")
                    navigate(episode, sand_x + 0.5, 20.5,
                             max_ticks=140, sprint=False, tolerance=0.2,
                             arrival_idle=0)
                    for _ in range(20):
                        episode.step()
                    if item_count(episode.obs, 12) >= 32:
                        break
                navigate(episode, 40.5, 20.5, max_ticks=300,
                         sprint=False, tolerance=0.2, arrival_idle=0)
                if episode.can_save:
                    episode.step(save_slot="portal-side9-dawn-sand")
                stable_blocks = sum(item_count(episode.obs, item)
                                    for item in (1, 4, 3, 35, 5, 24))
                needed_sandstone = max(0, 7 - stable_blocks)
                craft_sandstone(episode, needed_sandstone)
                if episode.can_save:
                    episode.step(save_slot="portal-side9-dawn-stock")
            if not at_sand_stock and episode.obs["x"] > 100.0:
                navigate(episode, 115.5, 41.5, max_ticks=300,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                navigate(episode, 115.5, 43.5, max_ticks=160,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                navigate(episode, 112.5, 43.5, max_ticks=300,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                descend_mixed_pillar(episode, 52.0)
                navigate(episode, 112.5, 32.5, max_ticks=500,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                navigate(episode, 40.5, 32.5, max_ticks=900,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                navigate(episode, 40.5, 20.5, max_ticks=500,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                if episode.can_save:
                    episode.step(save_slot="portal-side9-return-deep")
            if not at_sand_stock:
                pillar_up_mixed(episode, 63.0,
                                items=(12, 13, 4, 3, 35, 5, 1))
                sand_before = item_count(episode.obs, 12)
                for sand_y in (63, 64):
                    stable = 0
                    tool = available_break_tool(episode.obs)
                    slot = ensure_hotbar(episode, tool)
                    for _ in range(2400):
                        dyaw, dpitch = look_at(
                            episode.obs, 39.5, sand_y + 0.5, 20.5)
                        episode.step(attack=1, hotbar=slot, dyaw=dyaw,
                                     dpitch=dpitch, cam=1,
                                     probe_x=39, probe_y=sand_y, probe_z=20)
                        stable = (stable + 1 if
                                  episode.obs["probe"][0] == 0 else 0)
                        if stable >= 30:
                            break
                    else:
                        raise RuntimeError(
                            "side9 sand alcove did not stabilize")
                navigate(episode, 39.5, 20.5, max_ticks=120,
                         sprint=False, tolerance=0.2, arrival_idle=0)
                for _ in range(80):
                    episode.step()
                    if item_count(episode.obs, 12) >= sand_before + 2:
                        break
                navigate(episode, 40.5, 20.5, max_ticks=120,
                         sprint=False, tolerance=0.2, arrival_idle=0)
                if episode.can_save:
                    episode.step(save_slot="portal-side9-sand-stock")
                if args.stop_side_nine_stock:
                    write_jsonl(args.out, episode.rows)
                    print(json.dumps({"portal_side9_stock": {
                        key: episode.obs[key] for key in
                        ("t", "x", "y", "z", "health", "food")}}),
                          flush=True)
                    return
            pillar_up_mixed(episode, 67.0,
                            items=(24, 1, 4, 3, 35, 5, 12, 13),
                            centered=True)
            episode.step(probe_x=40, probe_y=69, probe_z=20)
            if episode.obs["probe"][0] != 0:
                mine_probed_coordinate(
                    episode, 40, 69, 20,
                    available_break_tool(episode.obs), max_ticks=2400)
            # Keep the whole jump in column 40.  The old 40.3 target let the
            # water drift the apex into column 39, where a downward use had
            # no supporting face and silently consumed the remaining jumps.
            navigate(episode, 40.65, 20.5, max_ticks=80,
                     sprint=False, tolerance=0.04, arrival_idle=0)
            pillar_up_mixed(episode, 70.0,
                            items=(24, 1, 4, 3, 35, 5, 12, 13),
                            centered=True)
            navigate(episode, 50.5, 20.5, max_ticks=500,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 50.5, 28.5, max_ticks=500,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            if episode.can_save:
                episode.step(save_slot="portal-side9-bridge")
            place_fluid_on(episode, 53, 67, 29, 327)
            for _ in range(8):
                episode.step()
            if not any(b[0] == 49 and b[1:] == [53, 68, 29]
                       for b in episode.obs["blocks"]):
                local = [b for b in episode.obs["blocks"]
                         if b != [0, 0, 0, 0]
                         and 49 <= b[1] <= 54 and 64 <= b[2] <= 70
                         and 28 <= b[3] <= 31]
                raise RuntimeError(
                    f"final portal side cast did not make obsidian; "
                    f"local={local}")
            if episode.can_save:
                episode.step(save_slot="portal-side-9")
            write_jsonl(args.out, episode.rows)
            print(json.dumps({"portal_side_cast": 9,
                              "tick": len(episode.rows)}), flush=True)
            return
        if args.resume_side_nine_pool:
            for dry_y in (60, 61):
                episode.step(probe_x=120, probe_y=dry_y, probe_z=41)
                if episode.obs["probe"][0] != 0:
                    mine_probed_coordinate(
                        episode, 120, dry_y, 41,
                        available_break_tool(episode.obs), max_ticks=2400)
            navigate(episode, 120.5, 41.5, max_ticks=160,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            pickup_fluid(episode, 120, 59, 42, 327)
            if episode.can_save:
                episode.step(save_slot="portal-side9-lava")
            write_jsonl(args.out, episode.rows)
            print(json.dumps({"portal_side9_lava": {
                key: episode.obs[key] for key in
                ("t", "x", "y", "z", "health", "food")}}), flush=True)
            return
        if args.resume_side_nine:
            for _ in range(args.wait_ticks):
                episode.step()
            if episode.obs["y"] >= 60.0:
                navigate(episode, 44.5, 25.5, max_ticks=300,
                         sprint=False, tolerance=0.3, arrival_idle=0)
                settle(episode)
                pillar_up_mixed(episode, 70.0,
                                items=(12, 13, 4, 3, 35, 5, 1))
                bridge_positive_axis_mixed(episode, "x", 50.5)
                bridge_positive_axis_mixed(episode, "z", 28.5)
                navigate(episode, 50.5, 20.5, max_ticks=500,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                navigate(episode, 40.5, 20.5, max_ticks=400,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                descend_mixed_pillar(episode, 52.0)
                if episode.can_save:
                    episode.step(save_slot="portal-side9-deep")
                if args.stop_side_nine_deep:
                    write_jsonl(args.out, episode.rows)
                    print(json.dumps({"portal_side9_deep": {
                        key: episode.obs[key] for key in
                        ("t", "x", "y", "z", "health", "food")}}),
                          flush=True)
                    return
            navigate(episode, 40.5, 32.5, max_ticks=500,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 50.5, 32.5, max_ticks=400,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            tunnel_y = math.floor(episode.obs["y"])
            for bulkhead_x in (57, 65, 73, 81, 89, 97, 105):
                navigate(episode, bulkhead_x - 0.5, 32.5, max_ticks=500,
                         sprint=False, tolerance=0.25, arrival_idle=0)
                tool = available_break_tool(episode.obs)
                for block_y in (tunnel_y, tunnel_y + 1):
                    episode.step(probe_x=bulkhead_x, probe_y=block_y,
                                 probe_z=32)
                    if episode.obs["probe"][0] != 0:
                        mine_probed_coordinate(
                            episode, bulkhead_x, block_y, 32, tool,
                            max_ticks=2400)
                navigate(episode, bulkhead_x + 0.5, 32.5,
                         max_ticks=120, sprint=False, tolerance=0.25,
                         arrival_idle=0)
            navigate(episode, 112.5, 35.5, max_ticks=400,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            for block_y in (tunnel_y, tunnel_y + 1):
                episode.step(probe_x=112, probe_y=block_y, probe_z=36)
                if episode.obs["probe"][0] != 0:
                    mine_probed_coordinate(
                        episode, 112, block_y, 36,
                        available_break_tool(episode.obs), max_ticks=2400)
            navigate(episode, 112.5, 43.5, max_ticks=400,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            pillar_up_mixed(episode, 60.0,
                            items=(12, 13, 4, 3, 35, 5, 1))
            navigate(episode, 115.5, 43.5, max_ticks=300,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            for dry_z in (42, 41):
                for dry_y in (60, 61):
                    episode.step(probe_x=115, probe_y=dry_y,
                                 probe_z=dry_z)
                    if episode.obs["probe"][0] != 0:
                        mine_probed_coordinate(
                            episode, 115, dry_y, dry_z,
                            available_break_tool(episode.obs),
                            max_ticks=2400)
            navigate(episode, 115.5, 41.5, max_ticks=160,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            for dry_x in range(116, 121):
                for dry_y in (60, 61):
                    episode.step(probe_x=dry_x, probe_y=dry_y,
                                 probe_z=41)
                    if episode.obs["probe"][0] != 0:
                        mine_probed_coordinate(
                            episode, dry_x, dry_y, 41,
                            available_break_tool(episode.obs),
                            max_ticks=2400)
            navigate(episode, 120.5, 41.5, max_ticks=240,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            east_sources = [
                (117, 59, 43), (118, 59, 42), (118, 59, 43),
                (118, 59, 44), (119, 59, 42), (119, 59, 43),
                (119, 59, 44), (119, 59, 45), (120, 59, 42),
                (120, 59, 43), (120, 59, 44), (120, 59, 45),
                (121, 59, 43), (121, 59, 44), (121, 59, 45),
                (123, 59, 44), (123, 59, 45), (123, 59, 46),
            ]
            if episode.can_save:
                episode.step(save_slot="portal-side9-pool")
            pickup_fluid(episode, 120, 59, 42, 327)
            if episode.can_save:
                episode.step(save_slot="portal-side9-lava")
            if args.stop_side_nine_lava:
                write_jsonl(args.out, episode.rows)
                print(json.dumps({"portal_side9_lava": {
                    key: episode.obs[key] for key in
                    ("t", "x", "y", "z", "health", "food")}}),
                      flush=True)
                return
            navigate(episode, 115.5, 41.5, max_ticks=240,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 115.5, 43.5, max_ticks=160,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 112.5, 43.5, max_ticks=300,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            descend_mixed_pillar(episode, 52.0)
            navigate(episode, 112.5, 32.5, max_ticks=500,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 40.5, 32.5, max_ticks=900,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 40.5, 20.5, max_ticks=500,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            pillar_up_mixed(episode, 70.0,
                            items=(12, 13, 4, 3, 35, 5, 1))
            navigate(episode, 50.5, 20.5, max_ticks=500,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            navigate(episode, 50.5, 28.5, max_ticks=500,
                     sprint=False, tolerance=0.25, arrival_idle=0)
            place_fluid_on(episode, 53, 67, 29, 327)
            for _ in range(8):
                episode.step()
            if not any(b[0] == 49 and b[1:] == [53, 68, 29]
                       for b in episode.obs["blocks"]):
                raise RuntimeError("final portal side cast did not make obsidian")
            if episode.can_save:
                episode.step(save_slot="portal-side-9")
            write_jsonl(args.out, episode.rows)
            print(json.dumps({"portal_side_cast": 9,
                              "tick": len(episode.rows)}), flush=True)
            return
        if args.resume_direct_shaft:
            if episode.obs["y"] < 60.0:
                navigate(episode, 50.5, 32.5, max_ticks=160,
                         sprint=False, tolerance=0.05, arrival_idle=0)
                settle(episode)
                episode.step(probe_x=50,
                             probe_y=math.floor(episode.obs["y"]), probe_z=32)
                if episode.obs["probe"][0] == 50:
                    mine_probed_coordinate(
                        episode, 50, math.floor(episode.obs["y"]), 32,
                        available_break_tool(episode.obs), max_ticks=80)
            mine_and_pillar_shaft(episode, 65)
            finish_east_portal_cast(
                episode, (50, 68, 29), (), direct_tunnel=True)
            if episode.can_save:
                episode.step(save_slot="portal-side-8")
            write_jsonl(args.out, episode.rows)
            print(json.dumps({"portal_side_cast": 8,
                              "tick": len(episode.rows)}), flush=True)
            return
        if run("opening"):
            opening(episode)
        result = {"opening_end": {key: episode.obs[key]
                                   for key in ("x", "y", "z", "yaw", "pitch")}}
        if run("first_log"):
            result["first_log"] = approach_and_mine_log(episode)
        if run("opening_craft"):
            result["logs_mined"] = collect_logs(episode, 2)
            craft_opening_items(episode)
        if run("wooden_pick"):
            collect_logs(episode, 2)
            while episode.obs["inv_counts"][1] < 7:
                craft_planks(episode)
            craft_sticks(episode)
            table = place_table(episode)
            open_table(episode, table)
            craft_wooden_pick(episode)
            result["table_position"] = table[1:]
        if run("cobblestone"):
            result["recovered_table"] = mine_block(
                episode, 58, 4, 270)[1:]
            collect_logs(episode, 1)
            for _ in range(20):
                if nearest_block(episode, 18) is None:
                    break
                mine_block(episode, 18, None, 5, max_ticks=80)
            navigate(episode, 70.5, -173.5, max_ticks=160)
            navigate(episode, 72.5, -179.5, max_ticks=240, tolerance=0.25)
            settle(episode)
            result["ravine_entry"] = {key: episode.obs[key]
                                      for key in ("x", "y", "z")}
            mined = []
            target_cobble = 11 if stage >= stages.index("stone_tools") else 3
            while episode.obs["inv_counts"][3] < target_cobble:
                mined.append(mine_block(episode, 1, 3, 270)[1:])
            result["stone_mined"] = mined
        if run("stone_tools"):
            mine_coordinate(episode, 73, 40, -180, 3, 270,
                            max_ticks=35, require_pickup=False, aim_y=39.5)
            mine_coordinate(episode, 73, 41, -180, 3, 270,
                            max_ticks=35, require_pickup=False)
            navigate(episode, 73.5, -179.5, max_ticks=80, tolerance=0.2)
            table = place_table_on(episode, 72, 39, -180)
            open_table(episode, table)
            craft_stone_pick_and_furnace(episode)
            result["ravine_table"] = table[1:]
        if run("iron_ore"):
            for z in range(-179, -164):
                mine_coordinate(episode, 73, 40, z, None, 274,
                                max_ticks=60, require_pickup=False,
                                aim_y=39.5)
                episode.step()
                mine_coordinate(episode, 73, 41, z, None, 274,
                                max_ticks=60, require_pickup=False)
                episode.step()
                navigate(episode, 73.5, z + 0.5, max_ticks=60,
                         sprint=False, tolerance=0.2)
            if episode.obs["inv_iron"][1] < 5:
                raise RuntimeError(
                    f"seed tunnel yielded only {episode.obs['inv_iron'][1]} iron ore")
        if run("smelted_iron"):
            settle(episode)
            for z in (-164, -163):
                mine_coordinate(episode, 73, 40, z, None, 274,
                                max_ticks=30, require_pickup=False,
                                aim_y=40.5, detect_depth=False)
                episode.step()
                mine_coordinate(episode, 73, 41, z, None, 274,
                                max_ticks=20, require_pickup=False,
                                detect_depth=False)
                episode.step()
                navigate(episode, 73.5, z + 0.5, max_ticks=60,
                         sprint=False, tolerance=0.2)
            mine_coordinate(episode, 73, 40, -162, None, 274,
                            max_ticks=20, require_pickup=False,
                            aim_y=40.5, detect_depth=False)
            episode.step()
            navigate(episode, 73.5, -161.5, max_ticks=60,
                     sprint=False, tolerance=0.2)
            furnace = place_furnace_on(episode, 74, 39, -162)
            open_furnace(episode, furnace)
            smelt_iron(episode, 5)
            result["furnace_position"] = furnace[1:]
        if run("iron_gear"):
            settle(episode)
            ore_before = episode.obs["inv_iron"][1]
            mine_coordinate(episode, 76, 43, -161, None, 274,
                            max_ticks=40, require_pickup=False)
            episode.step()
            navigate(episode, 75.5, -160.5, max_ticks=100,
                     sprint=False, tolerance=0.35)
            settle(episode)
            if episode.obs["inv_iron"][1] <= ore_before:
                raise RuntimeError("high cave iron did not enter inventory")
            furnace = [61, 74, 40, -162]
            open_furnace(episode, furnace)
            smelt_iron(episode, 1)
            craft_opening_items(episode)
            ensure_hotbar(episode, 58)
            table = place_table_on(episode, 75, 39, -162)
            open_table(episode, table)
            craft_sticks(episode)
            craft_iron_route_gear(episode)
            result["route_table"] = table[1:]
        if run("cave_exit"):
            navigate(episode, 73.5, -160.5, max_ticks=180,
                     sprint=False, tolerance=0.25)
            navigate(episode, 73.5, -179.5, max_ticks=500,
                     sprint=False, tolerance=0.25)
            ensure_hotbar(episode, 257)
            mine_coordinate(episode, 72, 40, -180, 4, 257,
                            max_ticks=120, require_pickup=True)
            navigate(episode, 72.5, -179.5, max_ticks=100,
                     sprint=False, tolerance=0.18)
            pillar_up(episode, 66.0)
            result["cave_exit"] = {key: episode.obs[key]
                                     for key in ("x", "y", "z")}
        if run("water_bucket"):
            navigate(episode, 69.5, -165.5, max_ticks=240,
                     sprint=True, tolerance=1.1)
            pickup_fluid(episode, 68, 66, -164, 326)
            hunt_passive(episode)
            eat_available_meat(episode)
            result["water_bucket"] = 326 in episode.obs["inventory_ids"]
        if run("lava_pool"):
            # Simplified surface-height A* route to the video landmark.  The
            # lava is the ravine cluster at x=56,z=42, not the sealed cluster
            # farther east at x=118,z=42.
            for x, z in ((69.5, -145.5), (69.5, -125.5),
                         (68.5, -106.5), (68.5, -100.5),
                         (66.5, -100.5), (65.5, -99.5), (65.5, -89.5),
                         (65.5, -69.5), (65.5, -67.5), (63.5, -67.5),
                         (63.5, -66.5), (59.5, -66.5), (59.5, -64.5),
                         (57.5, -64.5), (57.5, -63.5), (55.5, -63.5),
                         (55.5, -39.5), (55.5, -19.5), (55.5, 1.5),
                         (55.5, 21.5), (55.5, 40.5)):
                navigate(episode, x, z, max_ticks=500,
                         sprint=True, tolerance=0.55)
            result["lava_pool"] = {key: episode.obs[key]
                                    for key in ("x", "y", "z")}
        if run("portal_backing"):
            while item_count(episode.obs, 12) < 50:
                mine_block_item(episode, 12, 12, 257, max_ticks=100)
            for x, z in ((55.5, 40.5), (55.5, 35.5), (52.5, 31.5)):
                navigate(episode, x, z, max_ticks=180,
                         sprint=False, tolerance=0.4)
            place_fluid_on(episode, 52, 64, 31, 326)
            for x in range(50, 55):
                navigate(episode, x + 0.5, 28.5, max_ticks=220,
                         sprint=False, tolerance=0.3)
                pillar_up(episode, 71.0, item_id=12)
                navigate(episode, 52.5, 31.5, max_ticks=240,
                         sprint=False, tolerance=0.35)
                settle(episode)
            pickup_fluid(episode, 52, 65, 31, 326)
            navigate(episode, 54.5, 30.5, max_ticks=180,
                     sprint=False, tolerance=0.3)
            pillar_up(episode, 69.0, item_id=4)
        if run("water_curtain"):
            # The working pillar itself is a stable support. Water placed on
            # its top spreads north into the z=29 casting plane and down over
            # the lower mold; this does not depend on the gravity-block wall
            # surviving a chunk boundary.
            place_fluid_on(episode, 54, 67, 30, 326)
            result["portal_backing"] = {"x0": 50, "x1": 54,
                                         "y0": 65, "y1": 69, "z": 28}
        if run("lava_access"):
            for x, z in ((50.5, 40.5), (48.5, 39.5), (50.5, 39.5),
                         (52.5, 39.5), (54.5, 39.5), (55.5, 40.5),
                         (56.5, 41.5)):
                navigate(episode, x, z, max_ticks=180,
                         sprint=False, tolerance=0.4)
                settle(episode)
            result["lava_access"] = {key: episode.obs[key]
                                      for key in ("x", "y", "z")}
        if run("lava_descent"):
            descend_shaft(episode, 54.0)
            result["lava_descent"] = {key: episode.obs[key]
                                       for key in ("x", "y", "z")}
        if run("portal_one"):
            pickup_fluid(episode, 56, 53, 42, 327)
            pillar_up(episode, 66.0, item_id=12)
            for x, z in ((55.5, 40.5), (55.5, 35.5), (52.5, 31.5)):
                navigate(episode, x, z, max_ticks=220,
                         sprint=False, tolerance=0.45)
                settle(episode)
            place_fluid_on(episode, 51, 64, 29, 327)
            for _ in range(12):
                episode.step()
            if not any(b[0] == 49 and b[1:] == [51, 65, 29]
                       for b in episode.obs["blocks"]):
                raise RuntimeError("first portal cast did not make obsidian")
            result["portal_blocks_cast"] = 1
        if run("portal_daylight"):
            if episode.obs["food"] < 18:
                cook_route_mutton(episode)
                wait_for_daylight(episode)
            result["daylight_prep"] = {
                "health": episode.obs["health"], "food": episode.obs["food"]}
        if run("portal_sides"):
            sources = [
                (56, 53, 42), (57, 53, 42), (56, 52, 42),
                (56, 52, 43), (56, 52, 44), (56, 51, 44),
                (56, 51, 45), (56, 50, 45), (56, 50, 46),
                (56, 50, 47), (56, 49, 47), (55, 49, 48),
                (56, 49, 48), (57, 49, 48), (55, 49, 49),
                (56, 49, 49), (57, 49, 49), (56, 49, 50),
            ]
            east_sources = [
                (117, 59, 43), (118, 59, 42), (118, 59, 43),
                (118, 59, 44), (119, 59, 42), (119, 59, 43),
                (119, 59, 44), (119, 59, 45), (120, 59, 42),
                (120, 59, 43), (120, 59, 44), (120, 59, 45),
                (121, 59, 43), (121, 59, 44), (121, 59, 45),
                (123, 59, 44), (123, 59, 45), (123, 59, 46),
            ]
            targets = [
                (52, 65, 29), (50, 65, 29), (53, 65, 29),
                (50, 66, 29), (53, 66, 29), (50, 67, 29),
                (53, 67, 29), (50, 68, 29), (53, 68, 29),
            ]
            for index, target in enumerate(targets):
                if index < args.portal_side_start:
                    continue
                if not (args.skip_portal_food
                        and index == args.portal_side_start):
                    recover_portal_food(episode)
                if args.save_root:
                    episode.step(save_slot=f"portal-side-{index + 1}-food")
                if nearby_mob(episode, (4,), radius=8.0) is not None:
                    navigate(episode, 60.5, 40.5, max_ticks=120,
                             sprint=True, tolerance=0.8, arrival_idle=0,
                             swim=True)
                    kill_nearby_creepers(episode, radius=24.0)
                if index < 2:
                    portal_lava_trip(
                        episode, sources, target, 52.0 - index)
                else:
                    if index >= 3:
                        prime_portal_cast_with_water(
                            episode, target, prefer_curtain=index == 3)
                    east_pool_lava_trip(
                        episode, east_sources, target, index == 2,
                        safe_exit=index >= 4)
                if args.save_root:
                    episode.step(save_slot=f"portal-side-{index + 1}")
                print(json.dumps({"portal_side_cast": index + 1,
                                  "tick": len(episode.rows)}), flush=True)
            result["portal_blocks_cast"] = 10
        if args.save_slot:
            episode.step(save_slot=args.save_slot)
        result["ticks"] = len(episode.rows)
        result["end"] = {key: episode.obs[key]
                         for key in ("x", "y", "z", "yaw", "pitch")}
        result["logs"] = episode.obs["inv_counts"][0]
        result["planks"] = episode.obs["inv_counts"][1]
        result["table"] = episode.obs["inv_counts"][4]
        result["wooden_pick"] = episode.obs["inv_counts"][5]
        result["cobblestone"] = episode.obs["inv_counts"][3]
        result["stone_pick"] = episode.obs["inv_counts"][6]
        result["visible_iron"] = [b[1:] for b in episode.obs["blocks"]
                                  if b[0] == 15][:8]
        result["iron_ore"] = episode.obs["inv_iron"][1]
        result["iron_ingots"] = episode.obs["inv_iron"][2]
        result["iron_pick"] = 257 in episode.obs["inventory_ids"]
        result["bucket"] = 325 in episode.obs["inventory_ids"]
        result["water_bucket_item"] = 326 in episode.obs["inventory_ids"]
        result["sand"] = item_count(episode.obs, 12)
        result["health"] = episode.obs["health"]
        result["food"] = episode.obs["food"]
        result["dimension"] = episode.obs["dimension"]
        result["flint_and_steel"] = 259 in episode.obs["inventory_ids"]
        result["visible_stone"] = [b[1:] for b in episode.obs["blocks"]
                                   if b[0] == 1][:8]
        result["nearby_lava"] = [b[1:] for b in episode.obs["blocks"]
                                  if b[0] in (10, 11)][:16]
        result["nearby_obsidian"] = [b[1:] for b in episode.obs["blocks"]
                                      if b[0] == 49][:16]
        result["camera_lava_pixels"] = sum(
            value in (10, 11) for value in episode.obs["cam"])
        result["nearby_mobs"] = episode.obs["mobs"][:16]
        write_jsonl(args.out, episode.rows)
        print(json.dumps(result, sort_keys=True))
    finally:
        write_jsonl(args.out, episode.rows)
        episode.close()


if __name__ == "__main__":
    main()
