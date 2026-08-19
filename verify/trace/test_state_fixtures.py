"""Focused synthetic fixture tests for recorder/replay state diagnostics.

SYNTHETIC ONLY: fixtures under fixtures/synthetic/ are unit inputs, not
real-client tapes. No GPU, no long replay, no project-wide suite.
"""
from __future__ import annotations

import json
from pathlib import Path

import replay_tape

FIXTURES = Path(__file__).resolve().parent / "fixtures" / "synthetic"


def _load(name: str):
    data = json.loads((FIXTURES / name).read_text())
    assert data.get("label") == "synthetic"
    assert data.get("not_a_real_client_tape") is True
    return data["header"], data["ticks"]


def _magma_row(t, **extra):
    row = {
        "tick": t, "x": 0.5, "y": 70.0, "z": 0.5,
        "vx": 0.0, "vy": 0.0, "vz": 0.0,
        "on_ground": 1, "health": 20.0, "food": 20.0,
        "inventory": [], "entities": [],
        "cursor": [0, 0, 0], "grid": [[0, 0, 0]] * 9,
        "craft_result": [0, 0, 0],
    }
    row.update(extra)
    return row



def test_gclk_pickup_quick_move_throw_outside(tmp_path: Path):
    header, ticks = _load("gclk_clicks.json")
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    clicks = [e for e in events if e["type"] == "container_click"]
    assert [(c["slot"], c["button"], c["click_type"]) for c in clicks] == [
        (0, 0, 0),       # PICKUP hotbar0 -> magma slot 0
        (9, 0, 1),       # QUICK_MOVE main
        (0, 0, 4),       # THROW hotbar0
        (-999, 0, 0),    # outside click
    ]
    # Cursor compare: tape gcur vs magma cursor (same-tick; gclk allows adj).
    c_rows = [_magma_row(
        0,
        cursor=[5, 2, 0],  # magma [item, count, meta]
        inventory=[{"slot": 9, "item": 4, "count": 16, "meta": 0}],
        grid=[[5, 1, 0]] + [[0, 0, 0]] * 8,
        craft_result=[0, 0, 0],
    )]
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    assert state["inventory"]["gui_ticks_checked"] == 1
    assert state["inventory"]["pass"] is True


def test_gclk_cursor_mismatch_without_event_stays_strict():
    """Adjacent-tick tolerance is denied when no packet/GUI event is present.

    Build a bare inv+gcur row without gclk/gslots event flags... gcur itself
    is an event. Use a second tick with only inv and a lagged cursor on magma.
    """
    ticks = [
        {"t": 0, "x": 0.5, "y": 70.0, "z": 0.5, "vx": 0.0, "vy": 0.0,
         "vz": 0.0, "og": 1, "hp": 20.0, "food": 20, "ents": [],
         "inv": [[1, 0, 1]] + [0] * 40},
        {"t": 1, "x": 0.5, "y": 70.0, "z": 0.5, "vx": 0.0, "vy": 0.0,
         "vz": 0.0, "og": 1, "hp": 20.0, "food": 20, "ents": [],
         "inv": [[1, 0, 1]] + [0] * 40},
    ]
    # No gcur/gclk: only inv re-anchor path. Count-meta full compare.
    c_rows = [
        _magma_row(0, inventory=[{"slot": 0, "item": 1, "count": 1, "meta": 0}]),
        _magma_row(1, inventory=[{"slot": 0, "item": 1, "count": 1, "meta": 0}]),
        _magma_row(2, inventory=[{"slot": 0, "item": 1, "count": 1, "meta": 0}]),
    ]
    ok = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    assert ok["inventory"]["pass"] is True
    assert not replay_tape._has_explicit_gui_or_packet_event(ticks[0])


def test_dig_place_finals_script_and_mutation_timeline(tmp_path: Path):
    header, ticks = _load("dig_place_finals.json")
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    posts = [e for e in events if e["type"] == "set_block_post"]
    assert posts == [
        {"tick": 7, "type": "set_block_post",
         "x": 10, "y": 64, "z": -3, "id": 0, "meta": 0},
        {"tick": 7, "type": "set_block_post",
         "x": 11, "y": 64, "z": -3, "id": 1, "meta": 0},
    ]
    c_rows = [
        _magma_row(0, nearby_hash="aaaaaaaaaaaaaaaa",
                   nearby_anchor=[0, 70, 0]),
        _magma_row(1, nearby_hash="bbbbbbbbbbbbbbbb",
                   nearby_anchor=[0, 70, 0]),
    ]
    # Align c_rows tick indexes with tape row indices 0..1 (t values 7,8).
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    assert state["world"]["mutation_total"] == 2
    assert state["world"]["mutation_timeline"][0]["id"] == 0
    assert state["world"]["mutation_timeline"][1]["id"] == 1
    assert state["world"]["pass"] is True


