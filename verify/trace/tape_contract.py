"""Tape / replay harness contracts (capabilities, phases, gate status, hashes).

Backwards-compatible: legacy tapes without capability declarations keep their
historical availability inference and do not fail closed on empty optional
channels. New tapes may declare capabilities explicitly (header and/or
``.meta.json``); declared capabilities that produce zero evidence events fail
closed.

This module does not change simulation. Script events emitted to magma must
NOT carry a ``phase`` field: ``magma/game/script.c`` uses a strict keys_only
parser. Phases are a Python-side ordering contract for reports and tests.

Schema version: 1
"""
from __future__ import annotations

import hashlib
import json
import os
import platform
import sys
from collections.abc import Iterable, Mapping, Sequence
from typing import Any

# ---------------------------------------------------------------------------
# Schema constants
# ---------------------------------------------------------------------------

SCHEMA_VERSION = 1

# Formal event phases, in vanilla client-tick order.
EVENT_PHASES = (
    "INPUT",             # recorded look / movement intent for this tick
    "PRE_TICK_PACKET",   # SPacket* applied before the client tick
    "CLIENT_ACTION",     # action / container_click / craft before sim
    "SIMULATION",        # magma gm_runtime_tick (implicit; not a script event)
    "POST_TICK_FINAL",   # post-sim reanchors (block finals, inv, pose_post)
    "OBSERVATION",       # render-only views + frame capture
)

# Explicit tape capabilities. Declaration is optional; absence = legacy.
CAPABILITIES = (
    "block_finals",          # row field "bc" -> set_block_post
    "gui_clicks",            # row field "gclk" -> container_click
    "container_identity",    # open-container world identity (gpos / cid)
    "inventory_keyframes",   # sparse "inv" dumps / keyframes
    "dig_trace",             # dig/break progress trace (dig / bp / curblock)
    "packet_health",         # authoritative health packet arrivals (hpu / hp_pkt)
    "renderer_provenance",   # capture platform provenance for absolute pixels
    "world_snapshot",        # recstart Anvil save <tape>_world/
)

# Evidence-backed gates that must report verified | blocked | unavailable.
EVIDENCE_GATES = (
    "physics",
    "inventory",
    "entities",
    "world",
    "pixels",
    "block_finals",
    "gui_clicks",
    "container_identity",
    "inventory_keyframes",
    "dig_trace",
    "packet_health",
    "renderer_provenance",
    "world_snapshot",
)

GATE_STATUS = ("verified", "blocked", "unavailable")

# Map capability -> tape tick-row field aliases that count as evidence events.
CAPABILITY_TICK_FIELDS: dict[str, tuple[str, ...]] = {
    "block_finals": ("bc",),
    "gui_clicks": ("gclk",),
    "container_identity": ("gopen", "gclose", "gpos", "cid",
                           "container_pos", "container_id"),
    "inventory_keyframes": ("inv",),
    "dig_trace": ("dig", "bp", "curblock", "break_progress"),
    "packet_health": ("hpu", "hp_pkt", "phealth", "health_pkt"),
    # renderer_provenance / world_snapshot are sidecar/fs evidence, not tick fields
}

# Magma script event type -> formal phase (Python-side only).
EVENT_TYPE_PHASE: dict[str, str] = {
    # INPUT
    "set_look": "INPUT",
    "set_look_pre": "INPUT",
    # PRE_TICK_PACKET
    "set_packet_velocity": "PRE_TICK_PACKET",
    "add_velocity": "PRE_TICK_PACKET",
    "set_pose": "PRE_TICK_PACKET",  # ppos authoritative pose (also seed at t0)
    "set_velocity": "PRE_TICK_PACKET",
    "spawn_particle": "PRE_TICK_PACKET",
    "dragon_contact": "PRE_TICK_PACKET",
    "set_elytra_flag7": "PRE_TICK_PACKET",
    # CLIENT_ACTION
    "action": "CLIENT_ACTION",
    "container_click": "CLIENT_ACTION",
    "craft": "CLIENT_ACTION",
    "container_open": "CLIENT_ACTION",
    "container_slot": "CLIENT_ACTION",
    "container_cursor": "CLIENT_ACTION",
    "container_furnace_prop": "CLIENT_ACTION",
    "container_close": "CLIENT_ACTION",
    # POST_TICK_FINAL
    "set_block_post": "POST_TICK_FINAL",
    "set_inventory": "POST_TICK_FINAL",
    "set_elytra": "POST_TICK_FINAL",
    "set_vitals": "POST_TICK_FINAL",
    "set_vitals_post": "POST_TICK_FINAL",
    "set_regen_post": "POST_TICK_FINAL",
    "hold_regen_post": "POST_TICK_FINAL",
    "clear_hurt_velocity_post": "POST_TICK_FINAL",
    "hold_fall_damage_post": "POST_TICK_FINAL",
    "set_food_stats_post": "POST_TICK_FINAL",
    "set_pose_post": "POST_TICK_FINAL",
    "set_block": "POST_TICK_FINAL",
    "snapshot_block": "POST_TICK_FINAL",
    "snapshot_region": "POST_TICK_FINAL",
    "set_time": "POST_TICK_FINAL",
    "set_total_time": "POST_TICK_FINAL",
    "set_dimension": "POST_TICK_FINAL",
    "set_gamerules": "POST_TICK_FINAL",
    "set_skin": "POST_TICK_FINAL",
    "continue_after_death": "POST_TICK_FINAL",
    # OBSERVATION
    "inv_view": "OBSERVATION",
    "ent_view": "OBSERVATION",
    "ent_box": "OBSERVATION",
    "gui_view": "OBSERVATION",
    "gui_slot_view": "OBSERVATION",
    "gui_cursor_view": "OBSERVATION",
    "gui_furnace_view": "OBSERVATION",
    "player_view": "OBSERVATION",
    "potion_view": "OBSERVATION",
    "potion_clear": "OBSERVATION",
    "armor_view": "OBSERVATION",
}

