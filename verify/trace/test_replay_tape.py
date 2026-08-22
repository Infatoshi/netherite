import json
from pathlib import Path

import numpy as np
import pixel_gate
import pytest
import replay_tape
from PIL import Image

TRACE_DIR = Path(__file__).resolve().parent
CANONICAL_NAME = "20260712T055346Z_fast_s0_survival_default_rd8_77b5b462"


def _canonical_frame_pair(tick):
    frame_name = f"f_{tick:06d}.png"
    candidates = [
        TRACE_DIR.parent / "tapes" / f"{CANONICAL_NAME}_frames" / frame_name,
        TRACE_DIR.parent / "demo" / f"{CANONICAL_NAME}_frames" / frame_name,
    ]
    oracle_path = next((p for p in candidates if p.exists()), None)
    if oracle_path is None:
        pytest.skip(f"canonical oracle frame missing: {frame_name}")
    magma_path = (TRACE_DIR / "out" / f"tape_{CANONICAL_NAME}"
                  / "magma_frames.npy")
    if not magma_path.exists():
        # Synthetic magma stand-in when the replay npy is not present: start
        # from the oracle so marker-box overlays remain the only intentional
        # residual. Pre-existing cluster tests only assert the overlay fails.
        oracle = np.asarray(Image.open(oracle_path).convert("RGB"),
                            dtype=np.int16)
        return oracle, oracle.copy()
    oracle = np.asarray(Image.open(oracle_path).convert("RGB"), dtype=np.int16)
    magma_frames = np.load(magma_path, mmap_mode="r")
    magma = np.asarray(magma_frames[tick // 20], dtype=np.int16).copy()
    return oracle, magma


def _add_midframe_marker(magma):
    magma[210:310, 250:350] = 255
    return magma


def test_absolute_pixel_refusal_never_becomes_a_pass():
    gate = {"pass": False, "frames_checked": 5, "failed_frames": [{"tick": 1}]}
    status = {"status": "blocked", "pass": None, "reason": "provenance_mismatch"}
    refused = replay_tape.mark_absolute_pixel_refused(gate, status)
    assert refused["pass"] is None
    assert refused["absolute_pixel_refused"] is True
    assert refused["pixel_status"] == status
    assert gate["pass"] is False


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
    sheep = [7, "EntitySheep", 1.0, 70.0, 2.0, 30.0, 8.0,
             55.0, 12.0, 0.25, 4, 2, 28.0, 3, 1, 14, 0.75, 1.1]
    item = [8, "EntityItem", 2.0, 64.0, 3.0, 0.0, -1.0,
            318, 0, 3, 12, 1.25]
    ticks = [
        {"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5,
         "yaw": 0.0, "pitch": 0.0, "wt": 6001,
         "hp": 17.0, "food": 20, "dim": -1,
         "xpl": 7, "xpp": 0.625, "air": 123, "portal": 0.5,
         "portal_frame": 17, "portal_phase": 1234, "loading": 1,
         "hurt": 8, "maxhurt": 10, "hurtyaw": 27.5, "cd": 0.4,
         "pots": [[20, 0, 157]],
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
        set(range(41))
    )
    assert any(event["type"] == "player_view" and event["xp_level"] == 7
               and event["air"] == 123 and event["portal"] == 0.5
               and event["portal_frame"] == 17 and event["portal_phase"] == 1234
               and event["loading"] == 1 and event["hurt"] == 8
               and event["hurt_yaw"] == 27.5
               and event["attack_cooldown"] == 0.4 for event in tick0)
    assert any(event["type"] == "potion_clear" for event in tick0)
    assert any(event["type"] == "potion_view" and event["id"] == 20
               and event["duration"] == 157 for event in tick0)
    # 3-field legacy pots rows carry no showParticles: magma must keep
    # vanilla's shown default rather than invent a flag.
    assert all("show_particles" not in event for event in tick0
               if event["type"] == "potion_view")
    # No "armor" in the row -> no override event (item-derived value stands).
    assert not any(event["type"] == "armor_view" for event in tick0)
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
    assert any(event["type"] == "set_vitals" and event["health"] == 17.0
               for event in tick0)
    assert any(event["type"] == "ent_box" and event["x"] == 1.0
               and event["w"] == 0.9 for event in tick0)
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
        set(range(41))
    )


def test_large_fireball_is_modeled_and_never_becomes_a_ghost_pusher(tmp_path: Path):
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20,
    }
    fireball = [9, "EntityLargeFireball", 0.6, 70.5, 0.6, 0.0, -1.0]
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0,
              "z": 0.5, "yaw": 0.0, "pitch": 0.0, "ents": [fireball]}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert not any(event["type"] == "ent_box" for event in events)
    assert replay_tape.skipped_renderable_counts(ticks) == {}


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


def test_snapshot_probe_omits_authoritative_container_lifecycle(tmp_path: Path):
    patch = tmp_path / "snapshot.jsonl"
    patch.write_text("")
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20,
    }
    ticks = [{
        "t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0,
        "z": 0.5, "yaw": 0.0, "pitch": 0.0, "ents": [],
        "gopen": [["GuiCrafting", 1, "workbench", 0, 4, 3,
                   [0] * 10, 0]],
    }]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(
        header, ticks, str(script), snapshot_override=patch)
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert not any(event["type"].startswith("container_") for event in events)


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
    header = {"dim": 0, "x": 0.5, "z": 0.5}
    events = replay_tape.snapshot_arrival_events(patch, header, ticks)
    # The tape-start ensure lands on this same tick; both regions survive
    # (a dict keyed by tick used to silently drop the earlier one).
    assert {"tick": 7, "type": "snapshot_region", "dim": -1,
            "cx": 1, "cz": 1, "radius": 1} in events[7]
    blocks = [event for event in events[7] if event["type"] == "snapshot_block"]
    assert blocks == [{"tick": 7, "type": "snapshot_block", "dim": -1,
                       "x": 24, "y": 75, "z": 24, "id": 49, "meta": 0}]


def test_portal_transit_reloads_arrival_dimension_snapshot(tmp_path: Path):
    """A dim flip with no position packet is still a world transfer.

    Portal roundtrip 075228Z: the recorder logs dim -1 from t=133, but the
    first ppos row is t=168.  Without the dim-flip arrival the Nether patch is
    only ever applied at tick 0, to a world the player is not in yet.
    """
    patch = tmp_path / "snapshot.jsonl"
    patch.write_text(
        '{"tick":0,"type":"snapshot_block","dim":-1,'
        '"x":5,"y":22,"z":-5,"id":51,"meta":0}\n'
        '{"tick":0,"type":"snapshot_block","dim":-1,'
        '"x":9,"y":21,"z":-3,"id":11,"meta":0}\n'
        '{"tick":0,"type":"snapshot_block","dim":0,'
        '"x":8,"y":4,"z":0,"id":49,"meta":0}\n'
    )
    ticks = [{"t": 132, "dim": 0, "x": 8.5, "y": 4.0, "z": 0.5},
             {"t": 133, "dim": -1, "x": 5.5, "y": 22.0, "z": -5.0}]
    header = {"dim": 0, "x": 0.5, "z": 1.0}
    events = replay_tape.snapshot_arrival_events(patch, header, ticks)
    assert 133 in events
    region = events[133][0]
    assert region["type"] == "snapshot_region" and region["dim"] == -1
    assert (region["cx"], region["cz"]) == (0, -1)
    assert region["radius"] == 8
    cells = {(event["id"], event["x"], event["y"], event["z"])
             for event in events[133] if event["type"] == "snapshot_block"}
    assert cells == {(51, 5, 22, -5), (11, 9, 21, -3)}


def test_rain_thunder_header_and_row_changes(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 6000,
              "x": 0.5, "y": 4.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 20.0, "food": 20, "rain_strength": 1.0,
              "thunder_strength": 1.0}
    def row(t, rain=None, thunder=None):
        r = {"t": t, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 4.0, "z": 0.5,
             "yaw": 0.0, "pitch": 0.0, "hp": 20.0, "food": 20, "ents": []}
        if rain is not None:
            r["rain"] = rain
        if thunder is not None:
            r["thunder"] = thunder
        return r
    ticks = [row(0, 1.0, 1.0), row(1, 1.0, 1.0),
             row(2, 0.5, 0.5), row(3)]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()
              if json.loads(line)["type"] == "set_rain_thunder"]
    assert events == [
        {"tick": 0, "type": "set_rain_thunder", "rain": 1.0, "thunder": 1.0},
        {"tick": 2, "type": "set_rain_thunder", "rain": 0.5, "thunder": 0.5},
        {"tick": 3, "type": "set_rain_thunder", "rain": 0.0, "thunder": 0.0},
    ]


def test_clear_weather_tape_omits_rain_thunder(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 6000,
              "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 20.0, "food": 20, "rain_strength": 0.0,
              "thunder_strength": 0.0}
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0,
              "z": 0.5, "yaw": 0.0, "pitch": 0.0, "hp": 20.0,
              "food": 20, "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    types = [json.loads(line)["type"] for line in script.read_text().splitlines()]
    assert "set_rain_thunder" not in types


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


def test_packet_backed_mob_damage_is_seeded_before_regeneration(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 4.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 20.0, "food": 20}
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 4.0,
              "z": 0.5, "yaw": 0.0, "pitch": 0.0, "hp": 17.0,
              "food": 20, "pvel": [-130, 2886, -3197], "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 0, "type": "set_vitals", "health": 17.0,
            "food": 20} in events
    assert not any(event["type"] == "set_vitals_post" for event in events)


def test_packet_damage_keeps_same_tick_food_rollover_in_sim(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 4.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 17.0, "food": 20}
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 4.0,
              "z": 0.5, "yaw": 0.0, "pitch": 0.0, "hp": 15.0,
              "food": 19, "pvel": [100, 2886, -100], "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 0, "type": "set_vitals", "health": 15.0,
            "food": 20} in events