def test_neighbor_plant_cascade_ordered_bc(tmp_path: Path):
    header, ticks = _load("neighbor_plant_cascade.json")
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    posts = [json.loads(line) for line in script.read_text().splitlines()
             if json.loads(line)["type"] == "set_block_post"]
    # Plant above first, then grass — exact mutation order preserved.
    assert [(p["x"], p["y"], p["z"], p["id"]) for p in posts] == [
        (116, 66, 324, 0),
        (116, 65, 324, 0),
    ]
    total, timeline = replay_tape._mutation_timeline(ticks)
    assert total == 2
    assert timeline[0]["ord"] == 0 and timeline[1]["ord"] == 1


def test_fluid_update_bc_levels(tmp_path: Path):
    header, ticks = _load("fluid_update.json")
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    posts = [json.loads(line) for line in script.read_text().splitlines()
             if json.loads(line).get("type") == "set_block_post"]
    assert [p["meta"] for p in posts] == [0, 1, 2]
    assert all(p["id"] == 8 for p in posts)


def test_falling_landing_bc(tmp_path: Path):
    header, ticks = _load("falling_landing.json")
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    posts = [json.loads(line) for line in script.read_text().splitlines()
             if json.loads(line).get("type") == "set_block_post"]
    assert posts == [
        {"tick": 12, "type": "set_block_post",
         "x": 5, "y": 70, "z": 5, "id": 0, "meta": 0},
        {"tick": 12, "type": "set_block_post",
         "x": 5, "y": 64, "z": 5, "id": 12, "meta": 0},
    ]


def test_explosion_script_events(tmp_path: Path):
    header, ticks = _load("explosion.json")
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    adds = [e for e in events if e["type"] == "add_velocity"]
    snaps = [e for e in events if e["type"] == "snapshot_block"]
    posts = [e for e in events if e["type"] == "set_block_post"]
    assert adds == [{"tick": 4, "type": "add_velocity",
                     "x": 0.3, "y": 0.5, "z": 0.1}]
    assert len(snaps) == 4 and all(s["id"] == 0 for s in snaps)
    assert len(posts) == 4
    state = replay_tape.collect_state_assertions(
        ticks,
        [_magma_row(0, nearby_hash="00", nearby_anchor=[0, 70, 0])],
        sample_every=1,
    )
    assert state["world"]["mutation_total"] == 4