# Renderer provenance fields that absolute pixel gates require to agree.
PROVENANCE_FIELDS = (
    "os",
    "arch",
    "api",           # e.g. gl, metal, vulkan
    "backend",       # e.g. llvmpipe, nvidia, apple
    "gpu",
    "version",       # driver / GL version string
    "resolution",    # "WxH"
    "scaling",       # gui scale / framebuffer scale
    "hide_gui",
    "assets_hash",
)

# Oracle and magma necessarily use different renderer implementations (OpenGL
# vs the C software/CUDA/Metal backend). Absolute-gate compatibility is about
# the calibrated host/output envelope, not identical APIs.
PIXEL_COMPAT_FIELDS = (
    "os", "arch", "resolution", "scaling", "hide_gui", "assets_hash",
)
PIXEL_REQUIRED_FIELDS = ("os", "arch", "resolution")


# Public schema document (returned in reports; also the contract for tests).
SCHEMA: dict[str, Any] = {
    "schema_version": SCHEMA_VERSION,
    "event_phases": list(EVENT_PHASES),
    "capabilities": list(CAPABILITIES),
    "evidence_gates": list(EVIDENCE_GATES),
    "gate_status": list(GATE_STATUS),
    "capability_tick_fields": {k: list(v) for k, v in CAPABILITY_TICK_FIELDS.items()},
    "event_type_phase": dict(EVENT_TYPE_PHASE),
    "provenance_fields": list(PROVENANCE_FIELDS),
    "input_hash_keys": (
        "tape",
        "sidecars",
        "snapshot_patch",
        "magma_binary",
        "replay_script",
        "effective_config",
        "assets",
        "gate_implementation",
    ),
}


# ---------------------------------------------------------------------------
# Capability parse / evidence
# ---------------------------------------------------------------------------

def _normalize_capability_list(raw: Any) -> set[str]:
    """Accept list[str], dict[str,bool], or None."""
    if raw is None:
        return set()
    if isinstance(raw, Mapping):
        out = set()
        for k, v in raw.items():
            if v:
                name = str(k)
                if name not in CAPABILITIES:
                    raise ValueError(f"unknown tape capability: {name!r}")
                out.add(name)
        return out
    if isinstance(raw, (list, tuple, set)):
        out = set()
        for item in raw:
            name = str(item)
            if name not in CAPABILITIES:
                raise ValueError(f"unknown tape capability: {name!r}")
            out.add(name)
        return out
    raise ValueError(f"capabilities must be list or object, got {type(raw)}")


def load_meta(tape_path: str | None) -> dict[str, Any]:
    if not tape_path:
        return {}
    meta_path = os.path.splitext(tape_path)[0] + ".meta.json"
    try:
        with open(meta_path) as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError, TypeError):
        return {}


def resolve_capabilities(header: Mapping[str, Any],
                         meta: Mapping[str, Any] | None = None) -> dict[str, Any]:
    """Merge header + meta capability declarations.

    Returns:
      {
        "declared": sorted list (explicit only),
        "legacy": True when nothing was declared,
        "source": "header" | "meta" | "header+meta" | None,
      }
    """
    meta = meta or {}
    header_caps = _normalize_capability_list(header.get("capabilities"))
    meta_caps = _normalize_capability_list(meta.get("capabilities"))
    declared = header_caps | meta_caps
    if header_caps and meta_caps:
        source = "header+meta"
    elif header_caps:
        source = "header"
    elif meta_caps:
        source = "meta"
    else:
        source = None
    return {
        "declared": sorted(declared),
        "legacy": not declared,
        "source": source,
    }


