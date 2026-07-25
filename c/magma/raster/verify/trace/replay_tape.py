#!/usr/bin/env python3
"""replay_tape.py - replay a human-play tape through magma and find the first
divergence. THE human-verification flywheel step (see c/magma/VERIFY.md).

Input: a JSONL tape recorded by the qrl mod (recstart/recstop) while a human
plays the REAL game over Moonlight: header line (seed/world/pose/time), then one
line per tick with inputs (f/s/jump/sneak/sprint/atk/use/hb), absolute yaw/pitch,
player physics state, nearby entities, and a sparse oracle frame every N ticks.

Replay: the same inputs through magma (set_pose at t0 from the header,
set_look per tick with the recorded absolute rotation, action per tick).
Phase 1 diffs physics per tick and reports the FIRST tick+field over tolerance.
Phase 2 diffs magma frames against the tape's sparse oracle frames.

Usage:
    uv run --no-project --with numpy --with pillow python replay_tape.py TAPE.jsonl
    ... --out DIR (default out/tape_<name>)  --report (write report/tape_<name>.md)
"""
import argparse
from collections import Counter
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_lib as ol  # noqa: E402

# first-divergence tolerances per field: MC physics is double-exact, so any gap
# beyond float noise is a real defect. on_ground/hp/food must match exactly.
TOL = {"x": 1e-9, "y": 1e-9, "z": 1e-9,
       "vx": 1e-9, "vy": 1e-9, "vz": 1e-9,
       "og": 0, "hp": 1e-4, "food": 0, "dim": 0}

# Every tape entity that reaches script.c must either be modeled here or be an
# explicit non-rendering exception. This list intentionally mirrors
# gm_entity_type_for_name plus script.c's EntityItem special case. False
# positives are safer than silently soaking an invisible entity into a pixel
# divergence class: adding a renderer requires adding its tape class here too.
MODELED_ENTITY_TYPES = frozenset({
    "EntityItem", "EntityZombie", "EntityHusk", "EntityZombieVillager",
    "EntityPigZombie", "EntitySkeleton", "EntityStray",
    "EntityWitherSkeleton", "EntityCreeper", "EntitySpider",
    "EntityCaveSpider", "EntityEnderman", "EntityBlaze", "EntitySheep",
    "EntityPig", "EntityCow", "EntityMooshroom", "EntityChicken",
    "EntitySquid", "EntityWitch", "EntityBat", "EntityLlama",
    "EntityGhast", "EntityMagmaCube", "EntityMinecartEmpty",
    "EntityMinecartChest", "EntityMinecartFurnace", "EntityMinecartHopper",
    "EntityMinecartTNT", "EntityDragon", "EntityArrow",
    "EntityTippedArrow", "EntitySpectralArrow", "EntityEnderCrystal",
    "EntityEnderPearl", "EntityEnderEye", "EntitySnowball", "EntityEgg",
    "EntitySmallFireball", "EntityDragonFireball",
    "EntityArmorStand",
    "EntityXPOrb",
})

# RenderAreaEffectCloud itself has no geometry. Its client-side dragon-breath
# particles are RNG-unrecoverable from tape entity rows and are scoped by the
# scenario known-divergence sidecar.
SKIPPED_ENTITY_ALLOWLIST = frozenset({"EntityAreaEffectCloud"})
MISSING_MODEL_ROW_THRESHOLD = 4  # five repeated rows is no longer "a handful"


def skipped_renderable_counts(ticks):
    """Count tape entity rows that magma would silently skip as unmodeled."""
    counts = Counter()
    for row in ticks:
        for ent in row.get("ents", []):
            if len(ent) < 2:
                continue
            typ = ent[1]
            if (typ not in MODELED_ENTITY_TYPES and
                    typ not in SKIPPED_ENTITY_ALLOWLIST):
                counts[typ] += 1
    return dict(sorted(counts.items()))


def apply_missing_model_gate(gate, counts):
    """Fold repeated skipped renderables into the scenario gate (rc=3)."""
    failed = {typ: rows for typ, rows in counts.items()
              if rows > MISSING_MODEL_ROW_THRESHOLD}
    if gate is None:
        gate = {"pass": True, "frames_checked": 0, "classes": {},
                "failed_frames": []}
    gate["missing_models"] = counts
    gate["missing_model_row_threshold"] = MISSING_MODEL_ROW_THRESHOLD
    gate["missing_model_failures"] = failed
    if failed:
        gate["pass"] = False
    return gate


def sgn(v):
    return 1 if v > 1e-6 else (-1 if v < -1e-6 else 0)


# Ghost pushers: recorded oracle entities become ent_box events so magma can
# apply the vanilla applyEntityCollision player push (mobs=off replays have no
# pushers otherwise; found at tick 1471 of the fresh-world tape, sheep push).
# They ALSO become render-only ent_view events (see tape_to_script) so replay
# frames draw the recorded sheep/zombies (OPEN_DIVERGENCES #10).
# canBePushed() is false for items/orbs/arrows and other non-living props.
NONPUSHABLE = {"EntityItem", "EntityXPOrb", "EntityArrow", "EntityTippedArrow",
               "EntitySpectralArrow", "EntityFishHook", "EntityEnderPearl",
               "EntityEnderEye", "EntitySnowball", "EntityEgg", "EntityPotion",
               "EntityExpBottle", "EntityFallingBlock", "EntityTNTPrimed",
               "EntityLightningBolt", "EntityAreaEffectCloud",
               "EntityFireball", "EntitySmallFireball", "EntityWitherSkull",
               "EntityDragonFireball", "EntityShulkerBullet",
               "EntityItemFrame", "EntityPainting", "EntityArmorStand",
               "EntityEnderCrystal", "EntityLeashKnot"}
# vanilla width/height by class (EntityList names as taped)
ENT_SIZE = {"EntitySheep": (0.9, 1.3), "EntityCow": (0.9, 1.4),
            "EntityMooshroom": (0.9, 1.4), "EntityPig": (0.9, 0.9),
            "EntityChicken": (0.4, 0.7), "EntityRabbit": (0.4, 0.5),
            "EntityBat": (0.5, 0.9), "EntityWolf": (0.6, 0.85),
            "EntityOcelot": (0.6, 0.7), "EntityHorse": (1.3964844, 1.6),
            "EntityVillager": (0.6, 1.95), "EntityWitch": (0.6, 1.95),
            "EntityZombie": (0.6, 1.95), "EntityHusk": (0.6, 1.95),
            "EntityPigZombie": (0.6, 1.95), "EntitySkeleton": (0.6, 1.99),
            "EntityStray": (0.6, 1.99), "EntityWitherSkeleton": (0.7, 2.4),
            "EntityCreeper": (0.6, 1.7), "EntitySpider": (1.4, 0.9),
            "EntityCaveSpider": (0.7, 0.5), "EntityEnderman": (0.6, 2.9),
            "EntitySquid": (0.8, 0.8), "EntitySilverfish": (0.4, 0.3),
            "EntityEndermite": (0.4, 0.3), "EntityGuardian": (0.85, 0.85),
            "EntityPolarBear": (1.3, 1.4), "EntityIronGolem": (1.4, 2.7),
            "EntitySnowman": (0.7, 1.9), "EntityBlaze": (0.6, 1.8),
            "EntitySlime": (0.51, 0.51), "EntityMagmaCube": (0.51, 0.51)}


def gui_slot_id(gui, index):
    """Map vanilla Container slot-list indexes to magma's unified slots."""
    if gui == "GuiInventory":
        if index == 0:
            return 45
        if 1 <= index <= 4:
            return (36, 37, 39, 40)[index - 1]
        if 9 <= index <= 35:
            return index
        if 36 <= index <= 44:
            return index - 36
    elif gui == "GuiCrafting":
        if index == 0:
            return 45
        if 1 <= index <= 9:
            return 35 + index
        if 10 <= index <= 36:
            return index - 1
        if 37 <= index <= 45:
            return index - 37
    elif gui == "GuiFurnace":
        if 0 <= index <= 2:
            return 46 + index
        if 3 <= index <= 29:
            return index + 6
        if 30 <= index <= 38:
            return index - 30
    elif gui == "GuiChest":
        # ContainerChest single: 0..26 chest, 27..53 main, 54..62 hotbar
        if 0 <= index <= 26:
            return 53 + index
        if 27 <= index <= 53:
            return index - 27 + 9
        if 54 <= index <= 62:
            return index - 54
    return None


def tape_stack(stack):
    """Return a script stack in item/count/meta order from tape item/meta/count."""
    if stack == 0:
        return {"item": 0, "count": 0, "meta": 0}
    return {"item": int(stack[0]), "count": int(stack[2]),
            "meta": int(stack[1])}


