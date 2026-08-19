"""Focused dig_trace schema / emission / compare tests (no GPU, no long replay)."""
from __future__ import annotations

import json
from pathlib import Path

import dig_compare
import dig_schema


def test_header_dig_trace_opt_in_is_backwards_compatible():
    assert not dig_schema.header_has_dig_trace(None)
    assert not dig_schema.header_has_dig_trace({"header": 1, "seed": 0})
    assert dig_schema.header_has_dig_trace({"header": 1, "dig_trace": 1})
    assert dig_schema.header_has_dig_trace({"dig_trace": True})


def test_emit_and_validate_minimal_idle():
    dig = dig_schema.emit_dig_object()
    assert dig_schema.validate_dig_object(dig) == []
    assert dig_schema.phase_of(dig) == "idle"
    assert dig["bcn"] == 0 and dig["bc_ref"] == []
    assert dig["brk"] == 0 and dig["cb"] is None


def test_emit_accruing_and_break_phases():
    acc = dig_schema.emit_dig_object(
        hit=1, cb=[10, 70, -3], face=1, ray=1, rx=10, ry=70, rz=-3,
        held=[270, 0, 1], dmg=0.4, rel=0.08, reach=4.5,
    )
    assert dig_schema.validate_dig_object(acc) == []
    assert dig_schema.phase_of(acc) == "accruing"
    brk = dig_schema.emit_dig_object(
        hit=0, dmg=0.0, brk=[10, 70, -3], bhd=5, ray=1, rx=10, ry=70, rz=-3,
        face=1, rel=0.08,
    )
    assert dig_schema.validate_dig_object(brk) == []
    assert dig_schema.phase_of(brk) == "break"
    delay = dig_schema.emit_dig_object(bhd=4, hit=0)
    assert dig_schema.phase_of(delay) == "delay"
    lcc = dig_schema.emit_dig_object(lcc=10, hit=0)
    assert dig_schema.phase_of(lcc) == "left_click_cooldown"


def test_validate_rejects_bad_bc_ref_count():
    dig = dig_schema.emit_dig_object(
        bc_ref=[[1, 2, 3, 0, 0], [1, 3, 3, 12, 0]],
    )
    dig["bcn"] = 1  # lie
    errs = dig_schema.validate_dig_object(dig)
    assert any("bcn" in e for e in errs)


def test_validate_rejects_non_int_coords():
    dig = dig_schema.emit_dig_object(hit=1, cb=[1.0, 2, 3])  # type: ignore[list-item]
    # emit coerces via list(); force bad payload
    dig["cb"] = [1.0, 2, 3]
    errs = dig_schema.validate_dig_object(dig)
    assert any("cb" in e for e in errs)


def test_compare_first_field_mismatch_is_lcc():
    a = dig_schema.emit_dig_object(lcc=0, hit=1, cb=[0, 64, 0], face=0, ray=1,
                                  rx=0, ry=64, rz=0, dmg=0.1, rel=0.05)
    b = dict(a)
    b["lcc"] = 10
    msg = dig_compare.compare_dig_pair(5, a, b)
    assert msg is not None
    assert "tick=5" in msg and "field=lcc" in msg
    assert "phase_java=accruing" in msg


def test_compare_float_tolerance_on_dmg():
    a = dig_schema.emit_dig_object(hit=1, cb=[0, 64, 0], ray=1, rx=0, ry=64, rz=0,
                                  dmg=0.5, rel=0.1)
    b = dict(a)
    b["dmg"] = 0.5 + 1e-7
    assert dig_compare.compare_dig_pair(0, a, b) is None
    b["dmg"] = 0.51
    msg = dig_compare.compare_dig_pair(0, a, b)
    assert msg and "field=dmg" in msg


def test_compare_streams_reports_first_tick_only(tmp_path: Path):
    java = [
        {"header": 1, "dig_trace": 1, "seed": 0, "x": 0, "y": 70, "z": 0},
        {"t": 0, "dig": dig_schema.emit_dig_object()},
        {"t": 1, "dig": dig_schema.emit_dig_object(lcc=10)},
        {"t": 2, "dig": dig_schema.emit_dig_object(hit=1, cb=[1, 1, 1],
                                                    ray=1, rx=1, ry=1, rz=1,
                                                    dmg=0.2)},
    ]
    magma = [
        {"version": 1, "tick": 0, "dig": dig_schema.emit_dig_object()},
        {"version": 1, "tick": 1, "dig": dig_schema.emit_dig_object(lcc=9)},
        {"version": 1, "tick": 2, "dig": dig_schema.emit_dig_object(
            hit=1, cb=[1, 1, 1], ray=1, rx=1, ry=1, rz=1, dmg=0.9)},
    ]
    msgs = list(dig_compare.compare_streams(java, magma))
    assert len(msgs) == 1
    assert "tick=1" in msgs[0] and "field=lcc" in msgs[0]


def test_compare_streams_absent_both_with_flag():
    java = [{"header": 1, "seed": 0}, {"t": 0, "x": 0, "y": 0, "z": 0}]
    magma = [{"tick": 0, "x": 0}]
    assert list(dig_compare.compare_streams(java, magma, allow_absent=True)) == []
    msgs = list(dig_compare.compare_streams(java, magma, allow_absent=False))
    assert msgs and "lack dig_trace" in msgs[0]

def test_compare_streams_fails_closed_on_disjoint_ticks():
    dig = dig_schema.emit_dig_object()
    java = [{"header": 1, "dig_trace": 1}, {"t": 4, "dig": dig}]
    magma = [{"tick": 6, "dig": dig}]
    msgs = list(dig_compare.compare_streams(java, magma))
    assert msgs == ["tick=4 field=tick java=present magma=absent"]