def test_recorded_drowning_damage_is_seeded_before_regeneration(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 4.0, "z": 0.5, "yaw": 0.0, "pitch": 55.0,
              "hp": 17.5, "food": 20}
    ticks = [{"t": 402, "in": {"f": 1, "s": 0}, "x": 0.5, "y": 2.0,
              "z": 0.5, "yaw": 0.0, "pitch": 55.0, "hp": 15.5,
              "food": 20, "air": -2, "hurt": 9, "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 402, "type": "set_vitals", "health": 15.5,
            "food": 20} in events


def test_recorded_lava_damage_is_seeded_before_regeneration(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 4.0, "z": -1.5, "yaw": 0.0, "pitch": 20.0,
              "hp": 19.0, "food": 20}
    ticks = [{"t": 68, "in": {"f": 1, "s": 0}, "x": 0.5, "y": 3.8,
              "z": 2.0, "yaw": 0.0, "pitch": 20.0, "hp": 16.0,
              "food": 20, "fire": 1, "hurt": 9, "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 68, "type": "set_vitals", "health": 16.0,
            "food": 20} in events


def test_recorded_respawn_revives_before_the_destination_tick(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 3.0, "z": 2.0, "yaw": 0.0, "pitch": 20.0,
              "hp": 0.0, "food": 20}
    ticks = [{"t": 115, "in": {"f": 0, "s": 0}, "x": 508.5, "y": 4.0,
              "z": -0.5, "yaw": 0.0, "pitch": 0.0, "hp": 20.0,
              "vx": 0.0, "vy": 0.0, "vz": 0.0, "og": 0,
              "food": 20, "xpl": 0, "xpp": 0.0, "fire": 1,
              "ppos": [508.5, 4.0, -0.5, 0.0, 0.0,
                                      0.0, 0.0, 0.0], "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 115, "type": "set_vitals", "health": 20.0,
            "food": 20} in events
    assert {"tick": 0, "type": "continue_after_death"} in events
    assert not any(event["type"] == "set_regen_post" for event in events)
    assert any(event["type"] == "player_view" and event["loading"] == 2
               for event in events)


def test_combat_respawn_keeps_the_terminal_death_contract(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 4.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 0.0, "food": 20}
    ticks = [{"t": 10, "in": {"f": 0, "s": 0}, "x": 8.5, "y": 4.0,
              "z": 8.5, "yaw": 0.0, "pitch": 0.0, "hp": 20.0,
              "food": 20, "vx": 0.0, "vy": 0.0, "vz": 0.0, "og": 0,
              "ppos": [8.5, 4.0, 8.5, 0.0, 0.0,
                                      0.0, 0.0, 0.0],
              "ents": [[1, "EntityBlaze", 9.0, 4.0, 9.0, 0.0, 20.0]]}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert not any(event["type"] == "continue_after_death" for event in events)


def test_recorded_natural_regeneration_reconciles_hidden_timer(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 4.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 17.0, "food": 20}
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 4.0,
              "z": 0.5, "yaw": 0.0, "pitch": 0.0, "hp": 17.833334,
              "food": 20, "sat": 5.0, "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 0, "type": "set_regen_post", "health": 17.833334,
            "food": 20, "exhaustion": 5.0} in events


def test_stable_saturated_health_holds_early_server_regen(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 4.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 14.0, "food": 20}
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 4.0,
              "z": 0.5, "yaw": 0.0, "pitch": 0.0, "hp": 14.0,
              "food": 20, "sat": 3.0, "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 0, "type": "hold_regen_post"} in events


def test_stable_unsaturated_health_holds_early_server_regen(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 4.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 14.0, "food": 19}
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 4.0,
              "z": 0.5, "yaw": 0.0, "pitch": 0.0, "hp": 14.0,
              "food": 19, "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 0, "type": "hold_regen_post"} in events


def test_recorded_landing_defers_inferred_velocity_resend(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 14.0, "food": 20, "og": 0, "velocity_packets": 1}
    base = {"in": {"f": 0, "s": 0}, "x": 0.5, "z": 0.5,
            "yaw": 0.0, "pitch": 0.0, "hp": 14.0, "food": 20,
            "ents": []}
    ticks = [
        {**base, "t": 0, "y": 63.0, "og": 0, "fall": 4.5},
        {**base, "t": 1, "y": 62.0, "og": 1, "fall": 0.0},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 1, "type": "clear_hurt_velocity_post"} in events
    assert {"tick": 1, "type": "hold_fall_damage_post"} in events


def test_movement_start_uses_same_tick_look_change(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 18000,
              "x": 0.5, "y": 64.0, "z": 0.5, "yaw": -0.45,
              "pitch": -10.15, "hp": 20.0, "food": 20}
    ticks = [{"t": 0, "in": {"f": 0, "s": 1}, "x": 0.5, "y": 64.0,
              "z": 0.5, "yaw": 0.15, "pitch": -10.6, "hp": 20.0,
              "food": 20, "ents": []}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 0, "type": "set_look_pre", "yaw": 0.15,
            "pitch": -10.6} in events


def test_midwalk_turn_uses_position_evidence_for_same_tick_look(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 6000,
              "x": 0.0, "y": 64.0, "z": 0.0, "yaw": 1.8,
              "pitch": 0.0, "hp": 20.0, "food": 20}
    ticks = [
        {"t": 0, "in": {"f": 0, "s": -1}, "x": -0.1, "y": 64.0,
         "z": 0.0, "yaw": 1.8, "pitch": 0.0, "vx": -0.05, "vz": 0.0,
         "hp": 20.0, "food": 20, "ents": []},
        # displacement - previous velocity is exactly -X: yaw 0, not yaw 1.8
        {"t": 1, "in": {"f": 0, "s": -1}, "x": -0.25, "y": 64.0,
         "z": 0.0, "yaw": 0.0, "pitch": 0.0, "vx": -0.08, "vz": 0.0,
         "hp": 20.0, "food": 20, "ents": []},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 1, "type": "set_look_pre", "yaw": 0.0,
            "pitch": 0.0} in events


def test_recorded_saturation_zero_switches_foodstats_branch(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 6000,
              "x": 0.5, "y": 64.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 14.0, "food": 20}
    ticks = [
        {"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 64.0,
         "z": 0.5, "yaw": 0.0, "pitch": 0.0, "hp": 14.5,
         "food": 20, "sat": 1.0, "ents": []},
        {"t": 1, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 64.0,
         "z": 0.5, "yaw": 0.0, "pitch": 0.0, "hp": 14.5,
         "food": 20, "ents": []},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 0, "type": "set_food_stats_post", "saturation": 1.0,
            "exhaustion": 0.0} in events
    assert {"tick": 1, "type": "set_food_stats_post", "saturation": 0.0,
            "exhaustion": 0.0} in events


def test_dragon_packet_damage_uses_recorded_contact_box(tmp_path: Path):
    header = {"header": 1, "seed": 0, "world_time": 6000,
              "x": 0.5, "y": 64.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 10.0, "food": 20}
    dragon = [42, "EntityDragon", 2.0, 68.0, 3.0, 90.0, 200.0]
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 1.0, "y": 64.4,
              "z": 1.0, "yaw": 0.0, "pitch": 0.0, "hp": 5.0,
              "food": 20, "pvel": [100, 3200, 100], "ents": [dragon]}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    contact = next(e for e in events if e["type"] == "dragon_contact")
    assert contact["damage"] == 5.0 and contact["min_x"] == -9.0
    assert not any(e["type"] == "set_vitals" and e.get("health") == 5.0
                   for e in events)


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


def test_respawn_keeps_four_evidenced_empty_chunk_frames():
    header = {"hp": 2.0, "position_packets": 1}
    ticks = [
        {"t": 114, "hp": 0.0},
        {"t": 115, "hp": 20.0,
         "ppos": [508.5, 4.0, -0.5, 0.0, 0.0, 0.0, 0.0, 0.0]},
        {"t": 116, "hp": 20.0},
        {"t": 117, "hp": 20.0},
        {"t": 118, "hp": 20.0},
        {"t": 119, "hp": 20.0},
    ]
    assert replay_tape.tape_loading_ticks(header, ticks) == {115, 116, 117, 118}


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
    chest = [0] * 63
    chest[0] = [264, 0, 2]   # diamond in first chest slot
    chest[27] = [4, 0, 16]   # cobble in first main inv slot (vanilla idx 27)
    ticks = [
        {"t": 0, "in": empty_input, "x": 0.5, "y": 70.0, "z": 0.5,
         "yaw": 0.0, "pitch": 0.0, "ents": [], "gui": "GuiCrafting",
         "gslots": crafting, "gcur": [17, 1, 2]},
        {"t": 1, "in": empty_input, "x": 0.5, "y": 70.0, "z": 0.5,
         "yaw": 0.0, "pitch": 0.0, "ents": [], "gui": "GuiFurnace",
         "gslots": furnace, "gcur": 0, "gprop": [80, 1600, 100, 200]},
        {"t": 2, "in": empty_input, "x": 0.5, "y": 70.0, "z": 0.5,
         "yaw": 0.0, "pitch": 0.0, "ents": [], "gui": "GuiChest",
         "gslots": chest, "gcur": [297, 0, 3]},
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

    tick2 = [event for event in events if event["tick"] == 2]
    slots2 = {event["slot"]: event for event in tick2
              if event["type"] == "gui_slot_view"}
    assert slots2[53]["item"] == 264 and slots2[53]["count"] == 2
    assert slots2[9]["item"] == 4 and slots2[9]["count"] == 16
    assert next(event for event in tick2
                if event["type"] == "gui_cursor_view")["item"] == 297


def test_gui_chest_slot_id_mapping():
    assert replay_tape.gui_slot_id("GuiChest", 0) == 53
    assert replay_tape.gui_slot_id("GuiChest", 26) == 79
    assert replay_tape.gui_slot_id("GuiChest", 27) == 9
    assert replay_tape.gui_slot_id("GuiChest", 53) == 35
    assert replay_tape.gui_slot_id("GuiChest", 54) == 0
    assert replay_tape.gui_slot_id("GuiChest", 62) == 8
    assert replay_tape.gui_slot_id("GuiChest", 63) is None


def _hdr():
    return {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20,
    }


def _tick(**extra):
    base = {
        "t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5,
        "yaw": 0.0, "pitch": 0.0, "ents": [],
    }
    base.update(extra)
    return base


def test_gclk_emits_ordered_container_clicks_before_action(tmp_path: Path):
    """Captured inventory craft click sequence -> container_click events.

    GuiInventory slot list: 0=result, 1..4=2x2 grid, 9..35=main, 36..44=hotbar.
    Sequence (one log in hotbar 0 / vanilla idx 36): pick hotbar, place one in
    grid cell 0 (idx 1 -> GMC 36), take result (idx 0 -> GMC 45).
    """
    inv = [0] * 41
    inv[0] = [17, 0, 2]
    ticks = [
        _tick(gui="GuiInventory",
              gclk=[
                  ["GuiInventory", 36, 0, 0, "PICKUP"],
                  ["GuiInventory", 1, 1, 0, "PICKUP"],
                  ["GuiInventory", 0, 0, 0, "PICKUP"],
              ]),
        _tick(t=1, inv=inv),
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(_hdr(), ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    tick0 = [e for e in events if e["tick"] == 0]
    clicks = [e for e in tick0 if e["type"] == "container_click"]
    assert clicks == [
        {"tick": 0, "type": "container_click", "slot": 0, "button": 0,
         "click_type": 0, "window_id": 0},
        {"tick": 0, "type": "container_click", "slot": 36, "button": 1,
         "click_type": 0, "window_id": 0},
        {"tick": 0, "type": "container_click", "slot": 45, "button": 0,
         "click_type": 0, "window_id": 0},
    ]
    types0 = [e["type"] for e in tick0]
    if "action" in types0:
        assert types0.index("container_click") < types0.index("action")
    assert not any(e["type"] == "container_click" for e in events
                   if e["tick"] == 1)
    assert not any(e.get("type") == "set_inventory" and e.get("item") == 5
                   for e in events)


def test_gclk_inventory_outside_slot_and_quick_move(tmp_path: Path):
    ticks = [
        _tick(gui="GuiInventory",
              gclk=[
                  ["GuiInventory", 9, 0, 1, "QUICK_MOVE"],
                  ["GuiInventory", -999, 0, 0, "PICKUP"],
              ]),
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(_hdr(), ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    clicks = [e for e in events if e["type"] == "container_click"]
    assert clicks == [
        {"tick": 0, "type": "container_click", "slot": 9, "button": 0,
         "click_type": 1, "window_id": 0},
        {"tick": 0, "type": "container_click", "slot": -999, "button": 0,
         "click_type": 0, "window_id": 0},
    ]


def test_gopen_inventory_close_reopen(tmp_path: Path):
    """Player inventory open/click/close/reopen with window identity."""
    ticks = [
        _tick(gopen=[["GuiInventory", 0, "player"]]),
        _tick(t=1,
              gclk=[["GuiInventory", 36, 0, 0, "PICKUP", 0]],
              gclose=[[0, "GuiInventory"]]),
        _tick(t=2, gopen=[["GuiInventory", 0, "player"]]),
        _tick(t=3,
              gclk=[["GuiInventory", 9, 0, 1, "QUICK_MOVE", 0]],
              gclose=[[0, "GuiInventory"]]),
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(_hdr(), ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert [e for e in events if e["type"].startswith("container_")] == [
        {"tick": 0, "type": "container_open", "window_id": 0,
         "ctype": "player", "x": 0, "y": 0, "z": 0},
        {"tick": 0, "type": "container_cursor", "item": 0, "count": 0, "meta": 0},
        {"tick": 1, "type": "container_click", "slot": 0, "button": 0,
         "click_type": 0, "window_id": 0},
        {"tick": 1, "type": "container_close", "window_id": 0},
        {"tick": 2, "type": "container_open", "window_id": 0,
         "ctype": "player", "x": 0, "y": 0, "z": 0},
        {"tick": 2, "type": "container_cursor", "item": 0, "count": 0, "meta": 0},
        {"tick": 3, "type": "container_click", "slot": 9, "button": 0,
         "click_type": 1, "window_id": 0},
        {"tick": 3, "type": "container_close", "window_id": 0},
    ]


def test_gopen_workbench_emits_open_seed_click(tmp_path: Path):
    slots = [0] * 10
    ticks = [
        _tick(gopen=[["GuiCrafting", 1, "workbench", 3, 70, 5, slots, 0]]),
        _tick(t=1, gclk=[
            ["GuiCrafting", 37, 0, 0, "PICKUP", 1],  # hotbar0
            ["GuiCrafting", 1, 0, 0, "PICKUP", 1],  # grid0
        ]),
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(_hdr(), ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    cont = [e for e in events if e["type"].startswith("container_")]
    assert cont[0] == {
        "tick": 0, "type": "container_open", "window_id": 1,
        "ctype": "workbench", "x": 3, "y": 70, "z": 5,
    }
    seeds = [e for e in cont if e["type"] == "container_slot"]
    assert len(seeds) == 9
    assert seeds[0]["slot"] == 36
    assert any(e["type"] == "container_click" and e["slot"] == 0
               and e["window_id"] == 1 for e in cont)
    assert any(e["type"] == "container_click" and e["slot"] == 36
               and e["window_id"] == 1 for e in cont)


def test_gopen_furnace_and_chest(tmp_path: Path):
    fslots = [0, 0, 0]
    fslots[0] = [15, 0, 2]
    fslots[1] = [263, 0, 4]
    cslots = [0] * 27
    cslots[0] = [264, 0, 2]
    ticks = [
        _tick(gopen=[["GuiFurnace", 2, "furnace", 1, 64, 2, fslots, 0,
                      [80, 1600, 100, 200]]]),
        _tick(t=1, gclk=[["GuiFurnace", 2, 0, 0, "PICKUP", 2]]),
        # Switch: new gopen implies close of previous (no same-tick gclose).
        _tick(t=2,
              gopen=[["GuiChest", 3, "chest", 4, 64, 4, cslots, [297, 0, 3]]]),
        _tick(t=3, gclk=[["GuiChest", 0, 0, 0, "PICKUP", 3]]),
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(_hdr(), ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    opens = [e for e in events if e["type"] == "container_open"]
    assert opens[0]["ctype"] == "furnace" and opens[0]["window_id"] == 2
    assert opens[1]["ctype"] == "chest" and opens[1]["x"] == 4
    props = [e for e in events if e["type"] == "container_furnace_prop"]
    assert props == [{
        "tick": 0, "type": "container_furnace_prop",
        "burn": 80, "current_burn": 1600, "cook": 100, "total_cook": 200,
    }]
    fseeds = [e for e in events if e["type"] == "container_slot"
              and e["tick"] == 0]
    assert any(e["slot"] == 46 and e["item"] == 15 for e in fseeds)
    assert any(e["slot"] == 47 and e["item"] == 263 for e in fseeds)
    cseeds = [e for e in events if e["type"] == "container_slot"
              and e["tick"] == 2]
    assert any(e["slot"] == 53 and e["item"] == 264 and e["count"] == 2
               for e in cseeds)
    curs = [e for e in events if e["type"] == "container_cursor" and e["tick"] == 2]
    assert curs[0]["item"] == 297 and curs[0]["count"] == 3


def test_gclk_fails_closed_on_wrong_window_id(tmp_path: Path):
    ticks = [
        _tick(gopen=[["GuiInventory", 0, "player"]]),
        _tick(t=1, gclk=[["GuiInventory", 36, 0, 0, "PICKUP", 7]]),
    ]
    with pytest.raises(SystemExit, match="window_id 7 != open 0"):
        replay_tape.tape_to_script(
            _hdr(), ticks, str(tmp_path / "events.jsonl"))


def test_gopen_and_click_same_tick_fails_closed(tmp_path: Path):
    ticks = [
        _tick(gopen=[["GuiInventory", 0, "player"]],
              gclk=[["GuiInventory", 36, 0, 0, "PICKUP", 0]]),
    ]
    with pytest.raises(SystemExit, match="gopen and gclk share a tick"):
        replay_tape.tape_to_script(
            _hdr(), ticks, str(tmp_path / "events.jsonl"))


def test_gclk_fails_closed_on_unsupported_click_type(tmp_path: Path):
    ticks = [
        _tick(gclk=[["GuiInventory", 36, 0, 6, "PICKUP_ALL"]]),
    ]
    with pytest.raises(SystemExit, match="unsupported ClickType PICKUP_ALL"):
        replay_tape.tape_to_script(
            _hdr(), ticks, str(tmp_path / "events.jsonl"))


def test_gclk_fails_closed_on_unknown_gui_slot(tmp_path: Path):
    ticks = [
        _tick(gclk=[["GuiEnchantment", 0, 0, 0, "PICKUP"]]),
    ]
    with pytest.raises(SystemExit, match="legacy 5-field gclk only valid"):
        replay_tape.tape_to_script(
            _hdr(), ticks, str(tmp_path / "events.jsonl"))


def test_gopen_fails_closed_on_double_chest(tmp_path: Path):
    slots = [0] * 54
    ticks = [
        _tick(gopen=[["GuiChest", 1, "double_chest", 0, 64, 0, slots, 0]]),
    ]
    with pytest.raises(SystemExit, match="unsupported container ctype"):
        replay_tape.tape_to_script(
            _hdr(), ticks, str(tmp_path / "events.jsonl"))


def test_gopen_fails_closed_on_missing_position(tmp_path: Path):
    ticks = [
        _tick(gopen=[["GuiCrafting", 1, "workbench",
                      -2147483648, -2147483648, -2147483648,
                      [0] * 10, 0]]),
    ]
    with pytest.raises(SystemExit, match="missing world position"):
        replay_tape.tape_to_script(
            _hdr(), ticks, str(tmp_path / "events.jsonl"))


def test_gclk_fails_closed_on_type_name_mismatch(tmp_path: Path):
    ticks = [
        _tick(gclk=[["GuiInventory", 36, 0, 0, "THROW"]]),
    ]
    with pytest.raises(SystemExit, match="ordinal 0 is PICKUP"):
        replay_tape.tape_to_script(
            _hdr(), ticks, str(tmp_path / "events.jsonl"))


def test_gclk_schema_fails_closed(tmp_path: Path):
    bad_payloads = [
        None,
        [["GuiInventory", 36, 0, 0]],
        [["GuiInventory", 36, 0, 0, "PICKUP", "extra"]],
        [["GuiInventory", "36", 0, 0, "PICKUP"]],
        [["GuiInventory", 36, 0.0, 0, "PICKUP"]],
        [["GuiInventory", 36, 0, False, "PICKUP"]],
    ]
    for payload in bad_payloads:
        with pytest.raises(SystemExit):
            replay_tape.tape_to_script(
                _hdr(), [_tick(gclk=payload)],
                str(tmp_path / "events.jsonl"))


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


def test_pixel_gate_rejects_marker_box_during_known_rain_window():
    oracle, magma = _canonical_frame_pair(1880)
    tape = TRACE_DIR.parent / "tapes" / f"{CANONICAL_NAME}.jsonl"
    known = pixel_gate.load_known_divergences(tape)

    baseline = pixel_gate.gate_frame(oracle, magma, 854, 480, tick=1880,
                                     known=known)
    assert pixel_gate.frame_verdict(baseline)[0] is False

    clusters = pixel_gate.gate_frame(
        oracle, _add_midframe_marker(magma), 854, 480, tick=1880, known=known)
    assert pixel_gate.frame_verdict(clusters)[0] is True
    assert any(cluster["cls"] == "UNEXPLAINED" and cluster["px"] >= 10_000
               for cluster in clusters)


def test_pixel_gate_rejects_mild_global_wash_negative_control():
    """Uniform sub-threshold wash must fail even when no egregious cluster forms.

    Old cluster-only behavior: max-channel delta of 5 < DIFF_THRESH=25, so the
    mask is empty and the frame passes. New mild-shift gate fails on residual
    mean/coverage pinned from B=1.66/ch standing pin (FAIL_MEAN_ABS=3.32).
    """
    oracle = np.full((480, 854, 3), 120, dtype=np.int16)
    magma = np.clip(oracle - 5, 0, 255).astype(np.int16)
    clusters, mild = pixel_gate.gate_frame_ex(oracle, magma, 854, 480, tick=0)
    # No egregious clusters: every pixel is under DIFF_THRESH.
    assert all(cl["cls"] != "UNEXPLAINED" or cl["px"] < pixel_gate.FAIL_CLUSTER
               for cl in clusters) or not clusters
    assert pixel_gate.frame_verdict(clusters)[0] is False  # cluster-only pass
    assert mild["mean_abs"] > pixel_gate.FAIL_MEAN_ABS
    assert mild["pct_differing"] > pixel_gate.FAIL_LOW_PCT_DIFFERING
    assert pixel_gate.mild_shift_fails(mild)
    assert pixel_gate.frame_verdict(clusters, mild=mild)[0] is True
    gate = pixel_gate.summarize({0: clusters}, mild_per_tick={0: mild})
    assert gate["pass"] is False
    assert gate["mild_shift_failures"] >= 1


def test_pixel_gate_mild_thresholds_pinned_from_baseline_pins():
    """Defaults are 2× the VERIFY.md / GATES.md B standing pin (1.66/ch)."""
    assert pixel_gate.FAIL_MEAN_ABS == pytest.approx(1.66 * 2, rel=0, abs=1e-9)
    assert pixel_gate.LOW_DIFF_THRESH == 1
    assert pixel_gate.FAIL_LOW_PCT_DIFFERING == 40.0
    # Identical frames stay green under both gates.
    frame = np.full((480, 854, 3), 40, dtype=np.int16)
    clusters, mild = pixel_gate.gate_frame_ex(frame, frame, 854, 480)
    assert clusters == []
    assert mild["mean_abs"] == 0.0
    assert pixel_gate.frame_verdict(clusters, mild=mild)[0] is False


def test_pixel_gate_accepted_class_budget_still_blocks_missing_model_soak():
    """HUD/viewmodel soak cannot hide a screen-sized required-model miss."""
    oracle = np.full((480, 854, 3), 18, dtype=np.int16)
    magma = oracle.copy()
    # Fill the whole lower half (HUD+viewmodel band) with a solid miss.
    oracle[200:, :, :] = np.array([200, 40, 200], dtype=np.int16)
    clusters = pixel_gate.gate_frame(oracle, magma, 854, 480, tick=50)
    gate = pixel_gate.summarize({50: clusters})
    assert gate["pass"] is False
    assert any(cl.get("soak_from") in {"hud", "viewmodel", "particles"}
               or cl["cls"] == "UNEXPLAINED"
               for cl in gate["failed_frames"][0]["clusters"])


def test_pixel_gate_rejects_screen_sized_overlay_class_soak():
    """A missing orange fire wash cannot hide in HUD/viewmodel classes."""
    oracle = np.full((480, 854, 3), 18, dtype=np.int16)
    magma = oracle.copy()
    oracle[235:, :, :] = np.array([235, 92, 12], dtype=np.int16)
    clusters = pixel_gate.gate_frame(oracle, magma, 854, 480, tick=120)
    gate = pixel_gate.summarize({120: clusters})
    assert gate["pass"] is False
    assert any(cl.get("soak_from") in {"hud", "viewmodel", "particles"}
               for cl in gate["failed_frames"][0]["clusters"])


def test_pixel_gate_rejects_screen_sized_transit_soak():
    oracle = np.full((480, 854, 3), 18, dtype=np.int16)
    magma = oracle.copy()
    oracle[100:380, 120:734, :] = 235
    clusters = pixel_gate.gate_frame(oracle, magma, 854, 480, tick=1000)
    gate = pixel_gate.summarize({1000: clusters}, transit={1000})
    assert gate["pass"] is False
    assert any(cl.get("soak_from") == "transit"
               for cl in gate["failed_frames"][0]["clusters"])


def test_wrong_window_sidecar_does_not_suppress_marker_box(tmp_path: Path):
    oracle, magma = _canonical_frame_pair(600)
    tape = tmp_path / "clean.jsonl"
    sidecar = tmp_path / "clean.known_divergences.json"
    sidecar.write_text(json.dumps({
        "version": 1,
        "divergences": [{
            "ticks": [600, 600],
            "open_divergence": 54,
            "reason": "intentionally wrong window for sensitivity proof",
            "regions": [[45, 0, 383, 853]],
            "predicate": {"type": "non_solid_scene"},
        }],
    }))
    known = pixel_gate.load_known_divergences(tape)

    baseline = pixel_gate.gate_frame(oracle, magma, 854, 480, tick=600,
                                     known=known)
    assert pixel_gate.frame_verdict(baseline)[0] is False

    clusters = pixel_gate.gate_frame(
        oracle, _add_midframe_marker(magma), 854, 480, tick=600, known=known)
    assert pixel_gate.frame_verdict(clusters)[0] is True
    assert any(cluster["cls"] == "UNEXPLAINED" and cluster["px"] >= 10_000
               for cluster in clusters)


def test_missing_known_divergence_sidecar_defaults_to_empty(tmp_path: Path):
    assert pixel_gate.load_known_divergences(tmp_path / "plain.jsonl") == []


def test_missing_model_gate_rejects_fake_tape_and_allows_sidecar_entity():
    unknown = [7, "EntityNoSuchProjectile", 1.0, 64.0, 2.0, 0.0, -1.0]
    ticks = [{"t": tick, "ents": [unknown]} for tick in range(5)]
    counts = replay_tape.skipped_renderable_counts(ticks)
    assert counts == {"EntityNoSuchProjectile": 5}
    gate = replay_tape.apply_missing_model_gate(None, counts)
    assert gate["pass"] is False
    assert gate["missing_model_failures"] == {"EntityNoSuchProjectile": 5}

    cloud = [8, "EntityAreaEffectCloud", 1.0, 64.0, 2.0, 0.0, -1.0]
    allowlisted = [{"t": tick, "ents": [cloud]} for tick in range(20)]
    assert replay_tape.skipped_renderable_counts(allowlisted) == {}
    quiet_gate = replay_tape.apply_missing_model_gate(None, {})
    assert quiet_gate["pass"] is True

    # EntityXPOrb is modeled (RenderXPOrb billboard); must not trip missing_model.
    orb = [9, "EntityXPOrb", 1.5, 64.0, 2.5, 0.0, -1.0, 7, 40, 12]
    orb_ticks = [{"t": tick, "ents": [orb]} for tick in range(20)]
    assert "EntityXPOrb" not in replay_tape.skipped_renderable_counts(orb_ticks)
    assert "EntityXPOrb" in replay_tape.MODELED_ENTITY_TYPES

    # EntityFallingBlock is modeled (RenderFallingBlock full cube).
    fall = [10, "EntityFallingBlock", 3.5, 64.0, 132.5, 0.0, -1.0, 13, 0]
    fall_ticks = [{"t": tick, "ents": [fall]} for tick in range(7)]
    assert "EntityFallingBlock" not in replay_tape.skipped_renderable_counts(
        fall_ticks)
    assert "EntityFallingBlock" in replay_tape.MODELED_ENTITY_TYPES


def test_entity_xp_orb_tape_maps_value_color_age(tmp_path: Path):
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20, "dim": 0,
    }
    orb = [11, "EntityXPOrb", 3.0, 65.0, 4.0, 12.0, -1.0, 17, 55, 8]
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5,
              "yaw": 0.0, "pitch": 0.0, "ents": [orb]}]
    script = tmp_path / "orb_events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    view = next(e for e in events if e.get("type") == "ent_view"
                and e.get("ent") == "EntityXPOrb")
    assert view["item"] == 17
    assert view["item_meta"] == 55
    assert view["age"] == 8
    assert view["x"] == 3.0 and view["y"] == 65.0


def test_entity_falling_block_tape_maps_block_state(tmp_path: Path):
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20, "dim": 0,
    }
    fall = [12, "EntityFallingBlock", 3.5, 63.98, 132.5, 0.0, -1.0, 13, 0]
    ticks = [{"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5,
              "yaw": 0.0, "pitch": 0.0, "ents": [fall]}]
    script = tmp_path / "fall_events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    view = next(e for e in events if e.get("type") == "ent_view"
                and e.get("ent") == "EntityFallingBlock")
    assert view["item"] == 13
    assert view["item_meta"] == 0
    assert view["x"] == 3.5 and view["y"] == 63.98


def test_texture_luminance_sidecar_does_not_suppress_marker_box():
    oracle, magma = _canonical_frame_pair(600)
    known = [{
        "ticks": [600, 600],
        "open_divergence": 4,
        "regions": [[45, 0, 383, 853]],
        "predicate": {"type": "texture_luminance_modulation"},
    }]
    clusters = pixel_gate.gate_frame(
        oracle, _add_midframe_marker(magma), 854, 480, tick=600, known=known)
    assert pixel_gate.frame_verdict(clusters)[0] is True
    assert any(cluster["cls"] == "UNEXPLAINED" and cluster["px"] >= 10_000
               for cluster in clusters)


def test_recorded_flat_world_selects_superflat_generator():
    assert replay_tape.magma_world({"world": "qrl_0_flat"}) == "superflat"
    assert replay_tape.magma_world({"world": "qrl_0"}) == "default"
    assert replay_tape.magma_world({}) == "default"


def _elytra_inv(chest=None):
    """41-slot inv row; chest is EntityEquipmentSlot.CHEST (tape index 38)."""
    inv = [0] * 41
    if chest is not None:
        inv[38] = chest
    return inv


def test_starting_inventory_seeds_set_inventory_before_tick_zero(tmp_path: Path):
    """Recstart gear must land in live player.inv before tick 0 (not only inv_view)."""
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20,
    }
    inv = [0] * 41
    inv[0] = [261, 0, 1]   # bow
    inv[8] = [262, 0, 64]  # arrows
    inv[38] = [443, 0, 1]  # elytra chest
    ticks = [{
        "t": 0, "in": {"f": 0, "s": 0},
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "inv": inv, "ents": [],
    }, {
        "t": 1, "in": {"f": 0, "s": 0},
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "ents": [],
    }]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    seed0 = {e["slot"]: e for e in events
             if e["tick"] == 0 and e["type"] == "set_inventory"}
    assert seed0[0] == {"tick": 0, "type": "set_inventory", "slot": 0,
                        "item": 261, "count": 1, "meta": 0}
    assert seed0[8]["item"] == 262 and seed0[8]["count"] == 64
    assert seed0[38]["item"] == 443
    # Empty slots are cleared too so leftover state cannot leak in.
    assert seed0[1]["item"] == 0 and seed0[1]["count"] == 0
    types0 = [e["type"] for e in events if e["tick"] == 0]
    assert types0.index("set_inventory") < types0.index("set_look")
    # Deferred re-anchor of post-tick inv still fires on the next tick.
    assert any(e["tick"] == 1 and e["type"] == "set_inventory" and e["slot"] == 0
               for e in events)


def test_elytra_chest_seeds_set_elytra_before_tick_zero(tmp_path: Path):
    """Recstart with Items.ELYTRA (443) must arm elytra_equipped for tick-0 travel."""
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 24.0, "z": 0.5, "yaw": -90.0, "pitch": 8.0,
        "vx": 0.0, "vy": -0.0784000015258789, "vz": 0.0, "og": 1,
        "hp": 20.0, "food": 20,
    }
    ticks = [{
        "t": 0, "in": {"f": 0, "s": 0, "jump": 0},
        "x": 0.5, "y": 24.0, "z": 0.5, "yaw": -90.0, "pitch": 8.0,
        "vx": 0.0, "vy": -0.0784000015258789, "vz": 0.0, "og": 1,
        "hp": 20.0, "food": 20, "inv": _elytra_inv([443, 0, 1]), "ents": [],
    }]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    seed = next(e for e in events if e["type"] == "set_elytra")
    assert seed == {"tick": 0, "type": "set_elytra", "equipped": 1}
    # Seed must land before look/action on tick 0 so travel sees equipped=1.
    types0 = [e["type"] for e in events if e["tick"] == 0]
    assert "set_elytra" in types0 and "set_look" in types0
    assert types0.index("set_elytra") < types0.index("set_look")


def test_non_elytra_chest_does_not_seed_set_elytra(tmp_path: Path):
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20,
    }
    ticks = [{
        "t": 0, "in": {"f": 0, "s": 0},
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "inv": _elytra_inv([311, 0, 1]), "ents": [],  # diamond chestplate
    }]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert {"tick": 0, "type": "set_elytra", "equipped": 0} in events
    assert not any(e["type"] == "set_elytra" and e["equipped"] == 1
                   for e in events)


def test_elytra_inventory_change_applies_on_next_tick(tmp_path: Path):
    """Post-tick inv truth re-anchors elytra_equipped before the following travel."""
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 24.0, "z": 0.5, "yaw": -90.0, "pitch": 8.0,
        "hp": 20.0, "food": 20,
    }
    base = {"in": {"f": 0, "s": 0}, "x": 0.5, "y": 24.0, "z": 0.5,
            "yaw": -90.0, "pitch": 8.0, "hp": 20.0, "food": 20, "ents": []}
    ticks = [
        {**base, "t": 0, "inv": _elytra_inv([443, 0, 1])},
        {**base, "t": 1, "inv": _elytra_inv(0)},  # unequip after tick 0
        {**base, "t": 2},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    # Tick-0 inv (equipped) re-anchors on tick 1; tick-1 inv (empty) on tick 2.
    assert {"tick": 1, "type": "set_elytra", "equipped": 1} in events
    assert {"tick": 2, "type": "set_elytra", "equipped": 0} in events


def test_recorded_flag7_metadata_events_are_forwarded_at_observed_ticks(
        tmp_path: Path):
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 24.0, "z": 0.5, "yaw": -90.0, "pitch": 8.0,
        "hp": 20.0, "food": 20,
        "flag7_metadata": 1, "flag7_initial": 0,
    }
    base = {"in": {"f": 0, "s": 0}, "x": 0.5, "y": 24.0, "z": 0.5,
            "yaw": -90.0, "pitch": 8.0, "hp": 20.0, "food": 20,
            "ents": []}
    ticks = [
        {**base, "t": 0},
        {**base, "t": 1, "f7": [1]},
        {**base, "t": 2},
        {**base, "t": 3, "f7": [0, 1]},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    flag7 = [json.loads(line) for line in script.read_text().splitlines()
             if json.loads(line)["type"] == "set_elytra_flag7"]
    assert flag7 == [
        {"tick": 0, "type": "set_elytra_flag7", "flying": 0},
        {"tick": 1, "type": "set_elytra_flag7", "flying": 1},
        {"tick": 3, "type": "set_elytra_flag7", "flying": 0},
        {"tick": 3, "type": "set_elytra_flag7", "flying": 1},
    ]


def test_legacy_tape_does_not_enable_recorded_flag7_mode(tmp_path: Path):
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 24.0, "z": 0.5, "yaw": -90.0, "pitch": 8.0,
        "hp": 20.0, "food": 20,
    }
    ticks = [{
        "t": 0, "in": {"f": 0, "s": 0, "jump": 1},
        "x": 0.5, "y": 24.0, "z": 0.5, "yaw": -90.0, "pitch": 8.0,
        "hp": 20.0, "food": 20, "ents": [],
    }]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    assert not any(json.loads(line)["type"] == "set_elytra_flag7"
                   for line in script.read_text().splitlines())


def test_look_phase_tape_emits_recorded_pre_look(tmp_path: Path):
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 24.0, "z": 0.5, "yaw": -90.0, "pitch": 8.0,
        "hp": 20.0, "food": 20,
        "look_phase": 1,
    }
    base = {"in": {"f": 1.0, "s": 0}, "x": 0.5, "y": 24.0, "z": 0.5,
            "hp": 20.0, "food": 20, "ents": []}
    ticks = [
        # turn landed between ticks: physics of t=1 uses the NEW pitch
        {**base, "t": 0, "yaw": -90.0, "pitch": 8.0, "ry": -90.0, "rp": 8.0},
        {**base, "t": 1, "yaw": -90.0, "pitch": 6.8, "ry": -90.0, "rp": 6.8},
        # turn landed post-travel: physics of t=2 still used the OLD pitch
        {**base, "t": 2, "yaw": -90.0, "pitch": 5.0, "ry": -90.0, "rp": 6.8},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    looks = [e for e in events if e["type"] in ("set_look", "set_look_pre")]
    assert looks == [
        {"tick": 0, "type": "set_look_pre", "yaw": -90.0, "pitch": 8.0},
        {"tick": 0, "type": "set_look", "yaw": -90.0, "pitch": 8.0},
        {"tick": 1, "type": "set_look_pre", "yaw": -90.0, "pitch": 6.8},
        {"tick": 1, "type": "set_look", "yaw": -90.0, "pitch": 6.8},
        {"tick": 2, "type": "set_look_pre", "yaw": -90.0, "pitch": 6.8},
        {"tick": 2, "type": "set_look", "yaw": -90.0, "pitch": 5.0},
    ]


def test_legacy_tape_keeps_heuristic_look_emission(tmp_path: Path):
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 24.0, "z": 0.5, "yaw": -90.0, "pitch": 8.0,
        "hp": 20.0, "food": 20,
    }
    ticks = [{
        "t": 0, "in": {"f": 0, "s": 0},
        "x": 0.5, "y": 24.0, "z": 0.5, "yaw": -90.0, "pitch": 8.0,
        "hp": 20.0, "food": 20, "ents": [],
    }]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    looks = [e for e in events if e["type"] in ("set_look", "set_look_pre")]
    assert looks == [
        {"tick": 0, "type": "set_look", "yaw": -90.0, "pitch": 8.0},
    ]


def test_state_assertions_report_inventory_entities_and_world_hash():
    """Non-player state gate is separate from physics and is not silent."""
    inv = [[17, 0, 3]] + [0] * 40
    inv[38] = [443, 0, 1]
    inv[40] = [442, 0, 1]
    ticks = [{
        "t": 0, "x": 0.5, "y": 70.0, "z": 0.5, "vx": 0.0, "vy": 0.0, "vz": 0.0,
        "og": 1, "hp": 20.0, "food": 20,
        "inv": inv,
        "ents": [[7, "EntitySheep", 1.0, 70.0, 2.0, 0.0, 0.0]],
    }]
    c_rows = [{
        "tick": 0, "x": 0.5, "y": 70.0, "z": 0.5, "vx": 0.0, "vy": 0.0, "vz": 0.0,
        "on_ground": 1, "health": 20.0, "food": 20.0,
        "inventory": [
            {"slot": 0, "item": 17, "count": 3, "meta": 0},
            {"slot": 38, "item": 443, "count": 1, "meta": 0},
            {"slot": 40, "item": 442, "count": 1, "meta": 0},
        ],
        "entities": [{"type": 10, "x": 1.0, "y": 70.0, "z": 2.0}],
        "nearby_hash": "aabb001122334455",
    }]
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    assert state["kind"] == "state"
    assert state["inventory"]["available"] and state["inventory"]["pass"]
    # Tick-0 alone is seed verification, not independent inventory evolution.
    assert state["inventory"]["seeded_only"] is True
    assert state["inventory"]["ticks_independent"] == 0
    assert state["inventory"]["ticks_checked"] == 1
    assert state["entities"]["available"]
    assert state["entities"]["samples"][0]["tape_types"] == ["EntitySheep"]
    assert state["world"]["available"]
    assert state["world"]["samples"][0]["nearby_hash"] == "aabb001122334455"


def test_state_assertions_fail_inventory_mismatch_negative_control():
    """Old physics-only compare ignored inv; state gate must surface it."""
    ticks = [{
        "t": 0, "x": 0.5, "y": 70.0, "z": 0.5, "vx": 0.0, "vy": 0.0, "vz": 0.0,
        "og": 1, "hp": 20.0, "food": 20,
        "inv": [[355, 0, 1]] + [0] * 40,  # bed present on tape
        "ents": [],
    }]
    c_rows = [{
        "tick": 0, "x": 0.5, "y": 70.0, "z": 0.5, "vx": 0.0, "vy": 0.0, "vz": 0.0,
        "on_ground": 1, "health": 20.0, "food": 20.0,
        "inventory": [],  # magma missing the bed
        "entities": [],
        "nearby_hash": "00",
    }]
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    assert state["inventory"]["available"]
    assert state["inventory"]["pass"] is False
    assert state["inventory"]["mismatches"][0]["tape_item"] == 355
    # Physics still clean on the same rows.
    first, _ = replay_tape.first_divergence(ticks, c_rows)
    assert first is None


def _pose_tick(t, inv=None, **extra):
    row = {
        "t": t, "x": 0.5, "y": 70.0, "z": 0.5, "vx": 0.0, "vy": 0.0, "vz": 0.0,
        "og": 1, "hp": 20.0, "food": 20, "ents": [],
    }
    if inv is not None:
        row["inv"] = inv
    row.update(extra)
    return row


def _magma_row(t, inventory, nearby_hash="00"):
    return {
        "tick": t, "x": 0.5, "y": 70.0, "z": 0.5, "vx": 0.0, "vy": 0.0, "vz": 0.0,
        "on_ground": 1, "health": 20.0, "food": 20.0,
        "inventory": inventory,
        "entities": [],
        "nearby_hash": nearby_hash,
    }


def test_inventory_gate_seeded_only_when_only_tick_zero_has_inv():
    """A tape with only ticks[0]['inv'] must not claim independent verification."""
    inv = [[261, 0, 1], 0, 0, 0, 0, 0, 0, 0, [262, 0, 64]] + [0] * 32
    ticks = [_pose_tick(0, inv=inv), _pose_tick(1), _pose_tick(2)]
    c_rows = [
        _magma_row(0, [{"slot": 0, "item": 261, "count": 1, "meta": 0},
                       {"slot": 8, "item": 262, "count": 64, "meta": 0}]),
        _magma_row(1, [{"slot": 0, "item": 261, "count": 1, "meta": 0},
                       {"slot": 8, "item": 262, "count": 64, "meta": 0}]),
        _magma_row(2, [{"slot": 0, "item": 261, "count": 1, "meta": 0},
                       {"slot": 8, "item": 262, "count": 64, "meta": 0}]),
    ]
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=20)
    inv_s = state["inventory"]
    assert inv_s["pass"] is True
    assert inv_s["seeded_only"] is True
    assert inv_s["ticks_checked"] == 1
    assert inv_s["ticks_independent"] == 0


def test_inventory_gate_independent_pass_and_mutation_fail():
    """Prove the gate catches post-seed inventory divergence.

    Synthetic tape: inv at t=0 (seed) and t=20 (keyframe). Replay re-anchors
    post-tick inv on script tick t+1, so the gate reads magma at index t+1.
    Matching magma at t=21 PASSes; mutating that row FAILs. Tick 0 alone is
    not enough.
    """
    inv0 = [[261, 0, 1]] + [0] * 7 + [[262, 0, 64]] + [0] * 32
    inv20 = [[261, 0, 1]] + [0] * 7 + [[262, 0, 63]] + [0] * 32  # one arrow used
    ticks = [_pose_tick(t, inv=(inv0 if t == 0 else inv20 if t == 20 else None))
             for t in range(21)]
    # Index t holds pre-reanchor live inv; index t+1 holds tape inv[t].
    magma_ok = [
        _magma_row(t, (
            [{"slot": 0, "item": 261, "count": 1, "meta": 0},
             {"slot": 8, "item": 262, "count": 64 if t < 21 else 63, "meta": 0}]
        ))
        for t in range(22)
    ]
    magma_ok[21] = _magma_row(21, [
        {"slot": 0, "item": 261, "count": 1, "meta": 0},
        {"slot": 8, "item": 262, "count": 63, "meta": 0},
    ])
    ok = replay_tape.collect_state_assertions(ticks, magma_ok, sample_every=20)
    assert ok["inventory"]["pass"] is True
    assert ok["inventory"]["seeded_only"] is False
    assert ok["inventory"]["ticks_independent"] == 1
    assert ok["inventory"]["ticks_checked"] == 2

    # Mutation on the post-reanchor dump: arrows gone while tape still has 63.
    magma_bad = [dict(r) for r in magma_ok]
    magma_bad[21] = _magma_row(21, [
        {"slot": 0, "item": 261, "count": 1, "meta": 0},
    ])
    bad = replay_tape.collect_state_assertions(ticks, magma_bad, sample_every=20)
    assert bad["inventory"]["pass"] is False
    assert bad["inventory"]["seeded_only"] is False
    assert bad["inventory"]["ticks_independent"] == 1
    assert any(m["tick"] == 20 and m["slot"] == 8 and m["tape_item"] == 262
               for m in bad["inventory"]["mismatches"])

    # Alternate mutation: wrong item id in a non-empty slot (picked up dirt
    # instead of keeping arrows).
    magma_wrong_item = [dict(r) for r in magma_ok]
    magma_wrong_item[21] = _magma_row(21, [
        {"slot": 0, "item": 261, "count": 1, "meta": 0},
        {"slot": 8, "item": 3, "count": 63, "meta": 0},  # dirt, not arrow
    ])
    wrong = replay_tape.collect_state_assertions(
        ticks, magma_wrong_item, sample_every=20)
    assert wrong["inventory"]["pass"] is False
    assert wrong["inventory"]["mismatches"][0]["tape_item"] == 262
    assert wrong["inventory"]["mismatches"][0]["magma_item"] == 3


def test_inventory_gate_checks_off_grid_change_dumps():
    """Change-dump at t=77 must be checked even though sample_every=20 skips it."""
    inv0 = [[261, 0, 1]] + [0] * 40
    inv77 = [[261, 0, 1]] + [0] * 7 + [[262, 0, 1]] + [0] * 32  # picked up arrow
    ticks = [_pose_tick(t, inv=(inv0 if t == 0 else inv77 if t == 77 else None))
             for t in range(78)]
    # Magma never got the arrow on the post-reanchor dump (index 78) — FAIL.
    c_rows = [
        _magma_row(t, [{"slot": 0, "item": 261, "count": 1, "meta": 0}])
        for t in range(79)
    ]
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=20)
    assert state["inventory"]["ticks_checked"] == 2
    assert state["inventory"]["ticks_independent"] == 1
    assert state["inventory"]["pass"] is False
    assert state["inventory"]["mismatches"][0]["tick"] == 77
    assert state["inventory"]["mismatches"][0]["tape_item"] == 262


