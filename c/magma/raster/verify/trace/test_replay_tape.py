import json
from pathlib import Path

import replay_tape


def test_new_recorder_state_becomes_sorted_render_and_next_tick_events(tmp_path: Path):
    inv = [0] * 41
    inv[0] = [17, 0, 2]
    inv[40] = [442, 0, 1]
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "vx": 0.0, "vy": 0.0, "vz": 0.0, "hp": 20.0, "food": 20,
        "dim": 0,
        "velocity_packets": 1,
        "position_packets": 1,
    }
    sheep = [7, "EntitySheep", 1.0, 64.0, 2.0, 30.0, 8.0,
             55.0, 12.0, 0.25, 4, 2, 28.0, 3, 1, 14, 0.75, 1.1]
    item = [8, "EntityItem", 2.0, 64.0, 3.0, 0.0, -1.0,
            318, 0, 3, 12, 1.25]
    ticks = [
        {"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5,
         "yaw": 0.0, "pitch": 0.0, "wt": 6001,
         "hp": 17.0, "food": 20, "dim": -1,
         "xpl": 7, "xpp": 0.625, "air": 123, "portal": 0.5,
         "portal_frame": 17, "portal_phase": 1234, "loading": 1,
         "gui": "GuiDownloadTerrain",
         "vx": 0.01, "vy": 0.02, "vz": -0.03, "og": 0,
         "inv": inv, "ents": [sheep, item], "pvel": [80, 160, -240]},
        {"t": 1, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5,
         "yaw": 0.0, "pitch": 0.0, "wt": 6002,
         "hp": 17.0, "food": 20, "dim": 0,
         "xpl": 7, "xpp": 0.625, "ents": []},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert [event["tick"] for event in events] == sorted(event["tick"] for event in events)

    tick0 = [event for event in events if event["tick"] == 0]
    assert {event["slot"] for event in tick0 if event["type"] == "inv_view"} == (
        set(range(36)) | {40}
    )
    assert any(event["type"] == "player_view" and event["xp_level"] == 7
               and event["air"] == 123 and event["portal"] == 0.5
               and event["portal_frame"] == 17 and event["portal_phase"] == 1234
               and event["loading"] == 1 for event in tick0)
    tick1 = [json.loads(line) for line in script.read_text().splitlines()
             if json.loads(line)["tick"] == 1]
    assert any(event["type"] == "set_dimension" and event["dimension"] == -1
               for event in tick0)
    assert any(event["type"] == "set_dimension" and event["dimension"] == 0
               for event in tick1)
    assert any(event["type"] == "set_time" and event["value"] == 6001
               for event in tick0)
    assert any(event["type"] == "set_dimension" and event["dimension"] == 0
               for event in tick0)
    assert any(event["type"] == "set_packet_velocity" and event["x"] == 0.01
               and event["y"] == 0.02 and event["z"] == -0.03 for event in tick0)
    assert any(event["type"] == "set_vitals_post" and event["health"] == 17.0
               for event in tick0)
    assert not any(event["type"] == "ent_box" for event in tick0)
    sheep_event = next(event for event in tick0
                       if event["type"] == "ent_view" and event["ent"] == "EntitySheep")
    assert sheep_event["body_yaw"] == 28.0
    assert sheep_event["head_yaw"] == 55.0
    assert sheep_event["sheared"] == 1
    assert sheep_event["graze_x"] == 1.1
    item_event = next(event for event in tick0
                      if event["type"] == "ent_view" and event["ent"] == "EntityItem")
    assert item_event["item"] == 318
    assert item_event["count"] == 3
    assert item_event["hover"] == 1.25

    tick1 = [event for event in events if event["tick"] == 1]
    assert {event["slot"] for event in tick1 if event["type"] == "set_inventory"} == (
        set(range(36)) | {40}
    )


def test_cached_snapshot_patch_is_applied_at_tick_zero(tmp_path: Path):
    tape = tmp_path / "sample.jsonl"
    (tmp_path / "sample_world" / "region").mkdir(parents=True)
    cache = tmp_path / "sample.jsonl.snapshot_patch.jsonl"
    cache.write_text(
        '{"tick":0,"type":"snapshot_block","x":4,"y":65,"z":7,"id":17,"meta":4}\n'
    )
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20,
    }
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0,
              "z": 0.5, "yaw": 0.0, "pitch": 0.0, "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script), tape_path=str(tape))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    snapshot = next(event for event in events if event["type"] == "snapshot_block")
    assert snapshot == {"tick": 0, "type": "snapshot_block",
                        "x": 4, "y": 65, "z": 7, "id": 17, "meta": 4}