def count_tick_field_events(ticks: Sequence[Mapping[str, Any]],
                            fields: Sequence[str]) -> int:
    """Count non-empty evidence payloads for any of the given tick fields."""
    n = 0
    for row in ticks:
        for field in fields:
            if field not in row:
                continue
            val = row[field]
            if val in (None, 0, "", [], {}):
                continue
            if isinstance(val, list) and len(val) == 0:
                continue
            n += 1
            break
    return n


def capability_evidence(header: Mapping[str, Any],
                        ticks: Sequence[Mapping[str, Any]],
                        tape_path: str | None = None,
                        meta: Mapping[str, Any] | None = None) -> dict[str, dict[str, Any]]:
    """Per-capability evidence census (events + where found)."""
    meta = meta if meta is not None else load_meta(tape_path)
    out: dict[str, dict[str, Any]] = {}
    for cap in CAPABILITIES:
        fields = CAPABILITY_TICK_FIELDS.get(cap, ())
        events = count_tick_field_events(ticks, fields) if fields else 0
        sources: list[str] = []
        if events:
            sources.append("ticks")
        if cap == "world_snapshot" and tape_path:
            world_dir = os.path.splitext(tape_path)[0] + "_world"
            if os.path.isdir(os.path.join(world_dir, "region")):
                events = max(events, 1)
                sources.append("world_dir")
            snap = tape_path + ".snapshot_patch.jsonl"
            if os.path.isfile(snap) and os.path.getsize(snap) > 0:
                events = max(events, 1)
                sources.append("snapshot_patch")
        if cap == "renderer_provenance":
            prov = extract_renderer_provenance(header, meta)
            if any(prov.get(k) is not None for k in PROVENANCE_FIELDS):
                events = max(events, 1)
                sources.append("provenance")
        if cap == "inventory_keyframes" and events == 0:
            # header may seed inventory indirectly; still require tick inv rows
            pass
        out[cap] = {
            "events": events,
            "fields": list(fields),
            "sources": sources,
        }
    return out


# ---------------------------------------------------------------------------
# Gate status classification
# ---------------------------------------------------------------------------

def classify_gate(*,
                  declared: bool,
                  evidence_events: int,
                  compared: int,
                  mismatches: int = 0,
                  blocked_reason: str | None = None,
                  legacy: bool = True) -> dict[str, Any]:
    """Classify one evidence-backed gate as verified | blocked | unavailable.

    Rules:
      - blocked_reason set -> blocked (never pass/fail)
      - declared and evidence_events == 0 -> blocked (fail-closed caller)
      - compared > 0 -> verified (pass = mismatches == 0)
      - undeclared and no evidence -> unavailable (legacy silent skip)
      - evidence but compared == 0 -> blocked (cannot verify)
    """
    if blocked_reason:
        return {
            "status": "blocked",
            "pass": None,
            "compared": compared,
            "events": evidence_events,
            "mismatches": mismatches,
            "reason": blocked_reason,
        }
    if declared and evidence_events == 0:
        return {
            "status": "blocked",
            "pass": False,
            "compared": compared,
            "events": 0,
            "mismatches": mismatches,
            "reason": "declared_capability_produced_no_events",
        }
    if compared > 0:
        return {
            "status": "verified",
            "pass": mismatches == 0,
            "compared": compared,
            "events": evidence_events,
            "mismatches": mismatches,
            "reason": None,
        }
    if evidence_events > 0:
        return {
            "status": "blocked",
            "pass": None,
            "compared": 0,
            "events": evidence_events,
            "mismatches": mismatches,
            "reason": "evidence_present_but_not_compared",
        }
    if declared:
        return {
            "status": "blocked",
            "pass": False,
            "compared": 0,
            "events": 0,
            "mismatches": mismatches,
            "reason": "declared_capability_produced_no_events",
        }
    # legacy / undeclared / no evidence
    return {
        "status": "unavailable",
        "pass": None if not legacy else True,  # legacy informational green
        "compared": 0,
        "events": 0,
        "mismatches": 0,
        "reason": "legacy_undeclared" if legacy else "no_evidence",
    }