def test_inventory_gate_allows_set_inventory_reanchor_lag():
    """First-appearance inv dump must not fail when re-anchor lands at t+1.

    Mirrors survival GUI craft/pickup: tape inv at t has the new stack, magma
    live inv is still empty at dump t (set_inventory is deferred to t+1), then
    dump t+1 holds the taped item id. Same-index compare is a false fail.
    """
    inv0 = [0] * 41
    # t=5: first planks in hotbar 7 (GUI craft result), like tape t=668.
    inv5 = [0] * 7 + [[5, 0, 2]] + [0] * 33
    ticks = [_pose_tick(t, inv=(inv0 if t == 0 else inv5 if t == 5 else None))
             for t in range(6)]
    # Empty through dump 5; re-anchor visible at dump 6.
    c_rows = [_magma_row(t, []) for t in range(6)]
    c_rows.append(_magma_row(6, [
        {"slot": 7, "item": 5, "count": 2, "meta": 0},
    ]))
    ok = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=20)
    assert ok["inventory"]["pass"] is True
    assert ok["inventory"]["ticks_independent"] == 1
    assert ok["inventory"]["mismatches"] == []

    # If re-anchor never lands, still FAIL (real missing item).
    c_bad = [_magma_row(t, []) for t in range(7)]
    bad = replay_tape.collect_state_assertions(ticks, c_bad, sample_every=20)
    assert bad["inventory"]["pass"] is False
    assert bad["inventory"]["mismatches"][0]["tick"] == 5
    assert bad["inventory"]["mismatches"][0]["slot"] == 7
    assert bad["inventory"]["mismatches"][0]["tape_item"] == 5
    assert bad["inventory"]["mismatches"][0]["magma_item"] is None