def test_position_packet_reloads_nearby_snapshot_chunks(tmp_path: Path):
    patch = tmp_path / "snapshot.jsonl"
    patch.write_text(
        '{"tick":0,"type":"snapshot_block","dim":-1,'
        '"x":24,"y":75,"z":24,"id":49,"meta":0}\n'
        '{"tick":0,"type":"snapshot_block","dim":-1,'
        '"x":80,"y":75,"z":80,"id":1,"meta":0}\n'
    )
    ticks = [{"t": 7, "dim": -1,
              "ppos": [24.5, 76.0, 24.5, 270.0, 0.0, 0.0, 0.0, 0.0]}]
    events = replay_tape.snapshot_arrival_events(patch, ticks)
    assert events[7][0] == {"tick": 7, "type": "snapshot_region", "dim": -1,
                            "cx": 1, "cz": 1, "radius": 1}
    blocks = [event for event in events[7] if event["type"] == "snapshot_block"]
    assert blocks == [{"tick": 7, "type": "snapshot_block", "dim": -1,
                       "x": 24, "y": 75, "z": 24, "id": 49, "meta": 0}]


def test_recorded_food_change_is_reanchored_post_tick(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 6000,
              "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 20.0, "food": 20}
    ticks = [{"t": 0, "in": {"f": 1, "s": 0}, "x": 0.5, "y": 70.0,
              "z": 0.5, "yaw": 0.0, "pitch": 0.0, "hp": 20.0,
              "food": 19, "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 0, "type": "set_vitals_post", "health": 20.0,
            "food": 19} in events


def test_dimension_loading_and_position_packet_become_typed_pose_events(tmp_path: Path):
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20, "dim": 0, "position_packets": 1,
    }
    empty = {"f": 0, "s": 0}
    ticks = [{
        "t": 0, "in": empty, "x": 8.5, "y": 65.0, "z": 8.5,
        "yaw": -180.0, "pitch": 0.0, "vx": 0.0, "vy": 0.0, "vz": 0.0,
        "og": 0, "fall": 0.0, "hp": 20.0, "food": 20, "dim": -1,
        "loading": 1,
        "ppos": [24.5, 76.0, 24.5, 270.0, 0.0, 0.0, 0.0, 0.0],
    }]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert any(event["type"] == "set_pose" and event["x"] == 24.5
               for event in events)
    post = next(event for event in events if event["type"] == "set_pose_post")
    assert post["x"] == 8.5 and post["on_ground"] == 0 and post["fall"] == 0.0


def test_legacy_dimension_loading_plateau_ends_when_physics_resumes():
    header = {"dim": 0}
    base = {"in": {"f": 0, "s": 0}, "yaw": 0.0, "pitch": 0.0,
            "vx": 0.0, "vy": 0.0, "vz": 0.0, "og": 0}
    ticks = [
        {**base, "t": 0, "dim": 0, "x": 1.0, "y": 70.0, "z": 1.0},
        {**base, "t": 1, "dim": -1, "x": 8.5, "y": 65.0, "z": 8.5,
         "gui": "GuiDownloadTerrain"},
        {**base, "t": 2, "dim": -1, "x": 24.5, "y": 76.0, "z": 24.5},
        {**base, "t": 3, "dim": -1, "x": 24.5, "y": 76.0, "z": 24.5,
         "vy": -0.0784000015258789},
    ]
    assert replay_tape.tape_loading_ticks(header, ticks) == {1, 2}


def test_gui_container_slots_cursor_and_furnace_progress_are_mapped(tmp_path: Path):
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20,
    }
    empty_input = {"f": 0, "s": 0}
    crafting = [0] * 46
    crafting[0] = [58, 0, 1]
    crafting[1] = [5, 2, 3]
    crafting[37] = [280, 0, 1]
    furnace = [0] * 39
    furnace[0] = [15, 0, 2]
    furnace[1] = [263, 0, 4]
    furnace[2] = [265, 0, 1]
    ticks = [
        {"t": 0, "in": empty_input, "x": 0.5, "y": 70.0, "z": 0.5,
         "yaw": 0.0, "pitch": 0.0, "ents": [], "gui": "GuiCrafting",
         "gslots": crafting, "gcur": [17, 1, 2]},
        {"t": 1, "in": empty_input, "x": 0.5, "y": 70.0, "z": 0.5,
         "yaw": 0.0, "pitch": 0.0, "ents": [], "gui": "GuiFurnace",
         "gslots": furnace, "gcur": 0, "gprop": [80, 1600, 100, 200]},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]

    tick0 = [event for event in events if event["tick"] == 0]
    slots0 = {event["slot"]: event for event in tick0
              if event["type"] == "gui_slot_view"}
    assert slots0[45]["item"] == 58
    assert slots0[36] == {"tick": 0, "type": "gui_slot_view", "slot": 36,
                          "item": 5, "count": 3, "meta": 2}
    assert slots0[0]["item"] == 280
    assert next(event for event in tick0
                if event["type"] == "gui_cursor_view")["item"] == 17

    tick1 = [event for event in events if event["tick"] == 1]
    slots1 = {event["slot"]: event for event in tick1
              if event["type"] == "gui_slot_view"}
    assert [slots1[slot]["item"] for slot in (46, 47, 48)] == [15, 263, 265]
    assert next(event for event in tick1
                if event["type"] == "gui_cursor_view")["count"] == 0
    assert next(event for event in tick1
                if event["type"] == "gui_furnace_view") == {
                    "tick": 1, "type": "gui_furnace_view", "burn": 80,
                    "current_burn": 1600, "cook": 100, "total_cook": 200,
                }