def test_bulk_update_over_64_cells(tmp_path: Path):
    header, ticks = _load("bulk_update_over64.json")
    # Fill >64 setBlockState finals (client path). Chunk.fillChunk is a gap.
    ticks[0]["bc"] = [
        [x % 16, 64 + (x // 16) % 4, x // 64, 1, 0] for x in range(80)
    ]
    assert len(ticks[0]["bc"]) > 64
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    posts = [json.loads(line) for line in script.read_text().splitlines()
             if json.loads(line).get("type") == "set_block_post"]
    assert len(posts) == 80
    total, sample = replay_tape._mutation_timeline(ticks)
    assert total == 80
    assert len(sample) == replay_tape._MUTATION_TIMELINE_SAMPLE  # sample-capped
    assert sample[0]["ord"] == 0 and sample[-1]["ord"] == 63


def test_dimension_handoff_loading_ticks():
    header, ticks = _load("dimension_handoff.json")
    loading = replay_tape.tape_loading_ticks(header, ticks)
    assert 1 in loading and 2 in loading
    # After handoff, non-frozen motion ends the loading plateau.
    assert 3 not in loading or ticks[3].get("loading")
    # Script path still accepts dim fields without crashing.
    c_rows = [
        _magma_row(0, dim=0, nearby_hash="aaaaaaaaaaaaaaaa",
                   nearby_anchor=[0, 70, 0]),
        _magma_row(1, dim=-1, nearby_hash="bbbbbbbbbbbbbbbb",
                   nearby_anchor=[8, 64, 8]),
        _magma_row(2, dim=-1, nearby_hash="cccccccccccccccc",
                   nearby_anchor=[8, 64, 8]),
        _magma_row(3, dim=-1, nearby_hash="dddddddddddddddd",
                   nearby_anchor=[8, 64, 8]),
    ]
    # Patch poses into magma rows for physics quiet.
    for i, row in enumerate(ticks):
        c_rows[i]["x"] = row["x"]
        c_rows[i]["y"] = row["y"]
        c_rows[i]["z"] = row["z"]
        c_rows[i]["vx"] = row["vx"]
        c_rows[i]["vy"] = row["vy"]
        c_rows[i]["vz"] = row["vz"]
        c_rows[i]["on_ground"] = row["og"]
        c_rows[i]["dim"] = row["dim"]
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    assert state["world"]["mode"] == "java"
    assert state["world"]["same_anchor"] == 4
    assert state["world"]["changed_anchor"] == 0
    assert state["world"]["pass"] is True


def test_world_diagnostics_mismatch_total_intervals_reconverge_cell():
    _header, ticks = _load("world_diag_reconverge.json")
    # Magma: match t0, mismatch t1-t2, match t3, changed-anchor t4.
    c_rows = [
        _magma_row(0, nearby_hash="aaaaaaaaaaaaaaaa", nearby_anchor=[0, 70, 0],
                   nearby_cells=[[0, 70, 0, 1, 0], [1, 70, 0, 2, 0]]),
        _magma_row(1, nearby_hash="ffffffff00000001", nearby_anchor=[0, 70, 0],
                   nearby_cells=[[0, 70, 0, 1, 0], [1, 70, 0, 2, 0]]),  # still grass
        _magma_row(2, nearby_hash="ffffffff00000002", nearby_anchor=[0, 70, 0],
                   nearby_cells=[[0, 70, 0, 1, 0], [1, 70, 0, 2, 0]]),
        _magma_row(3, nearby_hash="dddddddddddddddd", nearby_anchor=[0, 70, 0],
                   nearby_cells=[[0, 70, 0, 1, 0], [1, 70, 0, 0, 0]]),
        _magma_row(4, nearby_hash="eeeeeeeeeeeeeeee", nearby_anchor=[0, 70, 0],
                   nearby_cells=[]),  # anchor disagree with tape wfa [1,70,0]
    ]
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    world = state["world"]
    assert world["mode"] == "java"
    assert world["mismatch_total"] == 2  # t1, t2
    assert world["interval_count"] == 1
    assert world["intervals"][0]["start"] == 1
    assert world["intervals"][0]["end"] == 2
    assert world["first_mismatch"]["tick"] == 1
    assert world["last_mismatch"]["tick"] == 2
    assert world["reconvergences"] == [3]
    assert world["same_anchor"] == 4
    assert world["changed_anchor"] == 1
    assert world["anchor_skips"] == 1  # legacy alias
    assert world["mutation_total"] == 1
    cell = world["first_differing_cell"]
    assert cell is not None
    assert (cell["x"], cell["y"], cell["z"]) == (1, 70, 0)
    assert cell["java_id"] == 0 and cell["magma_id"] == 2
    # Sample cap independence: even with many mismatches, total is uncapped.
    assert len(world["mismatches"]) <= replay_tape._STATE_MISMATCH_SAMPLE
    assert world["pass"] is False


def test_world_mismatch_total_independent_of_sample_cap():
    ticks = []
    c_rows = []
    for t in range(40):
        ticks.append({
            "t": t, "x": 0.5, "y": 70.0, "z": 0.5,
            "vx": 0.0, "vy": 0.0, "vz": 0.0, "og": 1,
            "hp": 20.0, "food": 20, "ents": [],
            "wfnv": f"{t:016x}", "wfa": [0, 70, 0],
        })
        c_rows.append(_magma_row(
            t, nearby_hash=f"{(t + 1):016x}", nearby_anchor=[0, 70, 0]))
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    world = state["world"]
    assert world["mismatch_total"] == 40
    assert len(world["mismatches"]) == replay_tape._STATE_MISMATCH_SAMPLE
    assert world["interval_count"] == 1
    assert world["first_mismatch"]["tick"] == 0
    assert world["last_mismatch"]["tick"] == 39
    assert world["reconvergences"] == []


def test_inventory_hotbar_region_labeled():
    inv = [[1, 0, 1]] + [0] * 40
    inv[0] = [3, 0, 2]  # dirt in hotbar 0
    ticks = [
        {"t": 0, "x": 0.5, "y": 70.0, "z": 0.5, "vx": 0.0, "vy": 0.0,
         "vz": 0.0, "og": 1, "hp": 20.0, "food": 20, "ents": [], "inv": inv},
    ]
    c_rows = [_magma_row(0, inventory=[])]  # missing hotbar stack
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    assert state["inventory"]["pass"] is False
    assert state["inventory"]["mismatches"][0]["region"] == "hotbar"
    assert state["inventory"]["mismatches"][0]["tape_count"] == 2


def test_legacy_tape_without_bc_or_wfnv_unchanged():
    """Legacy inputs: no wfnv / no bc keep c_only mode and empty timeline."""
    ticks = [
        {"t": 0, "x": 0.5, "y": 70.0, "z": 0.5, "vx": 0.0, "vy": 0.0,
         "vz": 0.0, "og": 1, "hp": 20.0, "food": 20, "ents": []},
    ]
    c_rows = [_magma_row(0, nearby_hash="deadbeefdeadbeef")]
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    assert state["world"]["mode"] == "c_only"
    assert state["world"]["verified"] is False
    assert state["world"]["pass"] is True  # informational legacy pass
    assert state["world"]["mutation_total"] == 0
    assert state["world"]["mismatch_total"] == 0
    assert state["world"]["same_anchor"] == 0
    assert state["kind"] == "state"