def test_inventory_gate_compares_count_and_meta():
    """Full stack compare: wrong count or durability must fail, not only item id."""
    inv0 = [[276, 0, 1]] + [0] * 40  # diamond sword
    inv1 = [[276, 5, 1]] + [0] * 40  # same sword, damage 5
    ticks = [_pose_tick(0, inv=inv0), _pose_tick(1, inv=inv1)]
    # Post-reanchor of t=1 is dump index 2.
    c_rows = [
        _magma_row(0, [{"slot": 0, "item": 276, "count": 1, "meta": 0}]),
        _magma_row(1, [{"slot": 0, "item": 276, "count": 1, "meta": 0}]),
        _magma_row(2, [{"slot": 0, "item": 276, "count": 1, "meta": 0}]),
    ]
    bad_meta = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    assert bad_meta["inventory"]["pass"] is False
    assert bad_meta["inventory"]["mismatches"][0]["tape_meta"] == 5
    assert bad_meta["inventory"]["mismatches"][0]["magma_meta"] == 0

    inv_count = [[262, 0, 64]] + [0] * 40
    inv_count1 = [[262, 0, 63]] + [0] * 40
    ticks_c = [_pose_tick(0, inv=inv_count), _pose_tick(1, inv=inv_count1)]
    c_count = [
        _magma_row(0, [{"slot": 0, "item": 262, "count": 64, "meta": 0}]),
        _magma_row(1, [{"slot": 0, "item": 262, "count": 64, "meta": 0}]),
        _magma_row(2, [{"slot": 0, "item": 262, "count": 64, "meta": 0}]),
    ]
    bad_count = replay_tape.collect_state_assertions(
        ticks_c, c_count, sample_every=1)
    assert bad_count["inventory"]["pass"] is False
    assert bad_count["inventory"]["mismatches"][0]["tape_count"] == 63
    assert bad_count["inventory"]["mismatches"][0]["magma_count"] == 64