def classify_state_gates(state_gate: Mapping[str, Any],
                         caps: Mapping[str, Any],
                         evidence: Mapping[str, Mapping[str, Any]]) -> dict[str, dict[str, Any]]:
    """Attach status to inventory / entities / world plus capability channels."""
    declared = set(caps.get("declared") or [])
    legacy = bool(caps.get("legacy", True))
    inv = state_gate.get("inventory") or {}
    ent = state_gate.get("entities") or {}
    world = state_gate.get("world") or {}

    inv_events = evidence.get("inventory_keyframes", {}).get("events", 0)
    inv_compared = int(inv.get("ticks_independent", 0) or 0)
    if inv_compared == 0 and inv.get("ticks_checked", 0) and not inv.get("seeded_only"):
        inv_compared = int(inv.get("ticks_checked", 0))
    # seeded-only is not independent verification
    if inv.get("seeded_only"):
        inv_cls = classify_gate(
            declared="inventory_keyframes" in declared,
            evidence_events=inv_events,
            compared=0,
            mismatches=len(inv.get("mismatches") or []),
            blocked_reason="seeded_only_not_independent",
            legacy=legacy,
        )
    else:
        inv_cls = classify_gate(
            declared="inventory_keyframes" in declared,
            evidence_events=inv_events,
            compared=inv_compared,
            mismatches=len(inv.get("mismatches") or []),
            legacy=legacy,
        )

    ent_compared = int(ent.get("ghost_ticks", 0) or 0)
    ent_events = ent_compared or int(ent.get("ticks_checked", 0) or 0)
    ent_cls = classify_gate(
        declared=False,  # entities are always on when ents present; no cap name
        evidence_events=ent_events,
        compared=ent_compared,
        mismatches=len(ent.get("mismatches") or []),
        legacy=legacy,
    )
    if ent_events > 0 and not ent.get("verified", False) and ent_compared == 0:
        ent_cls = classify_gate(
            declared=False,
            evidence_events=ent_events,
            compared=0,
            mismatches=0,
            blocked_reason="ghost_views_unavailable",
            legacy=legacy,
        )

    world_events = 1 if world.get("mode") == "java" or world.get("available") else 0
    world_compared = int(world.get("compared", 0) or 0)
    if world.get("mode") == "c_only":
        world_cls = classify_gate(
            declared="world_snapshot" in declared,
            evidence_events=evidence.get("world_snapshot", {}).get("events", 0) or world_events,
            compared=0,
            mismatches=0,
            blocked_reason="c_only_hash_unverified",
            legacy=legacy,
        )
    else:
        world_cls = classify_gate(
            declared="world_snapshot" in declared,
            evidence_events=max(
                evidence.get("world_snapshot", {}).get("events", 0),
                world_events,
            ),
            compared=world_compared,
            mismatches=len(world.get("mismatches") or []),
            legacy=legacy,
        )

    result = {
        "inventory": inv_cls,
        "entities": ent_cls,
        "world": world_cls,
    }
    # Capability channels beyond the three state gates
    for cap in CAPABILITIES:
        if cap in ("inventory_keyframes", "world_snapshot"):
            # already folded above
            continue
        ev = int(evidence.get(cap, {}).get("events", 0) or 0)
        is_decl = cap in declared
        if cap == "renderer_provenance":
            # classified separately via provenance check
            continue
        cap_gate = state_gate.get(cap) if isinstance(state_gate, Mapping) else None
        paired = (isinstance(cap_gate, Mapping)
                  and ("ticks_checked" in cap_gate or "compared" in cap_gate))
        compared = 0
        mismatches = 0
        if paired:
            compared = int(cap_gate.get("compared",
                                        cap_gate.get("ticks_checked", 0)) or 0)
            raw_mismatches = cap_gate.get("mismatches") or []
            mismatches = (len(raw_mismatches)
                          if isinstance(raw_mismatches, (list, tuple))
                          else int(raw_mismatches or 0))
            if cap_gate.get("pass") is False and mismatches == 0:
                mismatches = 1
        result[cap] = classify_gate(
            declared=is_decl,
            evidence_events=ev,
            compared=compared,
            mismatches=mismatches,
            legacy=legacy,
        )
    return result


# ---------------------------------------------------------------------------
# Fail-closed checks
# ---------------------------------------------------------------------------

def fail_closed_reasons(*,
                        frame_ticks_declared: int,
                        frames_checked: int,
                        state_compared_total: int,
                        state_evidence_present: bool,
                        caps: Mapping[str, Any],
                        evidence: Mapping[str, Mapping[str, Any]],
                        c_rows: int,
                        tape_ticks: int) -> list[str]:
    """Return human-readable fail-closed reasons (empty => ok to continue)."""
    reasons: list[str] = []
    if c_rows <= 0 and tape_ticks > 0:
        reasons.append(
            f"zero state rows from magma (0 of {tape_ticks} ticks)")
    # frames_checked < 0 means "pixel frames not in scope for this run"
    if frame_ticks_declared > 0 and frames_checked == 0:
        reasons.append(
            f"tape declares {frame_ticks_declared} golden frames but "
            f"frames_checked=0")
    if state_evidence_present and state_compared_total <= 0 and c_rows > 0:
        reasons.append(
            "zero compared state ticks despite tape state evidence")
    for cap in caps.get("declared") or []:
        ev = int(evidence.get(cap, {}).get("events", 0) or 0)
        if ev <= 0:
            reasons.append(
                f"declared capability {cap!r} produced no events")
    return reasons


