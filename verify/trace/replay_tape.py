#!/usr/bin/env python3
"""replay_tape.py - replay a human-play tape through magma and find the first
divergence. THE human-verification flywheel step (see magma/VERIFY.md).

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
import json
import math
import os
import sys
from collections import Counter
from itertools import pairwise

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_lib as ol

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
    "EntityGhast", "EntityMagmaCube", "EntitySilverfish",
    "EntityBoat", "EntityMinecartEmpty",
    "EntityMinecartChest", "EntityMinecartFurnace", "EntityMinecartHopper",
    "EntityMinecartTNT", "EntityDragon", "EntityArrow",
    "EntityTippedArrow", "EntitySpectralArrow", "EntityEnderCrystal",
    "EntityEnderPearl", "EntityEnderEye", "EntitySnowball", "EntityEgg",
    "EntitySmallFireball", "EntityLargeFireball", "EntityDragonFireball",
    "EntityArmorStand",
    "EntityXPOrb",
    "EntityFallingBlock",
    "EntityTNTPrimed",
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


def movement_uses_new_look(row, prev, old_yaw):
    """Infer whether a mid-walk look change preceded this tick's movement."""
    if prev is None or "pvel" in row or "ppos" in row:
        return False
    i = row["in"]
    forward, strafe = float(i["f"]), float(i["s"])
    if not (sgn(forward) or sgn(strafe)):
        return False
    # Remove last tick's post-friction motion from this tick's displacement.
    # The remainder is the yaw-rotated input acceleration, so its direction
    # distinguishes an old-yaw move from a same-tick mouse turn without
    # guessing the block's acceleration magnitude.
    ax = float(row["x"]) - float(prev["x"]) - float(prev["vx"])
    az = float(row["z"]) - float(prev["z"]) - float(prev["vz"])
    if ax * ax + az * az < 1e-8:
        return False

    def residual(yaw):
        yr = math.radians(float(yaw))
        ex = strafe * math.cos(yr) - forward * math.sin(yr)
        ez = forward * math.cos(yr) + strafe * math.sin(yr)
        norm = ex * ex + ez * ez
        scale = max(0.0, (ax * ex + az * ez) / norm)
        dx, dz = ax - scale * ex, az - scale * ez
        return dx * dx + dz * dz

    old_error = residual(old_yaw)
    new_error = residual(row["yaw"])
    return new_error < old_error * 0.25


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
               "EntityFireball", "EntitySmallFireball", "EntityLargeFireball",
               "EntityWitherSkull",
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
    elif gui == "GuiBrewingStand":
        if 0 <= index <= 4:
            return 80 + index
        if 5 <= index <= 31:
            return index + 4
        if 32 <= index <= 40:
            return index - 32
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


def tape_hide_gui(path):
    """Return whether the goldens were captured with the F1 overlay hidden.

    Not the same thing as qrl_launch.hide_gui, which only records what the
    launcher asked for: Malmo's ClientStateMachine forces hideGUI=true for the
    duration of a mission regardless, so the launch config reads false on tapes
    whose goldens have no HUD at all. capture.hide_gui is the measured value,
    read off the goldens themselves and written into the meta.
    """
    meta = os.path.splitext(path)[0] + ".meta.json"
    try:
        with open(meta) as f:
            return bool(json.load(f).get("capture", {}).get("hide_gui", False))
    except (OSError, ValueError, TypeError):
        return False


def tape_fog_color1(path):
    """EntityRenderer.fogColor1 as the recorder saw it at t=0, or None.

    Vanilla ramps it at 0.1/tick from 0 on a fresh EntityRenderer, and
    recording starts before it converges - so the first ~40 goldens of a tape
    are darker than steady state and magma, which seeds the smoother converged,
    is flat and too bright over exactly that window. The starting value depends
    on the light the client saw while the world loaded, not on anything the tape
    records: cobweb_fall and water_dive have near-identical total_time (112 vs
    113) and start at 0.99 and 0.96. Tapes recorded before the header field
    existed return None and keep the steady-state seed.
    """
    try:
        with open(path) as f:
            header = json.loads(f.readline())
    except (OSError, ValueError):
        return None
    if not isinstance(header, dict) or not header.get("header"):
        return None
    v = header.get("fog_color1")
    try:
        v = float(v)
    except (TypeError, ValueError):
        return None
    # the recorder writes -1 when it could not read the field
    return v if 0.0 <= v <= 1.0 else None


def tape_hand_from_tick(path):
    """First tick whose goldens draw the first-person viewmodel again, or None.

    hideGUI suppresses the hand as well as the overlay
    (EntityRenderer.renderHand), but the two are not always in step: on
    20260712T055346Z the viewmodel comes back at t=2440 while no golden in the
    tape ever has a hotbar. capture.hand_from_tick is the measured tick, from
    replaying the tape with and without the hand and scoring every golden's
    lower-right quadrant. Only meaningful alongside capture.hide_gui.
    """
    meta = os.path.splitext(path)[0] + ".meta.json"
    try:
        with open(meta) as f:
            v = json.load(f).get("capture", {}).get("hand_from_tick")
        return None if v is None else int(v)
    except (OSError, ValueError, TypeError):
        return None


def tape_texture_animations_pinned(path):
    """Return whether QRL froze atlas sprites on uploaded physical frame zero."""
    meta = os.path.splitext(path)[0] + ".meta.json"
    try:
        with open(meta) as f:
            return bool(json.load(f).get("qrl_launch", {}).get("determinism", {})
                        .get("pin_texture_animations", False))
    except (OSError, ValueError, TypeError):
        return False


