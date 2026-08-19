"""Focused tests for tape harness contracts (no replay, no GPU)."""
from __future__ import annotations

import json
from pathlib import Path

import tape_contract as tc


def test_schema_lists_required_capabilities_and_phases():
    assert tc.SCHEMA_VERSION == 1
    for name in (
        "block_finals", "gui_clicks", "container_identity",
        "inventory_keyframes", "dig_trace", "packet_health",
        "renderer_provenance", "world_snapshot",
    ):
        assert name in tc.CAPABILITIES
    assert tc.EVENT_PHASES == (
        "INPUT", "PRE_TICK_PACKET", "CLIENT_ACTION",
        "SIMULATION", "POST_TICK_FINAL", "OBSERVATION",
    )
    assert tc.phase_for_event_type("container_click") == "CLIENT_ACTION"
    for event_type in (
        "container_open", "container_slot", "container_cursor",
        "container_furnace_prop", "container_close",
    ):
        assert tc.phase_for_event_type(event_type) == "CLIENT_ACTION"
    assert tc.phase_for_event_type("set_block_post") == "POST_TICK_FINAL"
    assert tc.phase_for_event_type("set_packet_velocity") == "PRE_TICK_PACKET"
    assert tc.phase_for_event_type("inv_view") == "OBSERVATION"
    assert tc.phase_for_event_type("action") == "CLIENT_ACTION"


def test_legacy_tape_has_no_declared_capabilities():
    caps = tc.resolve_capabilities({"header": 1, "seed": 0}, {})
    assert caps["legacy"] is True
    assert caps["declared"] == []
    assert caps["source"] is None


def test_capabilities_from_header_and_meta_merge():
    header = {"capabilities": ["block_finals", "gui_clicks"]}
    meta = {"capabilities": {"inventory_keyframes": True, "dig_trace": False}}
    caps = tc.resolve_capabilities(header, meta)
    assert caps["legacy"] is False
    assert caps["declared"] == ["block_finals", "gui_clicks", "inventory_keyframes"]
    assert caps["source"] == "header+meta"


def test_unknown_capability_rejected():
    try:
        tc.resolve_capabilities({"capabilities": ["not_a_real_cap"]}, {})
        assert False, "expected ValueError"
    except ValueError as e:
        assert "unknown tape capability" in str(e)


def test_capability_evidence_counts_tick_fields():
    ticks = [
        {"t": 0, "bc": [[1, 2, 3, 4, 0]], "inv": [0] * 41},
        {"t": 1,
         "gopen": [["GuiInventory", 0, "player"]],
         "gclk": [["GuiInventory", 36, 0, 0, "PICKUP"]], "inv": [0] * 41},
        {"t": 2, "dig": {"x": 1, "y": 64, "z": 1, "progress": 0.5}},
        {"t": 3},
    ]
    ev = tc.capability_evidence({"header": 1}, ticks, tape_path=None, meta={})
    assert ev["block_finals"]["events"] == 1
    assert ev["gui_clicks"]["events"] == 1
    assert ev["container_identity"]["events"] == 1
    assert ev["inventory_keyframes"]["events"] == 2
    assert ev["dig_trace"]["events"] == 1
    assert ev["packet_health"]["events"] == 0


def test_declared_capability_with_zero_events_fails_closed():
    caps = {"declared": ["block_finals"], "legacy": False}
    evidence = {"block_finals": {"events": 0, "fields": ["bc"], "sources": []}}
    reasons = tc.fail_closed_reasons(
        frame_ticks_declared=0,
        frames_checked=-1,
        state_compared_total=5,
        state_evidence_present=True,
        caps=caps,
        evidence=evidence,
        c_rows=10,
        tape_ticks=10,
    )
    assert any("block_finals" in r for r in reasons)


def test_zero_frames_fails_closed_when_goldens_declared():
    reasons = tc.fail_closed_reasons(
        frame_ticks_declared=12,
        frames_checked=0,
        state_compared_total=3,
        state_evidence_present=True,
        caps={"declared": [], "legacy": True},
        evidence={},
        c_rows=10,
        tape_ticks=10,
    )
    assert any("golden frames" in r for r in reasons)