def state_compared_total(state_gate: Mapping[str, Any]) -> int:
    inv = state_gate.get("inventory") or {}
    ent = state_gate.get("entities") or {}
    world = state_gate.get("world") or {}
    inv_n = int(inv.get("ticks_independent", 0) or 0)
    if inv_n == 0 and inv.get("ticks_checked") and not inv.get("seeded_only"):
        inv_n = int(inv.get("ticks_checked", 0) or 0)
    return (inv_n
            + int(ent.get("ghost_ticks", 0) or 0)
            + int(world.get("compared", 0) or 0))


def state_evidence_present(ticks: Sequence[Mapping[str, Any]],
                           state_gate: Mapping[str, Any] | None = None
                           ) -> bool:
    """True when the tape carries *comparable* non-player state evidence.

    Seed-only inventory (tick-0 inv that only seeds replay) does not count:
    that is not an independent comparison channel. Independent inv dumps,
    entity rows, and Java world digests do.
    """
    inv_indep = 0
    if state_gate:
        inv = state_gate.get("inventory") or {}
        if not inv.get("seeded_only"):
            inv_indep = int(inv.get("ticks_independent", 0)
                            or inv.get("ticks_checked", 0) or 0)
    has_indep_inv = False
    has_ents = False
    has_wfnv = False
    for i, row in enumerate(ticks):
        if "inv" in row and i > 0:
            has_indep_inv = True
        if row.get("ents") is not None:
            has_ents = True
        if row.get("wfnv") is not None:
            has_wfnv = True
    if state_gate is not None:
        # Prefer gate census when available (seeded_only already filtered).
        return bool(inv_indep or has_ents or has_wfnv)
    return has_indep_inv or has_ents or has_wfnv


# ---------------------------------------------------------------------------
# Renderer provenance
# ---------------------------------------------------------------------------

def extract_renderer_provenance(header: Mapping[str, Any],
                                meta: Mapping[str, Any] | None = None
                                ) -> dict[str, Any]:
    """Pull provenance from header.renderer / meta.capture / meta.renderer."""
    meta = meta or {}
    raw: dict[str, Any] = {}
    # Lowest to highest authority: launch-time metadata is only intent; the
    # recorder header measures the live client; post-pack capture metadata
    # measures the actual golden output and wins over both.
    for blob in (
        meta.get("renderer"),
        meta.get("renderer_provenance"),
        header.get("renderer"),
        header.get("renderer_provenance"),
        (meta.get("capture") or {}).get("renderer")
        if isinstance(meta.get("capture"), Mapping) else None,
        (meta.get("capture") or {}).get("provenance")
        if isinstance(meta.get("capture"), Mapping) else None,
    ):
        if isinstance(blob, Mapping):
            raw.update(blob)
    # Common capture fields that double as provenance
    capture = meta.get("capture") if isinstance(meta.get("capture"), Mapping) else {}
    if "hide_gui" in capture:
        raw["hide_gui"] = capture["hide_gui"]
    if "assets_hash" in capture:
        raw["assets_hash"] = capture["assets_hash"]
    # Resolution from options / explicit
    opts = meta.get("options") if isinstance(meta.get("options"), Mapping) else {}
    if "resolution" not in raw:
        w = raw.get("width") or header.get("frame_w") or capture.get("width")
        h = raw.get("height") or header.get("frame_h") or capture.get("height")
        if w and h:
            raw["resolution"] = f"{int(w)}x{int(h)}"
    if "scaling" not in raw and "guiScale" in opts:
        raw["scaling"] = opts["guiScale"]
    out = {k: raw.get(k) for k in PROVENANCE_FIELDS}
    # Keep extras for diagnostics
    extras = {k: v for k, v in raw.items() if k not in out}
    if extras:
        out["_extras"] = extras
    return out