def load_tape(path):
    with open(path) as f:
        lines = [json.loads(ln) for ln in f if ln.strip()]
    if not lines or lines[0].get("header") != 1:
        raise SystemExit(f"{path}: not a tape (missing header line)")
    return lines[0], lines[1:]


def tape_strip_overlays(path):
    """Return the recorded qrl_launch strip.overlays setting, if archived."""
    meta = os.path.splitext(path)[0] + ".meta.json"
    try:
        with open(meta) as f:
            return bool(json.load(f).get("qrl_launch", {}).get("strip", {})
                        .get("overlays", False))
    except (OSError, ValueError, TypeError):
        return False


def tape_texture_animations_pinned(path):
    """Return whether QRL froze atlas sprites on uploaded physical frame zero."""
    meta = os.path.splitext(path)[0] + ".meta.json"
    try:
        with open(meta) as f:
            return bool(json.load(f).get("qrl_launch", {}).get("determinism", {})
                        .get("pin_texture_animations", False))
    except (OSError, ValueError, TypeError):
        return False


def magma_world(header):
    """Map the recorder world name to magma's matching Overworld generator."""
    return "superflat" if str(header.get("world", "")).endswith("_flat") else "default"


def externally_pose_anchored(header, ticks):
    """Detect recorder-driven fixed-pose tapes, never ordinary play.

    The animation fixture calls qrl set_pose before every step.  Its survival
    player is therefore airborne with bit-zero velocity and no input for nearly
    the whole tape, a state vanilla physics cannot sustain independently.  Such
    rows are authoritative post-tick anchors, including an immediate scene-pose
    jump that can precede the recorder's next sparse ppos packet.
    """
    if header.get("gamemode") != "survival" or len(ticks) < 20:
        return False
    pinned = 0
    positions = set()
    for row in ticks:
        i = row.get("in", {})
        zero_input = (abs(float(i.get("f", 0.0))) <= 1e-15
                      and abs(float(i.get("s", 0.0))) <= 1e-15
                      and not any(i.get(k, 0) for k in
                                  ("jump", "sneak", "sprint", "atk", "use")))
        zero_motion = all(abs(float(row.get(k, 0.0))) <= 1e-15
                          for k in ("vx", "vy", "vz"))
        if zero_input and zero_motion and not int(row.get("og", 0)):
            pinned += 1
        positions.add((float(row["x"]), float(row["y"]), float(row["z"])))
    return pinned * 20 >= len(ticks) * 19 and len(positions) <= 8