def test_zero_compared_state_ticks_fails_closed():
    reasons = tc.fail_closed_reasons(
        frame_ticks_declared=0,
        frames_checked=-1,
        state_compared_total=0,
        state_evidence_present=True,
        caps={"declared": [], "legacy": True},
        evidence={},
        c_rows=10,
        tape_ticks=10,
    )
    assert any("zero compared state ticks" in r for r in reasons)


def test_legacy_seeded_only_inv_is_not_comparable_state_evidence():
    ticks = [{"t": 0, "inv": [0] * 41}]
    state_gate = {
        "inventory": {
            "ticks_checked": 1, "ticks_independent": 0,
            "seeded_only": True, "mismatches": [], "pass": True,
            "available": True,
        },
        "entities": {"ghost_ticks": 0, "ticks_checked": 0},
        "world": {"compared": 0},
    }
    assert tc.state_evidence_present(ticks, state_gate) is False


def test_classify_gate_statuses():
    verified = tc.classify_gate(
        declared=True, evidence_events=3, compared=3, mismatches=0)
    assert verified["status"] == "verified" and verified["pass"] is True

    failed = tc.classify_gate(
        declared=True, evidence_events=3, compared=3, mismatches=2)
    assert failed["status"] == "verified" and failed["pass"] is False

    blocked = tc.classify_gate(
        declared=True, evidence_events=0, compared=0, mismatches=0)
    assert blocked["status"] == "blocked" and blocked["pass"] is False

    unavail = tc.classify_gate(
        declared=False, evidence_events=0, compared=0, legacy=True)
    assert unavail["status"] == "unavailable"

def test_dig_trace_gate_uses_paired_comparison_result():
    state = {
        "inventory": {},
        "entities": {},
        "world": {},
        "dig_trace": {
            "ticks_checked": 7,
            "mismatches": ["tick=6 field=lcc java=0 magma=10"],
            "pass": False,
        },
    }
    evidence = {
        name: {"events": 0} for name in tc.CAPABILITIES
    }
    evidence["dig_trace"] = {"events": 7}
    gates = tc.classify_state_gates(
        state,
        caps={"declared": ["dig_trace"], "legacy": False},
        evidence=evidence,
    )
    assert gates["dig_trace"]["status"] == "verified"
    assert gates["dig_trace"]["compared"] == 7
    assert gates["dig_trace"]["mismatches"] == 1
    assert gates["dig_trace"]["pass"] is False


def test_renderer_provenance_prefers_measured_capture_over_launch_intent():
    header = {
        "renderer_provenance": {
            "os": "linux", "arch": "x86_64", "resolution": "854x480",
            "scaling": 2, "hide_gui": True,
        },
    }
    meta = {
        "renderer_provenance": {
            "os": "darwin", "arch": "arm64", "hide_gui": False,
        },
        "capture": {
            "hide_gui": False,
            "provenance": {"resolution": "1280x720"},
        },
    }
    prov = tc.extract_renderer_provenance(header, meta)
    assert prov["os"] == "linux"
    assert prov["arch"] == "x86_64"
    assert prov["resolution"] == "1280x720"
    assert prov["hide_gui"] is False



def test_renderer_provenance_mismatch_blocks_absolute_pixels():
    tape = {
        "os": "darwin", "arch": "arm64", "api": "gl", "backend": "apple",
        "gpu": "Apple M4", "version": "2.1", "resolution": "854x480",
        "scaling": "2", "hide_gui": False, "assets_hash": "abc",
    }
    local = {
        "os": "linux", "arch": "x86_64", "api": "software", "backend": "cpu",
        "gpu": None, "version": None, "resolution": "854x480",
        "scaling": "2", "hide_gui": False, "assets_hash": "abc",
    }
    compat = tc.provenance_compatible(tape, local)
    assert compat["compatible"] is False
    assert any(m["field"] == "os" for m in compat["mismatches"])
    st = tc.pixel_gate_status(
        compat, frames_checked=40, gate_pass=False, declared_provenance=True)
    assert st["status"] == "blocked"
    assert st["pass"] is None  # refuse absolute pass/fail
    assert st.get("diagnostic_pass") is False  # triage retained


