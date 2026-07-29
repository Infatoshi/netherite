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