def tape_skin(path, header=None):
    """First-person arm skin: 'slim' (Alex) or 'default' (Steve).

    Prefer the tape header's ``skin`` field (recorded from
    AbstractClientPlayer.getSkinType). When missing (older tapes), honour
    qrl_launch.determinism.pin_skin: MixinRandomSkinTexture forces the classic
    Steve model whenever that flag is set, so replaying those goldens as Alex
    is a pure skin-mismatch (pale full-bright arm vs the oracle's dim Steve).
    Only unpinned sessions without a header field keep the historical slim
    default (Player0's offline UUID hash).
    """
    if header and header.get("skin"):
        s = header["skin"]
        return "slim" if s == "slim" else "default"
    if path:
        meta = os.path.splitext(path)[0] + ".meta.json"
        try:
            with open(meta) as f:
                pinned = bool(json.load(f).get("qrl_launch", {}).get(
                    "determinism", {}).get("pin_skin", False))
            if pinned:
                return "default"
        except (OSError, ValueError, TypeError):
            pass
    return "slim"


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
        dimension = int(row.get("dim", prev_dim))
        # A portal transit is a world transfer with NO position packet: the
        # server moves the player as part of changeDimension, so the row's own
        # dim flip is the only arrival signal in the tape (portal roundtrip
        # 075228Z: dim 0 -> -1 at t=133, first ppos not until t=168). The
        # destination needs the WHOLE pool neighbourhood, not a 3x3: its
        # tick-zero patch was applied to a world the player was not in, and a
        # non-start dimension's pool is filled by whatever the replay's own
        # generator touched on the way in.
        if dimension != prev_dim and "x" in row and "z" in row:
            arrivals.append((int(row["t"]), dimension,
                             math.floor(float(row["x"])) // 16,
                             math.floor(float(row["z"])) // 16, POOL_R))
        if "ppos" in row:
            x, _, z = row["ppos"][:3]
            cx, cz = math.floor(float(x)) // 16, math.floor(float(z)) // 16
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

    # A transfer can be signalled twice on the same tick (dim flip plus a
    # position packet), so accumulate instead of overwriting, and de-duplicate
    # the cells: applying one cell twice is harmless but doubles the script.
    by_tick = {}
    for tick, dimension, cx, cz, radius in arrivals:
        by_tick.setdefault(tick, []).append(
            {"tick": tick, "type": "snapshot_region", "dim": dimension,
             "cx": cx, "cz": cz, "radius": radius})
    seen = {tick: set() for tick, *_ in arrivals}
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
                    cell = (dimension, int(event["x"]), int(event["y"]),
                            int(event["z"]))
                    if cell in seen[tick]:
                        continue
                    seen[tick].add(cell)
                    by_tick[tick].append({**event, "tick": tick})
    return by_tick


_ARMOR_ITEM_IDS = {
    "minecraft:iron_helmet": 306,
    "minecraft:iron_chestplate": 307,
    "minecraft:iron_leggings": 308,
    "minecraft:iron_boots": 309,
    "minecraft:diamond_helmet": 310,
    "minecraft:diamond_chestplate": 311,
    "minecraft:diamond_leggings": 312,
    "minecraft:diamond_boots": 313,
}


def snapshot_armor_stands(tape_path, header, ticks):
    """Recover armor-stand equipment/display flags from the recstart save.

    The 2026-07-30 recorder models EntityArmorStand pose rows but does not yet
    append its equipment or ShowArms/NoBasePlate data. Those values are still
    authoritative in the tape's recstart Anvil snapshot. Match each tape id by
    its first recorded position and return render-only fields for ent_view.
    """
    first = {}
    last_dim = int(header.get("dim", 0))
    for row in ticks:
        dimension = int(row.get("dim", last_dim))
        last_dim = dimension
        for ent in row.get("ents", []):
            if len(ent) >= 5 and ent[1] == "EntityArmorStand":
                first.setdefault(
                    int(ent[0]),
                    (dimension, float(ent[2]), float(ent[3]), float(ent[4])),
                )
    if not first or not tape_path:
        return {}

    from pathlib import Path

    from nbt.region import RegionFile

    root = Path(tape_path).with_suffix("")
    root = root.with_name(root.name + "_world")
    dim_regions = {
        0: root / "region",
        -1: root / "DIM-1" / "region",
        1: root / "DIM1" / "region",
    }
    by_chunk = {}
    for ent_id, (dimension, x, y, z) in first.items():
        cx, cz = math.floor(x) // 16, math.floor(z) // 16
        by_chunk.setdefault((dimension, cx, cz), []).append((ent_id, x, y, z))

    recovered = {}
    for (dimension, cx, cz), targets in by_chunk.items():
        region_dir = dim_regions.get(dimension)
        if region_dir is None:
            continue
        region_path = region_dir / f"r.{cx >> 5}.{cz >> 5}.mca"
        if not region_path.is_file():
            continue
        try:
            # nbt.RegionFile(filename=...) opens r+b even for a read. The save
            # is an immutable tape input, so hand it an explicitly read-only
            # file object instead.
            with region_path.open("rb") as source:
                region = RegionFile(fileobj=source)
                chunk = region.get_chunk(cx & 31, cz & 31)
                entities = chunk["Level"].get("Entities", [])
                for saved in entities:
                    if str(saved.get("id", "")) != "minecraft:armor_stand":
                        continue
                    pos = saved.get("Pos", [])
                    if len(pos) != 3:
                        continue
                    sx, sy, sz = (float(tag.value) for tag in pos)
                    match = next(
                        (
                            target
                            for target in targets
                            if abs(target[1] - sx) < 1e-4
                            and abs(target[2] - sy) < 1e-4
                            and abs(target[3] - sz) < 1e-4
                        ),
                        None,
                    )
                    if match is None:
                        continue
                    armor = []
                    for stack in saved.get("ArmorItems", []):
                        count = int(stack.get("Count").value) if "Count" in stack else 0
                        name = str(stack.get("id", "")) if count > 0 else ""
                        armor.append(_ARMOR_ITEM_IDS.get(name, 0))
                    armor = (armor + [0, 0, 0, 0])[:4]
                    show_arms = (
                        int(saved.get("ShowArms").value)
                        if "ShowArms" in saved
                        else 0
                    )
                    no_base = (
                        int(saved.get("NoBasePlate").value)
                        if "NoBasePlate" in saved
                        else 0
                    )
                    small = (
                        int(saved.get("Small").value) if "Small" in saved else 0
                    )
                    recovered[match[0]] = {
                        "armor_feet": armor[0],
                        "armor_legs": armor[1],
                        "armor_chest": armor[2],
                        "armor_head": armor[3],
                        "stand_flags": (
                            (1 if show_arms else 0)
                            | (2 if no_base else 0)
                            | (4 if small else 0)
                        ),
                    }
        except Exception as exc:  # noqa: BLE001 - optional corrupt NBT is nonfatal
            print(
                f"[tape] WARNING: could not read armor-stand snapshot "
                f"{region_path}: {exc}"
            )
    return recovered


def tape_to_script(header, ticks, script_path, tape_path=None,
                   snapshot_override=None):
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
    # Entity.ticksExisted on the CLIENT. RenderDragon.renderCrystalBeams
    # scrolls the beam texture by -ticksExisted*0.01 per tick and pulses the
    # crystal end of the beam by sin(crystal.ticksExisted*0.2), so the beam is
    # only pixel-exact with the real counter. The recorder appends it (field 18
    # on a dragon row, 12 on a crystal row) from 2026-07-29 on; older tapes are
    # reconstructed as (tick - first tick the entity appears) + an offset,
    # which is exact for the OFFSET only, not for the absolute phase. The
    # dragon and the End crystals all become client entities the moment the
    # player is tracked into the dimension, so one shared offset covers both.
    # The default 7 is MEASURED, not guessed: sweeping the offset over its full
    # period (the scroll repeats every 100 ticks) on
    # scenario_dragon_kill_20260729T110941Z gives 76805 differing px summed over
    # ticks 300-476 at offset 7 against 109211-114438 at all 99 others - a
    # single sharp minimum, i.e. the QRL harness starts recording 7 ticks after
    # the client spawns the End entities. MAGMA_ENT_TICKS0 re-sweeps it; see
    # AGENTS.md on MAGMA_FOG_C1_INIT for the same "recorded value wins over the
    # reconstruction" rule.
    ticks0 = int(os.environ.get("MAGMA_ENT_TICKS0",
                                header.get("ent_ticks0", 7)))
    first_seen = {}
    for r in ticks:
        rt = int(r.get("tick", 0))
        for e in r.get("ents", []):
            first_seen.setdefault(int(e[0]), rt)
    armor_stands = snapshot_armor_stands(tape_path, header, ticks)
    if tape_path and os.path.exists(tape_path + ".worldpatch.jsonl"):
        with open(tape_path + ".worldpatch.jsonl") as pf:
            patch = [(max(int(json.loads(ln).get("tick", 1)), 1), ln)
                     for ln in pf if ln.strip()]
        patch.sort(key=lambda p: p[0])
    snapshot_patch = None
    arrival_events = {}
    if snapshot_override is not None:
        # Probe pass (snapshot_patch._game_states): the same script the real
        # replay runs, with the patch reduced to its snapshot_region ensures and
        # no snapshot_block. Those ensures - and the simulated walk - are what
        # fix the populate-window build order, so the world the probe generates
        # is the world the real replay will generate.
        snapshot_patch = snapshot_override
        arrival_events = snapshot_arrival_events(snapshot_patch, header, ticks)
    elif tape_path:
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
        # New recorder headers carry every string-backed GameRules entry. Keep
        # the complete object intact at the Python/C boundary: script.c honors
        # the rules magma implements and deliberately consumes the rest.
        if isinstance(header.get("gamerules"), dict):
            f.write(json.dumps({"tick": 0, "type": "set_gamerules",
                                **header["gamerules"]}) + "\n")
        # Recorder-aware tapes carry the initial player entity flag 7 plus an
        # f7 array on each tick where SPacketEntityMetadata delivered it. This
        # event enables authoritative mode in C; legacy headers omit it and
        # retain the historical one-tick predictor unchanged.
        flag7_metadata = (header.get("flag7_metadata") == 1
                          and "flag7_initial" in header)
        # look_phase tapes record the pre-travel rotation per tick (ry/rp,
        # sampled at ClientTickEvent.START), so the physics rotation is
        # observed rather than inferred: which side of travel a turn lands on
        # varies per recording (nether_elytra t=43 armed with the NEW pitch;
        # the fresh-world t371 walking turn used the OLD yaw). Legacy tapes
        # keep the movement_uses_new_look heuristic byte-for-byte.
        look_phase = header.get("look_phase") == 1
        if flag7_metadata:
            f.write(json.dumps({"tick": 0, "type": "set_elytra_flag7",
                                "flying": int(header["flag7_initial"])}) + "\n")
        if tape_has_respawn(header, ticks) and tape_is_fluid_episode(ticks):
            f.write(json.dumps({"tick": 0,
                                "type": "continue_after_death"}) + "\n")
        # first-person arm variant: header.skin when present; else pin_skin
        # meta -> Steve, else historical slim default (see tape_skin).
        f.write(json.dumps({"tick": 0, "type": "set_skin",
                            "skin": tape_skin(tape_path, header)}) + "\n")
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
        # The falling-liquid heuristics below (drop one-sided falls, clear
        # falling cells at first player contact) exist because LEGACY elytra
        # tapes recorded scenario liquids mid-growth: the post-capture world
        # save holds a fuller curtain than the goldens ever saw. Tapes whose
        # header carries fog_color1 come from the recorder generation that
        # also waits for scenario liquids to settle before recstart
        # (fix e4937f0 + cb1add4 landed together), so their saves are
        # capture-consistent and the heuristics DELETE real water/lava the
        # goldens render (t=70 curtain interior, t=150 lava-dip fog on the
        # 20260727 re-record). Scope them to the legacy tapes.
        elytra_tape = bool(ticks and len(ticks[0].get("inv", [])) > 38
                           and ticks[0]["inv"][38] != 0
                           and int(ticks[0]["inv"][38][0]) == 443
                           and (tape_path is None
                                or tape_fog_color1(tape_path) is None))
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
            for prev, row in pairwise(ticks):
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
        for row_index, row in enumerate(ticks):
            t = row["t"]
            if flag7_metadata:
                f.writelines(json.dumps({"tick": t,
                                        "type": "set_elytra_flag7",
                                        "flying": int(flying)}) + "\n" for flying in row.get("f7", []))
            if pending_elytra is not None:
                f.write(json.dumps({"tick": t, "type": "set_elytra",
                                    "equipped": pending_elytra}) + "\n")
                pending_elytra = None
            f.writelines(json.dumps({"tick": t, "type": "set_inventory", **state}) + "\n" for state in pending_inv)
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
            prev_row = ticks[row_index - 1] if row_index else None
            if look_phase and "ry" in row:
                # Recorded pre-travel rotation drives this tick's physics; the
                # deferred post-tick set_look keeps state/frame capture on the
                # recorded end-of-tick rotation.
                f.write(json.dumps({"tick": t, "type": "set_look_pre",
                                    "yaw": row["ry"], "pitch": row["rp"]}) + "\n")
                f.write(json.dumps({"tick": t, "type": "set_look",
                                    "yaw": row["yaw"], "pitch": row["pitch"]}) + "\n")
            else:
                look_before_move = (
                    move_now and look_changed and
                    (not last_move or
                     movement_uses_new_look(row, prev_row, last_yaw)))
                look_type = "set_look_pre" if look_before_move else "set_look"
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
            # SPacketExplosion applied before this client tick: knockback is
            # ADDED to the player's motion (unlike pvel, which replaces it),
            # and the packet's affected-block list is cleared client-side by
            # Explosion.doExplosionB. New recorders emit "expl"; old tapes
            # without it diverge at the blast tick (recorder gap, filed).
            for expl in row.get("expl", []):
                _ex, _ey, _ez, _strength, mx, my, mz, blocks = expl
                if mx or my or mz:
                    f.write(json.dumps({"tick": t, "type": "add_velocity",
                                        "x": float(mx), "y": float(my),
                                        "z": float(mz)}) + "\n")
                f.writelines(json.dumps({"tick": t, "type": "snapshot_block",
                                        "dim": int(row.get("dim", 0)),
                                        "x": int(bx), "y": int(by),
                                        "z": int(bz), "id": 0, "meta": 0})
                            + "\n" for bx, by, bz in blocks)
            # Exact client-side explosion-class World.spawnParticle calls.
            # One primitive script event per captured spawn stays within the
            # strict flat JSON parser while preserving tick order and doubles.
            for particle in row.get("pcl", []):
                particle_id, x, y, z, vx, vy, vz = particle
                f.write(json.dumps({"tick": t, "type": "spawn_particle",
                                    "id": int(particle_id),
                                    "x": x, "y": y, "z": z,
                                    "vx": vx, "vy": vy, "vz": vz}) + "\n")
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
            if pose_anchored or t in loading_ticks:
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
                for pot in row.get("pots", []):
                    # 4th element (recorder >= 2026-07-29) is
                    # PotionEffect.doesShowParticles: 0 means vanilla draws NO
                    # top-right icon (`/effect ... <amp> true`). Rows without
                    # it keep the shown default.
                    potion_id, amplifier, duration = pot[0], pot[1], pot[2]
                    ev = {"tick": t, "type": "potion_view",
                          "id": int(potion_id), "amplifier": int(amplifier),
                          "duration": int(duration)}
                    if len(pot) >= 4:
                        ev["show_particles"] = int(pot[3])
                    f.write(json.dumps(ev) + "\n")
                # Recorded generic.armor total. Only the recorder can know it:
                # an ItemStack with an AttributeModifiers tag REPLACES the
                # armor item's default modifiers, so magma's item-id guess is
                # wrong for e.g. the dragon_kill knockback chestplate (0, not
                # 3). Absent from older tapes -> magma keeps the guess.
                if "armor" in row:
                    f.write(json.dumps({"tick": t, "type": "armor_view",
                                        "points": int(row["armor"])}) + "\n")
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
                if e[1] == "EntityArmorStand" and int(e[0]) in armor_stands:
                    view.update(armor_stands[int(e[0])])
                if "Arrow" in e[1] and len(e) >= 8:
                    # recorder appends rotationPitch for arrows (RenderArrow
                    # Rz tilt); 7-field rows from older tapes render flat.
                    view["pitch"] = e[7]
                elif e[1] == "EntityItem" and len(e) >= 12:
                    view.update(item=e[7], item_meta=e[8], count=e[9],
                                age=e[10], hover=e[11], has_hover=1)
                elif e[1] == "EntityFallingBlock" and len(e) >= 9:
                    # recorder: Block.getIdFromBlock + meta (fallTile).
                    # 7-field legacy rows leave item unset; script.c defaults
                    # to sand (EntityFallingBlock NBT load fallback).
                    view.update(item=int(e[7]), item_meta=int(e[8]))
                elif e[1] == "EntityXPOrb" and len(e) >= 10:
                    # recorder: xpValue, xpColor, xpOrbAge after the base 7.
                    view.update(item=int(e[7]), item_meta=int(e[8]),
                                age=int(e[9]))
                elif e[1] == "EntityEnderCrystal" and len(e) >= 14:
                    # Current recorder: innerRotation, shouldShowBottom,
                    # explicit beam presence, signed target, ticksExisted.
                    view.update(crystal_rot=e[7], show_bottom=e[8],
                                has_beam=int(e[9]), beam_x=e[10],
                                beam_y=e[11], beam_z=e[12],
                                ticks_existed=int(e[13]))
                elif e[1] == "EntityEnderCrystal" and len(e) >= 13:
                    # Earlier explicit-presence rows predate ticksExisted.
                    view.update(crystal_rot=e[7], show_bottom=e[8],
                                has_beam=int(e[9]), beam_x=e[10],
                                beam_y=e[11], beam_z=e[12])
                elif e[1] == "EntityEnderCrystal" and len(e) >= 12:
                    # Legacy tapes used the ambiguous (-1,-1,-1) sentinel.
                    view.update(crystal_rot=e[7], show_bottom=e[8],
                                beam_x=e[9], beam_y=e[10], beam_z=e[11],
                                has_beam=int(not (e[9] == -1 and e[10] == -1
                                                 and e[11] == -1)))
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
                        if len(e) >= 24:
                            view.update(ticks_existed=e[18],
                                        has_heal_beam=int(e[19]),
                                        heal_x=e[20], heal_y=e[21],
                                        heal_z=e[22],
                                        heal_crystal_ticks=e[23])
                        elif len(e) >= 19:
                            view["ticks_existed"] = int(e[18])
                if "ticks_existed" not in view:
                    view["ticks_existed"] = (t - first_seen[int(e[0])]) + ticks0
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
                if gui == "GuiBrewingStand" and "gprop" in row:
                    brew, fuel = row["gprop"]
                    f.write(json.dumps({
                        "tick": t,
                        "type": "gui_brewing_view",
                        "brew": int(brew),
                        "fuel": int(fuel),
                    }) + "\n")
        # pending_inv after the final row has no consumer: inv_view already
        # supplies that row's render truth, and there is no following action
        # to re-anchor. Emitting it at t+1 makes an otherwise complete replay
        # fail with "event lies beyond --ticks".


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
    errors = []
    for slot in row.get("inventory") or []:
        slot_id = int(slot.get("slot", -1))
        if slot_id < 0 or slot_id >= 41:
            errors.append({"kind": "slot_range", "slot": slot_id})
            continue
        if slot_id in out:
            errors.append({"kind": "duplicate_slot", "slot": slot_id})
            continue
        item = int(slot.get("item", 0))
        if item <= 0:
            continue
        out[slot_id] = (item, int(slot.get("count", 1)),
                        int(slot.get("meta", 0)))
    return out, errors


def _tape_entity_types(row):
    types = []
    for ent in row.get("ents") or []:
        if len(ent) >= 2 and isinstance(ent[1], str):
            types.append(ent[1])
    return types


MAGMA_MOB_CLASS = {
    2: "EntityZombie",
    3: "EntitySkeleton",
    4: "EntityCreeper",
    5: "EntitySpider",
    6: "EntityEnderman",
    7: "EntityBlaze",
    10: "EntitySheep",
    11: "EntityPig",
    12: "EntityCow",
    13: "EntityChicken",
    15: "EntityPigZombie",
    26: "EntityGhast",
    27: "EntityMagmaCube",
    32: "EntityWitherSkeleton",
    35: "EntitySlime",
    36: "EntitySilverfish",
    37: "EntityBoat",
    39: "EntityCaveSpider",
    40: "EntityVillager",
}


def _magma_entity_class(ent):
    kind = ent.get("kind")
    if kind == "item":
        return "EntityItem"
    if kind == "xp_orb":
        return "EntityXPOrb"
    if kind == "falling_block":
        return "EntityFallingBlock"
    if kind == "primed_tnt":
        return "EntityTNTPrimed"
    if kind == "end_crystal":
        return "EntityEnderCrystal"
    if kind == "area_effect_cloud":
        return "EntityAreaEffectCloud"
    if kind == "fish_hook":
        return "EntityFishHook"
    if kind == "minecart":
        return {
            0: "EntityMinecartEmpty",
            1: "EntityMinecartChest",
            2: "EntityMinecartFurnace",
            3: "EntityMinecartTNT",
            4: "EntityMinecartMobSpawner",
            5: "EntityMinecartHopper",
            6: "EntityMinecartCommandBlock",
        }.get(int(ent.get("minecart_kind", 0)), "EntityMinecartEmpty")
    if kind == "projectile":
        return {
            1: "EntityTippedArrow",
            2: "EntityTippedArrow",
            3: "EntitySmallFireball",
            4: "EntityEnderEye",
            5: "EntityLargeFireball",
            6: "EntityPotion",
        }.get(int(ent.get("type", -1)), f"MagmaProjectile{ent.get('type')}")
    if kind == "boss":
        return {8: "EntityEnderCrystal", 9: "EntityDragon"}.get(
            int(ent.get("type", -1)), f"MagmaBoss{ent.get('type')}")
    return MAGMA_MOB_CLASS.get(int(ent.get("type", -1)),
                               f"MagmaEntity{ent.get('type')}")


def _magma_entity_types(row):
    """Map magma's kind/type pair to Java 1.11.2 simple class names."""
    types = []
    for ent in row.get("entities") or []:
        if isinstance(ent, dict) and "type" in ent:
            types.append(_magma_entity_class(ent))
    return types


LIVING_ENTITY_CLASSES = set(MAGMA_MOB_CLASS.values()) | {"EntityDragon"}
ENTITY_FLOAT_TOL = {
    "x": 1e-9,
    "y": 1e-9,
    "z": 1e-9,
    "yaw": 1e-6,
    "pitch": 1e-6,
    "health": 1e-6,
}


def _tape_entity_state(ent):
    if not isinstance(ent, (list, tuple)) or len(ent) < 7:
        return None
    typ = str(ent[1])
    state = {
        "eid": int(ent[0]),
        "type": typ,
        "x": float(ent[2]),
        "y": float(ent[3]),
        "z": float(ent[4]),
        "yaw": float(ent[5]),
        "health": float(ent[6]),
    }
    if typ in LIVING_ENTITY_CLASSES and len(ent) >= 14:
        state.update({
            "pitch": float(ent[8]),
            "hurt_time": int(ent[10]),
            "death_time": int(ent[11]),
        })
    elif typ in {"EntityArrow", "EntityTippedArrow", "EntitySpectralArrow"} \
            and len(ent) >= 8:
        state["pitch"] = float(ent[7])
    elif typ == "EntityItem" and len(ent) >= 12:
        state.update({
            "item": int(ent[7]),
            "meta": int(ent[8]),
            "count": int(ent[9]),
            "age": int(ent[10]),
        })
    elif typ == "EntityXPOrb" and len(ent) >= 10:
        state.update({
            "value": int(ent[7]),
            "color": int(ent[8]),
            "age": int(ent[9]),
        })
    elif typ == "EntityEnderCrystal" and len(ent) >= 13:
        state.update({
            "inner_rotation": int(ent[7]),
            "show_bottom": int(ent[8]),
            "has_beam": int(ent[9]),
            "beam_x": int(ent[10]),
            "beam_y": int(ent[11]),
            "beam_z": int(ent[12]),
        })
    elif typ == "EntityFallingBlock" and len(ent) >= 9:
        state.update({"block": int(ent[7]), "meta": int(ent[8])})
    return state


def _magma_entity_state(ent):
    state = {"eid": int(ent.get("eid", -1)),
             "type": _magma_entity_class(ent)}
    for field in ("x", "y", "z", "yaw", "pitch", "health", "hurt_time",
                  "death_time", "item", "meta", "count", "age", "value",
                  "color", "inner_rotation", "show_bottom", "has_beam",
                  "beam_x", "beam_y", "beam_z", "block"):
        if field in ent and ent[field] is not None:
            state[field] = ent[field]
    return state


def _compare_entity_tick(t, tape_row, magma_row, mismatches,
                         max_mismatches=20):
    """Compare a complete nearby entity set and all mutually represented state."""
    tape_entities = []
    malformed = 0
    for ent in tape_row.get("ents") or []:
        state = _tape_entity_state(ent)
        if state is None:
            malformed += 1
        else:
            tape_entities.append(state)
    magma_entities = [_magma_entity_state(ent)
                      for ent in magma_row.get("entities") or []
                      if isinstance(ent, dict)]
    total = 0

    def record(payload):
        nonlocal total
        total += 1
        if len(mismatches) < max_mismatches:
            mismatches.append({"tick": t, **payload})

    if malformed:
        record({"field": "schema", "tape_malformed": malformed})
    tape_types = {}
    magma_types = {}
    for ent in tape_entities:
        tape_types.setdefault(ent["type"], []).append(ent)
    for ent in magma_entities:
        magma_types.setdefault(ent["type"], []).append(ent)
    for typ in sorted(set(tape_types) | set(magma_types)):
        java_group = sorted(tape_types.get(typ, []), key=lambda ent: ent["eid"])
        native_group = list(magma_types.get(typ, []))
        if len(java_group) != len(native_group):
            record({"field": "count", "type": typ,
                    "tape_count": len(java_group),
                    "magma_count": len(native_group)})
        # Entity ids are process-local. Pair equal types by nearest post-tick
        # position, then compare the state fields the recorder actually carries.
        for java_ent in java_group:
            if not native_group:
                break
            best_i = min(
                range(len(native_group)),
                key=lambda i: (
                    sum((float(java_ent[field])
                         - float(native_group[i].get(field, float("inf")))) ** 2
                        for field in ("x", "y", "z")),
                    native_group[i]["eid"],
                ),
            )
            native_ent = native_group.pop(best_i)
            for field, java_value in java_ent.items():
                if field in {"eid", "type"}:
                    continue
                if field not in native_ent:
                    record({"field": field, "type": typ,
                            "tape_eid": java_ent["eid"],
                            "magma_eid": native_ent["eid"],
                            "tape": java_value, "magma": None})
                    continue
                native_value = native_ent[field]
                tolerance = ENTITY_FLOAT_TOL.get(field, 0.0)
                if abs(float(java_value) - float(native_value)) > tolerance:
                    record({"field": field, "type": typ,
                            "tape_eid": java_ent["eid"],
                            "magma_eid": native_ent["eid"],
                            "tape": java_value, "magma": native_value,
                            "abs_diff": abs(float(java_value)
                                            - float(native_value)),
                            "tolerance": tolerance})
    return total


def _nearby_blocks_schedule(ticks):
    """Return (every, offset) for raw block checkpoints, or None."""
    checkpoints = [int(row.get("t", i)) for i, row in enumerate(ticks)
                   if "nearby_blocks" in row]
    if not checkpoints:
        return None
    if len(checkpoints) == 1:
        return (len(ticks) + 1, checkpoints[0])
    differences = [b - a for a, b in zip(checkpoints, checkpoints[1:])]
    every = differences[0]
    if every <= 0 or any(diff != every for diff in differences):
        # Irregular legacy checkpoints need every row from C; comparison still
        # consumes only the Java-bearing rows.
        return (1, 0)
    return (every, checkpoints[0])


def _compare_inv_tick(t, tape_row, magma_row, mismatches, max_mismatches=20):
    """Compare all 41 slots by exact item id, count, and metadata."""
    tape_map = {}
    for slot, stack in enumerate(tape_row["inv"]):
        norm = _tape_inv_slot(stack)
        if norm is not None and slot < 41:
            tape_map[slot] = norm
    magma_map, schema_errors = _magma_inv_map(magma_row)
    for error in schema_errors:
        if len(mismatches) >= max_mismatches:
            return
        mismatches.append({"tick": t, **error})
    empty = (0, 0, 0)
    fields = ("item", "count", "meta")
    for slot in range(41):
        j = tape_map.get(slot, empty)
        c = magma_map.get(slot, empty)
        if j == c:
            continue
        if len(mismatches) >= max_mismatches:
            return
        field = next(name for name, jv, cv in zip(fields, j, c) if jv != cv)
        mismatches.append({
            "tick": t,
            "slot": slot,
            "field": field,
            "tape": {name: value for name, value in zip(fields, j)},
            "magma": {name: value for name, value in zip(fields, c)},
            # Keep the old diagnostic keys so archived report consumers remain
            # readable while the exact stack payload carries count and meta.
            "tape_item": None if j[0] == 0 else j[0],
            "magma_item": None if c[0] == 0 else c[0],
        })


def _ghost_match_tick(t, j_row, c_row, mismatches, max_mismatches=20):
    """Every modeled tape entity must reappear in magma's ingested ghost
    views at the taped position (float32 round-trip tolerance). One-way on
    purpose: magma may emit extra synthetic views (fireball impact ghosts),
    and unmodeled types are the missing_model gate's job, not this one.
    Returns the expected count, or None when the state row predates
    ghost_views emission (nothing verifiable)."""
    ghosts = c_row.get("ghost_views")
    if ghosts is None:
        return None
    expected = [e for e in (j_row.get("ents") or [])
                if len(e) >= 5 and e[1] in MODELED_ENTITY_TYPES]
    used = [False] * len(ghosts)
    for e in expected:
        ex, ey, ez = float(e[2]), float(e[3]), float(e[4])
        best, best_d = -1, None
        for gi, g in enumerate(ghosts):
            if used[gi]:
                continue
            d = max(abs(float(g["x"]) - ex), abs(float(g["y"]) - ey),
                    abs(float(g["z"]) - ez))
            if best_d is None or d < best_d:
                best, best_d = gi, d
        # float32 storage of the taped double: eps ~1.2e-7 relative, with an
        # absolute floor well under any real position corruption.
        tol = max(1e-3, 2.4e-7 * max(abs(ex), abs(ey), abs(ez)))
        if best < 0 or best_d > tol:
            if len(mismatches) < max_mismatches:
                mismatches.append({
                    "tick": t, "type": e[1], "x": ex, "y": ey, "z": ez,
                    "nearest": None if best_d is None else round(best_d, 6)})
        else:
            used[best] = True
    return len(expected)


def collect_state_assertions(ticks, c_rows, sample_every=20):
    """Build explicit non-player state assertions for scenario/gate output.

    Returns a dict with inventory, entity, and world-hash findings. This is a
    *state* gate, not a physics gate: pose/vitals stay in first_divergence.

    Inventory: check every tick that carries an ``inv`` row (sparse dumps), not
    only the sample grid. Tick 0 is compared for seed correctness but is NOT
    counted as an independent verification - replay seeds live inventory from
    that same row, so a tick-0-only tape is reported as ``seeded_only``.

    Entities: every tick with an ``ents`` row is matched against the ghost
    views magma actually ingested (see _ghost_match_tick); presence samples
    stay on the sample grid for the report.

    World: rows carrying the recorder's ``wfnv`` digest are compared against
    magma's ``nearby_hash`` whenever both sides agree on the anchor block
    (``wfa`` vs ``nearby_anchor``); anchor disagreements are counted, not
    failed - a position divergence is the physics gate's verdict. Tapes
    without ``wfnv`` keep the legacy C-only delta accounting and are marked
    verified=False: presence of a hash is not evidence.
    """
    n = min(len(ticks), len(c_rows))
    inv_checked = 0
    inv_independent = 0
    inv_mismatches = []
    ent_checked = 0
    ent_presence = []
    ent_ghost_ticks = 0
    ent_expected = 0
    ghost_mismatches = []
    ent_mismatches = []
    ent_mismatch_count = 0
    hash_checked = 0
    raw_blocks_checked = 0
    world_mismatches = []
    world_mismatch_count = 0
    hash_samples = []
    prev_hash = None
    hash_deltas = 0
    tape_has_wfnv = False
    world_compared = 0
    world_anchor_skips = 0
    # Inventory: every inv-bearing tick (change dumps + keyframes). Sparse, so
    # full coverage is cheap and catches mid-tape evolution that misses the
    # sample grid (e.g. arrow consume at t=77 when sample_every=20).
    for t in range(n):
        j = ticks[t]
        if "inv" not in j:
            continue
        inv_checked += 1
        if t > 0:
            inv_independent += 1
        _compare_inv_tick(t, j, c_rows[t], inv_mismatches)
    seeded_only = inv_checked > 0 and inv_independent == 0
    # Entity ghost match + Java/C world digest: every tick, exact first
    # failing tick semantics (both compares are cheap).
    for t in range(n):
        j, c = ticks[t], c_rows[t]
        if "ents" in j:
            exp = _ghost_match_tick(t, j, c, ghost_mismatches)
            if exp is not None:
                ent_ghost_ticks += 1
                ent_expected += exp
        wf = j.get("wfnv")
        if wf is not None:
            tape_has_wfnv = True
            nh = c.get("nearby_hash")
            if nh is not None:
                if j.get("wfa") == c.get("nearby_anchor"):
                    world_compared += 1
                    if wf != nh:
                        world_mismatch_count += 1
                        if len(world_mismatches) < 20:
                            world_mismatches.append({
                                "tick": t, "field": "wfnv",
                                "tape": wf, "magma": nh,
                                "anchor": j.get("wfa")})
                else:
                    world_anchor_skips += 1
    # Entity rows are complete post-tick snapshots, not sparse deltas. Compare
    # every one; sample_every only controls how many diagnostics are retained.
    for t in range(n):
        j, c = ticks[t], c_rows[t]
        if "ents" in j:
            ent_checked += 1
            tape_types = _tape_entity_types(j)
            magma_types = _magma_entity_types(c)
            ent_mismatch_count += _compare_entity_tick(
                t, j, c, ent_mismatches)
            if t % max(1, sample_every) == 0 and len(ent_presence) < 16:
                ent_presence.append({
                    "tick": t,
                    "tape_count": len(tape_types),
                    "tape_types": sorted(set(tape_types))[:12],
                    "magma_count": len(magma_types),
                    "magma_types": sorted(set(magma_types))[:12],
                })
        tape_hash = j.get("nearby_hash")
        magma_hash = c.get("nearby_hash")
        if tape_hash is not None:
            hash_checked += 1
            if magma_hash != tape_hash:
                world_mismatch_count += 1
                if len(world_mismatches) < 20:
                    world_mismatches.append({
                        "tick": t, "field": "nearby_hash",
                        "tape": tape_hash, "magma": magma_hash,
                    })
            if "nearby_blocks" in j:
                raw_blocks_checked += 1
                tape_blocks = j.get("nearby_blocks")
                magma_blocks = c.get("nearby_blocks")
                raw_schema_valid = (
                    isinstance(tape_blocks, list)
                    and isinstance(magma_blocks, list)
                    and len(tape_blocks) == 729
                    and len(magma_blocks) == 729)
                if not raw_schema_valid or tape_blocks != magma_blocks:
                    world_mismatch_count += 1
                    first_block = None
                    if isinstance(tape_blocks, list) and isinstance(magma_blocks, list):
                        limit = min(len(tape_blocks), len(magma_blocks))
                        first_block = next(
                            (i for i in range(limit)
                             if tape_blocks[i] != magma_blocks[i]),
                            limit if len(tape_blocks) != len(magma_blocks) else None)
                    if len(world_mismatches) < 20:
                        world_mismatches.append({
                            "tick": t, "field": "nearby_blocks",
                            "schema_valid": raw_schema_valid,
                            "first_index": first_block,
                            "tape_len": (len(tape_blocks)
                                         if isinstance(tape_blocks, list) else None),
                            "magma_len": (len(magma_blocks)
                                          if isinstance(magma_blocks, list) else None),
                            "tape": (tape_blocks[first_block]
                                     if first_block is not None
                                     and isinstance(tape_blocks, list)
                                     and first_block < len(tape_blocks) else None),
                            "magma": (magma_blocks[first_block]
                                      if first_block is not None
                                      and isinstance(magma_blocks, list)
                                      and first_block < len(magma_blocks) else None),
                        })
            if prev_hash is not None and magma_hash != prev_hash:
                hash_deltas += 1
            if t % max(sample_every * 5, 1) == 0 and len(hash_samples) < 32:
                hash_samples.append({"tick": t, "tape_hash": tape_hash,
                                     "nearby_hash": magma_hash})
            prev_hash = magma_hash
    result = {
        "kind": "state",  # not "physics"
        # How much of the tape was actually simulated. A run that stops at a
        # terminal death verifies only its prefix; without this the gate.json
        # records frames_checked with no hint that 60% of the tape was never
        # replayed, and nightly reports the tape clean. No silent caps.
        "coverage": {
            "ticks_total": len(ticks),
            "ticks_run": len(c_rows),
            "truncated": len(c_rows) < len(ticks),
        },
        "inventory": {
            "comparison": "exact_item_count_meta_all_41_slots",
            "ticks_checked": inv_checked,
            "ticks_independent": inv_independent,
            "seeded_only": seeded_only,
            "mismatches": inv_mismatches,
            "pass": inv_checked == 0 or len(inv_mismatches) == 0,
            "available": inv_checked > 0,
        },
        "entities": {
            "comparison": "exact_type_count_and_recorded_state_nearest_pair",
            "ticks_checked": ent_checked,
            "samples": ent_presence[:16],
            # Ghost match: fails when a modeled tape entity never reached
            # magma's ingested views at its taped position (dropped, capped,
            # or corrupted). verified=False means the state rows predate
            # ghost_views emission and nothing was actually checked.
            "ghost_ticks": ent_ghost_ticks,
            "ghost_expected": ent_expected,
            "ghost_mismatches": ghost_mismatches,
            "verified": ent_ghost_ticks > 0,
            "mismatches": ent_mismatches,
            "mismatch_count": ent_mismatch_count + len(ghost_mismatches),
            "pass": ((ent_checked == 0 or ent_mismatch_count == 0)
                     and (ent_ghost_ticks == 0 or not ghost_mismatches)),
            "available": ent_checked > 0 or ent_ghost_ticks > 0,
        },
        "world": {
            "comparison": "exact_fnv64_each_tick_raw_9x9x9_checkpoints",
            "ticks_checked": hash_checked,
            "raw_blocks_checked": raw_blocks_checked,
            "hash_deltas": hash_deltas,
            "samples": hash_samples,
            # mode "java": the recorder emitted its own wfnv digest and the
            # comparison is real truth. mode "c_only": legacy tape, the C
            # hash exists but verifies nothing (verified=False, informational
            # pass kept so old pin verdicts do not shift).
            "mode": "java" if tape_has_wfnv else "c_only",
            "compared": world_compared,
            "anchor_skips": world_anchor_skips,
            "verified": world_compared > 0,
            "mismatches": world_mismatches,
            "mismatch_count": world_mismatch_count,
            "pass": world_mismatch_count == 0,
            "available": hash_checked > 0 or world_compared > 0,
        },
    }
    result["pass"] = (
        not result["coverage"]["truncated"]
        and all(not result[name]["available"] or result[name]["pass"]
                for name in ("inventory", "entities", "world")))
    result["complete"] = (
        not result["coverage"]["truncated"]
        and result["inventory"]["available"]
        and result["inventory"]["ticks_independent"] > 0
        and result["entities"]["available"]
        and result["world"]["available"]
        and result["world"]["raw_blocks_checked"] > 0)
    result["strict_pass"] = result["pass"] and result["complete"]
    return result


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
    ap.add_argument("--metal", action="store_true",
                    help="render with the Metal backend (magma_game_metal;"
                         " macOS only, see magma/VERIFY.md Metal section)")
    ap.add_argument("--no-gate", action="store_true",
                    help="skip the structural pixel gate (pixel_gate.py)")
    ap.add_argument("--window-compose", action="store_true",
                    help="render frames through the interactive window compositor")
    ap.add_argument("--strict-state", action="store_true",
                    help="require complete inventory/entity/world truth, not only"
                         " absence of detected state divergences")
    args = ap.parse_args()
    # CUDA raster is the flywheel default (12k tape: 9.2 s vs 43 s CPU,
    # identical verdicts); --cpu forces the software path; --metal is the
    # macOS backend (mac_metal_verify.sh). The gate consumes the produced
    # frames identically regardless of backend.
    backend = "cpu" if args.cpu else ("metal" if args.metal else "cuda")

    name = os.path.splitext(os.path.basename(args.tape))[0]
    out = os.path.abspath(
        args.out or os.path.join(here, "out", f"tape_{name}")
    )
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
    # The recorder writes ABSOLUTE golden paths. Retiring a tape moves both the
    # jsonl and its _frames/ dir into tapes/retired/, so those baked paths go
    # dead and every golden silently vanishes - the gate then "PASSes" over 0
    # frames. Re-anchor on the tape's own <tape>_frames/ dir whenever the
    # recorded path is gone.
    if frame_ticks and not os.path.exists(frame_ticks[0][1]):
        fdir = args.tape[:-len(".jsonl")] + "_frames"
        if os.path.isdir(fdir):
            frame_ticks = [(t, os.path.join(fdir, os.path.basename(p)))
                           for t, p in frame_ticks]
            print(f"[tape] goldens re-anchored to {fdir} "
                  f"(recorded paths no longer exist)")
        else:
            raise SystemExit(f"[tape] golden frames missing: neither the "
                             f"recorded path {frame_ticks[0][1]} nor {fdir}")
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
        every = max(min((b - a for a, b in pairwise(fts)), default=1), 1)
        offset = fts[0]
    # mobs=False: magma's spawn RNG cannot match the oracle session's, so a
    # mobs-on replay grows phantom mobs that hit the player and corrupt the
    # trajectory (killed the player at t~11000 on the 12k tape). Mob parity is
    # judged against the tape's recorded ents, not magma's own spawner.
    died_early = False
    try:
        # Config registry overrides (core/config.def capture/replay toggles).
        # Passed as repeated --set key=value; MAGMA_* env for these is dead.
        replay_set = []
        replay_env = {}
        if tape_strip_overlays(args.tape):
            replay_set.append("strip_overlays=1")
        if tape_hide_gui(args.tape):
            replay_set.append("hide_gui=1")
            # An explicit MAGMA_HAND_FROM_TICK in the caller's environment wins
            # over the sidecar (still accepted as a caller-side override so
            # viewmodel investigation does not require editing shared demo/).
            # It is converted to --set hand_from_tick=N for the binary.
            hand_from = os.environ.get("MAGMA_HAND_FROM_TICK")
            if hand_from is None:
                v = tape_hand_from_tick(args.tape)
                hand_from = None if v is None else str(v)
            if hand_from is not None:
                replay_set.append(f"hand_from_tick={hand_from}")
        # EntityRenderer.fogColor1 had not converged when recording started, so
        # the first ~40 goldens of a tape ramp while magma is flat from t=0
        # (measured: suffocate_camera t=0 is 7.69 mean/ch against a 0.75 floor,
        # decaying 0.35 per 10 ticks, which is vanilla's 0.9^10). The starting
        # value is a property of the recording session and cannot be derived
        # from the tape, so magma uses it only when the recorder wrote it.
        # An explicit MAGMA_FOG_C1_INIT still wins (caller override -> --set).
        fog_c1 = os.environ.get("MAGMA_FOG_C1_INIT")
        if fog_c1 is None:
            v = tape_fog_color1(args.tape)
            fog_c1 = None if v is None else repr(v)
        if fog_c1 is not None:
            replay_set.append(f"fog_c1_init={fog_c1}")
        block_schedule = _nearby_blocks_schedule(ticks)
        if block_schedule is not None:
            block_every, block_offset = block_schedule
            replay_env["MAGMA_STATE_NEARBY_BLOCKS_EVERY"] = str(block_every)
            # write_state runs after the step and labels the first tape row as
            # runtime tick 1, so C's emission schedule is one-based.
            replay_env["MAGMA_STATE_NEARBY_BLOCKS_OFFSET"] = str(block_offset + 1)
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
                                  compose="window" if args.window_compose else "capture",
                                  extra_env=replay_env or None,
                                  set_kv=replay_set or None)
        else:
            ol.run_magma_script(scr, len(ticks), None, state,
                                  w=args.w, h=args.h,
                                  seed=int(header["seed"]), mobs=False,
                                  daylight=False, world=world,
                                  compose="window" if args.window_compose else "capture",
                                  extra_env=replay_env or None,
                                  set_kv=replay_set or None)
    except RuntimeError as e:
        # A dead magma player stops consuming script events and the run exits
        # rc=2 ("event lies beyond --ticks"). The state written up to the death
        # is still the divergence evidence we want; report it loudly.
        if not os.path.exists(state) or os.path.getsize(state) == 0:
            raise SystemExit(
                f"[tape] magma run failed before writing state: {e}"
            ) from e
        died_early = True
        print(f"[tape] WARNING: magma run ended early ({e}); "
              f"diffing the partial state")
    with open(state) as state_file:
        c_rows = [json.loads(ln) for ln in state_file]
    if not c_rows:
        # A run that produced ZERO state rows never simulated anything: the
        # binary failed at startup (stale build missing a new script event,
        # bad script). Every downstream gate would then pass vacuously over
        # nothing - the goldens-side twin of the 2026-07-29 "PASS over 0
        # frames" hole. Caught live 2026-08-02: a stale magma_game rejected
        # set_elytra_flag7 and the scenario gate reported rc=0.
        sys.exit(f"[tape] FATAL: magma produced no state rows (0 of "
                 f"{len(ticks)} ticks) - harness failure (stale binary? "
                 f"script error?), not a clean tape")
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
    if not inv_s["available"]:
        inv_status = "n/a"
        inv_detail = "0 ticks"
    elif inv_s.get("seeded_only"):
        # Tick-0 inv is the seed source; do not claim independent verification.
        inv_status = "PASS" if inv_s["pass"] else "FAIL"
        inv_detail = (f"seeded-only; {inv_s['ticks_checked']} seed tick(s), "
                      f"0 independent, {len(inv_s['mismatches'])} mismatches")
    else:
        inv_status = "PASS" if inv_s["pass"] else "FAIL"
        inv_detail = (f"{inv_s.get('ticks_independent', 0)} independent ticks, "
                      f"{inv_s['ticks_checked']} compared, "
                      f"{len(inv_s['mismatches'])} mismatches")
    ent_status = ("n/a" if not ent_s["available"] else
                  "PASS" if ent_s["pass"] else "FAIL")
    world_status = ("n/a" if not world_s["available"] else
                    "PASS" if world_s["pass"] else "FAIL")
    print(f"[tape] state: inventory {inv_status} ({inv_detail}); "
          f"entities {ent_status} "
          f"({ent_s['ticks_checked']} ticks, "
          f"{ent_s.get('mismatch_count', 0)} mismatches); "
          f"world {world_status} "
          f"({world_s['ticks_checked']} hashes, "
          f"{world_s.get('raw_blocks_checked', 0)} raw checkpoints, "
          f"{world_s.get('mismatch_count', 0)} mismatches)")
    cov = state_gate["coverage"]
    if cov["truncated"]:
        print(f"[tape] COVERAGE: only {cov['ticks_run']} of "
              f"{cov['ticks_total']} tape ticks were replayed "
              f"({100.0 * cov['ticks_run'] / max(1, cov['ticks_total']):.0f}%);"
              f" the rest of the tape is UNVERIFIED")

    # ---- pixels at the tape's sparse oracle frames (written by the one run) ----
    pix = []
    gate = None
    if frame_ticks:
        # oracle side: decoded ONCE per tape into a sidecar npy (tapes are
        # immutable); magma side: the run's npy, memory-mapped (no PPM/PNG
        # round trip at all). Frames not at the replay resolution (mid-session
        # window resize) are skipped by the cache, loudly, as before.
        from concurrent.futures import ThreadPoolExecutor

        import numpy as np
        oticks, oframes, skipped_res, missing_g = ol.oracle_frames_cache(
            frame_ticks, args.w, args.h, tape_path=args.tape)
        if missing_g:
            print(f"[tape] WARNING: {missing_g}/{len(frame_ticks)} goldens not "
                  f"found at their baked path nor next to the tape file")
        if not oticks:
            # A tape that declares goldens but resolves none used to "PASS"
            # the pixel gate over 0 frames. That is a harness failure, not a
            # clean tape - say so and fail.
            sys.exit(f"[tape] FATAL: tape declares {len(frame_ticks)} golden "
                     f"frames but none could be loaded at {args.w}x{args.h}; "
                     f"first baked path {frame_ticks[0][1]}")
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
                from scipy import ndimage as _nd  # noqa: F401

                import pixel_gate as pg
                known_divergences = pg.load_known_divergences(args.tape)
                # No overlay was drawn on either side, so the bottom rows are
                # scene pixels and must not be accepted positionally. The
                # viewmodel quadrant is the same story until the tick where the
                # oracle's hand comes back, after which both sides draw one.
                gate_hide_gui = tape_hide_gui(args.tape)
                gate_hand_from = tape_hand_from_tick(args.tape)
                # Same override as the replay env above, so a forced-hand run
                # gates the same frames it rendered.
                if os.environ.get("MAGMA_HAND_FROM_TICK") is not None:
                    gate_hand_from = int(os.environ["MAGMA_HAND_FROM_TICK"])
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
                hide_hand = gate_hide_gui and (gate_hand_from is None
                                               or t < gate_hand_from)
                clusters, mild = pg.gate_frame_ex(
                    o16, c16, args.w, args.h, tick=t,
                    known=known_divergences, hide_gui=gate_hide_gui,
                    hide_hand=hide_hand)
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
                mc_png = ol.relocate_golden(dict(frame_ticks)[t],
                                           args.tape)
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
                ol.side_by_side(ol.relocate_golden(
                                    dict(frame_ticks)[t], args.tape), cpng,
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
                    f"independent={inv_s.get('ticks_independent', 0)} "
                    f"seeded_only={inv_s.get('seeded_only', False)} "
                    f"mismatches={len(inv_s['mismatches'])} "
                    f"available={inv_s['available']} "
                    f"pass={inv_s['pass']}\n")
            f.write(f"- entities: checked={ent_s['ticks_checked']} "
                    f"ghost_ticks={ent_s.get('ghost_ticks', 0)} "
                    f"mismatches={len(ent_s.get('mismatches', []))} "
                    f"verified={ent_s.get('verified', False)} "
                    f"available={ent_s['available']} pass={ent_s['pass']}\n")
            f.write(f"- world hash: mode={world_s.get('mode', 'c_only')} "
                    f"compared={world_s.get('compared', 0)} "
                    f"anchor_skips={world_s.get('anchor_skips', 0)} "
                    f"mismatches={len(world_s.get('mismatches', []))} "
                    f"deltas={world_s['hash_deltas']} "
                    f"verified={world_s.get('verified', False)} "
                    f"available={world_s['available']} "
                    f"pass={world_s['pass']}\n\n")
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
        pixel_pass = bool(gate["pass"])
        required_state_pass = (state_gate["strict_pass"] if args.strict_state
                               else state_gate["pass"])
        gate["pixel_pass"] = pixel_pass
        gate["state_pass"] = state_gate["pass"]
        gate["strict_state_pass"] = state_gate["strict_pass"]
        gate["strict_state_required"] = args.strict_state
        gate["pass"] = pixel_pass and required_state_pass
        gate["state"] = state_gate
        gj = os.path.join(here, "report", f"tape_{name}.gate.json")
        with open(gj, "w") as f:
            json.dump(gate, f, indent=1)
        print(f"[gate] baseline -> {gj}")
        if not pixel_pass:
            # An inventory divergence must not be swallowed by a pixel failure:
            # a tape can fail both, and returning 3 alone hid two real missing
            # items on the canonical tape (t=3257 slot 1, t=3267 slot 2) behind
            # its long-standing pixel FAIL. Say it out loud before returning.
            failed_state = [
                name for name in ("inventory", "entities", "world")
                if state_gate[name]["available"]
                and not state_gate[name]["pass"]
            ]
            if failed_state:
                print("[gate] NOTE: state ALSO failed: "
                      f"{', '.join(failed_state)}; rc=3 reports the pixel "
                      "gate, the state failure is in the gate.json state block")
            return 3
    else:
        # No pixel frames: still emit a state-only gate sidecar for scenarios.
        gj = os.path.join(here, "report", f"tape_{name}.gate.json")
        os.makedirs(os.path.dirname(gj), exist_ok=True)
        required_state_pass = (state_gate["strict_pass"] if args.strict_state
                               else state_gate["pass"])
        with open(gj, "w") as f:
            json.dump({"pass": required_state_pass, "pixel_pass": True,
                       "state_pass": state_gate["pass"],
                       "strict_state_pass": state_gate["strict_pass"],
                       "strict_state_required": args.strict_state,
                       "frames_checked": 0, "classes": {},
                       "failed_frames": [], "state": state_gate}, f, indent=1)
        print(f"[gate] state-only baseline -> {gj}")
    if first is not None:
        return 4  # physics divergence: exact replay is the primary contract
    if not (state_gate["strict_pass"] if args.strict_state
            else state_gate["pass"]):
        return 5  # non-player state divergence (not physics)
    return 0


if __name__ == "__main__":
    sys.exit(main())