def test_health_packet_alignment_accepts_adjacent_tick():
    base = {"x": 0.0, "y": 64.0, "z": 0.0, "vx": 0.0, "vy": 0.0,
            "vz": 0.0, "og": 1, "food": 20}
    ticks = [
        {**base, "hp": 18.0},
        {**base, "hp": 18.833334},
        {**base, "hp": 18.833334},
    ]
    c_rows = [
        {**base, "on_ground": 1, "health": 18.0},
        {**base, "on_ground": 1, "health": 18.0},
        {**base, "on_ground": 1, "health": 18.833334},
    ]
    for row in c_rows:
        row.pop("og")
        row.pop("hp", None)
    first, _ = replay_tape.first_divergence(ticks, c_rows)
    assert first is None


def test_dimension_is_compared_exactly_when_recorded():
    tape = {"x": 0.0, "y": 64.0, "z": 0.0, "vx": 0.0, "vy": 0.0,
            "vz": 0.0, "og": 1, "hp": 20.0, "food": 20, "dim": -1}
    magma = {"x": 0.0, "y": 64.0, "z": 0.0, "vx": 0.0, "vy": 0.0,
               "vz": 0.0, "on_ground": 1, "health": 20.0,
               "food": 20, "dim": 0}
    first, _ = replay_tape.first_divergence([tape], [magma])
    assert first == (0, "dim", -1, 0, 1.0)


def test_replay_comparison_stops_at_terminal_death():
    base = {"x": 0.0, "y": 64.0, "z": 0.0, "vx": 0.0, "vy": 0.0,
            "vz": 0.0, "og": 1, "food": 20, "dim": 0}
    ticks = [{**base, "hp": 0.0}, {**base, "hp": 20.0, "x": 100.0}]
    c_rows = [{**base, "health": 0.0, "on_ground": 1}]
    c_rows[0].pop("og")
    c_rows[0].pop("hp", None)
    first, distances = replay_tape.first_divergence(ticks, c_rows)
    assert first is None
    assert distances == [0.0]