def snapshot_arrival_events(snapshot_patch, header, ticks, chunk_radius=1):
    """Reload saved blocks around authoritative cross-dimension arrivals.

    The live world store is a bounded toroidal cache.  A whole-session snapshot
    can therefore evict an arrival chunk after its tick-zero delta was loaded.
    Position packets are the server's authoritative world-transfer boundary, so
    ensure and reapply the saved 3x3-chunk neighborhood immediately before that
    arrival is simulated.  The tape START is the same boundary: a multi-dim
    session's tick-zero patch can span more chunks than the pool holds, evicting
    the start area before tick 0 even runs (nether roundtrip, 2026-07-13).
    """
    POOL_R = 8
    arrivals = []
    if ticks:
        # Tape start: reapply the WHOLE pool-radius neighborhood, not just
        # 3x3. A 1000+-chunk multi-dim tick-zero patch floods the pool and
        # evicts staged arenas a few chunks from spawn (163654Z: the build
        # arena floor at chunk (2,9) vanished and the replay fell to death).
        # Nothing is dug before tick 0, so the wide reapply cannot resurrect
        # ghost blocks.
        arrivals.append((int(ticks[0]["t"]), int(header.get("dim", 0)),
                         math.floor(float(header["x"])) // 16,
                         math.floor(float(header["z"])) // 16, POOL_R))
    # Only pool-evicting transfers qualify: a dimension change or a chunk jump
    # beyond the toroidal pool radius. Re-applying on EVERY ppos (aim-pin tps
    # fire one per face_point) resurrects blocks the session already dug -
    # mine tape 120328Z: the t~116 aim-pin re-placed the log chopped at t~57
    # and the replay walked into the ghost block at t190.
    prev_dim = int(header.get("dim", 0))
    prev_cx = math.floor(float(header["x"])) // 16
    prev_cz = math.floor(float(header["z"])) // 16
    for row in ticks:
        if "ppos" in row:
            x, _, z = row["ppos"][:3]
            cx, cz = math.floor(float(x)) // 16, math.floor(float(z)) // 16
            dimension = int(row.get("dim", prev_dim))
            if (dimension != prev_dim or abs(cx - prev_cx) > POOL_R
                    or abs(cz - prev_cz) > POOL_R):
                arrivals.append((int(row["t"]), dimension, cx, cz,
                                 chunk_radius))
        if "x" in row and "z" in row:
            prev_cx = math.floor(float(row["x"])) // 16
            prev_cz = math.floor(float(row["z"])) // 16
        prev_dim = int(row.get("dim", prev_dim))
    if not arrivals:
        return {}

    by_tick = {
        tick: [{"tick": tick, "type": "snapshot_region", "dim": dimension,
                "cx": cx, "cz": cz, "radius": radius}]
        for tick, dimension, cx, cz, radius in arrivals
    }
    with open(snapshot_patch) as sf:
        for line in sf:
            event = json.loads(line)
            if event.get("type") != "snapshot_block":
                continue
            bx, bz = int(event["x"]) // 16, int(event["z"]) // 16
            dimension = int(event.get("dim", 0))
            for tick, target_dim, cx, cz, radius in arrivals:
                if (dimension == target_dim and abs(bx - cx) <= radius
                        and abs(bz - cz) <= radius):
                    by_tick[tick].append({**event, "tick": tick})
    return by_tick


def tape_to_script(header, ticks, script_path, tape_path=None):
    """Emit the magma JSONL event script for this tape.

    Optional sidecar <tape>.worldpatch.jsonl: set_block / set_inventory events
    spliced in at their own "tick" (min 1; tick 0 is overwritten by worldgen)
    to re-anchor state the replay cannot reproduce:
    - world cells whose live-session decoration is populate-order/cascade
      sensitive (OPEN_DIVERGENCES.md #8), and
    - inventory slots filled by unrecorded GUI interactions (container /
      crafting clicks are not taped; OPEN_DIVERGENCES.md #9) so the replay's
      own place/dig logic still runs faithfully from the patched slot.
    Patch values come from the oracle session's save, with in-tape-broken
    blocks restored so the replay's own digs stay faithful."""
    patch = []
    animations_pinned = bool(tape_path and
                             tape_texture_animations_pinned(tape_path))
    if tape_path and os.path.exists(tape_path + ".worldpatch.jsonl"):
        with open(tape_path + ".worldpatch.jsonl") as pf:
            patch = [(max(int(json.loads(ln).get("tick", 1)), 1), ln)
                     for ln in pf if ln.strip()]
        patch.sort(key=lambda p: p[0])
    snapshot_patch = None
    arrival_events = {}
    if tape_path:
        snapshot_root = os.path.splitext(tape_path)[0] + "_world"
        if os.path.isdir(os.path.join(snapshot_root, "region")):
            from snapshot_patch import ensure_snapshot_patch
            snapshot_patch = ensure_snapshot_patch(tape_path, header, ticks)
            if snapshot_patch:
                arrival_events = snapshot_arrival_events(snapshot_patch, header,
                                                         ticks)

    with open(script_path, "w") as f:
        f.write(json.dumps({"tick": 0, "type": "set_time",
                            "value": int(header["world_time"])}) + "\n")
        f.write(json.dumps({"tick": 0, "type": "set_total_time",
                            "value": int(header.get("total_time", 0))}) + "\n")
        f.write(json.dumps({"tick": 0, "type": "set_dimension",
                            "dimension": int(header.get("dim", 0))}) + "\n")
        if tape_has_respawn(header, ticks) and tape_is_fluid_episode(ticks):
            f.write(json.dumps({"tick": 0,
                                "type": "continue_after_death"}) + "\n")
        # first-person arm variant: offline UUIDs hash to steve or alex.
        # This env's pinned Player0 session renders alex, so default slim
        # when the tape predates the header field.
        f.write(json.dumps({"tick": 0, "type": "set_skin",
                            "skin": header.get("skin", "slim")}) + "\n")
        f.write(json.dumps({"tick": 0, "type": "set_pose",
                            "x": header["x"], "y": header["y"], "z": header["z"],
                            "yaw": header["yaw"], "pitch": header["pitch"]}) + "\n")
        # seed the recorded start state: set_pose zeroes motion and vitals are
        # fresh 20/20 by default; a mid-session tape starts from neither.
        if "vx" in header:
            ev = {"tick": 0, "type": "set_velocity",
                  "x": header["vx"], "y": header["vy"], "z": header["vz"]}
            if "og" in header:  # first-tick friction: 0.546 ground vs 0.91 air
                ev["on_ground"] = int(header["og"])
            f.write(json.dumps(ev) + "\n")
        if "hp" in header:
            f.write(json.dumps({"tick": 0, "type": "set_vitals",
                                "health": header["hp"],
                                "food": int(header["food"])}) + "\n")
        # Seed live inventory before tick 0 from the first recorded inv row.
        # inv rows are post-tick truth, re-anchored via set_inventory on tick
        # t+1 so action t still sees the pre-tick stack; inv_view is render-
        # only. Without a tick-0 seed, player.inv stays empty on the first
        # state dump (and the state gate fails on any gear present at recstart).
        # Same approximation set_elytra already uses for chest equipment.
        if ticks and "inv" in ticks[0]:
            for tape_slot, stack in enumerate(ticks[0]["inv"]):
                if tape_slot > 40:
                    continue
                item, meta, count = (0, 0, 0) if stack == 0 else stack
                f.write(json.dumps({"tick": 0, "type": "set_inventory",
                                    "slot": int(tape_slot),
                                    "item": int(item),
                                    "count": int(count),
                                    "meta": int(meta)}) + "\n")
        # Armor slot 38 is EntityEquipmentSlot.CHEST. Seed the flight flag
        # before tick 0 even when the chest stack is empty (test hook); when
        # the chest holds elytra, set_inventory above already synced it.
        if ticks and len(ticks[0].get("inv", [])) > 38:
            chest = ticks[0]["inv"][38]
            equipped = int(chest != 0 and int(chest[0]) == 443)
            f.write(json.dumps({"tick": 0, "type": "set_elytra",
                                "equipped": equipped}) + "\n")
        # New tapes carry the whole recstart save. Convert only saved chunks
        # visible along the taped path into the sparse id+meta delta against
        # magma worldgen, then apply it after set_pose has generated the
        # starting window but before tick 0 executes.
        elytra_tape = bool(ticks and len(ticks[0].get("inv", [])) > 38
                           and ticks[0]["inv"][38] != 0
                           and int(ticks[0]["inv"][38][0]) == 443)
        source_x = None
        source_z_range = None
        if elytra_tape and snapshot_patch:
            source_cells = []
            with open(snapshot_patch) as sf:
                for line in sf:
                    if not line.strip():
                        continue
                    event = json.loads(line)
                    if (event.get("type") == "snapshot_block"
                            and event.get("id") in (8, 9)
                            and int(event.get("meta", 0)) < 8):
                        source_cells.append((int(event["x"]), int(event["z"])))
            if source_cells:
                columns = {(x, z): source_cells.count((x, z))
                           for x, z in set(source_cells)}
                counts = {x: sum(n for (sx, _), n in columns.items() if sx == x)
                          for x, _ in source_cells}
                source_x = max(counts, key=counts.get)
                zs = [z for (x, z), n in columns.items()
                      if x == source_x and n > 1]
                source_z_range = (min(zs), max(zs))

        def post_capture_spread(event):
            if not (source_x is not None
                    and event.get("type") == "snapshot_block"
                    and event.get("id") in (8, 9)
                    and int(event.get("meta", 0)) >= 8):
                return False
            ex = int(event["x"])
            approach = 1 if float(ticks[-1]["x"]) > float(ticks[0]["x"]) else -1
            return ((ex - source_x) * approach > 0
                    or (ex == source_x
                        and not source_z_range[0] <= int(event["z"]) <= source_z_range[1]))

        falling_snapshot = []
        if snapshot_patch:
            with open(snapshot_patch) as sf:
                for line in sf:
                    if line.strip():
                        event = json.loads(line)
                        if (elytra_tape and event.get("type") == "snapshot_block"
                                and event.get("id") in (8, 9, 10, 11)
                                and int(event.get("meta", 0)) >= 8):
                            falling_snapshot.append(event)
                        if not post_capture_spread(event):
                            f.write(line if line.endswith("\n") else line + "\n")
        # World snapshots are saved after capture. Keep their falling-liquid
        # cells for the distant curtain frames, then remove them before the
        # recorded player first intersects one; otherwise post-capture spread
        # is backdated into both travel() and the camera. Source cells remain.
        falling_clear_tick = None
        if elytra_tape and falling_snapshot:
            cells = {(int(e["x"]), int(e["y"]), int(e["z"]))
                     for e in falling_snapshot}
            for prev, row in zip(ticks, ticks[1:]):
                x, y, z = float(prev["x"]), float(prev["y"]), float(prev["z"])
                if any(x - 0.3 < bx + 1 and x + 0.3 > bx
                       and y + 0.2 < by + 1 and y + 0.4 > by
                       and z - 0.3 < bz + 1 and z + 0.3 > bz
                       for bx, by, bz in cells):
                    falling_clear_tick = int(row["t"])
                    break
        last_hb = 0
        last_wt = int(header["world_time"])
        last_dim = int(header.get("dim", 0))
        last_hp = float(header.get("hp", 20.0))
        last_food = int(header.get("food", 20))
        last_sat = None
        last_og = int(header.get("og", 0))
        last_fall = float(header.get("fall", 0.0))
        last_yaw = float(header.get("yaw", 0.0))
        last_pitch = float(header.get("pitch", 0.0))
        last_move = False
        has_sat = any("sat" in tape_row for tape_row in ticks)
        pending_inv = []
        pending_elytra = None
        velocity_ticks = {int(row["t"]) for row in ticks if "pvel" in row}
        loading_ticks = tape_loading_ticks(header, ticks)
        pose_anchored = externally_pose_anchored(header, ticks)
        # Portal-pane animation phase: newer tapes record portal_frame every
        # tick; older ones only while timeInPortal>0. frameCounter advances
        # exactly 1/tick (32 frames, frametime 1), so anchor on any recorded
        # row and extrapolate for the rest of the session.
        pf_anchor = next(((int(r["t"]), int(r["portal_frame"]))
                          for r in ticks if "portal_frame" in r), None)
        for row in ticks:
            t = row["t"]
            if pending_elytra is not None:
                f.write(json.dumps({"tick": t, "type": "set_elytra",
                                    "equipped": pending_elytra}) + "\n")
                pending_elytra = None
            for state in pending_inv:
                f.write(json.dumps({"tick": t, "type": "set_inventory", **state}) + "\n")
            pending_inv = []
            while patch and patch[0][0] <= t:
                ln = patch.pop(0)[1]
                f.write(ln if ln.endswith("\n") else ln + "\n")
            # Replay runs with daylight disabled and consumes the recorder's
            # post-tick clock directly. This supports both frozen fast-profile
            # tapes and vanilla doDaylightCycle sessions without config guesses.
            if "wt" in row and int(row["wt"]) != last_wt:
                last_wt = int(row["wt"])
                f.write(json.dumps({"tick": t, "type": "set_time",
                                    "value": last_wt}) + "\n")
            dimension = int(row.get("dim", last_dim))
            if dimension != last_dim:
                last_dim = dimension
                f.write(json.dumps({"tick": t, "type": "set_dimension",
                                    "dimension": dimension}) + "\n")
            for event in arrival_events.get(t, []):
                if not post_capture_spread(event):
                    f.write(json.dumps(event) + "\n")
            if t == falling_clear_tick:
                for event in falling_snapshot:
                    f.write(json.dumps({"tick": t, "type": "snapshot_block",
                                        "dim": int(event.get("dim", 0)),
                                        "x": int(event["x"]), "y": int(event["y"]),
                                        "z": int(event["z"]), "id": 0, "meta": 0}) + "\n")
            i = row["in"]
            move_now = bool(sgn(i["f"]) or sgn(i["s"]))
            look_changed = (float(row["yaw"]) != last_yaw
                            or float(row["pitch"]) != last_pitch)
            look_type = ("set_look_pre" if move_now and not last_move
                         and look_changed else "set_look")
            f.write(json.dumps({"tick": t, "type": look_type,
                                "yaw": row["yaw"], "pitch": row["pitch"]}) + "\n")
            # Authoritative SPacketEntityVelocity delivered to the local
            # player before this client tick. New tapes record raw packet
            # shorts, preserving vanilla's exact 1/8000 quantization.
            if "pvel" in row:
                vx, vy, vz = row["pvel"]
                f.write(json.dumps({"tick": t, "type": "set_packet_velocity",
                                    "x": int(vx) / 8000.0,
                                    "y": int(vy) / 8000.0,
                                    "z": int(vz) / 8000.0}) + "\n")
            if "ppos" in row:
                x, y, z, yaw, pitch, vx, vy, vz = row["ppos"]
                f.write(json.dumps({"tick": t, "type": "set_pose",
                                    "x": x, "y": y, "z": z,
                                    "yaw": yaw, "pitch": pitch}) + "\n")
                f.write(json.dumps({"tick": t, "type": "set_velocity",
                                    "x": vx, "y": vy, "z": vz}) + "\n")
            hp = float(row.get("hp", last_hp))
            food = int(row.get("food", last_food))
            food_changed = food != last_food
            remote_damage = "pvel" in row and hp < last_hp
            environmental_damage = (hp < last_hp and not remote_damage
                                    and (("air" in row and int(row["air"]) <= 0)
                                         or bool(row.get("fire", 0))))
            dragon = next((e for e in row.get("ents", [])
                           if e[1] == "EntityDragon"), None)
            damage_delta = last_hp - hp
            dragon_contact = (remote_damage and dragon is not None
                              and 4.5 <= damage_delta <= 5.1
                              and abs(float(dragon[2]) - float(row["x"])) < 16.0
                              and abs(float(dragon[3]) - float(row["y"])) < 12.0
                              and abs(float(dragon[4]) - float(row["z"])) < 16.0)
            cloud_present = any(e[1] == "EntityAreaEffectCloud"
                                for e in row.get("ents", []))
            dragon_breath = (hp < last_hp and not dragon_contact and cloud_present
                             and (abs(damage_delta - 6.0) < 0.01
                                  or abs(damage_delta - 1.0) < 0.01))
            if dragon_contact:
                # EntityDragon.collideWithEntities queries each wing part's
                # 4x4 box expanded by (4,2,4) and offset down two blocks. The
                # client root can trail the authoritative server part packet;
                # the 22x16x22 union bounds both expanded wings while still
                # rejecting the distant dragon at the earlier breath/fall hits.
                x, y, z = map(float, dragon[2:5])
                f.write(json.dumps({"tick": t, "type": "dragon_contact",
                                    "min_x": x - 11.0, "min_y": y - 8.0,
                                    "min_z": z - 11.0, "max_x": x + 11.0,
                                    "max_y": y + 8.0, "max_z": z + 11.0,
                                    "damage": 5.0}) + "\n")
            elif dragon_breath:
                # EntityAreaEffectCloud applies INSTANT_DAMAGE II (raw 6).
                # Tick 463 exposes the vanilla lastDamage delta: raw 6 inside
                # the wing hit's resistance window removes only 6-5 = 1 hp.
                f.write(json.dumps({"tick": t, "type": "mob_damage",
                                    "damage": 6.0}) + "\n")
            elif remote_damage:
                # EntityPlayer.attackEntityFrom runs before FoodStats.onUpdate
                # in the recorded server tick. Seed packet-backed mob damage
                # before magma's tick too, so foodTimer advances on the same
                # ten-tick regeneration interval. A post-tick anchor here
                # injects the recorded heal without resetting foodTimer and
                # causes a duplicate 5/6 heal on the following tick.
                f.write(json.dumps({"tick": t, "type": "set_vitals",
                                    "health": hp,
                                    # A same-tick FoodStats exhaustion rollover
                                    # still has to execute locally. The packet
                                    # health is post-damage, but the pre-tick
                                    # food input is the preceding row's value.
                                    "food": last_food if food_changed else food}) + "\n")
            elif environmental_damage:
                # Drowning and lava/fire damage happen on the integrated
                # server. The tape observes the later SPacketUpdateHealth,
                # so applying collision damage locally would lead the client
                # row by the packet delay. Seed the authoritative packet edge
                # before FoodStats, exactly like packet-backed mob damage.
                f.write(json.dumps({"tick": t, "type": "set_vitals",
                                    "health": hp,
                                    "food": last_food if food_changed else food}) + "\n")
            elif last_hp <= 0.0 and hp > 0.0:
                # A position packet on the row after GuiGameOver is the
                # client respawn. It must revive the runtime before this tick,
                # not through the ordinary post-tick regeneration path.
                f.write(json.dumps({"tick": t, "type": "set_vitals",
                                    "health": hp, "food": food}) + "\n")
            elif hp > last_hp and not food_changed:
                # Client health packets can expose server regeneration one
                # tick before magma's local FoodStats phase. Reconcile the
                # visible heal and, only if magma has not applied it already,
                # its hidden exhaustion + foodTimer side effects as one event.
                recorded_sat = row.get("sat")
                regen_exhaustion = (float(recorded_sat)
                                    if recorded_sat is not None and recorded_sat > 0
                                    else min((hp - last_hp) * 6.0, 6.0))
                f.write(json.dumps({"tick": t, "type": "set_regen_post",
                                    "health": hp, "food": food,
                                    "exhaustion": regen_exhaustion}) + "\n")
            elif t in velocity_ticks or t + 1 in velocity_ticks or food_changed:
                # Remote-player damage is not simulated when tape replay runs
                # with mobs disabled. Re-anchor post-tick vitals on the packet
                # row and its immediately preceding row: vanilla may regenerate
                # there just before the next server hit. Outside those narrow
                # windows, fall/hunger/regen remain independently compared.
                f.write(json.dumps({"tick": t, "type": "set_vitals_post",
                                    "health": hp, "food": food}) + "\n")
            if hp == last_hp and hp < 20.0 and food >= 18:
                # FoodStats is server-side, while tape rows are client ticks.
                # If either local regeneration branch reaches its foodTimer
                # threshold before SPacketUpdateHealth is visible, hide only
                # that positive health edge. The script runtime retains the
                # already-applied exhaustion and timer reset for the later
                # recorded health event.
                f.write(json.dumps({"tick": t,
                                    "type": "hold_regen_post"}) + "\n")
            og = int(row.get("og", last_og))
            fall = float(row.get("fall", 0.0))
            if (header.get("velocity_packets") and og and not last_og
                    and last_fall > 3.0):
                # The local fall calculation can precede the integrated
                # server's recorded EntityTracker velocity resend by several
                # client ticks. Keep the taped current motion until the later
                # set_packet_velocity event supplies the authoritative value.
                f.write(json.dumps({"tick": t,
                                    "type": "clear_hurt_velocity_post"}) + "\n")
                f.write(json.dumps({"tick": t,
                                    "type": "hold_fall_damage_post"}) + "\n")
            if has_sat:
                # The recorder omits saturation once it reaches exactly zero.
                # Preserve its post-tick transitions so FoodStats switches from
                # the 10-tick saturated branch to the 80-tick food branch on
                # the same tick as Java.
                sat = float(row.get("sat", 0.0))
                if last_sat is None or sat != last_sat:
                    f.write(json.dumps({"tick": t, "type": "set_food_stats_post",
                                        "saturation": sat,
                                        # Exhaustion is not taped. Reset it at
                                        # the authoritative saturation edge;
                                        # subsequent movement/regen rebuilds it
                                        # locally, preventing a stale extra
                                        # rollover on the following tick.
                                        "exhaustion": 0.0}) + "\n")
                    last_sat = sat
            last_hp, last_food = hp, food
            last_og, last_fall = og, fall
            last_yaw, last_pitch = float(row["yaw"]), float(row["pitch"])
            last_move = move_now
            ev = {"tick": t, "type": "action"}
            # tape f/s are MC's already-scaled movementInput values (sneak 0.3,
            # use-item 0.2 folded in); magma applies its own scaling from the
            # flag inputs, so replay the raw key sign.
            if sgn(i["f"]):
                ev["forward"] = sgn(i["f"])
            if sgn(i["s"]):
                # tape s is vanilla moveStrafe (+1 = LEFT); GmAction.strafe is
                # magma's input convention (+1 = D/right, negated internally
                # before the vanilla kernel - player_ctl.c). Found at tick 3870
                # of the first 12k human tape: first strafe mirrored x exactly.
                ev["strafe"] = -sgn(i["s"])
            for k_tape, k_ev in (("jump", "jump"), ("sneak", "sneak"),
                                 ("sprint", "sprint"), ("atk", "attack"),
                                 ("use", "use")):
                if i.get(k_tape):
                    ev[k_ev] = 1
            if i.get("hb", 0) != last_hb:
                ev["hotbar"] = i["hb"]
                last_hb = i["hb"]
            if len(ev) > 2:
                f.write(json.dumps(ev) + "\n")
            if pose_anchored:
                f.write(json.dumps({"tick": t, "type": "set_pose_post",
                                    "x": row["x"], "y": row["y"], "z": row["z"],
                                    "yaw": row["yaw"], "pitch": row["pitch"],
                                    "vx": row["vx"], "vy": row["vy"], "vz": row["vz"],
                                    "on_ground": int(row["og"]),
                                    "fall": float(row.get("fall", 0.0))}) + "\n")
            elif t in loading_ticks:
                f.write(json.dumps({"tick": t, "type": "set_pose_post",
                                    "x": row["x"], "y": row["y"], "z": row["z"],
                                    "yaw": row["yaw"], "pitch": row["pitch"],
                                    "vx": row["vx"], "vy": row["vy"], "vz": row["vz"],
                                    "on_ground": int(row["og"]),
                                    "fall": float(row.get("fall", 0.0))}) + "\n")
            # The recorder's inv row is POST-tick truth. Keep it render-only
            # for frame t so the current action still sees its real pre-tick
            # stack, then re-anchor the live inventory before tick t+1. This
            # handles both modeled survival consumption and untaped GUI clicks
            # without the old hand-written set_inventory worldpatch entries.
            if "inv" in row:
                for tape_slot, stack in enumerate(row["inv"]):
                    # 0..35 main, 36..39 armor (chest=38), 40 offhand. Replay
                    # all four armor slots with metadata; keep set_elytra as a
                    # narrow flight-eligibility re-anchor from chest content.
                    if tape_slot == 38:
                        pending_elytra = int(
                            stack != 0 and int(stack[0]) == 443
                            and int(stack[1]) < 431)
                    if tape_slot > 40:
                        continue
                    slot = tape_slot
                    item, meta, count = (0, 0, 0) if stack == 0 else stack
                    state = {"slot": slot, "item": int(item),
                             "count": int(count), "meta": int(meta)}
                    f.write(json.dumps({"tick": t, "type": "inv_view", **state}) + "\n")
                    pending_inv.append(state)
            # HUD truth which magma already knows how to draw. Absent air
            # means the full 300-tick breath bar and therefore no bubbles.
            if "xpl" in row and "xpp" in row:
                f.write(json.dumps({"tick": t, "type": "player_view",
                                    "xp_level": int(row["xpl"]),
                                    "xp_frac": float(row["xpp"]),
                                    "air": int(row.get("air", -1)),
                                    "portal": float(row.get("portal", 0.0)),
                                    "portal_frame": int(row.get(
                                        "portal_frame",
                                        (pf_anchor[1] - pf_anchor[0] + t) % 32
                                        if pf_anchor else -1)),
                                    "portal_phase": int(row.get("portal_phase", 0)),
                                    "texture_animations_pinned":
                                        int(animations_pinned),
                                    "fire": int(row.get("fire", 0)),
                                    "creative": int(header.get("gamemode") ==
                                                    "creative"),
                                    "hurt": int(row.get("hurt", 0)),
                                    "max_hurt": int(row.get("maxhurt", 10)),
                                    "hurt_yaw": float(row.get("hurtyaw", 0.0)),
                                    "attack_cooldown": float(row.get("cd", 1.0)),
                                    # Physics remains frozen until the player is
                                    # loaded, but the brown loading screen is
                                    # visible only while this GUI is actually
                                    # open. The brief post-close blank frame is
                                    # rendered from the world framebuffer.
                                    "loading": (1 if row.get("gui") ==
                                                "GuiDownloadTerrain" else
                                                2 if t in loading_ticks else 0)}) + "\n")
                f.write(json.dumps({"tick": t, "type": "potion_clear"}) + "\n")
                for potion_id, amplifier, duration in row.get("pots", []):
                    f.write(json.dumps({"tick": t, "type": "potion_view",
                                        "id": int(potion_id),
                                        "amplifier": int(amplifier),
                                        "duration": int(duration)}) + "\n")
            # ghost pushers near the oracle player (push reach is ~1.5 blocks;
            # 4 gives slack for magma-vs-oracle drift within tolerance)
            for e in row.get("ents", []):
                typ = e[1]
                if typ in NONPUSHABLE:
                    continue
                ex, ey, ez = e[2], e[3], e[4]
                if (abs(ex - row["x"]) < 4.0 and abs(ez - row["z"]) < 4.0
                        and abs(ey - row["y"]) < 4.0):
                    w, h = ENT_SIZE.get(typ, (0.6, 1.8))
                    f.write(json.dumps({"tick": t, "type": "ent_box",
                                        "x": ex, "y": ey, "z": ez,
                                        "w": w, "h": h}) + "\n")
            # renderable ghost entities (OPEN_DIVERGENCES #10): every recorded
            # entity also becomes an ent_view so magma DRAWS it at this
            # tick's frame capture (mob models via gm_entities_emit). Purely
            # render-side; post-2026-07-12 rows append the exact vanilla pose,
            # flags, sheep state, or EntityItem stack/bob phase. Old 7-field
            # rows retain the legacy inference path.
            for e in row.get("ents", []):
                view = {"tick": t, "type": "ent_view", "ent": e[1],
                        "x": e[2], "y": e[3], "z": e[4], "yaw": e[5],
                        "hp": e[6], "id": e[0]}
                if "Arrow" in e[1] and len(e) >= 8:
                    # recorder appends rotationPitch for arrows (RenderArrow
                    # Rz tilt); 7-field rows from older tapes render flat.
                    view["pitch"] = e[7]
                elif e[1] == "EntityItem" and len(e) >= 12:
                    view.update(item=e[7], item_meta=e[8], count=e[9],
                                age=e[10], hover=e[11], has_hover=1)
                elif e[1] == "EntityXPOrb" and len(e) >= 10:
                    # recorder: xpValue, xpColor, xpOrbAge after the base 7.
                    view.update(item=int(e[7]), item_meta=int(e[8]),
                                age=int(e[9]))
                elif e[1] == "EntityEnderCrystal" and len(e) >= 12:
                    # recorder appends innerRotation, shouldShowBottom, beam
                    # target block (-1,-1,-1 = no beam).
                    view.update(crystal_rot=e[7], show_bottom=e[8],
                                beam_x=e[9], beam_y=e[10], beam_z=e[11])
                elif len(e) >= 14:
                    view.update(tape_pose=1, head_yaw=e[7], pitch=e[8],
                                swing=e[9], hurt=e[10], death=e[11],
                                body_yaw=e[12], flags=e[13])
                    if e[1] == "EntitySheep" and len(e) >= 17:
                        view.update(sheared=e[14], fleece=e[15], graze_y=e[16])
                        if len(e) >= 18:
                            view["graze_x"] = e[17]
                    elif e[1] == "EntityDragon" and len(e) >= 16:
                        # animTime (wing flap) + deathTicks (0..200 collapse)
                        view.update(anim_time=e[14], death_ticks=e[15])
                        if len(e) >= 18:
                            # AI phase id + stationary (getHeadPartYOffset)
                            view.update(phase_id=e[16], stationary=e[17])
                f.write(json.dumps(view) + "\n")
            # open GUI screen (OPEN_DIVERGENCES #9): when the recorder emits
            # "gui" (GuiScreen simple name) + optional gmx/gmy (ScaledResolution
            # mouse), emit a render-only gui_view. magma maps the class to a
            # container kind and draws gm_screen_draw after the HUD. Canonical
            # tapes predating the recorder change have no gui fields -> no events.
            if "gui" in row:
                # default center of 427x240 gui space (854x480 scale 2); script.c
                # also centers when mx/my are omitted, but emit them explicitly.
                mx = int(row["gmx"]) if "gmx" in row else 213
                my = int(row["gmy"]) if "gmy" in row else 120
                f.write(json.dumps({"tick": t, "type": "gui_view",
                                    "gui": row["gui"], "mx": mx, "my": my})
                        + "\n")
                # Post-tick visible slot/cursor truth. Container list indexes
                # differ by vanilla screen, so translate them to magma's
                # unified inventory/grid/result/furnace slot ids. Unsupported
                # screens remain gui_view-only and are skipped by script.c.
                gui = row["gui"]
                for index, stack in enumerate(row.get("gslots", [])):
                    slot = gui_slot_id(gui, index)
                    if slot is not None:
                        f.write(json.dumps({"tick": t, "type": "gui_slot_view",
                                            "slot": slot, **tape_stack(stack)})
                                + "\n")
                if "gcur" in row and gui_slot_id(gui, 0) is not None:
                    f.write(json.dumps({"tick": t, "type": "gui_cursor_view",
                                        **tape_stack(row["gcur"])}) + "\n")
                if gui == "GuiFurnace" and "gprop" in row:
                    burn, current, cook, total = row["gprop"]
                    f.write(json.dumps({"tick": t, "type": "gui_furnace_view",
                                        "burn": int(burn),
                                        "current_burn": int(current),
                                        "cook": int(cook),
                                        "total_cook": int(total)}) + "\n")
        if pending_inv and ticks:
            for state in pending_inv:
                f.write(json.dumps({"tick": ticks[-1]["t"] + 1,
                                    "type": "set_inventory", **state}) + "\n")


def tape_has_respawn(header, ticks):
    previous_hp = float(header.get("hp", 20.0))
    for row in ticks:
        hp = float(row.get("hp", previous_hp))
        if "ppos" in row and previous_hp <= 0.0 and hp > 0.0:
            return True
        previous_hp = hp
    return False


def tape_is_fluid_episode(ticks):
    if any("air" in row for row in ticks):
        return True
    return (any(row.get("fire") for row in ticks)
            and not any(row.get("ents") for row in ticks))


def tape_loading_ticks(header, ticks):
    """Ticks where vanilla deliberately did not simulate the client player.

    Current tapes record this directly. Legacy tapes infer only the bounded
    cross-dimension plateau: it starts at a dimension change and ends at the
    first nonzero velocity or grounded state. This covers the temporary spawn,
    authoritative portal-position jump, and chunk-insertion delay without
    re-anchoring ordinary stationary gameplay.
    """
    explicit = {int(row["t"]) for row in ticks if row.get("loading")}
    # SPacketRespawn recreates RenderGlobal and its chunk renderers. These two
    # fresh tapes bound the visible empty-world interval: it is still blank at
    # respawn+3 (water t1000) and populated by respawn+5 (lava t120). Preserve
    # the four evidenced blank ticks; loading=2 renders vanilla's sky/fog plus
    # crosshair/HUD without pretending GuiDownloadTerrain is still open.
    previous_hp = float(header.get("hp", 20.0))
    for row in ticks:
        hp = float(row.get("hp", previous_hp))
        if "ppos" in row and previous_hp <= 0.0 and hp > 0.0:
            start = int(row["t"])
            explicit.update(range(start, start + 4))
        previous_hp = hp
    if header.get("position_packets"):
        return explicit
    result = set(explicit)
    last_dim = int(header.get("dim", 0))
    transition = False
    for row in ticks:
        dim = int(row.get("dim", last_dim))
        if dim != last_dim:
            transition = True
        if transition:
            frozen = (not int(row.get("og", 0))
                      and all(abs(float(row.get(k, 0.0))) <= 1e-15
                              for k in ("vx", "vy", "vz")))
            if row.get("gui") == "GuiDownloadTerrain" or frozen:
                result.add(int(row["t"]))
            else:
                transition = False
        last_dim = dim
    return result


def first_divergence(ticks, c_rows):
    """Return (tick, field, tape_val, magma_val, |d|) of the first per-field
    tolerance violation, plus the full per-tick euclid list."""
    fmap = {"x": "x", "y": "y", "z": "z", "vx": "vx", "vy": "vy", "vz": "vz",
            "og": "on_ground", "hp": "health", "food": "food",
            "dim": "dim"}
    # hp/food packets can land on either side of the client's local
    # regen/exhaustion update. Accept the closest adjacent row; all movement and
    # dimension fields remain strictly same-tick. Evidence: fall damage is often
    # one row late, while regen after an authoritative mob hit can be one early.
    lagged = {"hp", "food"}
    first = None
    euclid = []
    n = min(len(ticks), len(c_rows))
    # Survival episodes terminate at death. The live bridge may keep recording
    # the death screen or respawn the Oracle, while magma correctly stops the
    # episode; neither state is part of the replay contract after hp reaches 0.
    death = next((i for i, row in enumerate(ticks[:n])
                  if float(row.get("hp", 1.0)) <= 0.0), None)
    if death is not None:
        n = death + 1
    for t in range(n):
        j, c = ticks[t], c_rows[t]
        euclid.append(math.sqrt((j["x"] - c["x"]) ** 2 + (j["y"] - c["y"]) ** 2
                                + (j["z"] - c["z"]) ** 2))
        if first is None:
            for k, ck in fmap.items():
                if k == "dim" and k not in j:
                    continue
                candidates = [c[ck]]
                if k in lagged and t > 0:
                    candidates.append(c_rows[t - 1][ck])
                if k in lagged and t + 1 < n:
                    candidates.append(c_rows[t + 1][ck])
                cv = min(candidates, key=lambda v: abs(float(j[k]) - float(v)))
                d = abs(float(j[k]) - float(cv))
                if d > TOL[k]:
                    first = (t, k, j[k], cv, d)
                    break
    return first, euclid


# ---- Non-player state gate (inventory / entities / world hash) -------------
# Deliberately separate from the physics (pose/vitals/dim) gate above. These
# fields are reported in magma_state.jsonl and, when the tape carries them, are
# asserted here. Missing tape fields are recorded as "unavailable", not green.


def _tape_inv_slot(stack):
    """Normalize a tape inv entry to (item, count, meta) or None if empty."""
    if stack in (0, None, [], {}):
        return None
    if isinstance(stack, (list, tuple)):
        if len(stack) < 1 or int(stack[0]) == 0:
            return None
        item = int(stack[0])
        meta = int(stack[1]) if len(stack) > 1 else 0
        count = int(stack[2]) if len(stack) > 2 else 1
        return (item, count, meta)
    return None


def _magma_inv_map(row):
    out = {}
    for slot in row.get("inventory") or []:
        item = int(slot.get("item", 0))
        if item <= 0:
            continue
        out[int(slot["slot"])] = (item, int(slot.get("count", 1)),
                                  int(slot.get("meta", 0)))
    return out


def _tape_entity_types(row):
    types = []
    for ent in row.get("ents") or []:
        if len(ent) >= 2 and isinstance(ent[1], str):
            types.append(ent[1])
    return types


def _magma_entity_types(row):
    """Magma emits numeric type ids; keep them as 'type:<id>' for presence."""
    types = []
    for ent in row.get("entities") or []:
        if isinstance(ent, dict) and "type" in ent:
            types.append(f"type:{int(ent['type'])}")
    return types


def collect_state_assertions(ticks, c_rows, sample_every=20):
    """Build explicit non-player state assertions for scenario/gate output.

    Returns a dict with inventory, entity, and world-hash findings. This is a
    *state* gate, not a physics gate: pose/vitals stay in first_divergence.
    """
    n = min(len(ticks), len(c_rows))
    inv_checked = 0
    inv_mismatches = []
    ent_checked = 0
    ent_presence = []
    hash_checked = 0
    hash_samples = []
    prev_hash = None
    hash_deltas = 0
    for t in range(0, n, max(1, sample_every)):
        j, c = ticks[t], c_rows[t]
        if "inv" in j:
            inv_checked += 1
            tape_map = {}
            for slot, stack in enumerate(j["inv"]):
                norm = _tape_inv_slot(stack)
                if norm is not None and slot < 41:
                    tape_map[slot] = norm
            magma_map = _magma_inv_map(c)
            # Presence of non-empty tape slots in magma (counts may lag one
            # tick on GUI interactions; compare item id identity only).
            for slot, (item, _count, _meta) in tape_map.items():
                m = magma_map.get(slot)
                if (m is None or m[0] != item) and len(inv_mismatches) < 20:
                    inv_mismatches.append({
                        "tick": t, "slot": slot, "tape_item": item,
                        "magma_item": None if m is None else m[0],
                    })
        if "ents" in j:
            ent_checked += 1
            tape_types = _tape_entity_types(j)
            magma_types = _magma_entity_types(c)
            ent_presence.append({
                "tick": t,
                "tape_count": len(tape_types),
                "tape_types": sorted(set(tape_types))[:12],
                "magma_count": len(magma_types),
                "magma_types": sorted(set(magma_types))[:12],
            })
        nh = c.get("nearby_hash")
        if nh is not None:
            hash_checked += 1
            if prev_hash is not None and nh != prev_hash:
                hash_deltas += 1
            if t % max(sample_every * 5, 1) == 0 and len(hash_samples) < 32:
                hash_samples.append({"tick": t, "nearby_hash": nh})
            prev_hash = nh
    return {
        "kind": "state",  # not "physics"
        "inventory": {
            "ticks_checked": inv_checked,
            "mismatches": inv_mismatches,
            "pass": inv_checked == 0 or len(inv_mismatches) == 0,
            "available": inv_checked > 0,
        },
        "entities": {
            "ticks_checked": ent_checked,
            "samples": ent_presence[:16],
            "pass": True,  # presence is informational; types are recorded
            "available": ent_checked > 0,
        },
        "world": {
            "ticks_checked": hash_checked,
            "hash_deltas": hash_deltas,
            "samples": hash_samples,
            "pass": hash_checked > 0,  # hash emitted = world gate available
            "available": hash_checked > 0,
        },
    }


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("tape")
    ap.add_argument("--out", default=None)
    ap.add_argument("--report", action="store_true",
                    help="write report/tape_<name>.md")
    ap.add_argument("--w", type=int, default=854)
    ap.add_argument("--h", type=int, default=480)
    ap.add_argument("--cuda", action="store_true",
                    help="(default) render on the GPU (magma_game_cuda,"
                         " bit-exact vs CPU, GPU1 per repo policy)")
    ap.add_argument("--cpu", action="store_true",
                    help="force the CPU raster (parity checks / no-GPU boxes)")
    ap.add_argument("--no-gate", action="store_true",
                    help="skip the structural pixel gate (pixel_gate.py)")
    args = ap.parse_args()
    # CUDA raster is the flywheel default (12k tape: 9.2 s vs 43 s CPU,
    # identical verdicts); --cpu forces the software path.
    backend = "cpu" if args.cpu else "cuda"

    name = os.path.splitext(os.path.basename(args.tape))[0]
    out = args.out or os.path.join(here, "out", f"tape_{name}")
    os.makedirs(out, exist_ok=True)
    header, ticks = load_tape(args.tape)
    skipped_renderables = skipped_renderable_counts(ticks)
    world = magma_world(header)
    print(f"[tape] {name}: {len(ticks)} ticks, seed {header['seed']}, "
          f"start ({header['x']:.2f},{header['y']:.2f},{header['z']:.2f}) "
          f"wt={header['world_time']}")

    # ---- ONE magma run: state + (if the tape has golden frames) pixels.
    # The raster never feeds back into the sim, so the frames run's state is
    # byte-identical to a physics-only run (verified: cmp on the 12k tape) -
    # the old separate physics pass was pure duplicate work.
    scr = os.path.join(out, "magma_script.jsonl")
    tape_to_script(header, ticks, scr, tape_path=args.tape)
    state = os.path.join(out, "magma_state.jsonl")
    frame_ticks = [(row["t"], row["frame"]) for row in ticks if "frame" in row]
    frames_npy = None
    every = 1
    offset = 0
    if frame_ticks:
        # npy-direct: magma appends each rendered frame to ONE uint8
        # [N,H,W,3] file (no 500+ PPM writes then re-reads); this file IS
        # the magma_frames.npy record. Frame i is tick offset + i*every.
        frames_npy = os.path.join(out, "magma_frames.npy")
        # render only the golden ticks: the tape's frames are a regular
        # cadence, and rendering every tick made 12k-tick replays ~20x slower
        fts = [t for t, _ in frame_ticks]
        every = max(min((b - a for a, b in zip(fts, fts[1:])), default=1), 1)
        offset = fts[0]
    # mobs=False: magma's spawn RNG cannot match the oracle session's, so a
    # mobs-on replay grows phantom mobs that hit the player and corrupt the
    # trajectory (killed the player at t~11000 on the 12k tape). Mob parity is
    # judged against the tape's recorded ents, not magma's own spawner.
    died_early = False
    try:
        if frames_npy:
            # daylight=False: the trace profile (fast.yaml) records with
            # doDaylightCycle=false, so the oracle session's world_time never
            # advances past the header value; magma must freeze too or its
            # sky/lightmap drift makes every late frame diverge.
            ol.run_magma_script(scr, len(ticks), frames_npy, state,
                                  w=args.w, h=args.h, seed=int(header["seed"]),
                                  frame_every=every, frame_offset=offset,
                                  mobs=False, backend=backend, daylight=False,
                                  world=world,
                                  extra_env=({"MAGMA_STRIP_OVERLAYS": "1"}
                                             if tape_strip_overlays(args.tape)
                                             else None))
        else:
            ol.run_magma_script(scr, len(ticks), None, state,
                                  w=args.w, h=args.h,
                                  seed=int(header["seed"]), mobs=False,
                                  daylight=False, world=world,
                                  extra_env=({"MAGMA_STRIP_OVERLAYS": "1"}
                                             if tape_strip_overlays(args.tape)
                                             else None))
    except RuntimeError as e:
        # A dead magma player stops consuming script events and the run exits
        # rc=2 ("event lies beyond --ticks"). The state written up to the death
        # is still the divergence evidence we want; report it loudly.
        died_early = True
        print(f"[tape] WARNING: magma run ended early ({e}); "
              f"diffing the partial state")
    c_rows = [json.loads(ln) for ln in open(state)]
    if died_early:
        last = c_rows[-1] if c_rows else {}
        print(f"[tape] WARNING: magma stopped at tick {last.get('tick')} "
              f"(hp={last.get('health')} dead={last.get('dead')}) "
              f"of {len(ticks)} tape ticks")
    first, euclid = first_divergence(ticks, c_rows)
    state_gate = collect_state_assertions(ticks, c_rows)

    if first is None:
        scope = (f"{len(euclid)} ticks through terminal death"
                 if len(euclid) < len(ticks) else f"{len(ticks)} ticks")
        print(f"[tape] physics: NO divergence over {scope} "
              f"(tolerances {TOL})")
    else:
        t, k, jv, cv, d = first
        print(f"[tape] FIRST DIVERGENCE tick {t} field {k}: "
              f"oracle={jv!r} magma={cv!r} |d|={d:.3g}")
        print(f"[tape] euclid at t={t}: {euclid[t]:.6f}, "
              f"end t={len(euclid)-1}: {euclid[-1]:.6f}")
        ctx = ticks[t]
        print(f"[tape] inputs at divergence: {ctx['in']}  "
              f"yaw={ctx['yaw']:.2f} pitch={ctx['pitch']:.2f} og={ctx['og']}")

    inv_s = state_gate["inventory"]
    ent_s = state_gate["entities"]
    world_s = state_gate["world"]
    print(f"[tape] state: inventory "
          f"{'n/a' if not inv_s['available'] else ('PASS' if inv_s['pass'] else 'FAIL')} "
          f"({inv_s['ticks_checked']} ticks, {len(inv_s['mismatches'])} mismatches); "
          f"entities {'n/a' if not ent_s['available'] else 'recorded'} "
          f"({ent_s['ticks_checked']} ticks); "
          f"world_hash {'n/a' if not world_s['available'] else 'recorded'} "
          f"({world_s['ticks_checked']} ticks, {world_s['hash_deltas']} deltas)")

    # ---- pixels at the tape's sparse oracle frames (written by the one run) ----
    pix = []
    gate = None
    if frame_ticks:
        # oracle side: decoded ONCE per tape into a sidecar npy (tapes are
        # immutable); magma side: the run's npy, memory-mapped (no PPM/PNG
        # round trip at all). Frames not at the replay resolution (mid-session
        # window resize) are skipped by the cache, loudly, as before.
        import numpy as np
        from concurrent.futures import ThreadPoolExecutor
        oticks, oframes, skipped_res = ol.oracle_frames_cache(
            frame_ticks, args.w, args.h)
        # Recorder artifact: right after the recstart handoff the renderer can
        # lag the bridge, so the first frames of a tape are byte-identical
        # stale duplicates of frame 0. Drop the stale prefix LOUDLY - diffing
        # magma against a frame of the pre-tape scene is pure noise.
        stale = 0
        while stale + 1 < len(oticks) and np.array_equal(oframes[stale + 1],
                                                         oframes[0]):
            stale += 1
        if stale:
            print(f"[tape] WARNING: dropping {stale + 1} stale leading oracle "
                  f"frames (byte-identical through t={oticks[stale]}; renderer "
                  f"lagged the recstart handoff)")
            oticks, oframes = oticks[stale + 1:], oframes[stale + 1:]
        carr = (np.load(frames_npy, mmap_mode="r")
                if os.path.exists(frames_npy) else np.zeros((0, 1, 1, 3)))
        cticks = []

        # structural gate (pixel_gate.py): per-frame diff clusters classified
        # against the accepted OPEN_DIVERGENCES classes; anything unexplained
        # and big fails the tape. scipy is the only extra dep; degrade loudly.
        gate_on = not args.no_gate
        if gate_on:
            try:
                import pixel_gate as pg
                from scipy import ndimage as _nd  # noqa: F401
                known_divergences = pg.load_known_divergences(args.tape)
            except ImportError as e:
                raise SystemExit(
                    f"[tape] pixel gate deps missing ({e}); add --with scipy "
                    f"or pass --no-gate to skip the gate explicitly")

        def diff_one(i_t):
            # numpy releases the GIL, so threads give real parallelism here
            i, t = i_t
            j = (t - offset) // every
            if j < 0 or j >= len(carr):   # died early: no frame for this tick
                return None
            b8 = np.asarray(carr[j])
            o16 = oframes[i].astype(np.int16)
            c16 = b8.astype(np.int16)
            s = ol.diff_regions_arrays(o16, c16, args.w, args.h)
            if gate_on:
                clusters, mild = pg.gate_frame_ex(
                    o16, c16, args.w, args.h, tick=t,
                    known=known_divergences)
            else:
                clusters, mild = None, None
            return t, b8, s, clusters, mild

        with ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 1)) as ex:
            results = list(ex.map(diff_one, enumerate(oticks)))
        gate_ticks = {}
        mild_ticks = {}
        for r_ in results:
            if r_ is None:
                continue
            t, b8, s, clusters, mild = r_
            pix.append((t, s))
            cticks.append(t)
            if clusters is not None:
                gate_ticks[t] = clusters
            if mild is not None:
                mild_ticks[t] = mild
            print(f"[tape] pixels t={t:5d} whole {s['whole']['mean_abs']:6.2f}/ch "
                  f"({s['whole']['pct_differing']:5.2f}%) terrain "
                  f"{s['terrain']['mean_abs']:6.2f}")
            if first is not None and abs(t - first[0]) <= 40:
                cpng = os.path.join(out, f"magma_t{t:06d}.png")
                ol.rgb_to_png(b8, cpng)
                mc_png = dict(frame_ticks)[t]
                ol.side_by_side(mc_png, cpng,
                                os.path.join(out, f"sbs_t{t:06d}.png"))
        gate = (pg.summarize(gate_ticks, transit=pg.transit_ticks(ticks),
                             mild_per_tick=mild_ticks)
                if gate_on and gate_ticks else None)
        if gate_on:
            gate = apply_missing_model_gate(gate, skipped_renderables)
        if gate:
            for cls, s_ in sorted(gate["classes"].items()):
                print(f"[gate] class {cls:12s} frames {s_['frames']:5d} "
                      f"px {s_['px']:9d} max_cluster {s_['max_cluster']}")
            # auto-extract the worst offenders so a failure is a picture,
            # not an hour of digging
            for row in gate["failed_frames"][:5]:
                t = row["tick"]
                j = (t - offset) // every
                cpng = os.path.join(out, f"gatefail_t{t:06d}.png")
                ol.rgb_to_png(np.asarray(carr[j]), cpng)
                ol.side_by_side(dict(frame_ticks)[t], cpng,
                                os.path.join(out, f"gatefail_sbs_t{t:06d}.png"))
            if skipped_renderables:
                failed_rows = sum(gate["missing_model_failures"].values())
                print(f"[gate] class missing_model types "
                      f"{len(skipped_renderables)} rows "
                      f"{sum(skipped_renderables.values())} failures "
                      f"{failed_rows} threshold "
                      f">{MISSING_MODEL_ROW_THRESHOLD}")
                for typ, rows in skipped_renderables.items():
                    verdict = ("FAIL" if rows > MISSING_MODEL_ROW_THRESHOLD
                               else "below-threshold")
                    print(f"[gate] skipped renderable {typ}: {rows} rows "
                          f"({verdict})")
            if gate["pass"]:
                print(f"[gate] PASS: no unexplained clusters over "
                      f"{gate['frames_checked']} frames")
            elif gate["failed_frames"]:
                ff = gate["failed_frames"]
                print(f"[gate] FAIL: {len(ff)} frames with unexplained "
                      f"clusters; worst t={ff[0]['tick']} "
                      f"({ff[0]['unexplained_px']} px) - see gatefail_sbs_*.png")
            else:
                print("[gate] FAIL: repeated renderable entity rows have no "
                      "magma model (missing_model)")
        if skipped_res:
            print(f"[tape] WARNING: skipped {skipped_res} oracle frames not at "
                  f"{args.w}x{args.h} (window resized mid-session); those ticks "
                  f"have no pixel verdict")
        # magma_frames.npy is written by the run itself; record its ticks
        np.save(os.path.join(out, "magma_frames.ticks.npy"),
                offset + every * np.arange(len(carr)))

    # ---- report ----
    if args.report:
        rp = os.path.join(here, "report", f"tape_{name}.md")
        with open(rp, "w") as f:
            f.write(f"# Tape replay: {name}\n\n")
            f.write(f"{len(ticks)} ticks, seed {header['seed']}, world_time "
                    f"{header['world_time']}, start ({header['x']:.2f},"
                    f"{header['y']:.2f},{header['z']:.2f}).\n\n")
            if first is None:
                f.write(f"**Physics: clean.** No divergence (tol {TOL}).\n\n")
            else:
                t, k, jv, cv, d = first
                f.write(f"**FIRST DIVERGENCE: tick {t}, field `{k}`** "
                        f"oracle={jv!r} magma={cv!r} |d|={d:.3g}; inputs "
                        f"{ticks[t]['in']}. End-of-tape euclid "
                        f"{euclid[-1]:.4f} blocks.\n\n")
            f.write("**State gate** (inventory / entities / world hash; not "
                    "physics):\n\n")
            f.write(f"- inventory: checked={inv_s['ticks_checked']} "
                    f"mismatches={len(inv_s['mismatches'])} "
                    f"available={inv_s['available']} "
                    f"pass={inv_s['pass']}\n")
            f.write(f"- entities: checked={ent_s['ticks_checked']} "
                    f"available={ent_s['available']}\n")
            f.write(f"- world nearby_hash: checked={world_s['ticks_checked']} "
                    f"deltas={world_s['hash_deltas']} "
                    f"available={world_s['available']}\n\n")
            if gate:
                f.write(f"**Pixel gate: {'PASS' if gate['pass'] else 'FAIL'}"
                        f"** over {gate['frames_checked']} frames.\n\n")
                f.write("| class | frames | px | max cluster |\n"
                        "|---|---|---|---|\n")
                for cls, s_ in sorted(gate["classes"].items()):
                    f.write(f"| {cls} | {s_['frames']} | {s_['px']} | "
                            f"{s_['max_cluster']} |\n")
                f.write("\n")
                if gate.get("missing_models"):
                    f.write("Skipped renderable entity rows (more than "
                            f"{MISSING_MODEL_ROW_THRESHOLD} fails the gate):\n\n")
                    for typ, rows in gate["missing_models"].items():
                        verdict = ("FAIL" if rows > MISSING_MODEL_ROW_THRESHOLD
                                   else "below threshold")
                        f.write(f"- `{typ}`: {rows} rows ({verdict})\n")
                    f.write("\n")
                if not gate["pass"]:
                    f.write("Failed frames (worst first, top 20):\n\n")
                    for row in gate["failed_frames"][:20]:
                        f.write(f"- t={row['tick']}: "
                                f"{row['unexplained_px']} unexplained px, "
                                f"clusters {row['clusters'][:4]}\n")
                    f.write("\n")
            if pix:
                f.write("| tick | whole mean/ch | %diff | terrain mean/ch |\n"
                        "|---|---|---|---|\n")
                for t, s in pix:
                    f.write(f"| {t} | {s['whole']['mean_abs']:.2f} | "
                            f"{s['whole']['pct_differing']:.2f}% | "
                            f"{s['terrain']['mean_abs']:.2f} |\n")
        print(f"[tape] report -> {rp}")
    # Tapes without sparse frames still get the entity-model completeness gate.
    if gate is None and not args.no_gate:
        gate = apply_missing_model_gate(None, skipped_renderables)
        if skipped_renderables:
            print(f"[gate] class missing_model types "
                  f"{len(skipped_renderables)} rows "
                  f"{sum(skipped_renderables.values())} threshold "
                  f">{MISSING_MODEL_ROW_THRESHOLD}")
            for typ, rows in skipped_renderables.items():
                print(f"[gate] skipped renderable {typ}: {rows} rows")
    if gate is not None:
        gate["state"] = state_gate
        gj = os.path.join(here, "report", f"tape_{name}.gate.json")
        with open(gj, "w") as f:
            json.dump(gate, f, indent=1)
        print(f"[gate] baseline -> {gj}")
        if not gate["pass"]:
            return 3
    else:
        # No pixel frames: still emit a state-only gate sidecar for scenarios.
        gj = os.path.join(here, "report", f"tape_{name}.gate.json")
        os.makedirs(os.path.dirname(gj), exist_ok=True)
        with open(gj, "w") as f:
            json.dump({"pass": True, "frames_checked": 0, "classes": {},
                       "failed_frames": [], "state": state_gate}, f, indent=1)
        print(f"[gate] state-only baseline -> {gj}")
    if first is not None:
        return 4  # physics divergence: exact replay is the primary contract
    if state_gate["inventory"]["available"] and not state_gate["inventory"]["pass"]:
        return 5  # non-player state divergence (not physics)
    return 0


if __name__ == "__main__":
    sys.exit(main())