def local_renderer_provenance(*,
                              backend: str = "cpu",
                              width: int = 854,
                              height: int = 480,
                              hide_gui: bool = False,
                              assets_hash: str | None = None,
                              gui_scale: Any | None = None) -> dict[str, Any]:
    """Provenance of the local magma replay host for comparison."""
    api = {"cpu": "software", "cuda": "cuda", "metal": "metal"}.get(backend, backend)
    return {
        "os": sys.platform,
        "arch": platform.machine(),
        "api": api,
        "backend": backend,
        "gpu": None,  # filled by caller when known
        "version": None,
        "resolution": f"{int(width)}x{int(height)}",
        "scaling": gui_scale,
        "hide_gui": bool(hide_gui),
        "assets_hash": assets_hash,
    }


def provenance_compatible(tape_prov: Mapping[str, Any],
                          local_prov: Mapping[str, Any],
                          *,
                          required_fields: Sequence[str] | None = None
                          ) -> dict[str, Any]:
    """Compare tape vs local provenance for absolute pixel gates.

    Only fields present (not None) on the *tape* side are required to match.
    Missing tape provenance => compatible=False with reason undeclared
    (absolute pixel pass/fail refused unless both sides omit the field set
    entirely — then diagnostics-only is still allowed via blocked status).
    """
    required_fields = list(required_fields or PIXEL_COMPAT_FIELDS)
    missing = [f for f in PIXEL_REQUIRED_FIELDS
               if f in required_fields and tape_prov.get(f) is None]
    if missing:
        return {
            "compatible": False,
            "reason": ("tape_renderer_provenance_absent" if
                       len(missing) == len(PIXEL_REQUIRED_FIELDS)
                       else "tape_renderer_provenance_incomplete"),
            "missing_fields": missing,
            "mismatches": [],
            "compared_fields": [],
        }
    present = [f for f in required_fields if tape_prov.get(f) is not None]
    mismatches = []
    for f in present:
        tv, lv = tape_prov.get(f), local_prov.get(f)
        if lv is None:
            mismatches.append({"field": f, "tape": tv, "local": None,
                               "reason": "local_missing"})
            continue
        # normalize bools / strings loosely
        if isinstance(tv, bool) or isinstance(lv, bool):
            if bool(tv) != bool(lv):
                mismatches.append({"field": f, "tape": tv, "local": lv})
        else:
            if str(tv) != str(lv):
                mismatches.append({"field": f, "tape": tv, "local": lv})
    return {
        "compatible": len(mismatches) == 0,
        "reason": None if not mismatches else "provenance_mismatch",
        "mismatches": mismatches,
        "compared_fields": present,
    }


def pixel_gate_status(compat: Mapping[str, Any],
                      *,
                      frames_checked: int,
                      gate_pass: bool | None,
                      declared_provenance: bool) -> dict[str, Any]:
    """Absolute pixel gate status under provenance rules.

    Incompatible provenance => blocked (pass is None); diagnostics may still
    populate clusters/frames. Compatible + frames => verified.
    """
    if frames_checked <= 0:
        return classify_gate(
            declared=declared_provenance,
            evidence_events=1 if declared_provenance else 0,
            compared=0,
            mismatches=0,
            blocked_reason="zero_frames",
            legacy=not declared_provenance,
        )
    if not declared_provenance:
        # Legacy tapes keep their historical measured pixel verdict. New tapes
        # must declare provenance before incompatibility can block that verdict.
        return {
            "status": "verified",
            "pass": gate_pass,
            "compared": frames_checked,
            "events": frames_checked,
            "mismatches": 0 if gate_pass else 1,
            "reason": "legacy_undeclared",
        }
    if not compat.get("compatible", False):
        return {
            "status": "blocked",
            "pass": None,  # refuse pass/fail
            "compared": frames_checked,
            "events": frames_checked,
            "mismatches": 0,
            "reason": compat.get("reason") or "provenance_incompatible",
            "provenance": compat,
            # diagnostics still allowed
            "diagnostic_pass": gate_pass,
        }
    return {
        "status": "verified",
        "pass": bool(gate_pass),
        "compared": frames_checked,
        "events": frames_checked,
        "mismatches": 0 if gate_pass else 1,
        "reason": None,
        "provenance": compat,
    }


# ---------------------------------------------------------------------------
# Event phase helpers (Python-side only; never inject into magma scripts)
# ---------------------------------------------------------------------------

def phase_for_event_type(event_type: str) -> str | None:
    return EVENT_TYPE_PHASE.get(event_type)


def annotate_events_with_phases(events: Iterable[Mapping[str, Any]]
                                ) -> list[dict[str, Any]]:
    """Return copies of events with a ``phase`` key for reports/tests only."""
    out = []
    for ev in events:
        e = dict(ev)
        e["phase"] = phase_for_event_type(str(ev.get("type", ""))) or "SIMULATION"
        out.append(e)
    return out


