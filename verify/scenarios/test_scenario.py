import json
from pathlib import Path

import pytest
import scenario


def base_spec():
    return {
        "name": "test",
        "world": {"seed": 0, "mode": "survival", "type": "flat"},
        "setup_commands": ["/time set 6000"],
        "duration_ticks": 40,
        "input": {"segments": [{"seconds": 1.0, "keys": ["w"]}]},
    }


def test_materialize_segments_pads_to_duration(tmp_path):
    spec_path = tmp_path / "test.yaml"
    spec_path.write_text(__import__("yaml").safe_dump(base_spec()))
    spec = scenario.load_spec(spec_path)
    output = tmp_path / "segments.jsonl"

    segments = scenario.materialize_segments(spec, spec_path, output)

    assert sum(row["seconds"] for row in segments) == 2.0
    assert segments[-1]["keys"] == []
    assert len(output.read_text().splitlines()) == 2


def test_materialize_segments_rejects_overrun(tmp_path):
    raw = base_spec()
    raw["input"]["segments"][0]["seconds"] = 3.0
    spec_path = tmp_path / "test.yaml"
    spec_path.write_text(__import__("yaml").safe_dump(raw))
    spec = scenario.load_spec(spec_path)

    with pytest.raises(ValueError, match="longer than duration_ticks"):
        scenario.materialize_segments(spec, spec_path, tmp_path / "out.jsonl")

def test_oracle_process_filter_ignores_live_display_zero(monkeypatch):
    monkeypatch.setattr(scenario, "process_command",
                        lambda _pid: ("Xvfb", "Xvfb :0 -screen 0 1920x1080x24"))
    assert not scenario.is_oracle_process(1)
    monkeypatch.setattr(scenario, "process_command",
                        lambda _pid: ("Xvfb", "Xvfb :1 -screen 0 1280x720x24"))
    assert scenario.is_oracle_process(1)


def test_oracle_process_filter_scopes_openbox_to_display_one(monkeypatch):
    monkeypatch.setattr(scenario, "process_command",
                        lambda _pid: ("openbox", "openbox"))
    monkeypatch.setattr(scenario, "process_display", lambda _pid: ":0")
    assert not scenario.is_oracle_process(1)
    monkeypatch.setattr(scenario, "process_display", lambda _pid: ":1")
    assert scenario.is_oracle_process(1)



def test_scenario_specs_declare_focused_combat_coverage():
    """Pyramid-visible: focused scenario YAMLs cover required combat classes."""
    root = Path(__file__).resolve().parent
    required = {
        "smoke_zombie.yaml",
        "enderman_fight.yaml",
        "blaze_bow.yaml",
        "ender_dragon.yaml",
    }
    present = {p.name for p in root.glob("*.yaml")}
    missing = required - present
    assert not missing, f"focused scenarios missing from pyramid: {missing}"
    for name in required:
        spec = scenario.load_spec(root / name)
        assert spec["duration_ticks"] > 0
        assert "segments" in spec["input"] or "file" in spec["input"]