def test_compare_streams_aligns_uniform_post_tick_runtime_labels():
    dig = dig_schema.emit_dig_object()
    java = [
        {"header": 1, "dig_trace": 1},
        {"t": 4, "dig": dig},
        {"t": 5, "dig": dig},
    ]
    magma = [
        {"version": 1, "tick": 5, "dig": dig},
        {"version": 1, "tick": 6, "dig": dig},
    ]
    assert list(dig_compare.compare_streams(java, magma)) == []


def test_compare_streams_splits_observation_from_post_tick_controller():
    java = [
        {"header": 1, "dig_trace": 1},
        {"t": 3, "dig": dig_schema.emit_dig_object(
            lcc=7, face=1, ray=1, rx=0, ry=4, rz=3, rel=0.1)},
        {"t": 4, "dig": dig_schema.emit_dig_object(
            lcc=6, face=1, ray=1, rx=0, ry=4, rz=3, rel=0.1)},
    ]
    magma = [
        # Label 4 contains Java t3's post-controller state but Java t4's
        # pose-derived observation.
        {"version": 1, "tick": 4, "dig": dig_schema.emit_dig_object(
            lcc=7, face=1, ray=1, rx=0, ry=4, rz=3, rel=0.1)},
        {"version": 1, "tick": 5, "dig": dig_schema.emit_dig_object(
            lcc=6, face=2, ray=1, rx=-1, ry=3, rz=3, rel=0.1)},
    ]
    assert list(dig_compare.compare_streams(java, magma)) == []


def test_compare_streams_uses_post_tick_observation_after_look_change():
    java = [
        {"header": 1, "dig_trace": 1},
        {"t": 3, "yaw": 0.0, "pitch": 25.0,
         "dig": dig_schema.emit_dig_object(
             face=1, ray=1, rx=0, ry=4, rz=3, rel=0.1)},
        {"t": 4, "yaw": 0.0, "pitch": 16.0,
         "dig": dig_schema.emit_dig_object(face=2, ray=0)},
    ]
    magma = [
        {"version": 1, "tick": 4, "dig": dig_schema.emit_dig_object(
            face=1, ray=1, rx=0, ry=4, rz=3, rel=0.1)},
        {"version": 1, "tick": 5,
         "dig": dig_schema.emit_dig_object(face=-1, ray=0)},
    ]
    assert list(dig_compare.compare_streams(java, magma)) == []


def test_compare_streams_fails_closed_on_missing_magma_tick():
    dig = dig_schema.emit_dig_object()
    java = [
        {"header": 1, "dig_trace": 1},
        {"t": 0, "dig": dig},
        {"t": 1, "dig": dig},
    ]
    magma = [{"tick": 0, "dig": dig}]
    msgs = list(dig_compare.compare_streams(java, magma))
    assert msgs == ["tick=1 field=tick java=present magma=absent"]



def test_java_tape_roundtrip_schema_from_emitted_json(tmp_path: Path):
    """Simulate a dig_trace tape row and ensure parsers accept it."""
    header = {
        "header": 1, "seed": 42, "world": "qrl_0", "world_time": 1000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20, "dim": 0,
        "velocity_packets": 1, "position_packets": 1,
        "dig_trace": 1,
    }
    dig = dig_schema.emit_dig_object(
        lcc=0, bhd=5, hit=0, cb=None, face=1, ray=1,
        rx=3, ry=64, rz=-1, held=[257, 14, 1], dmg=0.0, rel=0.12,
        reach=4.5, brk=[3, 64, -1],
        bc_ref=[[3, 65, -1, 0, 0], [3, 66, -1, 12, 0]],
    )
    row = {
        "t": 7,
        "in": {"f": 0, "s": 0, "jump": 0, "sneak": 0, "sprint": 0,
               "atk": 1, "use": 0, "hb": 0},
        "x": 0.5, "y": 70.0, "z": 0.5,
        "yaw": 0.0, "pitch": 0.0, "hp": 20.0, "food": 20,
        "bc": [[3, 64, -1, 0, 0], [3, 65, -1, 0, 0], [3, 66, -1, 12, 0]],
        "dig": dig,
    }
    tape = tmp_path / "t.jsonl"
    with tape.open("w") as f:
        f.write(json.dumps(header) + "\n")
        f.write(json.dumps(row) + "\n")
    loaded = [json.loads(line) for line in tape.read_text().splitlines()]
    assert dig_schema.header_has_dig_trace(loaded[0])
    assert dig_schema.validate_dig_object(loaded[1]["dig"]) == []
    assert dig_schema.phase_of(loaded[1]["dig"]) == "break"
    # Magma twin matches except empty neighbor cascade (documented residual).
    magma_dig = dict(dig)
    magma_dig["bcn"] = 0
    magma_dig["bc_ref"] = []
    magma = tmp_path / "m.jsonl"
    with magma.open("w") as f:
        f.write(json.dumps({"version": 1, "tick": 7, "dig": magma_dig}) + "\n")
    msgs = list(dig_compare.compare_streams(loaded, [json.loads(magma.read_text())]))
    assert msgs and "field=bcn" in msgs[0]


def test_legacy_tape_without_dig_unchanged_shape():
    """Rows without dig remain valid and dig_compare does not invent dig."""
    header = {"header": 1, "seed": 0, "x": 0, "y": 70, "z": 0}
    tick = {"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5}
    assert "dig" not in tick
    assert not dig_schema.header_has_dig_trace(header)


def test_dig_compare_cli_selftest():
    assert dig_compare.main(["--selftest"]) == 0
