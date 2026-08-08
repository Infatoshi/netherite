#!/usr/bin/env python3
"""Resumable video-conditioned inverse-control search for magma.

This is deliberately policy-free: it derives camera proposals from the video,
expands ordinary-input macros, rolls every candidate through the real simulator,
and commits only a replayed winner.  No pose, inventory, world, or RNG state is
patched into the recovered tape.
"""
import argparse
import concurrent.futures
import dataclasses
import functools
import hashlib
import json
import math
import os
import pathlib
import random
import subprocess
import time
import uuid

import numpy as np


ACTION_KEYS = (
    "forward", "strafe", "dyaw", "dpitch", "jump", "sneak", "sprint",
    "attack", "use", "do_break", "do_place", "close_container", "hotbar",
    "death_click", "death_button", "inv_slot", "inv_button", "inv_type",
    "open_inventory",
)
CAM_W = 64
CAM_H = 36
SCHEMA = 1


@dataclasses.dataclass(frozen=True)
class Primitive:
    forward: int = 0
    strafe: int = 0
    sprint: int = 0
    jump_period: int = 0
    sneak: int = 0
    attack: int = 0
    use: int = 0
    hotbar: int = -1
    yaw_total: float = 0.0
    pitch_total: float = 0.0
    open_inventory: int = 0
    close_container: int = 0
    inv_slot: int = -10000
    inv_button: int = 0
    inv_type: int = 0
    inv_sequence: tuple = ()

    def actions(self, ticks):
        rows = []
        scheduled = {}
        if self.inv_sequence:
            count = len(self.inv_sequence)
            for index, click in enumerate(self.inv_sequence):
                scheduled[min(ticks - 1, (index * ticks) // count)] = click
        for tick in range(ticks):
            jump = int(self.jump_period > 0 and tick % self.jump_period == 0)
            row = {
                "forward": self.forward, "strafe": self.strafe,
                "dyaw": self.yaw_total / ticks,
                "dpitch": self.pitch_total / ticks,
                "jump": jump, "sneak": self.sneak,
                "sprint": self.sprint, "attack": self.attack,
                "use": int(self.use == 2 or (self.use == 1 and tick == 0)),
                "do_break": self.attack,
                "do_place": int(self.use > 0), "hotbar": self.hotbar,
                "open_inventory": int(self.open_inventory and tick == 0),
                "close_container": int(self.close_container and tick == 0),
            }
            if tick in scheduled:
                slot, button, click_type = scheduled[tick]
                row.update({"inv_slot": slot, "inv_button": button,
                            "inv_type": click_type})
            elif self.inv_slot != -10000 and tick == 0:
                row.update({"inv_slot": self.inv_slot,
                            "inv_button": self.inv_button,
                            "inv_type": self.inv_type})
            rows.append(row)
        return rows


def _dilate(value):
    padded = np.pad(value, 1)
    out = np.zeros_like(value, dtype=bool)
    for dy in range(3):
        for dx in range(3):
            out |= padded[dy:dy + value.shape[0],
                          dx:dx + value.shape[1]]
    return out


def chamfer_cost(source_edge, source_mask, candidate_edge, radius=5):
    """Symmetric bounded chamfer distance in [0,1], robust to textures."""
    source = np.asarray(source_edge, dtype=bool) & np.asarray(source_mask,
                                                               dtype=bool)
    candidate = np.asarray(candidate_edge, dtype=bool) & source_mask.astype(bool)
    if not source.any() or not candidate.any():
        return 1.0

    def directed(points, target):
        remaining = points.copy()
        reached = target.copy()
        total = np.zeros(points.shape, dtype=np.float32)
        for distance in range(radius + 1):
            hit = remaining & reached
            total[hit] = distance
            remaining &= ~reached
            if not remaining.any():
                break
            reached = _dilate(reached)
        total[remaining] = radius + 1
        return float(total[points].mean() / (radius + 1))

    return 0.5 * (directed(source, candidate) +
                  directed(candidate, source))


def state_hash(obs):
    keep = {key: obs.get(key) for key in (
        "t", "world_time", "x", "y", "z", "yaw", "pitch", "dead",
        "dimension", "health", "food", "inventory_ids", "inventory_counts",
        "mobs", "items", "projectiles", "portal_time", "portal_cooldown",
        "gui", "container", "cursor", "craft_grid", "craft_result",
    )}
    payload = json.dumps(keep, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(payload.encode()).hexdigest()


class FeatureTape:
    def __init__(self, path):
        values = np.load(path)
        self.fps = float(values["fps"][0])
        self.gray = values["gray"]
        self.rgb = values["rgb"] if "rgb" in values.files else None
        self.edges = values["edges"]
        self.masks = values["masks"]
        self.shifts = values["shifts"]
        # Recompute the clock mask from source pixels so old feature caches do
        # not confuse bright, featureless chunk-fade sky with a paused loading
        # screen. Dark low-variance frames are the actual 1.11.2 load screen.
        mean = self.gray.mean(axis=(1, 2))
        spread = self.gray.std(axis=(1, 2))
        texture = ((self.edges & self.masks).sum(axis=(1, 2)) /
                   np.maximum(self.masks.sum(axis=(1, 2)), 1))
        self.loading = ((mean < 45) & (spread < 35) &
                        (texture < 0.015)).astype(np.uint8)
        self.usable = values["usable"]
        self.third_person = (values["third_person"]
                             if "third_person" in values.files else
                             np.zeros(len(self.gray), dtype=np.uint8))
        self.attack_hint = (values["attack_hint"]
                            if "attack_hint" in values.files else
                            np.zeros(len(self.gray), dtype=np.uint8))
        self.gui = (values["gui"] if "gui" in values.files else
                    np.zeros(len(self.gray), dtype=np.uint8))
        # The avatar can be briefly hidden by tall grass or turn edge-on. Fill
        # short detector gaps between confident F5 frames; an actual camera
        # toggle produces a much longer first-person span.
        hits = np.flatnonzero(self.third_person)
        max_gap = max(1, round(self.fps))
        for left, right in zip(hits, hits[1:]):
            if right - left <= max_gap:
                self.third_person[left:right + 1] = 1

    def frame_for_tick(self, start_frame, tick):
        wanted = max(1, round(tick * self.fps / 20.0))
        frame = start_frame
        running = 0
        while frame < len(self.edges) - 1 and running < wanted:
            frame += 1
            if not self.loading[frame]:
                running += 1
        return frame

    def motion(self, begin, end):
        if end <= begin + 1:
            return 0.0, 0.0
        shift = self.shifts[begin + 1:end + 1].astype(np.float32).sum(axis=0)
        # Phase correlation reports the translation that aligns the new image
        # to the old one.  Convert pixels to a camera proposal using the RL
        # camera's 70-degree horizontal FOV.  Both signs are explored later.
        pitch = float(shift[0]) * 70.0 / CAM_W
        yaw = float(shift[1]) * 70.0 / CAM_W
        return yaw, pitch


class Magma:
    def __init__(self, args, spawn_offset, slot=None, fov=None):
        self.args = args
        self.spawn_offset = spawn_offset
        self.slot = slot
        self.fov = float(fov if fov is not None else getattr(args, "fov", 70.0))

    def command(self, frame_path=None, frame_offset=0, third_person=False):
        x, z = self.spawn_offset
        command = [
            str(self.args.game), "--seed", str(self.args.seed),
            "--world", "default", "--view-distance",
            str(self.args.view_distance), "--mobs", "on", "--rl",
            "--set", "vanilla_spawn=1", "--set", f"spawn_offset_x={x}",
            "--set", f"spawn_offset_z={z}",
            "--set", f"fov_setting={self.fov}",
            "--set", f"third_person_camera={int(third_person)}",
        ]
        if frame_path is not None:
            command.extend([
                "--width", "320", "--height", "180",
                "--frames-out", str(frame_path), "--frame-every",
                str(frame_offset + 1), "--frame-offset", str(frame_offset),
            ])
        return command

    def rollout(self, actions, save_slot=None, render=False,
                third_person=False):
        env = os.environ.copy()
        env["MAGMA_NATIVE_WORLD_ROOT"] = str(self.args.checkpoints)
        if self.slot:
            env["MAGMA_RL_LOAD_SLOT"] = self.slot
        wire = []
        for index, row in enumerate(actions):
            value = dict(row)
            value["cam"] = int(index + 1 == len(actions))
            if save_slot and index + 1 == len(actions):
                value["save_slot"] = save_slot
            wire.append(json.dumps(value, separators=(",", ":")))
        frame_path = None
        if render:
            self.args.render_scratch.mkdir(parents=True, exist_ok=True)
            frame_path = self.args.render_scratch / f"rollout-{uuid.uuid4().hex}.npy"
        proc = subprocess.run(
            self.command(frame_path, len(actions) - 1, third_person),
            input="\n".join(wire) + "\n", text=True,
            capture_output=True, env=env)
        if proc.returncode:
            if frame_path is not None and frame_path.exists():
                frame_path.unlink()
            return {"error": proc.stderr.strip(), "returncode": proc.returncode}
        lines = proc.stdout.splitlines()
        if len(lines) != len(actions) + 1:
            return {"error": f"expected {len(actions) + 1} observations, "
                              f"got {len(lines)}", "returncode": -1}
        try:
            initial = json.loads(lines[0])
            final = json.loads(lines[-1])
        except json.JSONDecodeError as exc:
            return {"error": str(exc), "returncode": -1}
        result = {"initial": initial, "final": final,
                  "stderr": proc.stderr.strip()}
        if frame_path is not None:
            try:
                frames = np.load(frame_path)
                if frames.shape != (1, 180, 320, 3):
                    raise ValueError(f"unexpected frame shape {frames.shape}")
                # Exact 5x area reduction to the observation-camera shape.
                rgb = frames[0].astype(np.uint32).reshape(
                    CAM_H, 5, CAM_W, 5, 3).mean(axis=(1, 3))
                result["render_rgb"] = np.rint(rgb).astype(np.uint8)
                result["render_gray"] = np.rint(
                    rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 +
                    rgb[..., 2] * 0.114).astype(np.uint8)
            except (OSError, ValueError) as exc:
                result["render_error"] = str(exc)
            finally:
                if frame_path.exists():
                    frame_path.unlink()
        return result


def primitive_candidates(features, begin_frame, end_frame, count, seed,
                         hotbar=-1):
    proposed_yaw, proposed_pitch = features.motion(begin_frame, end_frame)
    rng = random.Random(seed)
    movement_fixed = [
        Primitive(), Primitive(forward=1), Primitive(forward=1, sprint=1),
        Primitive(forward=1, sprint=1, jump_period=10),
        Primitive(yaw_total=proposed_yaw, pitch_total=proposed_pitch),
        Primitive(yaw_total=-proposed_yaw, pitch_total=-proposed_pitch),
        Primitive(forward=1, sprint=1, yaw_total=proposed_yaw,
                  pitch_total=proposed_pitch),
        Primitive(forward=1, sprint=1, yaw_total=-proposed_yaw,
                  pitch_total=-proposed_pitch),
        Primitive(attack=1), Primitive(use=1), Primitive(use=2),
        Primitive(forward=-1), Primitive(strafe=-1), Primitive(strafe=1),
    ]
    # During a detected hand-action span, spend the fixed proposal budget on
    # a deterministic crosshair stencil.  Each candidate is still an ordinary
    # mouse+attack tape and is accepted only after a real engine rollout.
    attack_fixed = [Primitive(attack=1)]
    for yaw, pitch in ((-10, 0), (-5, 0), (-2, 0), (2, 0), (5, 0), (10, 0),
                       (0, -8), (0, -4), (0, 4), (0, 8),
                       (-5, -4), (-5, 4), (5, -4), (5, 4)):
        attack_fixed.append(Primitive(
            attack=1, yaw_total=float(yaw), pitch_total=float(pitch)))
    hints = getattr(features, "attack_hint", ())
    hinted = len(hints) > end_frame and bool(hints[end_frame])
    fixed = attack_fixed + movement_fixed if hinted else movement_fixed
    values = list(fixed)
    movement = [(0, 0), (1, 0), (-1, 0), (0, -1), (0, 1),
                (1, -1), (1, 1)]
    while len(values) < count:
        forward, strafe = rng.choice(movement)
        sign = rng.choice((-1.0, 1.0))
        yaw = sign * proposed_yaw * rng.choice((0.65, 0.85, 1.0, 1.2))
        yaw += rng.choice((-10.0, -5.0, -2.0, 0.0, 2.0, 5.0, 10.0))
        pitch = sign * proposed_pitch * rng.choice((0.65, 1.0, 1.2))
        pitch += rng.choice((-4.0, 0.0, 4.0))
        interaction = rng.randrange(12)
        values.append(Primitive(
            forward=forward, strafe=strafe,
            sprint=int(forward > 0 and rng.random() < 0.65),
            jump_period=rng.choice((0, 0, 0, 10, 12)),
            sneak=int(rng.random() < 0.04),
            attack=int(interaction in (0, 1)),
            use=1 if interaction == 2 else 2 if interaction == 3 else 0,
            hotbar=hotbar if rng.random() < 0.75 else rng.randrange(9),
            yaw_total=yaw, pitch_total=pitch))
    # Preserve order while removing exact duplicates from low-motion spans.
    return list(dict.fromkeys(values))[:count]


def targeted_attack_primitives(obs, limit=8):
    """Propose ordinary mouse+attack inputs toward nearby observed blocks."""
    if not obs or limit <= 0:
        return []
    x = float(obs.get("x", 0.0))
    y = float(obs.get("y", 0.0)) + 1.62
    z = float(obs.get("z", 0.0))
    yaw0 = float(obs.get("yaw", 0.0))
    pitch0 = float(obs.get("pitch", 0.0))
    targets = []
    seen = set()
    # Dedicated resource arrays survive truncation of the generic surface
    # list. Video scoring still chooses the target; this only densifies aim.
    for key, rows in (("logs", obs.get("logs", ())),
                      ("coal", obs.get("coal", ())),
                      ("blocks", obs.get("blocks", ()))):
        for row in rows:
            if key == "blocks":
                if len(row) < 4 or not int(row[0]):
                    continue
                bx, by, bz = map(int, row[1:4])
            else:
                if len(row) < 3:
                    continue
                bx, by, bz = map(int, row[:3])
            identity = (bx, by, bz)
            if identity in seen:
                continue
            seen.add(identity)
            dx, dy, dz = bx + 0.5 - x, by + 0.5 - y, bz + 0.5 - z
            distance = math.sqrt(dx * dx + dy * dy + dz * dz)
            if distance > 5.0 or distance < 0.05:
                continue
            target_yaw = math.degrees(math.atan2(-dx, dz))
            target_pitch = -math.degrees(math.atan2(
                dy, math.sqrt(dx * dx + dz * dz)))
            dyaw = (target_yaw - yaw0 + 180.0) % 360.0 - 180.0
            dpitch = max(-90.0, min(90.0, target_pitch)) - pitch0
            targets.append((key != "logs", distance,
                            abs(dyaw) + abs(dpitch),
                            Primitive(attack=1, yaw_total=dyaw,
                                      pitch_total=dpitch)))
    targets.sort(key=lambda value: value[:3])
    return list(dict.fromkeys(value[3] for value in targets))[:limit]


@functools.lru_cache(maxsize=1)
def _connected_recipe_shapes():
    """Normalized connected 3x3 layouts, ordered from simple to complex."""
    shapes = set()
    for mask in range(1, 1 << 9):
        cells = [(index % 3, index // 3) for index in range(9)
                 if mask & (1 << index)]
        if len(cells) > 5:
            continue
        pending = {cells[0]}
        reached = set()
        while pending:
            cell = pending.pop()
            if cell in reached:
                continue
            reached.add(cell)
            x, y = cell
            pending.update(other for other in cells
                           if abs(other[0] - x) + abs(other[1] - y) == 1)
        if len(reached) != len(cells):
            continue
        min_x = min(x for x, _ in cells)
        min_y = min(y for _, y in cells)
        shapes.add(tuple(sorted((x - min_x, y - min_y) for x, y in cells)))
    return tuple(sorted(shapes, key=lambda shape: (
        len(shape),
        (max(x for x, _ in shape) + 1) * (max(y for _, y in shape) + 1),
        shape)))


def _two_ingredient_recipe_macros(obs):
    """Enumerate generic two-stack layouts using only ordinary GUI clicks."""
    occupied = [index for index, amount in enumerate(
                obs.get("inventory_counts", ())) if amount]
    counts = obs.get("inventory_counts", ())
    values = []
    for left_index, left in enumerate(occupied):
        for right in occupied[left_index + 1:]:
            for shape in _connected_recipe_shapes():
                if len(shape) < 2:
                    continue
                for assignment in range(1, (1 << len(shape)) - 1):
                    left_cells = [cell for index, cell in enumerate(shape)
                                  if assignment & (1 << index)]
                    right_cells = [cell for index, cell in enumerate(shape)
                                   if not assignment & (1 << index)]
                    if (len(left_cells) > counts[left] or
                            len(right_cells) > counts[right]):
                        continue
                    sequence = [(left, 0, 0)]
                    sequence.extend((36 + y * 3 + x, 1, 0)
                                    for x, y in left_cells)
                    if counts[left] > len(left_cells):
                        sequence.append((left, 0, 0))
                    sequence.append((right, 0, 0))
                    sequence.extend((36 + y * 3 + x, 1, 0)
                                    for x, y in right_cells)
                    if counts[right] > len(right_cells):
                        sequence.append((right, 0, 0))
                    sequence.append((45, 0, 1))
                    values.append(Primitive(inv_sequence=tuple(sequence)))
    return list(dict.fromkeys(values))


def gui_primitives(parent_index, count, opening=False, closing=False, obs=None,
                   phase=0):
    """Deterministically shard ordinary inventory clicks across beam parents."""
    obs = obs or {}
    fixed = [Primitive()]
    if opening:
        # A GUI appearing in video may come from the inventory key or from
        # using a world container. Let engine rollouts and pixels distinguish
        # them; both remain ordinary input paths.
        fixed[0:0] = [Primitive(open_inventory=1), Primitive(use=1),
                      Primitive(use=2)]
        # Detected menu boundaries are approximate. Preserve an engine-aimed
        # completion path for a world interaction that spans the first menu
        # frame; once a GUI opens, attack inputs naturally stop affecting the
        # world. This avoids baking task-specific timing into the extractor.
        fixed.extend(targeted_attack_primitives(obs, max(2, count // 4)))
    if closing:
        fixed.insert(0, Primitive(close_container=1))
    occupied_slots = [index for index, amount in enumerate(
                      obs.get("inventory_counts", ())) if amount]
    empty_slots = [index for index, amount in enumerate(
                   obs.get("inventory_counts", ())) if not amount]
    player_grid = (36, 37, 39, 40)
    if obs.get("container") == 1:
        # Rotate a bounded slice of the generic 3x3, two-ingredient layout
        # space across parents and generations. Engine recipe output is the
        # only success signal; no recipe or item identity is encoded here.
        recipes = _two_ingredient_recipe_macros(obs)
        if recipes:
            budget = max(1, count // 3)
            start = (phase * budget + parent_index * budget) % len(recipes)
            sample = [recipes[(start + offset) % len(recipes)]
                      for offset in range(min(budget, len(recipes)))]
            fixed[1:1] = sample
    if opening:
        # World containers often require selecting the carried hotbar block,
        # placing it, then reusing after stepping clear. Held-use naturally
        # retries on vanilla's four-tick right-click timer, so these remain
        # compact ordinary-input macros rather than engine interactions.
        for slot in occupied_slots:
            if slot >= 9:
                continue
            fixed.extend((
                Primitive(use=2, hotbar=slot),
                Primitive(forward=-1, use=2, hotbar=slot),
                Primitive(strafe=-1, use=2, hotbar=slot),
                Primitive(strafe=1, use=2, hotbar=slot),
            ))
    # High-frequency, generic GUI probes composed only of ordinary clicks.
    # The engine result and source pixels decide whether a sequence matters.
    for slot in occupied_slots:
        amount = obs.get("inventory_counts", ())[slot]
        for shape in ((36,), (36, 37), (36, 39), player_grid):
            if amount < len(shape):
                continue
            sequence = [(slot, 0, 0)]
            sequence.extend((grid_slot, 1, 0) for grid_slot in shape)
            if amount > len(shape):
                sequence.append((slot, 0, 0))
            # Return the unused ingredients before touching the output. A
            # non-empty cursor cannot accept a different recipe result.
            # QUICK_MOVE then transfers the validated result without needing
            # an item-specific destination slot.
            sequence.append((45, 0, 1))
            fixed.append(Primitive(inv_sequence=tuple(sequence)))
    cursor = obs.get("cursor", ())
    if len(cursor) > 1 and cursor[1] > 0:
        for shape in ((36,), (36, 37), (36, 39), player_grid):
            if cursor[1] < len(shape):
                continue
            sequence = [(grid_slot, 1, 0) for grid_slot in shape]
            if cursor[1] > len(shape) and empty_slots:
                sequence.append((empty_slots[0], 0, 0))
            sequence.append((45, 0, 1))
            fixed.append(Primitive(inv_sequence=tuple(sequence)))
    result = obs.get("craft_result", ())
    if len(result) > 1 and result[1] > 0:
        for slot in empty_slots[:4]:
            fixed.append(Primitive(inv_sequence=((45, 0, 0),
                                                  (slot, 0, 0))))
    # State-aware clicks make every carried item and craft result reachable
    # from every parent; the remaining slots are still globally sharded.
    for slot in occupied_slots + [45]:
        for click_type, button in ((0, 0), (0, 1), (1, 0)):
            fixed.append(Primitive(inv_slot=slot, inv_button=button,
                                   inv_type=click_type))
    if len(cursor) > 1 and cursor[1] > 0:
        for slot in range(36, 45):
            fixed.append(Primitive(inv_slot=slot))
    # Player inventory, 2x2 grid/result, armor, left/right and shift-click.
    space = []
    for click_type in (0, 1):
        for button in (0, 1):
            for slot in list(range(46)) + list(range(49, 53)):
                space.append(Primitive(inv_slot=slot, inv_button=button,
                                       inv_type=click_type))
    fixed = list(dict.fromkeys(fixed))
    start = parent_index * max(1, count - len(fixed))
    for offset in range(max(0, count - len(fixed))):
        fixed.append(space[(start + offset) % len(space)])
    return list(dict.fromkeys(fixed))[:count]


def _gray_edges(gray):
    value = gray.astype(np.uint32)
    padded = np.pad(value, 2, mode="edge")
    integral = np.pad(padded, ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    smooth = ((integral[5:, 5:] - integral[:-5, 5:] -
               integral[5:, :-5] + integral[:-5, :-5]) // 25).astype(np.int16)
    gx = np.zeros_like(smooth)
    gy = np.zeros_like(smooth)
    gx[:, 1:-1] = smooth[:, 2:] - smooth[:, :-2]
    gy[1:-1, :] = smooth[2:, :] - smooth[:-2, :]
    return (np.abs(gx) + np.abs(gy)) >= 22


def score_rollout(result, target_gray, target_edge, target_mask, primitive,
                  third_person=False, target_rgb=None):
    if "error" in result:
        return 1e6, {"process": 1.0, "message": result["error"][-300:]}
    obs = result["final"]
    if obs.get("dead"):
        return 1e5, {"dead": 1.0}
    rendered = result.get("render_gray")
    if rendered is not None:
        candidate = _gray_edges(rendered)
        edge_cost = chamfer_cost(target_edge, target_mask, candidate)
        valid = target_mask.astype(bool)
        source_value = target_gray[valid].astype(np.float32)
        candidate_value = rendered[valid].astype(np.float32)
        source_value = ((source_value - source_value.mean()) /
                        max(source_value.std(), 8.0))
        candidate_value = ((candidate_value - candidate_value.mean()) /
                           max(candidate_value.std(), 8.0))
        tone_cost = float(np.minimum(
            np.abs(source_value - candidate_value) / 3.0, 1.0).mean())
        # Keep world overlays out of structural matching, but explicitly score
        # the center-bottom HUD.  This is how ordinary world outcomes become
        # observable to the inverse search: a collected log, crafted tool,
        # selected slot, health loss, or opened GUI changes these pixels.
        hud_source = target_gray[CAM_H - 7:, 12:52].astype(np.float32)
        hud_candidate = rendered[CAM_H - 7:, 12:52].astype(np.float32)
        hud_cost = float(np.abs(hud_source - hud_candidate).mean() / 255.0)
        hand_source = target_gray[18:, 44:].astype(np.float32)
        hand_candidate = rendered[18:, 44:].astype(np.float32)
        hand_cost = float(np.abs(hand_source - hand_candidate).mean() / 255.0)
        color_cost = 1.0
        if target_rgb is not None and result.get("render_rgb") is not None:
            source_rgb = np.asarray(target_rgb, dtype=np.float32)[valid]
            candidate_rgb = result["render_rgb"].astype(np.float32)[valid]
            # The video codec and UI overlays prevent literal pixel equality,
            # but vanilla assets/light still make spatial RGB much more
            # discriminative than edge density alone. Scale the raw residual
            # so a dark leaf wall cannot masquerade as an open grassy field.
            color_cost = float(min(
                np.abs(source_rgb - candidate_rgb).mean() / 102.0, 1.0))
        if third_person:
            # Magma currently renders first-person only. The world background
            # and HUD still constrain a third-person source frame, but the
            # absent avatar/hand must not pull the simulated player toward the
            # source camera's four-block offset.
            visual = (0.40 * edge_cost + 0.20 * tone_cost +
                      0.35 * color_cost + 0.05 * hud_cost)
        else:
            visual = (0.38 * edge_cost + 0.17 * tone_cost +
                      0.25 * color_cost + 0.11 * hud_cost +
                      0.09 * hand_cost)
    else:
        candidate = np.asarray(obs.get("edge", []), dtype=np.uint8)
        if candidate.size != CAM_W * CAM_H:
            return 1e6, {"camera": 1.0}
        candidate = candidate.reshape(CAM_H, CAM_W)
        edge_cost = chamfer_cost(target_edge, target_mask, candidate)
        tone_cost = 1.0
        hud_cost = 1.0
        hand_cost = 1.0
        color_cost = 1.0
        visual = edge_cost
    simplicity = 0.002 * (
        abs(primitive.forward) + abs(primitive.strafe) + primitive.sprint +
        bool(primitive.jump_period) + primitive.attack + bool(primitive.use))
    damage = max(0.0, 20.0 - float(obs.get("health", 20.0))) / 100.0
    total = visual + simplicity + damage
    return total, {"visual": visual, "edge_chamfer": edge_cost,
                   "tone": tone_cost, "hud": hud_cost,
                   "hand": hand_cost,
                   "color": color_cost,
                   "simplicity": simplicity,
                   "damage": damage,
                   "rendered": rendered is not None}


def write_json(path, value):
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temp.replace(path)


def append_rows(path, rows):
    with path.open("a", encoding="utf-8") as stream:
        for row in rows:
            stream.write(json.dumps(row, sort_keys=True,
                                    separators=(",", ":")) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


def gameplay_start(features):
    # First stable bright run after a loading screen.  The first world frames
    # can be almost featureless sky/fog while chunks arrive; waiting for edge
    # density would skip those real game ticks and often land after an F5
    # camera toggle.  Brightness cleanly separates them from the brown loading
    # screen in 1.11.2 captures.
    run = max(3, round(features.fps * 0.4))
    mean = features.gray.mean(axis=(1, 2))
    for index in range(len(mean) - run):
        recent = features.loading[max(0, index - round(features.fps)):
                                  index]
        if recent.any() and (mean[index:index + run] > 70).all():
            return index
    for index in range(len(features.usable) - run):
        recent = features.loading[max(0, index - round(features.fps)):
                                  index]
        if recent.any() and features.usable[index:index + run].all():
            return index
    hits = np.flatnonzero(features.usable)
    return int(hits[0]) if len(hits) else 0


def bootstrap_world(args, features, state, tape_path, segments_path):
    """Disambiguate spawn fuzz and initial view directly against the video."""
    target = state["video_start_frame"]
    grid = []
    for x in range(-10, -5):
        for z in range(-10, -5):
            for yaw in range(0, 360, 30):
                for pitch in (-25.0, 0.0, 25.0):
                    grid.append(((x, z), Primitive(
                        yaw_total=yaw - 180.0, pitch_total=pitch)))
    rng = random.Random(args.seed ^ 0x51A7)
    rng.shuffle(grid)
    preferred = ((args.spawn_offset_x, args.spawn_offset_z), Primitive())
    grid = [preferred] + grid[:max(0, args.bootstrap_candidates - 1)]

    def evaluate(value):
        spawn, primitive = value
        result = Magma(args, spawn).rollout(
            primitive.actions(1), render=True)
        score, terms = score_rollout(
            result, features.gray[target], features.edges[target],
            features.masks[target], primitive)
        return score, spawn, primitive, result, terms

    with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.workers) as pool:
        ranked = list(pool.map(evaluate, grid))
    ranked.sort(key=lambda value: value[0])
    score, spawn, primitive, _, terms = ranked[0]
    slot = "auto-bootstrap"
    actions = primitive.actions(1)
    committed = Magma(args, spawn).rollout(
        actions, save_slot=slot, render=True)
    replay_score, replay_terms = score_rollout(
        committed, features.gray[target], features.edges[target],
        features.masks[target], primitive)
    if abs(score - replay_score) > 1e-9:
        raise RuntimeError(f"non-deterministic bootstrap: {score} != "
                           f"{replay_score}")
    row = dict(actions[0], tick=0, type="action")
    append_rows(tape_path, [row])
    final = committed["final"]
    digest = state_hash(final)
    append_rows(segments_path, [{
        "schema": SCHEMA, "kind": "bootstrap", "tick_start": 0,
        "tick_end": 1, "video_frame": target,
        "spawn_offset": list(spawn), "score": replay_score,
        "score_terms": replay_terms, "primitive": dataclasses.asdict(primitive),
        "end_hash": digest,
        "end": {key: final.get(key) for key in
                ("x", "y", "z", "yaw", "pitch", "dimension", "health",
                 "food", "dead")},
    }])
    state.update({"tick": 1, "slot": slot, "spawn_offset": list(spawn),
                  "score": replay_score, "state_hash": digest})
    return {"score": replay_score, "spawn_offset": list(spawn),
            "yaw": final["yaw"], "pitch": final["pitch"],
            "terms": terms, "candidates": len(ranked)}


def run_search(args):
    features = FeatureTape(args.features)
    args.checkpoints.mkdir(parents=True, exist_ok=True)
    args.workspace.mkdir(parents=True, exist_ok=True)
    state_path = args.workspace / "search_state.json"
    tape_path = args.workspace / "actions.jsonl"
    segments_path = args.workspace / "segments.jsonl"
    progress_path = args.workspace / "progress.jsonl"

    if state_path.exists() and not args.restart:
        state = json.loads(state_path.read_text())
    else:
        for path in (tape_path, segments_path, progress_path):
            path.write_text("")
        state = {
            "schema": SCHEMA, "segment": 0, "tick": 0,
            "video_start_frame": gameplay_start(features),
            "video_frame": gameplay_start(features), "slot": None,
            "spawn_offset": [args.spawn_offset_x, args.spawn_offset_z],
            "score": None, "status": "searching",
        }
        write_json(state_path, state)

    if state["tick"] == 0 and state["slot"] is None and not args.no_bootstrap:
        report = bootstrap_world(
            args, features, state, tape_path, segments_path)
        write_json(state_path, state)
        print(json.dumps({"bootstrap": report}, sort_keys=True), flush=True)

    started = time.monotonic()
    completed = 0
    while state["video_frame"] < len(features.edges) - 1:
        if args.max_segments and completed >= args.max_segments:
            break
        horizon = args.horizon_ticks
        begin_frame = state["video_frame"]
        end_frame = features.frame_for_tick(begin_frame, horizon)
        if end_frame <= begin_frame:
            end_frame = begin_frame + 1
        primitives = primitive_candidates(
            features, begin_frame, end_frame, args.candidates,
            args.seed ^ state["segment"], hotbar=-1)
        magma = Magma(args, tuple(state["spawn_offset"]), state["slot"])

        def evaluate(primitive):
            result = magma.rollout(primitive.actions(horizon), render=True)
            score, terms = score_rollout(
                result, features.gray[end_frame], features.edges[end_frame],
                features.masks[end_frame], primitive)
            return score, primitive, result, terms

        with concurrent.futures.ThreadPoolExecutor(
                max_workers=args.workers) as pool:
            ranked = list(pool.map(evaluate, primitives))
        ranked.sort(key=lambda value: value[0])
        score, winner, result, terms = ranked[0]
        if score >= 1e5:
            state["status"] = "blocked"
            state["error"] = terms
            write_json(state_path, state)
            raise RuntimeError(f"every rollout failed at segment "
                               f"{state['segment']}: {terms}")

        winner_primitives = [winner]
        actions = winner.actions(horizon)
        selection_score = score
        if args.lookahead_candidates > 0:
            next_end = features.frame_for_tick(end_frame, horizon)
            prefixes = ranked[:min(args.beam_width, len(ranked))]
            expanded = []
            for prefix_index, prefix in enumerate(prefixes):
                second_values = primitive_candidates(
                    features, end_frame, next_end,
                    args.lookahead_candidates,
                    args.seed ^ (state["segment"] << 8) ^ prefix_index)
                for second in second_values:
                    expanded.append((prefix, second))

            def evaluate_pair(value):
                prefix, second = value
                sequence = (prefix[1].actions(horizon) +
                            second.actions(horizon))
                pair_result = magma.rollout(sequence, render=True)
                pair_score, pair_terms = score_rollout(
                    pair_result, features.gray[next_end],
                    features.edges[next_end], features.masks[next_end], second)
                # The first endpoint remains an explicit term so lookahead
                # cannot buy a good future frame with a nonsensical jump now.
                selection = pair_score + 0.25 * prefix[0]
                return (selection, pair_score, prefix[1], second,
                        pair_result, pair_terms)

            with concurrent.futures.ThreadPoolExecutor(
                    max_workers=args.workers) as pool:
                pairs = list(pool.map(evaluate_pair, expanded))
            pairs.sort(key=lambda value: value[0])
            (selection_score, score, first, second, result,
             terms) = pairs[0]
            winner_primitives = [first, second]
            winner = second
            actions = first.actions(horizon) + second.actions(horizon)
            horizon *= 2
            end_frame = next_end

        next_slot = f"auto-{state['segment'] + 1:06d}"
        committed = magma.rollout(actions, save_slot=next_slot, render=True)
        if "error" in committed:
            raise RuntimeError(f"winner replay failed: {committed['error']}")
        replay_score, replay_terms = score_rollout(
            committed, features.gray[end_frame], features.edges[end_frame],
            features.masks[end_frame], winner)
        if abs(replay_score - score) > 1e-9:
            raise RuntimeError(f"non-deterministic winner replay: "
                               f"{score} != {replay_score}")

        tick0 = state["tick"]
        tape_rows = []
        for offset, action in enumerate(actions):
            row = {key: action.get(key, 0) for key in ACTION_KEYS
                   if key in action}
            row.update({"tick": tick0 + offset, "type": "action"})
            tape_rows.append(row)
        append_rows(tape_path, tape_rows)
        final = committed["final"]
        segment_row = {
            "schema": SCHEMA, "kind": "segment",
            "index": state["segment"], "tick_start": tick0,
            "tick_end": tick0 + horizon, "video_frame_start": begin_frame,
            "video_frame_end": end_frame, "score": replay_score,
            "selection_score": selection_score,
            "score_terms": replay_terms,
            "primitives": [dataclasses.asdict(value)
                           for value in winner_primitives],
            "start_hash": state.get("state_hash"),
            "end_hash": state_hash(final), "checkpoint": next_slot,
            "end": {key: final.get(key) for key in
                    ("x", "y", "z", "yaw", "pitch", "dimension", "health",
                     "food", "dead")},
        }
        append_rows(segments_path, [segment_row])
        state.update({
            "segment": state["segment"] + 1,
            "tick": tick0 + horizon, "video_frame": end_frame,
            "slot": next_slot, "score": replay_score,
            "state_hash": segment_row["end_hash"], "status": "searching",
        })
        write_json(state_path, state)
        progress = {
            "segment": state["segment"], "tick": state["tick"],
            "video_s": state["video_frame"] / features.fps,
            "source_pct": 100.0 * state["video_frame"] / len(features.edges),
            "score": replay_score, "candidates": len(ranked),
            "rollouts_per_s": len(ranked) / max(time.monotonic() - started,
                                                1e-9),
            "end": segment_row["end"],
        }
        append_rows(progress_path, [progress])
        print(json.dumps(progress, sort_keys=True), flush=True)
        completed += 1
        started = time.monotonic()

    if state["video_frame"] >= len(features.edges) - 1:
        state["status"] = "source_exhausted"
        write_json(state_path, state)
    return state


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--features", type=pathlib.Path, required=True)
    parser.add_argument("--workspace", type=pathlib.Path, required=True)
    parser.add_argument("--checkpoints", type=pathlib.Path, required=True)
    parser.add_argument("--game", type=pathlib.Path,
                        default=pathlib.Path("magma/magma_game"))
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--spawn-offset-x", type=int, default=-7)
    parser.add_argument("--spawn-offset-z", type=int, default=-10)
    parser.add_argument("--view-distance", type=int, default=2)
    parser.add_argument("--fov", type=float, default=70.0)
    parser.add_argument("--workers", type=int, default=16)
    parser.add_argument("--candidates", type=int, default=96)
    parser.add_argument("--horizon-ticks", type=int, default=20)
    parser.add_argument("--beam-width", type=int, default=4)
    parser.add_argument("--lookahead-candidates", type=int, default=12)
    parser.add_argument("--max-segments", type=int, default=0)
    parser.add_argument("--bootstrap-candidates", type=int, default=360)
    parser.add_argument("--no-bootstrap", action="store_true")
    parser.add_argument("--restart", action="store_true")
    args = parser.parse_args()
    args.game = args.game.resolve()
    args.features = args.features.resolve()
    args.workspace = args.workspace.resolve()
    args.checkpoints = args.checkpoints.resolve()
    args.render_scratch = args.workspace / "render_scratch"
    if (args.workers < 1 or args.candidates < 1 or
            args.horizon_ticks < 1 or args.beam_width < 1 or
            args.lookahead_candidates < 0 or args.bootstrap_candidates < 1):
        parser.error("workers, candidates, and horizon must be positive")
    print(json.dumps(run_search(args), sort_keys=True))


if __name__ == "__main__":
    main()