def test_survival_campaign_auto_is_long_automated_route(tmp_path):
    """Automated replacement for the long human seed-80302 survival tape.

    Explicit contract: this is a scripted mcwindow route with a fresh seed, not
    human evidence, and it carries no known-divergence exemptions.
    """
    root = Path(__file__).resolve().parent
    spec_path = root / "survival_campaign_auto.yaml"
    assert spec_path.is_file(), "survival_campaign_auto.yaml missing"
    text = spec_path.read_text(encoding="utf-8")
    assert "AUTOMATED" in text or "automated" in text
    assert "NOT human" in text or "not human" in text.lower()
    assert "80302" in text  # documents the human tape it replaces

    spec = scenario.load_spec(spec_path)
    assert spec["name"] == "survival_campaign_auto"
    assert spec["world"]["seed"] == 917351
    assert spec["world"]["seed"] != 80302
    assert spec["world"]["mode"] == "survival"
    assert spec["world"]["type"] == "flat"
    # Long but bounded: longer than focused combat scenarios, shorter than an
    # unbounded human session (~10k+ ticks on seed 80302).
    assert 3600 <= spec["duration_ticks"] <= 7200
    assert spec["duration_ticks"] == 4800
    assert spec["known_divergences"] == []
    assert "file" in spec["input"]

    setup = "\n".join(spec["setup_commands"])
    for needle in (
        "iron_shovel",
        "iron_pickaxe",
        "iron_axe",
        "cobblestone",
        "diamond_sword",
        "crafting_table",
        "furnace",
        "chest",
        "minecraft:sand",
        "minecraft:gravel",
        "minecraft:water",
        "tallgrass",
        "reeds",
        "zombie",
        "slot.inventory.0",
        "slot.hotbar.1",
    ):
        assert needle in setup, f"setup missing {needle!r}"

    # Runway must span multiple chunk borders (16-block strips).
    assert "64" in setup or "48" in setup

    output = tmp_path / "survival_campaign_auto.segments.jsonl"
    segments = scenario.materialize_segments(spec, spec_path, output)
    total = sum(row["seconds"] for row in segments)
    assert total == pytest.approx(spec["duration_ticks"] / 20.0)
    # Trailing idle pad from materialize_segments: empty controls, no look/cursor.
    assert segments[-1]["keys"] == [] and segments[-1]["buttons"] == []
    assert segments[-1].get("look") is None and segments[-1].get("cursor") is None
    scripted_seconds = total - segments[-1]["seconds"]
    assert scripted_seconds >= 140.0, f"scripted route too short: {scripted_seconds}s"
    # Control-bearing time must still be most of the scripted route.
    control_seconds = sum(
        row["seconds"]
        for row in segments[:-1]
        if row["keys"]
        or row["buttons"]
        or row.get("look") is not None
        or row.get("cursor") is not None
    )
    assert control_seconds >= 100.0, f"control route too short: {control_seconds}s"

    keys: set[str] = set()
    buttons: set[int] = set()
    cursors: list[list[float]] = []
    looks = 0
    shift_quick_move = False
    throw_q = False
    outside_click = False
    for row in segments:
        keys.update(row["keys"])
        buttons.update(row["buttons"])
        if row.get("look") is not None:
            looks += 1
        cursor = row.get("cursor")
        if cursor is not None:
            cursors.append(list(cursor))
            if list(cursor) == [5, 5] and 1 in row["buttons"]:
                outside_click = True
        if "Shift_L" in row["keys"] and 1 in row["buttons"]:
            shift_quick_move = True
        if "q" in row["keys"] and cursor is not None:
            throw_q = True

    # Movement / locomotion
    for key in ("w", "a", "d", "s", "Control_L", "space"):
        assert key in keys, f"route missing movement key {key!r}"
    # Inventory GUI open/close, hotbar tools, container close
    for key in ("e", "Escape", "1", "3", "4", "5", "6"):
        assert key in keys, f"route missing control key {key!r}"
    assert 1 in buttons and 3 in buttons  # attack + use
    assert looks >= 8, f"expected multi-look route, got {looks}"
    # Proven gui_scale=2 inventory slot centers from capture_gui_actions.
    assert [282, 258] in cursors  # main inv slot A -> PICKUP
    assert [318, 258] in cursors  # slot B
    assert [354, 258] in cursors  # slot C
    assert [282, 374] in cursors  # hotbar 0
    assert shift_quick_move, "QUICK_MOVE (Shift+LMB) missing"
    assert throw_q, "THROW (q over slot) missing"
    assert outside_click, "outside-GUI PICKUP drop missing"
    assert output.is_file()


def test_archive_rewrites_paths_and_seeds_known_divergences(tmp_path, monkeypatch):
    source_base = tmp_path / "20260722T010203Z_fast_s0_survival_flat_rd8_hash"
    tape = source_base.with_suffix(".jsonl")
    frames = Path(str(source_base) + "_frames")
    frames.mkdir()
    (frames / "f_000000.png").write_bytes(b"png")
    tape.write_text(
        json.dumps({"header": 1})
        + "\n"
        + json.dumps({"t": 0, "frame": str(frames / "f_000000.png")})
        + "\n"
    )
    source_base.with_suffix(".meta.json").write_text(
        json.dumps(
            {
                "created_utc": "20260722T010203Z",
                "tape_jsonl": str(tape),
                "frames_dir": str(frames),
            }
        )
    )
    spec = {
        "name": "test",
        "duration_ticks": 40,
        "known_divergences": [
            {
                "ticks": [0, 0],
                "open_divergence": 40,
                "reason": "test",
                "regions": [[0, 0, 1, 1]],
                "predicate": {"type": "non_solid_scene"},
            }
        ],
    }
    monkeypatch.setattr(scenario, "run", lambda *_args, **_kwargs: "")

    archived = scenario.archive_tape(tape, spec, tmp_path / "test.yaml")

    assert archived.name == "scenario_test_20260722T010203Z.jsonl"
    row = json.loads(archived.read_text().splitlines()[1])
    assert Path(row["frame"]).parent.name == "scenario_test_20260722T010203Z_frames"
    known = archived.with_suffix(".known_divergences.json")
    assert json.loads(known.read_text())["divergences"] == spec["known_divergences"]
