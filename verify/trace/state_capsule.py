#!/usr/bin/env python3
"""state_capsule.py - Java->capsule->magma pre-tick state capsules.

Block id and metadata are not the world state.  Two worlds can read back
byte-identical from `getblocks_locked` and still diverge on the very next tick,
because 1.11.2 keeps the deciding state somewhere a cuboid read cannot see:

  scheduled ticks   WorldServer.pendingTickListEntriesTreeSet, ordered by
                    (scheduledTime, priority, tickEntryID).  A pending callback
                    is invisible in the block array.
  tile entities     TileEntityFurnace's burn/cook counters.  A furnace three
                    ticks from finishing a smelt and one that just lit are the
                    same block id with the same metadata.
  torch burnout     BlockRedstoneTorch.toggles, a *static*
                    WeakHashMap<World,List<Toggle>> of (pos, totalWorldTime).
                    Eight toggles at one position inside a 60-tick window burns
                    the torch out for 160 ticks.  It is not in the block, not in
                    the metadata, and not in the chunk NBT - it is pure JVM heap,
                    which makes it the cleanest proof in the game that "just
                    reload the save file" is not a checkpoint.

A capsule records those classes at a parked lockstep boundary and, critically,
says out loud what it can and cannot do with each one.  That is the capability
ledger: every field class is tagged

  exact          bit-faithful AND restorable into magma; a strict gate may
                 depend on it.
  captured_only  recorded faithfully, but magma has no receiver for it.  Useful
                 as evidence, never as a premise.
  unavailable    not recorded at all.

and a gate that needs a field it did not get refuses instead of running.  That
refusal is the whole point.  A continuation proof over a field the capsule never
carried is a proof about nothing, and it will still print PASS.

Design ported from bluecoconut's PR #5 magma/trace/state_capsule.py (schema name,
the three ledger classes, the manifest+binary-sidecar layout, the per-collection
`*_complete` sentinel, and tamper-evident ledger equality).  Code is
reimplemented: theirs is a 6.1k-line module against a magma that has a full
redstone engine and Java RNG injection, and this tree has neither.

Deviations from their v2, each forced by a real difference in this tree:

  * Their ledger marks world.rng.*_seed48 "exact" because their magma has
    set_world_random_seed events.  Ours are captured_only: magma's runtime RNG
    is the stateless counter hash mc_hash_seed(seed,tick,x,y,z,purpose)
    (blaze/core/mc_rng.h:1-11), which has no cursor to force.  See
    verify/trace/rng_cursor.py INJECTION_NOTE - wave-2 measured this rather than
    assuming it.
  * Their world.redstone_torch_toggle_history is "exact"; ours is captured_only.
    The Java capture is real and bit-faithful, but magma/ has no redstone at all
    (blaze/core/block_props_table.h:10 lists redstone as CUT), so there is no
    receiver.  This is the field the strict torch gate names when it refuses.
  * Their refuse-if-incomplete is global: --require-complete fails if ANY ledger
    entry is non-exact, which their own v2 can never satisfy.  A global flag
    cannot express "this gate needs the furnace counters and does not care about
    entities", so `validate --require FIELD[,FIELD...]` takes the gate's actual
    dependency set and refuses on that.  --require-complete is kept with their
    semantics for parity.
  * Refusal exits 3 (BLOCKED, named capability) rather than their 2, so a
    capability refusal is distinguishable from a malformed capsule.  Structural
    errors keep exit 2.
  * No audio.  audio.sound_events is permanently `unavailable`; this repo
    contains no sound code and this file does not add any.

Restore direction is Java -> capsule -> magma, one way.  Nothing here writes a
Java save.

CLI:

    state_capsule.py create   --dump D.json --blocks B.bin --out DIR [--drop F]
    state_capsule.py validate --capsule DIR [--require F,F] [--require-complete]
    state_capsule.py emit-magma --capsule DIR --out EVENTS.jsonl
    state_capsule.py selftest

Exit codes: 0 ok | 2 malformed capsule | 3 BLOCKED, a required capability is not
`exact` (the missing names are printed).
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import itertools
import json
import pathlib
import struct
import sys

SCHEMA = "netherite.state_capsule"
VERSION = 1
MANIFEST_FILE = "manifest.json"
BLOCK_FILE = "blocks.u16le"
BLOCK_ENCODING = "u16le:id<<4|meta:y-z-x"

EXACT = "exact"
CAPTURED_ONLY = "captured_only"
UNAVAILABLE = "unavailable"
CLASSES = (EXACT, CAPTURED_ONLY, UNAVAILABLE)

#: The registry of every field class this schema knows about, with the class it
#: gets when the Java side captured it successfully.  `exact` means the magma
#: emitter below actively restores it, so a strict gate may depend on it; the
#: other two are contract states, not commentary - validate() and the --require
#: refusal both consume them.
BASE_CAPABILITIES = {
    # -- exact: magma_events() emits a restore event for these ----------------
    "world.block_cuboid": EXACT,
    "world.time": EXACT,
    "tile_entities.furnace_smelt_state": EXACT,
    # -- captured_only: faithfully recorded, no magma receiver ---------------
    "world.scheduled_ticks": CAPTURED_ONLY,
    "world.redstone_torch_toggle_history": CAPTURED_ONLY,
    "world.rng.java_random_seed48": CAPTURED_ONLY,
    "world.rng.math_random_seed48": CAPTURED_ONLY,
    "world.rng.block_random_seed48": CAPTURED_ONLY,
    "world.rng.update_lcg": CAPTURED_ONLY,
    "world.rng_cursors": CAPTURED_ONLY,
    "player.pose_motion": CAPTURED_ONLY,
    "entities": CAPTURED_ONLY,
    # -- unavailable: not recorded -------------------------------------------
    "entities.hidden_state": UNAVAILABLE,
    "player.inventory_arbitrary_nbt": UNAVAILABLE,
    "world.light.sky_nibbles": UNAVAILABLE,
    "audio.sound_events": UNAVAILABLE,
}

#: Java capture flag -> the ledger entries it gates.  When the bridge reports
#: captured_*=false (a hostile JDK, a Forge build that moved a private field),
#: those classes downgrade to `unavailable` rather than silently claiming a
#: capture that did not happen.  Same fail-soft rule as the wave-2 RNG
#: accessors: degrade the gate to a refusal, never to a false PASS.
CAPTURE_FLAGS = {
    "captured_scheduled_ticks": ("world.scheduled_ticks",),
    "captured_tile_entities": ("tile_entities.furnace_smelt_state",),
    "captured_torch_toggles": ("world.redstone_torch_toggle_history",),
}

#: magma furnace container slot ids (magma/game/container_live.h:43).
GMC_FURNACE0 = 46


class CapsuleError(ValueError):
    """The capsule violates its on-disk contract. Structural, exit 2."""


class CapabilityRefusal(Exception):
    """A required field class is not `exact`. BLOCKED, exit 3.

    Carries the offending names so the caller can print them rather than a bare
    "incomplete", which is what makes the refusal actionable.
    """

    def __init__(self, missing, ledger):
        self.missing = list(missing)
        self.ledger = dict(ledger)
        detail = ", ".join(
            f"{name}={ledger.get(name, 'unknown')}" for name in self.missing)
        super().__init__(
            "capsule cannot support this gate; required capabilities not "
            f"exact: {detail}")


def sha256(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def cell_count(box) -> int:
    """Inclusive six-int box -> cell count, with the same bounds Java enforces."""
    if not isinstance(box, (list, tuple)) or len(box) != 6:
        raise CapsuleError("box must be six integers [x0,y0,z0,x1,y1,z1]")
    x0, y0, z0, x1, y1, z1 = (int(v) for v in box)
    if x1 < x0 or y1 < y0 or z1 < z0:
        raise CapsuleError(f"box is inverted: {list(box)}")
    if y0 < 0 or y1 > 255:
        raise CapsuleError(f"box y must lie in 0..255: {list(box)}")
    return (x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1)


def coordinate(index: int, box):
    """Linear index -> (x,y,z) for the y-major, then z, then x order."""
    x0, y0, z0, x1, _y1, z1 = (int(v) for v in box)
    nx = x1 - x0 + 1
    nz = z1 - z0 + 1
    return x0 + index % nx, y0 + index // (nx * nz), z0 + (index // nx) % nz


def block_index(box, x: int, y: int, z: int) -> int:
    x0, y0, z0, x1, _y1, z1 = (int(v) for v in box)
    nx = x1 - x0 + 1
    nz = z1 - z0 + 1
    return ((y - y0) * nz + (z - z0)) * nx + (x - x0)


# --------------------------------------------------------------------------
# ledger
# --------------------------------------------------------------------------

def derive_capabilities(dump: dict, drop=()) -> dict:
    """The ledger a capsule with this Java dump is entitled to claim.

    Deterministic in (capture flags, drop list), which is what lets validate()
    recompute it and reject a hand-edited manifest.
    """
    caps = copy.deepcopy(BASE_CAPABILITIES)
    for flag, names in CAPTURE_FLAGS.items():
        if not dump.get(flag, False):
            for name in names:
                caps[name] = UNAVAILABLE
    for name in drop:
        if name not in caps:
            raise CapsuleError(f"--drop names an unknown capability: {name}")
        caps[name] = UNAVAILABLE
    return caps


def require_exact(manifest: dict, required) -> None:
    """Refuse unless every named field class is `exact`.

    This is the refuse-if-incomplete gate, scoped to the dependency set the
    caller actually has.  A gate that compares furnace continuation needs
    tile_entities.furnace_smelt_state; it does not need entities, and a global
    all-or-nothing flag would either block it forever or wave it through.
    """
    ledger = manifest.get("capabilities") or {}
    unknown = [name for name in required if name not in BASE_CAPABILITIES]
    if unknown:
        raise CapsuleError(
            "required capability is not in the registry: " + ", ".join(unknown))
    missing = [name for name in required if ledger.get(name) != EXACT]
    if missing:
        raise CapabilityRefusal(missing, ledger)


# --------------------------------------------------------------------------
# create
# --------------------------------------------------------------------------

def _validate_dump(dump: dict, raw: bytes) -> dict:
    """Normalize + range-check one capsule_dump_locked reply."""
    if dump.get("schema") != SCHEMA:
        raise CapsuleError(
            f"dump schema is {dump.get('schema')!r}, expected {SCHEMA!r}")
    if dump.get("phase") != "pre_tick":
        raise CapsuleError("capsule must be taken at a pre_tick boundary")
    box = [int(v) for v in dump["box"]]
    cells = cell_count(box)
    if len(raw) != cells * 2:
        raise CapsuleError(
            f"{BLOCK_FILE}: expected {cells * 2} bytes, got {len(raw)}")
    if int(dump.get("cells", -1)) != cells:
        raise CapsuleError("dump.cells disagrees with the box")
    states = struct.unpack(f"<{cells}H", raw)
    bad = next((i for i, v in enumerate(states) if (v >> 4) > 4095), None)
    if bad is not None:
        raise CapsuleError(
            f"{BLOCK_FILE}: invalid block id at {coordinate(bad, box)}")

    time = dump["time"]
    total_time = int(time["total_time"])
    state = {
        "box": box,
        "time": {
            "world_time": int(time["world_time"]),
            "total_time": total_time,
            "dimension": int(time["dimension"]),
        },
        "world_rng": _validate_rng(dump["world_rng"]),
        "scheduled_ticks": _validate_scheduled(dump, box, states),
        "scheduled_ticks_complete": bool(dump.get("scheduled_ticks_complete")),
        "tile_entities": _validate_tile_entities(dump, box),
        "tile_entities_complete": bool(dump.get("tile_entities_complete")),
        "redstone_torch_toggles": _validate_torch(dump, total_time),
        "redstone_torch_toggles_complete": bool(
            dump.get("redstone_torch_toggles_complete")),
        "entity_count": int(dump.get("entity_count", 0)),
        "entities_complete": bool(dump.get("entities_complete", False)),
    }
    player = dump.get("player")
    if isinstance(player, dict):
        state["player"] = player
    return state


def _validate_rng(rng: dict) -> dict:
    out = {}
    for key in ("world_seed48", "math_seed48", "block_seed48"):
        value = int(rng[key])
        # -1 is the wave-2 fail-soft sentinel from rngSeed48().
        if value != -1 and not 0 <= value < (1 << 48):
            raise CapsuleError(f"world_rng.{key} is not a 48-bit seed: {value}")
        out[key] = value
    lcg = int(rng["update_lcg"])
    if not -(1 << 31) <= lcg < (1 << 31):
        raise CapsuleError(f"world_rng.update_lcg is not int32: {lcg}")
    out["update_lcg"] = lcg
    out["world_have_gaussian"] = bool(rng.get("world_have_gaussian", False))
    out["world_gaussian_bits"] = str(rng.get("world_gaussian_bits", "0" * 16))
    return out


def _validate_scheduled(dump: dict, box, states) -> list:
    """Pending block updates, required to be a strict total order.

    (time, priority, order) is exactly NextTickListEntry.compareTo's key, and
    tickEntryID is unique, so a correct capture is strictly increasing.  A tie
    means the capture lost an entry or read a mutating tree, and replaying it
    would pick an execution order vanilla never had.
    """
    entries = dump.get("scheduled_ticks") or []
    if not isinstance(entries, list):
        raise CapsuleError("scheduled_ticks must be an array")
    out = []
    seen = set()
    previous = None
    for index, entry in enumerate(entries):
        label = f"scheduled_ticks[{index}]"
        if set(entry) != {"x", "y", "z", "block", "time", "priority", "order"}:
            raise CapsuleError(f"{label} has an unexpected field set")
        row = {k: int(entry[k]) for k in entry}
        if not 0 <= row["y"] <= 255 or not 1 <= row["block"] <= 4095:
            raise CapsuleError(f"{label} has an invalid position/block")
        if not -128 <= row["priority"] <= 127 or row["order"] < 0:
            raise CapsuleError(f"{label} has an invalid priority/order")
        key = (row["x"], row["y"], row["z"], row["block"])
        if key in seen:
            raise CapsuleError(f"{label} duplicates a position/block key")
        seen.add(key)
        sort_key = (row["time"], row["priority"], row["order"])
        if previous is not None and sort_key <= previous:
            raise CapsuleError(
                "scheduled_ticks must be strictly ordered by "
                "time/priority/order")
        previous = sort_key
        # Cross-check against the block array: an entry whose block disagrees
        # with the cuboid means the two reads were not atomic.
        idx = block_index(box, row["x"], row["y"], row["z"])
        if 0 <= idx < len(states) and states[idx] >> 4 != row["block"]:
            raise CapsuleError(
                f"{label} block {row['block']} does not match the captured "
                f"cell state {states[idx] >> 4}")
        out.append(row)
    return out


def _validate_tile_entities(dump: dict, box) -> list:
    entries = dump.get("tile_entities") or []
    if not isinstance(entries, list):
        raise CapsuleError("tile_entities must be an array")
    out = []
    for index, entry in enumerate(entries):
        label = f"tile_entities[{index}]"
        if entry.get("type") != "furnace":
            raise CapsuleError(f"{label} has an unsupported type")
        expected = {"type", "x", "y", "z", "burn_time", "current_burn_time",
                    "cook_time", "total_cook_time", "items"}
        if set(entry) != expected:
            raise CapsuleError(f"{label} has an incomplete furnace schema")
        row = {"type": "furnace"}
        for key in ("x", "y", "z", "burn_time", "current_burn_time",
                    "cook_time", "total_cook_time"):
            row[key] = int(entry[key])
        for key in ("burn_time", "current_burn_time", "cook_time",
                    "total_cook_time"):
            if not 0 <= row[key] <= 32767:
                raise CapsuleError(f"{label}.{key} out of range: {row[key]}")
        items = []
        for slot_entry in entry["items"]:
            if set(slot_entry) != {"slot", "id", "count", "meta"}:
                raise CapsuleError(f"{label} has an invalid item schema")
            item = {k: int(slot_entry[k]) for k in slot_entry}
            if not 0 <= item["slot"] <= 2:
                raise CapsuleError(f"{label} item slot must be 0..2")
            if not 1 <= item["count"] <= 64:
                raise CapsuleError(f"{label} item count must be 1..64")
            items.append(item)
        items.sort(key=lambda i: i["slot"])
        row["items"] = items
        idx = block_index(box, row["x"], row["y"], row["z"])
        if idx < 0:
            raise CapsuleError(f"{label} lies outside the captured box")
        out.append(row)
    out.sort(key=lambda e: (e["y"], e["z"], e["x"]))
    return out


def _validate_torch(dump: dict, total_time: int) -> list:
    """BlockRedstoneTorch.toggles, chronological.

    Vanilla appends on every lit->unlit transition and prunes from the head once
    an entry is more than 60 ticks old, so the list is non-decreasing in time and
    positions repeat - eight repeats at one position is the burnout condition.
    Duplicated positions are the signal, not a defect.
    """
    entries = dump.get("redstone_torch_toggles") or []
    if not isinstance(entries, list):
        raise CapsuleError("redstone_torch_toggles must be an array")
    if len(entries) > 4096:
        raise CapsuleError("redstone_torch_toggles exceeds the 4096 bound")
    out = []
    previous = None
    for index, entry in enumerate(entries):
        label = f"redstone_torch_toggles[{index}]"
        if set(entry) != {"x", "y", "z", "time"}:
            raise CapsuleError(f"{label} must contain exactly x, y, z, time")
        row = {k: int(entry[k]) for k in entry}
        if not 0 <= row["y"] <= 255:
            raise CapsuleError(f"{label} has an invalid y")
        if not 0 <= row["time"] <= total_time:
            raise CapsuleError(
                f"{label}.time {row['time']} is outside 0..{total_time}")
        if previous is not None and row["time"] < previous:
            raise CapsuleError("redstone_torch_toggles must be chronological")
        previous = row["time"]
        out.append(row)
    return out


def create_capsule(dump: dict, raw: bytes, out_dir: pathlib.Path,
                   drop=(), source_version="1.11.2") -> pathlib.Path:
    """Write a validated capsule directory from one capsule_dump_locked reply."""
    state = _validate_dump(dump, raw)
    capabilities = derive_capabilities(dump, drop=drop)
    box = state["box"]
    manifest = {
        "schema": SCHEMA,
        "version": VERSION,
        "phase": "pre_tick",
        "source": {
            "engine": "minecraft-java",
            "version": source_version,
            "bridge": "qrl.capsule_dump_locked",
        },
        "dropped": sorted(drop),
        "state": state,
        "blocks": {
            "file": BLOCK_FILE,
            "encoding": BLOCK_ENCODING,
            "box": box,
            "cells": cell_count(box),
            "bytes": len(raw),
            "sha256": sha256(raw),
        },
        "capabilities": capabilities,
    }
    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / BLOCK_FILE).write_bytes(raw)
    (out_dir / MANIFEST_FILE).write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return out_dir


# --------------------------------------------------------------------------
# validate
# --------------------------------------------------------------------------

def validate_capsule(capsule_dir, require=(), require_complete=False):
    """Re-derive everything checkable and refuse anything that disagrees.

    The ledger is recomputed from the capsule's own recorded inputs (capture
    flags implied by the *_complete sentinels, plus the `dropped` list) and
    compared for equality, so hand-editing a manifest to claim `exact` is caught
    here rather than at the point where a gate would have trusted it.
    """
    capsule_dir = pathlib.Path(capsule_dir)
    manifest_path = capsule_dir / MANIFEST_FILE
    if not manifest_path.is_file():
        raise CapsuleError(f"{manifest_path} does not exist")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != SCHEMA:
        raise CapsuleError(f"schema is {manifest.get('schema')!r}")
    if manifest.get("version") != VERSION:
        raise CapsuleError(
            f"version {manifest.get('version')} is not {VERSION}")
    if manifest.get("phase") != "pre_tick":
        raise CapsuleError("capsule phase must be pre_tick")

    blocks = manifest["blocks"]
    if blocks.get("encoding") != BLOCK_ENCODING:
        raise CapsuleError(f"unsupported block encoding {blocks.get('encoding')}")
    box = [int(v) for v in blocks["box"]]
    cells = cell_count(box)
    if int(blocks.get("cells", -1)) != cells:
        raise CapsuleError("blocks.cells disagrees with blocks.box")
    raw = (capsule_dir / blocks["file"]).read_bytes()
    if len(raw) != cells * 2 or int(blocks.get("bytes", -1)) != len(raw):
        raise CapsuleError(
            f"{blocks['file']}: expected {cells * 2} bytes, got {len(raw)}")
    digest = sha256(raw)
    if blocks.get("sha256") != digest:
        raise CapsuleError(
            f"{blocks['file']}: sha256 mismatch "
            f"(manifest {blocks.get('sha256')}, actual {digest})")

    state = manifest["state"]
    if state.get("box") != box:
        raise CapsuleError("state.box disagrees with blocks.box")
    for name in ("scheduled_ticks", "tile_entities", "redstone_torch_toggles"):
        if not isinstance(state.get(name), list):
            raise CapsuleError(f"state.{name} must be an array")

    # A collection that was captured must say so. An empty array with
    # complete=true means "provably nothing there"; complete=false means "not
    # readable", and the ledger must already have downgraded it.
    implied = {
        "captured_scheduled_ticks": bool(state.get("scheduled_ticks_complete")),
        "captured_tile_entities": bool(state.get("tile_entities_complete")),
        "captured_torch_toggles": bool(
            state.get("redstone_torch_toggles_complete")),
    }
    expected = derive_capabilities(implied, drop=manifest.get("dropped") or ())
    if manifest.get("capabilities") != expected:
        raise CapsuleError(
            "capability ledger is missing or has been altered (it must be the "
            "ledger implied by the capture flags and the dropped list)")

    if require_complete:
        incomplete = sorted(
            k for k, v in expected.items() if v != EXACT)
        if incomplete:
            raise CapabilityRefusal(incomplete, expected)
    if require:
        require_exact(manifest, require)
    return manifest, raw


# --------------------------------------------------------------------------
# restore: Java -> capsule -> magma
# --------------------------------------------------------------------------

def magma_events(capsule_dir, anchor=None) -> list:
    """Translate every `exact` field into tick-zero magma script events.

    One direction only.  Nothing here can write a Java world, and nothing that
    is not `exact` in the ledger contributes an event - a captured_only field
    silently emitting a restore event is exactly the failure the ledger exists
    to prevent.
    """
    manifest, raw = validate_capsule(capsule_dir)
    caps = manifest["capabilities"]
    state = manifest["state"]
    box = state["box"]
    x0, _y0, z0, x1, _y1, z1 = box
    cells = cell_count(box)
    states = struct.unpack(f"<{cells}H", raw)

    events = []
    time = state["time"]
    if caps["world.time"] == EXACT:
        events.append({"tick": 0, "type": "set_time",
                       "value": int(time["world_time"])})
        events.append({"tick": 0, "type": "set_total_time",
                       "value": int(time["total_time"])})

    if caps["world.block_cuboid"] != EXACT:
        raise CapsuleError("world.block_cuboid must be exact to emit a restore")

    # Preload the chunks the cuboid touches, then overlay the captured cells.
    # snapshot_region is the bulk chunk load; snapshot_block is the dim-aware
    # per-cell load that does NOT run neighbour updates, which is what a
    # pre-tick restore wants (a placement would schedule ticks Java did not).
    dim = int(time["dimension"])
    cx0, cx1 = x0 >> 4, x1 >> 4
    cz0, cz1 = z0 >> 4, z1 >> 4
    center_cx, center_cz = (cx0 + cx1) // 2, (cz0 + cz1) // 2
    radius = max(center_cx - cx0, cx1 - center_cx,
                 center_cz - cz0, cz1 - center_cz) + 1
    events.append({"tick": 0, "type": "snapshot_region", "dim": dim,
                   "cx": center_cx, "cz": center_cz, "radius": radius})
    for index, value in enumerate(states):
        x, y, z = coordinate(index, box)
        events.append({"tick": 0, "type": "snapshot_block", "dim": dim,
                       "x": x, "y": y, "z": z,
                       "id": value >> 4, "meta": value & 0xF})

    furnaces = [e for e in state["tile_entities"] if e["type"] == "furnace"]
    if furnaces and caps["tile_entities.furnace_smelt_state"] == EXACT:
        # magma's furnace is world-positioned and ticks every runtime tick once
        # registered (magma/game/runtime.c:757); registration happens through
        # container_open, and container_furnace_prop then seeds the four
        # TileEntityFurnace counters (runtime.c:1896).  Opening furnace B leaves
        # A registered and still ticking - only `active_furnace` moves - so every
        # captured furnace is restored by opening it in turn, and the PRIMARY
        # (lowest y,z,x) is re-opened last because magma's per-tick state row
        # reports only the active one, and that row is what the continuation is
        # compared on.
        for furnace in furnaces + ([furnaces[0]] if len(furnaces) > 1 else []):
            px, py, pz = anchor or default_anchor(furnace)
            events.append({"tick": 0, "type": "set_pose", "x": px, "y": py,
                           "z": pz, "yaw": 0.0, "pitch": 0.0})
            events.append({"tick": 0, "type": "container_open", "window_id": 1,
                           "ctype": "furnace", "x": furnace["x"],
                           "y": furnace["y"], "z": furnace["z"]})
            by_slot = {item["slot"]: item for item in furnace["items"]}
            for slot in (0, 1, 2):
                item = by_slot.get(slot)
                events.append({
                    "tick": 0, "type": "container_slot",
                    "slot": GMC_FURNACE0 + slot,
                    "item": item["id"] if item else 0,
                    "count": item["count"] if item else 0,
                    "meta": item["meta"] if item else 0,
                })
            events.append({
                "tick": 0, "type": "container_furnace_prop",
                "burn": furnace["burn_time"],
                "current_burn": furnace["current_burn_time"],
                "cook": furnace["cook_time"],
                "total_cook": furnace["total_cook_time"],
            })
    return events


def default_anchor(furnace: dict):
    """Player stance adjacent to the furnace, inside magma's interaction reach.

    gm_runtime_container_open goes through the survival use path, which enforces
    vanilla's canInteractWith range (runtime.c:519, dist^2 > 36 rejects), so the
    restore has to stand the player next to the block it is about to open.
    """
    return (furnace["x"] + 1.5, float(furnace["y"]), furnace["z"] + 0.5)


def emit_magma(capsule_dir, out_path, anchor=None) -> int:
    events = magma_events(capsule_dir, anchor=anchor)
    out_path = pathlib.Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as stream:
        for row in events:
            stream.write(json.dumps(row, separators=(",", ":")) + "\n")
    return len(events)


# --------------------------------------------------------------------------
# fixtures
# --------------------------------------------------------------------------

def parse_sequence(text: str) -> list:
    """Parse a `.sequence` fixture: `TICK DX DY DZ BLOCK META` rows, # comments.

    Adopted unchanged from PR #5's magma/trace/fixtures - the review called the
    fixtures "clean text formats we can consume nearly directly", and this is
    the one carrying the torch-burnout drive sequence.
    """
    rows = []
    for line_no, line in enumerate(text.splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 6:
            raise CapsuleError(
                f"sequence line {line_no}: expected 6 fields, got {len(parts)}")
        try:
            tick, dx, dy, dz, block, meta = (int(p) for p in parts)
        except ValueError as exc:
            raise CapsuleError(
                f"sequence line {line_no}: non-integer field") from exc
        rows.append({"tick": tick, "dx": dx, "dy": dy, "dz": dz,
                     "block": block, "meta": meta})
    if not rows:
        raise CapsuleError("sequence fixture is empty")
    if any(b["tick"] < a["tick"] for a, b in itertools.pairwise(rows)):
        raise CapsuleError("sequence fixture must be tick-sorted")
    return rows


# --------------------------------------------------------------------------
# selftest
# --------------------------------------------------------------------------

def _synthetic_dump():
    box = [0, 64, 0, 2, 65, 2]
    cells = cell_count(box)
    states = [1 << 4] * cells
    # a lit furnace (id 62) at (1,64,1) and a lit redstone torch (76) at (2,65,2)
    states[block_index(box, 1, 64, 1)] = (62 << 4) | 2
    states[block_index(box, 2, 65, 2)] = (76 << 4) | 5
    raw = struct.pack(f"<{cells}H", *states)
    dump = {
        "schema": SCHEMA, "phase": "pre_tick", "box": box, "cells": cells,
        "time": {"world_time": 6000, "total_time": 41234, "dimension": 0},
        "world_rng": {"world_seed48": 0x5DEECE664, "math_seed48": 12345,
                      "block_seed48": 999, "update_lcg": 1094913777,
                      "world_have_gaussian": False,
                      "world_gaussian_bits": "0000000000000000"},
        "scheduled_ticks": [
            {"x": 2, "y": 65, "z": 2, "block": 76, "time": 41236,
             "priority": 0, "order": 91},
        ],
        "scheduled_ticks_complete": True,
        "tile_entities": [
            {"type": "furnace", "x": 1, "y": 64, "z": 1, "burn_time": 800,
             "current_burn_time": 1600, "cook_time": 77,
             "total_cook_time": 200,
             "items": [{"slot": 0, "id": 15, "count": 3, "meta": 0},
                       {"slot": 1, "id": 263, "count": 5, "meta": 0}]},
        ],
        "tile_entities_complete": True,
        "redstone_torch_toggles": [
            {"x": 2, "y": 65, "z": 2, "time": 41180},
            {"x": 2, "y": 65, "z": 2, "time": 41200},
        ],
        "redstone_torch_toggles_complete": True,
        "entity_count": 4, "entities_complete": False,
        "captured_scheduled_ticks": True, "captured_tile_entities": True,
        "captured_torch_toggles": True,
    }
    return dump, raw


def selftest() -> None:
    import tempfile

    dump, raw = _synthetic_dump()
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)

        capsule = create_capsule(dump, raw, tmp / "full")
        manifest, _ = validate_capsule(capsule)
        caps = manifest["capabilities"]
        assert caps["tile_entities.furnace_smelt_state"] == EXACT
        assert caps["world.redstone_torch_toggle_history"] == CAPTURED_ONLY
        assert caps["audio.sound_events"] == UNAVAILABLE

        # a gate that only needs the furnace runs
        validate_capsule(capsule, require=("tile_entities.furnace_smelt_state",))

        # a gate that needs the torch history is refused, by name
        try:
            validate_capsule(
                capsule, require=("world.redstone_torch_toggle_history",))
        except CapabilityRefusal as exc:
            assert exc.missing == ["world.redstone_torch_toggle_history"]
            assert "captured_only" in str(exc)
        else:
            raise AssertionError("torch-history requirement was not refused")

        # their global semantics still refuse, since captured_only exists
        try:
            validate_capsule(capsule, require_complete=True)
        except CapabilityRefusal as exc:
            assert "world.rng_cursors" in exc.missing
        else:
            raise AssertionError("--require-complete passed a partial capsule")

        # dropping a field downgrades it and the furnace gate then refuses
        dropped = create_capsule(dump, raw, tmp / "dropped",
                                 drop=("tile_entities.furnace_smelt_state",))
        try:
            validate_capsule(
                dropped, require=("tile_entities.furnace_smelt_state",))
        except CapabilityRefusal as exc:
            assert exc.missing == ["tile_entities.furnace_smelt_state"]
        else:
            raise AssertionError("dropped furnace state was not refused")

        # and its restore emits no furnace events at all
        events = magma_events(dropped)
        assert not any(e["type"].startswith("container") for e in events)
        full_events = magma_events(capsule)
        prop = [e for e in full_events if e["type"] == "container_furnace_prop"]
        assert len(prop) == 1 and prop[0]["burn"] == 800 and prop[0]["cook"] == 77
        slots = [e for e in full_events if e["type"] == "container_slot"]
        assert len(slots) == 3 and slots[0]["item"] == 15
        assert any(e["type"] == "snapshot_region" for e in full_events)
        assert sum(1 for e in full_events if e["type"] == "snapshot_block") == 18

        # a failed Java capture downgrades rather than claiming a capture
        soft = copy.deepcopy(dump)
        soft["captured_torch_toggles"] = False
        soft["redstone_torch_toggles"] = []
        soft["redstone_torch_toggles_complete"] = False
        soft_capsule = create_capsule(soft, raw, tmp / "soft")
        soft_manifest, _ = validate_capsule(soft_capsule)
        assert soft_manifest["capabilities"][
            "world.redstone_torch_toggle_history"] == UNAVAILABLE

        # tamper-evidence: promoting a class by hand is rejected
        tampered = tmp / "tampered"
        create_capsule(dump, raw, tampered)
        path = tampered / MANIFEST_FILE
        doc = json.loads(path.read_text(encoding="utf-8"))
        doc["capabilities"]["world.redstone_torch_toggle_history"] = EXACT
        path.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
        try:
            validate_capsule(tampered)
        except CapsuleError as exc:
            assert "altered" in str(exc)
        else:
            raise AssertionError("hand-promoted ledger passed validation")

        # payload integrity
        corrupt = tmp / "corrupt"
        create_capsule(dump, raw, corrupt)
        (corrupt / BLOCK_FILE).write_bytes(b"\x00" * len(raw))
        try:
            validate_capsule(corrupt)
        except CapsuleError as exc:
            assert "sha256 mismatch" in str(exc)
        else:
            raise AssertionError("corrupt block payload passed validation")

        # scheduled ticks must be a strict total order
        unordered = copy.deepcopy(dump)
        unordered["scheduled_ticks"] = [
            {"x": 0, "y": 64, "z": 0, "block": 1, "time": 5,
             "priority": 0, "order": 2},
            {"x": 1, "y": 65, "z": 0, "block": 1, "time": 5,
             "priority": 0, "order": 1},
        ]
        try:
            create_capsule(unordered, raw, tmp / "unordered")
        except CapsuleError as exc:
            assert "strictly ordered" in str(exc)
        else:
            raise AssertionError("out-of-order scheduled ticks were accepted")

        # torch history must be chronological
        backwards = copy.deepcopy(dump)
        backwards["redstone_torch_toggles"] = [
            {"x": 2, "y": 65, "z": 2, "time": 41200},
            {"x": 2, "y": 65, "z": 2, "time": 41180},
        ]
        try:
            create_capsule(backwards, raw, tmp / "backwards")
        except CapsuleError as exc:
            assert "chronological" in str(exc)
        else:
            raise AssertionError("non-chronological toggles were accepted")

        # sequence fixture parsing
        rows = parse_sequence(
            "# comment\n0 4 -1 0 152 0\n2 4 -1 0 1 0\n")
        assert rows == [
            {"tick": 0, "dx": 4, "dy": -1, "dz": 0, "block": 152, "meta": 0},
            {"tick": 2, "dx": 4, "dy": -1, "dz": 0, "block": 1, "meta": 0},
        ]
    print(f"state_capsule selftest OK: schema={SCHEMA} version={VERSION} "
          f"registry={len(BASE_CAPABILITIES)} classes")


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    create = sub.add_parser("create")
    create.add_argument("--dump", required=True, type=pathlib.Path)
    create.add_argument("--blocks", required=True, type=pathlib.Path)
    create.add_argument("--out", required=True, type=pathlib.Path)
    create.add_argument("--drop", default="",
                        help="comma-separated capabilities to force unavailable")

    validate = sub.add_parser("validate")
    validate.add_argument("--capsule", required=True, type=pathlib.Path)
    validate.add_argument("--require", default="",
                          help="comma-separated capabilities this gate needs")
    validate.add_argument("--require-complete", action="store_true")

    emit = sub.add_parser("emit-magma")
    emit.add_argument("--capsule", required=True, type=pathlib.Path)
    emit.add_argument("--out", required=True, type=pathlib.Path)

    sub.add_parser("selftest")
    return parser.parse_args(argv)


def _split(value):
    return tuple(v for v in (value or "").split(",") if v)


def main(argv=None) -> int:
    args = parse_args(argv)
    try:
        if args.command == "create":
            dump = json.loads(args.dump.read_text(encoding="utf-8"))
            raw = args.blocks.read_bytes()
            path = create_capsule(dump, raw, args.out, drop=_split(args.drop))
            manifest = json.loads(
                (path / MANIFEST_FILE).read_text(encoding="utf-8"))
            exact = sum(v == EXACT for v in manifest["capabilities"].values())
            print(f"wrote validated state capsule -> {path} "
                  f"(exact {exact}/{len(BASE_CAPABILITIES)})")
        elif args.command == "validate":
            manifest, _ = validate_capsule(
                args.capsule, require=_split(args.require),
                require_complete=args.require_complete)
            exact = sum(v == EXACT for v in manifest["capabilities"].values())
            print(f"state capsule valid: schema={SCHEMA} version={VERSION} "
                  f"exact_capabilities={exact}/{len(BASE_CAPABILITIES)}")
        elif args.command == "emit-magma":
            count = emit_magma(args.capsule, args.out)
            print(f"wrote {count} tick-zero magma events -> {args.out}")
        else:
            selftest()
    except CapabilityRefusal as exc:
        print(f"BLOCKED: {exc}", file=sys.stderr)
        for name in exc.missing:
            print(f"BLOCKED: missing capability {name}="
                  f"{exc.ledger.get(name, 'unknown')}", file=sys.stderr)
        return 3
    except (OSError, CapsuleError) as exc:
        print(f"state_capsule: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