def test_gate_baseline_diff_missing_baseline_is_failure(tmp_path: Path):
    """Negative control: missing required baseline must not be silent green."""
    import subprocess
    import sys
    current = tmp_path / "current.gate.json"
    current.write_text(json.dumps({
        "classes": {"UNEXPLAINED": {"frames": 1, "px": 100, "max_cluster": 100}},
        "failed_frames": [{"tick": 0, "unexplained_px": 100, "clusters": []}],
    }))
    script = TRACE_DIR / "gate_baseline_diff.py"
    proc = subprocess.run(
        [sys.executable, str(script),
         "--baseline", str(tmp_path / "missing.gate.json"),
         "--current", str(current)],
        capture_output=True, text=True, check=False,
    )
    assert proc.returncode == 1
    assert "FAIL: no committed baseline" in proc.stdout


def test_elytra_falling_liquid_cleared_before_player_intersection(tmp_path: Path):
    """Post-capture falling-liquid cells must not backdate into travel()."""
    tape = tmp_path / "elytra.jsonl"
    (tmp_path / "elytra_world" / "region").mkdir(parents=True)
    # Source water column at x=10 (meta<8) plus downwind falling water (meta>=8).
    cache = tmp_path / "elytra.jsonl.snapshot_patch.jsonl"
    cache.write_text(
        '{"tick":0,"type":"snapshot_block","dim":0,"x":10,"y":5,"z":0,'
        '"id":9,"meta":0}\n'
        '{"tick":0,"type":"snapshot_block","dim":0,"x":10,"y":6,"z":0,'
        '"id":9,"meta":0}\n'
        '{"tick":0,"type":"snapshot_block","dim":0,"x":11,"y":5,"z":0,'
        '"id":8,"meta":8}\n'
        '{"tick":0,"type":"snapshot_block","dim":0,"x":11,"y":6,"z":0,'
        '"id":8,"meta":8}\n'
    )
    header = {
        "header": 1, "seed": 0, "world_time": 6000,
        "x": 0.5, "y": 24.0, "z": 0.5, "yaw": -90.0, "pitch": 8.0,
        "hp": 20.0, "food": 20,
    }
    base_in = {"f": 1, "s": 0, "jump": 0}
    # Approach +x; first body AABB that hits falling cell (11,5,0) around t=1.
    ticks = [
        {"t": 0, "in": base_in, "x": 0.5, "y": 5.0, "z": 0.5,
         "yaw": -90.0, "pitch": 8.0, "hp": 20.0, "food": 20,
         "inv": _elytra_inv([443, 0, 1]), "ents": []},
        {"t": 1, "in": base_in, "x": 10.9, "y": 5.0, "z": 0.5,
         "yaw": -90.0, "pitch": 8.0, "hp": 20.0, "food": 20, "ents": []},
        {"t": 2, "in": base_in, "x": 11.2, "y": 5.0, "z": 0.5,
         "yaw": -90.0, "pitch": 8.0, "hp": 20.0, "food": 20, "ents": []},
        {"t": 3, "in": base_in, "x": 12.0, "y": 5.0, "z": 0.5,
         "yaw": -90.0, "pitch": 8.0, "hp": 20.0, "food": 20, "ents": []},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script), tape_path=str(tape))
    events = [json.loads(line) for line in script.read_text().splitlines()]

    # Downwind falling water is filtered from tick-0 snapshot (post_capture_spread).
    t0_falling = [
        e for e in events
        if e["tick"] == 0 and e.get("type") == "snapshot_block"
        and e.get("x") == 11 and e.get("id") in (8, 9)
    ]
    assert t0_falling == []

    # Source column remains.
    assert any(e.get("type") == "snapshot_block" and e.get("x") == 10
               and e.get("id") == 9 and e.get("meta") == 0
               for e in events if e["tick"] == 0)

    # Falling cells are kept only until the clear tick (air overwrite).
    clears = [
        e for e in events
        if e.get("type") == "snapshot_block" and e.get("id") == 0
        and e.get("x") == 11
    ]
    assert clears, "expected falling liquid air-clears before intersection"
    assert all(e["tick"] == clears[0]["tick"] for e in clears)
    assert clears[0]["tick"] in (1, 2)  # first intersecting row after prev