def test_matching_provenance_allows_verified_pixel_gate():
    prov = {
        "os": "linux", "arch": "x86_64", "api": "software", "backend": "cpu",
        "gpu": None, "version": None, "resolution": "854x480",
        "scaling": None, "hide_gui": True, "assets_hash": "deadbeef",
    }
    compat = tc.provenance_compatible(prov, prov)
    assert compat["compatible"] is True
    st = tc.pixel_gate_status(
        compat, frames_checked=10, gate_pass=True, declared_provenance=True)
    assert st["status"] == "verified" and st["pass"] is True

def test_platform_match_allows_different_renderer_implementations():
    tape = {
        "os": "linux", "arch": "x86_64", "api": "gl",
        "backend": "llvmpipe", "resolution": "854x480",
        "scaling": "2", "hide_gui": False,
    }
    local = {
        "os": "linux", "arch": "x86_64", "api": "software",
        "backend": "cpu", "resolution": "854x480",
        "scaling": "2", "hide_gui": False,
    }
    compat = tc.provenance_compatible(tape, local)
    assert compat["compatible"] is True
    assert "api" not in compat["compared_fields"]


def test_partial_tape_provenance_blocks_absolute_pixels():
    tape = {"scaling": "2", "hide_gui": False}
    compat = tc.provenance_compatible(
        tape, tc.local_renderer_provenance(gui_scale="2"))
    assert compat["compatible"] is False
    assert compat["reason"] == "tape_renderer_provenance_absent"
    assert compat["missing_fields"] == ["os", "arch", "resolution"]



def test_absent_legacy_provenance_keeps_historical_pixel_verdict():
    compat = tc.provenance_compatible({}, tc.local_renderer_provenance())
    assert compat["compatible"] is False
    assert compat["reason"] == "tape_renderer_provenance_absent"
    st = tc.pixel_gate_status(
        compat, frames_checked=5, gate_pass=False, declared_provenance=False)
    assert st["status"] == "verified"
    assert st["pass"] is False
    assert st["reason"] == "legacy_undeclared"


def test_declared_absent_provenance_blocks_absolute_pixels():
    compat = tc.provenance_compatible({}, tc.local_renderer_provenance())
    st = tc.pixel_gate_status(
        compat, frames_checked=5, gate_pass=True, declared_provenance=True)
    assert st["status"] == "blocked"
    assert st["pass"] is None


def test_presence_only_capability_evidence_is_blocked_not_verified():
    state = {"inventory": {}, "entities": {}, "world": {}}
    caps = {"declared": [], "legacy": False}
    evidence = {"gui_clicks": {"events": 3}}
    gates = tc.classify_state_gates(state, caps, evidence)
    assert gates["gui_clicks"]["status"] == "blocked"
    assert gates["gui_clicks"]["pass"] is None
    assert gates["gui_clicks"]["reason"] == "evidence_present_but_not_compared"


def test_input_hashes_cover_required_keys(tmp_path: Path):
    tape = tmp_path / "t.jsonl"
    tape.write_text('{"header":1,"seed":0}\n{"t":0}\n')
    meta = tmp_path / "t.meta.json"
    meta.write_text(json.dumps({"name": "t", "options": {"guiScale": "2"}}))
    script = tmp_path / "script.jsonl"
    script.write_text('{"tick":0,"type":"set_time","value":0}\n')
    impl = tmp_path / "fake_gate.py"
    impl.write_text("# gate impl\n")
    hashes = tc.collect_input_hashes(
        tape_path=str(tape),
        replay_script_path=str(script),
        magma_binary_path=None,
        effective_config={"backend": "cpu", "w": 854},
        gate_impl_paths=[str(impl)],
    )
    for key in tc.SCHEMA["input_hash_keys"]:
        assert key in hashes
    assert hashes["tape"] and len(hashes["tape"]) == 64
    assert hashes["replay_script"] and len(hashes["replay_script"]) == 64
    assert hashes["effective_config"] and len(hashes["effective_config"]) == 64
    assert hashes["gate_implementation"] and len(hashes["gate_implementation"]) == 64
    assert "meta" in hashes["sidecars"]