def validate_phase_order(events: Sequence[Mapping[str, Any]],
                         *,
                         same_tick_only: bool = True) -> list[str]:
    """Soft check: within a tick, phases should be non-decreasing.

    Returns a list of warning strings (empty if ok). Does not raise: legacy
    scripts intentionally interleave some observation with post-finals.
    """
    order = {p: i for i, p in enumerate(EVENT_PHASES)}
    warnings: list[str] = []
    last_tick = None
    last_phase_i = -1
    for ev in events:
        t = ev.get("tick")
        phase = ev.get("phase") or phase_for_event_type(str(ev.get("type", "")))
        if phase is None or phase not in order:
            continue
        if not same_tick_only:
            continue
        if t != last_tick:
            last_tick = t
            last_phase_i = -1
        pi = order[phase]
        # OBSERVATION may appear before POST_TICK_FINAL for render views that
        # are emitted mid-tick; only flag INPUT after CLIENT_ACTION etc.
        if phase in ("INPUT", "PRE_TICK_PACKET", "CLIENT_ACTION"):
            if pi < last_phase_i and last_phase_i >= 0:
                warnings.append(
                    f"tick {t}: phase {phase} after later phase index "
                    f"{last_phase_i}")
            last_phase_i = max(last_phase_i, pi)
        else:
            last_phase_i = max(last_phase_i, pi)
    return warnings


# ---------------------------------------------------------------------------
# Input hashes
# ---------------------------------------------------------------------------

def _sha256_file(path: str, *, limit: int | None = None) -> str | None:
    if not path or not os.path.isfile(path):
        return None
    h = hashlib.sha256()
    with open(path, "rb") as f:
        if limit is None:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        else:
            h.update(f.read(limit))
    return h.hexdigest()


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_json(obj: Any) -> str:
    return _sha256_bytes(json.dumps(obj, sort_keys=True, default=str).encode())


def collect_input_hashes(*,
                         tape_path: str | None,
                         replay_script_path: str | None = None,
                         magma_binary_path: str | None = None,
                         effective_config: Mapping[str, Any] | None = None,
                         assets_paths: Sequence[str] | None = None,
                         gate_impl_paths: Sequence[str] | None = None,
                         ) -> dict[str, Any]:
    """Stable input identity for gate reports (reproducibility record)."""
    tape_path = tape_path or ""
    base = os.path.splitext(tape_path)[0] if tape_path else ""
    sidecars = {}
    for label, path in (
        ("meta", base + ".meta.json"),
        ("worldpatch", tape_path + ".worldpatch.jsonl" if tape_path else ""),
        ("geom", base + ".geom.jsonl"),
        ("known_divergences", base + ".known_divergences.json"),
        ("parquet", base + ".parquet"),
    ):
        digest = _sha256_file(path) if path else None
        if digest:
            sidecars[label] = {"path": os.path.basename(path), "sha256": digest}

    snap = (tape_path + ".snapshot_patch.jsonl") if tape_path else ""
    snap_hash = _sha256_file(snap) if snap else None

    if gate_impl_paths is None:
        here = os.path.dirname(os.path.abspath(__file__))
        gate_impl_paths = [
            os.path.join(here, "tape_contract.py"),
            os.path.join(here, "pixel_gate.py"),
            os.path.join(here, "replay_tape.py"),
        ]
    gate_parts = []
    for p in gate_impl_paths:
        d = _sha256_file(p)
        if d:
            gate_parts.append((os.path.basename(p), d))
    gate_impl = _sha256_json(gate_parts) if gate_parts else None

    assets = {}
    for p in assets_paths or ():
        d = _sha256_file(p)
        if d:
            assets[os.path.basename(p)] = d
    assets_hash = _sha256_json(assets) if assets else None

    return {
        "tape": _sha256_file(tape_path) if tape_path else None,
        "sidecars": sidecars,
        "snapshot_patch": snap_hash,
        "magma_binary": _sha256_file(magma_binary_path) if magma_binary_path else None,
        "replay_script": _sha256_file(replay_script_path) if replay_script_path else None,
        "effective_config": _sha256_json(effective_config or {}),
        "assets": assets_hash,
        "assets_detail": assets or None,
        "gate_implementation": gate_impl,
        "gate_implementation_files": [
            {"file": n, "sha256": d} for n, d in gate_parts
        ],
    }


def resolve_magma_binary(backend: str = "cpu",
                         repo_root: str | None = None) -> str | None:
    here = os.path.dirname(os.path.abspath(__file__))
    root = repo_root or os.path.abspath(os.path.join(here, "..", ".."))
    names = {
        "cpu": "magma_game",
        "cuda": "magma_game_cuda",
        "metal": "magma_game_metal",
    }
    name = names.get(backend, "magma_game")
    for rel in (os.path.join("magma", name), name):
        p = os.path.join(root, rel)
        if os.path.isfile(p):
            return p
    return None