def test_potion_show_particles_and_armor_rows(tmp_path):
    """Recorder >= 2026-07-29 rows: pots gain doesShowParticles, and the
    player's generic.armor total is recorded because NBT AttributeModifiers
    make it underivable from the inventory dump."""
    header = {"header": 1, "seed": 0, "world": "qrl_0", "world_time": 6000,
              "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
              "hp": 20.0, "food": 20, "dim": 1,
              "velocity_packets": 1, "position_packets": 1}
    ticks = [
        {"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5,
         "yaw": 0.0, "pitch": 0.0, "wt": 6001, "hp": 20.0, "food": 20,
         "dim": 1, "xpl": 0, "xpp": 0.0,
         "pots": [[11, 4, 32721, 0], [20, 0, 199, 1]], "armor": 0,
         "vx": 0.0, "vy": 0.0, "vz": 0.0, "og": 1, "ents": []},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    pots = [e for e in events if e["type"] == "potion_view"]
    assert [(e["id"], e["show_particles"]) for e in pots] == [(11, 0), (20, 1)]
    armor = [e for e in events if e["type"] == "armor_view"]
    assert len(armor) == 1 and armor[0]["points"] == 0


def test_recorded_explosion_particles_map_to_script_ops(tmp_path):
    header = {
        "header": 1, "seed": 0, "world": "qrl_0", "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20, "dim": 0,
    }
    base = {
        "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5,
        "yaw": 0.0, "pitch": 0.0, "hp": 20.0, "food": 20,
        "ents": [],
    }
    ticks = [
        {
            **base,
            "t": 4,
            "pcl": [
                [0, 1.25, 2.5, 3.75, -0.125, 0.25, 0.5],
                [1, 4.0, 5.0, 6.0, 0.75, 0.0, 0.0],
                [2, 7.0, 8.0, 9.0, 0.0, 0.0, 0.0],
            ],
        },
        {**base, "t": 5},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    particles = [event for event in events if event["type"] == "spawn_particle"]
    assert particles == [
        {"tick": 4, "type": "spawn_particle", "id": 0,
         "x": 1.25, "y": 2.5, "z": 3.75,
         "vx": -0.125, "vy": 0.25, "vz": 0.5},
        {"tick": 4, "type": "spawn_particle", "id": 1,
         "x": 4.0, "y": 5.0, "z": 6.0,
         "vx": 0.75, "vy": 0.0, "vz": 0.0},
        {"tick": 4, "type": "spawn_particle", "id": 2,
         "x": 7.0, "y": 8.0, "z": 9.0,
         "vx": 0.0, "vy": 0.0, "vz": 0.0},
    ]


def test_recorded_block_changes_map_to_set_block_post(tmp_path):
    """Tape row-t ``bc`` finals become post-tick set_block_post on the same
    tick so action t still simulates first, then client truth reanchors."""
    header = {
        "header": 1, "seed": 0, "world": "qrl_0", "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20, "dim": 0,
    }
    base = {
        "in": {"f": 0, "s": 0, "jump": 0, "sneak": 0, "sprint": 0,
               "atk": 1, "use": 0, "hb": 0},
        "x": 0.5, "y": 70.0, "z": 0.5,
        "yaw": 0.0, "pitch": 0.0, "hp": 20.0, "food": 20,
        "ents": [],
    }
    ticks = [
        {
            **base,
            "t": 7,
            "bc": [
                [10, 64, -3, 2, 0],
                [10, 65, -3, 0, 0],
                [10, 64, -3, 0, 0],
                [11, 64, -3, 2, 0],
            ],
        },
        {**base, "t": 8, "in": {**base["in"], "atk": 0}},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    posts = [e for e in events if e["type"] == "set_block_post"]
    assert posts == [
        {"tick": 7, "type": "set_block_post",
         "x": 10, "y": 64, "z": -3, "id": 2, "meta": 0},
        {"tick": 7, "type": "set_block_post",
         "x": 10, "y": 65, "z": -3, "id": 0, "meta": 0},
        {"tick": 7, "type": "set_block_post",
         "x": 10, "y": 64, "z": -3, "id": 0, "meta": 0},
        {"tick": 7, "type": "set_block_post",
         "x": 11, "y": 64, "z": -3, "id": 2, "meta": 0},
    ]
    # Action for the dig tick still emits independently on the same tick;
    # script.c defers set_block_post until after gm_runtime_tick regardless of
    # line order within the tick group.
    actions = [e for e in events if e["type"] == "action" and e["tick"] == 7]
    assert actions and actions[0].get("attack") == 1
    assert all(e["tick"] == 7 for e in posts)
    # Legacy rows without bc produce no set_block_post (byte-compat path).
    assert not any(e["type"] == "set_block_post" and e["tick"] == 8
                   for e in events)


def test_block_change_payload_fails_closed(tmp_path):
    header = {
        "header": 1, "seed": 0, "world": "qrl_0", "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20, "dim": 0,
    }
    base = {
        "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5,
        "yaw": 0.0, "pitch": 0.0, "hp": 20.0, "food": 20, "ents": [],
    }
    script = tmp_path / "events.jsonl"
    bad_cases = [
        [[10, 64, 0, 0]],                 # too short
        [[10, 64, 0, 0, 0, 1]],           # too long
        [[10, 64, 0, "air", 0]],          # non-int id
        [[10, 64, 0, "1", 0]],            # numeric strings are not schema ints
        [[10, 64, 0, 1.0, 0]],            # integral floats are not schema ints
        [[10, 64, 0, True, 0]],           # JSON booleans are not schema ints
        [[10, -1, 0, 0, 0]],              # y out of range
        [[10, 64, 0, 5000, 0]],           # id out of range
        [[10, 64, 0, 1, 16]],             # meta out of range
        [[2147483648, 64, 0, 1, 0]],       # x outside C int range
    ]
    for bc in bad_cases:
        ticks = [{**base, "t": 0, "bc": [bc]}]
        try:
            replay_tape.tape_to_script(header, ticks, str(script))
        except ValueError:
            continue
        raise AssertionError(f"expected ValueError for bc={bc!r}")


def test_legacy_tape_without_bc_unchanged(tmp_path):
    """Old tapes omit bc; script must not invent set_block_post events."""
    header = {
        "header": 1, "seed": 0, "world": "qrl_0", "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20, "dim": 0,
    }
    ticks = [
        {"t": 0, "in": {"f": 0, "s": 0}, "x": 0.5, "y": 70.0, "z": 0.5,
         "yaw": 0.0, "pitch": 0.0, "hp": 20.0, "food": 20, "ents": []},
    ]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    assert not any(e["type"] == "set_block_post" for e in events)
    assert not any(e["type"] == "snapshot_block" for e in events
                   if e.get("tick", 0) > 0)


def test_bulk_bc_over_clumping_threshold_maps_to_set_block_post(tmp_path):
    """Partial SPacketChunkData path can exceed Forge clumpingThreshold (64).

    Recorder emits those cell finals on the shared ``bc`` channel; replay must
    expand every entry to set_block_post in tape order with no 64-cap.
    """
    header = {
        "header": 1, "seed": 0, "world": "qrl_0", "world_time": 6000,
        "x": 0.5, "y": 70.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
        "hp": 20.0, "food": 20, "dim": 0,
    }
    base = {
        "in": {"f": 0, "s": 0, "jump": 0, "sneak": 0, "sprint": 0,
               "atk": 0, "use": 0, "hb": 0},
        "x": 0.5, "y": 70.0, "z": 0.5,
        "yaw": 0.0, "pitch": 0.0, "hp": 20.0, "food": 20,
        "ents": [],
    }
    n = 80  # > ForgeModContainer.clumpingThreshold default 64
    bulk = [[100 + i, 64, 200, 1, i & 15] for i in range(n)]
    ticks = [{**base, "t": 3, "bc": bulk}]
    script = tmp_path / "events.jsonl"
    replay_tape.tape_to_script(header, ticks, str(script))
    events = [json.loads(line) for line in script.read_text().splitlines()]
    posts = [e for e in events if e["type"] == "set_block_post"]
    assert len(posts) == n
    assert posts[0] == {
        "tick": 3, "type": "set_block_post",
        "x": 100, "y": 64, "z": 200, "id": 1, "meta": 0,
    }
    assert posts[63] == {
        "tick": 3, "type": "set_block_post",
        "x": 163, "y": 64, "z": 200, "id": 1, "meta": 15,
    }
    assert posts[79] == {
        "tick": 3, "type": "set_block_post",
        "x": 179, "y": 64, "z": 200, "id": 1, "meta": 15,
    }
    assert all(e["tick"] == 3 for e in posts)
    # Quiet subsequent row still invents nothing (legacy-compatible).
    ticks2 = [{**base, "t": 3, "bc": bulk}, {**base, "t": 4}]
    script2 = tmp_path / "events2.jsonl"
    replay_tape.tape_to_script(header, ticks2, str(script2))
    events2 = [json.loads(line) for line in script2.read_text().splitlines()]
    assert not any(e["type"] == "set_block_post" and e["tick"] == 4
                   for e in events2)


# ---- tri-state gate outcome: harness faults must never read as parity ----

def test_signal_death_is_infrastructure_not_parity():
    """rc=-11 is the SIGSEGV in AGENTS.md; diffing its partial state made it rc=4."""
    assert replay_tape.magma_run_failure_is_infrastructure(
        "magma_game failed (rc=-11)")
    assert replay_tape.magma_run_failure_is_infrastructure(
        "magma_game failed (rc=139)")
    assert replay_tape.magma_run_failure_is_infrastructure(
        "magma_game build failed")


def test_live_player_death_stays_a_parity_verdict():
    """rc=2 is a dead magma player, whose partial state IS divergence evidence."""
    assert not replay_tape.magma_run_failure_is_infrastructure(
        "magma_game failed (rc=2)")
    assert not replay_tape.magma_run_failure_is_infrastructure(
        "magma_game failed (rc=1)")
    assert not replay_tape.magma_run_failure_is_infrastructure("some other error")


def test_infrastructure_payload_carries_no_boolean_verdict():
    payload = replay_tape.infrastructure_gate_payload("magma produced no state rows")
    assert payload["outcome"] == replay_tape.OUTCOME_INFRA_FAIL
    assert payload["pass"] is None       # never True, never False
    assert payload["infrastructure_fail"]["reason"]
    assert payload["frames_checked"] == 0


def test_parity_rc_precedence_unchanged():
    clean = {k: {"available": False, "pass": True} for k in
             ("inventory", "entities", "world")}
    contract = {"gate_status": {}}
    assert replay_tape._parity_rc(None, clean, contract) == 0
    assert replay_tape._parity_rc({"tick": 7}, clean, contract) == 4
    bad_inv = dict(clean, inventory={"available": True, "pass": False})
    assert replay_tape._parity_rc(None, bad_inv, contract) == 5
    # physics outranks state, exactly as before
    assert replay_tape._parity_rc({"tick": 7}, bad_inv, contract) == 4


def test_fail_closed_writes_infrastructure_not_a_failed_parity_gate(tmp_path: Path):
    """Regression: this path used to stamp pass=False on a run with no evidence."""
    contract = {"fail_closed": {"ok": False,
                                "reasons": ["zero state rows from magma"]},
                "gate_status": {}}
    replay_tape._write_gate_artifacts(
        str(tmp_path), "t", None, {"kind": "state"}, contract, False,
        {}, [], None, None, {}, {}, {}, None)
    written = json.loads(
        (tmp_path / "report" / "tape_t.gate.json").read_text())
    assert written["outcome"] == replay_tape.OUTCOME_INFRA_FAIL
    assert written["pass"] is None
    assert "zero state rows" in written["infrastructure_fail"]["reason"]