def test_build_contract_report_legacy_ok(tmp_path: Path):
    tape = tmp_path / "legacy.jsonl"
    tape.write_text('{"header":1,"seed":0,"x":0,"y":64,"z":0}\n'
                    '{"t":0,"x":0,"y":64,"z":0,"ents":[]}\n'
                    '{"t":1,"x":0,"y":64,"z":0,"ents":[],"wfnv":1,"wfa":0}\n')
    header = {"header": 1, "seed": 0}
    ticks = [
        {"t": 0, "ents": []},
        {"t": 1, "ents": [], "wfnv": 1, "wfa": 0},
    ]
    state_gate = {
        "kind": "state",
        "coverage": {"ticks_total": 2, "ticks_run": 2, "truncated": False},
        "inventory": {
            "ticks_checked": 0, "ticks_independent": 0, "seeded_only": False,
            "mismatches": [], "pass": True, "available": False,
        },
        "entities": {
            "ticks_checked": 2, "ghost_ticks": 2, "ghost_expected": 0,
            "mismatches": [], "verified": True, "pass": True, "available": True,
        },
        "world": {
            "ticks_checked": 2, "hash_deltas": 0, "mode": "java",
            "compared": 1, "anchor_skips": 0, "mismatches": [],
            "verified": True, "pass": True, "available": True,
        },
    }
    report = tc.build_contract_report(
        header=header, ticks=ticks, tape_path=str(tape),
        state_gate=state_gate, backend="cpu",
        frame_ticks_declared=0, frames_checked=0, pixel_pass=None,
    )
    assert report["capabilities"]["legacy"] is True
    assert report["fail_closed"]["ok"] is True
    assert report["gate_status"]["entities"]["status"] == "verified"
    assert report["gate_status"]["world"]["status"] == "verified"
    assert report["gate_status"]["pixels"]["status"] in ("blocked", "unavailable")
    assert "input_hashes" in report
    assert report["schema_version"] == 1


def test_build_contract_report_declared_empty_capability_fails(tmp_path: Path):
    tape = tmp_path / "cap.jsonl"
    tape.write_text('{"header":1,"seed":0,"capabilities":["block_finals"]}\n'
                    '{"t":0}\n')
    header = {"header": 1, "seed": 0, "capabilities": ["block_finals"]}
    ticks = [{"t": 0}]
    state_gate = {
        "coverage": {"ticks_total": 1, "ticks_run": 1, "truncated": False},
        "inventory": {
            "ticks_checked": 0, "ticks_independent": 0, "seeded_only": False,
            "mismatches": [], "pass": True, "available": False,
        },
        "entities": {
            "ticks_checked": 0, "ghost_ticks": 0, "mismatches": [],
            "verified": False, "pass": True, "available": False,
        },
        "world": {
            "ticks_checked": 0, "hash_deltas": 0, "mode": "c_only",
            "compared": 0, "anchor_skips": 0, "mismatches": [],
            "verified": False, "pass": True, "available": False,
        },
    }
    report = tc.build_contract_report(
        header=header, ticks=ticks, tape_path=str(tape),
        state_gate=state_gate, backend="cpu",
        frame_ticks_declared=0, frames_checked=0,
    )
    assert report["fail_closed"]["ok"] is False
    assert any("block_finals" in r for r in report["fail_closed"]["reasons"])
    assert report["gate_status"]["block_finals"]["status"] == "blocked"


def test_annotate_events_with_phases_does_not_mutate_and_orders():
    events = [
        {"tick": 0, "type": "set_look", "yaw": 0, "pitch": 0},
        {"tick": 0, "type": "set_packet_velocity", "x": 0, "y": 0, "z": 0},
        {"tick": 0, "type": "action", "forward": 0},
        {"tick": 0, "type": "set_block_post", "x": 0, "y": 0, "z": 0,
         "id": 0, "meta": 0},
        {"tick": 0, "type": "inv_view", "slot": 0, "item": 0, "count": 0,
         "meta": 0},
    ]
    annotated = tc.annotate_events_with_phases(events)
    assert events[0].get("phase") is None  # original untouched
    phases = [e["phase"] for e in annotated]
    assert phases == [
        "INPUT", "PRE_TICK_PACKET", "CLIENT_ACTION",
        "POST_TICK_FINAL", "OBSERVATION",
    ]
    # Soft order check: early phases before later ones within the tick
    assert tc.validate_phase_order(annotated) == []