# ---------------------------------------------------------------------------
# Report assembly
# ---------------------------------------------------------------------------

def build_contract_report(*,
                         header: Mapping[str, Any],
                         ticks: Sequence[Mapping[str, Any]],
                         tape_path: str | None,
                         state_gate: Mapping[str, Any],
                         backend: str = "cpu",
                         width: int = 854,
                         height: int = 480,
                         hide_gui: bool = False,
                         frame_ticks_declared: int = 0,
                         frames_checked: int = 0,
                         pixel_pass: bool | None = None,
                         replay_script_path: str | None = None,
                         effective_config: Mapping[str, Any] | None = None,
                         assets_hash: str | None = None,
                         ) -> dict[str, Any]:
    """Full contract block for gate.json / markdown reports."""
    meta = load_meta(tape_path)
    caps = resolve_capabilities(header, meta)
    evidence = capability_evidence(header, ticks, tape_path, meta)
    gate_status = classify_state_gates(state_gate, caps, evidence)

    tape_prov = extract_renderer_provenance(header, meta)
    local_prov = local_renderer_provenance(
        backend=backend, width=width, height=height,
        hide_gui=hide_gui, assets_hash=assets_hash,
        gui_scale=(meta.get("options") or {}).get("guiScale")
        if isinstance(meta.get("options"), Mapping) else None,
    )
    compat = provenance_compatible(tape_prov, local_prov)
    declared_prov = "renderer_provenance" in set(caps["declared"])
    # If provenance capability declared but empty, force incompatible
    if declared_prov and evidence.get("renderer_provenance", {}).get("events", 0) == 0:
        compat = {
            "compatible": False,
            "reason": "declared_capability_produced_no_events",
            "mismatches": [],
            "compared_fields": [],
        }
    gate_status["pixels"] = pixel_gate_status(
        compat,
        frames_checked=frames_checked,
        gate_pass=pixel_pass,
        declared_provenance=declared_prov,
    )
    gate_status["renderer_provenance"] = classify_gate(
        declared=declared_prov,
        evidence_events=int(evidence.get("renderer_provenance", {}).get("events", 0)),
        compared=(len(compat.get("compared_fields") or [])
                  if declared_prov else 0),
        mismatches=len(compat.get("mismatches") or []),
        blocked_reason=(None if compat.get("compatible")
                        else (compat.get("reason") or "provenance_incompatible")),
        legacy=caps["legacy"],
    )
    if (declared_prov and compat.get("compatible")
            and (compat.get("compared_fields") or [])):
        gate_status["renderer_provenance"] = {
            "status": "verified",
            "pass": True,
            "compared": len(compat["compared_fields"]),
            "events": 1,
            "mismatches": 0,
            "reason": None,
        }

    compared_total = state_compared_total(state_gate)
    # When no goldens were declared, pass frames_checked=-1 so the zero-frame
    # rule does not fire (legacy physics-only tapes).
    reasons = fail_closed_reasons(
        frame_ticks_declared=frame_ticks_declared,
        frames_checked=(frames_checked if frame_ticks_declared > 0 else -1),
        state_compared_total=compared_total,
        state_evidence_present=state_evidence_present(ticks, state_gate),
        caps=caps,
        evidence=evidence,
        c_rows=int((state_gate.get("coverage") or {}).get("ticks_run", 0) or 0),
        tape_ticks=len(ticks),
    )

    input_hashes = collect_input_hashes(
        tape_path=tape_path,
        replay_script_path=replay_script_path,
        magma_binary_path=resolve_magma_binary(backend),
        effective_config=effective_config,
    )

    return {
        "schema_version": SCHEMA_VERSION,
        "schema": {
            "event_phases": list(EVENT_PHASES),
            "capabilities": list(CAPABILITIES),
            "gate_status": list(GATE_STATUS),
        },
        "capabilities": caps,
        "evidence": evidence,
        "gate_status": gate_status,
        "renderer_provenance": {
            "tape": {k: tape_prov.get(k) for k in PROVENANCE_FIELDS},
            "local": local_prov,
            "compatibility": compat,
        },
        "fail_closed": {
            "ok": len(reasons) == 0,
            "reasons": reasons,
        },
        "input_hashes": input_hashes,
        "absolute_pixel_gate_refused": (
            declared_prov
            and gate_status.get("pixels", {}).get("status") == "blocked"
            and gate_status.get("pixels", {}).get("reason") not in (
                None, "zero_frames",
            )
        ),
    }
