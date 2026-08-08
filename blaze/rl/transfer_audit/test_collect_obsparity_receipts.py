import math
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("collect_obsparity_receipts.py")
SPEC = spec_from_file_location("collect_obsparity_receipts", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
AUDIT = module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def test_interface_shapes_and_action_heads():
    observation = AUDIT.interface_schema()
    action = AUDIT.action_schema()

    assert len(observation["frame"]["channels"]) == 18
    assert len(observation["scalars"]) == 27
    assert [len(head["categories"]) for head in action["heads"]] == [
        3,
        3,
        3,
        2,
        2,
        2,
        7,
        2,
        10,
    ]


def test_archived_action_and_runtime_receipt():
    receipt = AUDIT.pilot_runtime(AUDIT.DEFAULT_ARCHIVE)

    assert receipt["artifact"]["actions_total"] == 66_968
    assert receipt["artifact"]["attempts_action_sequence_exact"] == 15
    assert receipt["artifact"]["attempts_action_digest_exact"] == 15
    assert receipt["artifact"]["attempts_world_seed_exact"] == 15
    assert receipt["client_log"]["fresh_world_launches"] == 16
    assert receipt["client_log"]["launch_settings_applied"] == 1
    assert receipt["server_ticks_per_bridge_action"][
        "server_ticks_per_bridge_action_at_fastest_full_episode"
    ] > 65.0


def test_controlled_camera_and_spawn_receipt():
    receipt = AUDIT.snapshot_receipts(AUDIT.DEFAULT_REFERENCE, AUDIT.DEFAULT_ARCHIVE)
    expected_distances = {
        "2": 248.4854120466632,
        "3": 295.94594100950263,
        "10": 241.20530674095875,
    }

    for seed, expected in expected_distances.items():
        row = receipt["seeds"][seed]
        assert math.isclose(
            row["spawn_distance_java_to_snapshot"], expected, rel_tol=0, abs_tol=1e-12
        )
        assert (
            row["controlled_java_double_minus_blaze_float"][
                "any_raw_field_differing_pixels"
            ]
            == 0
        )
        assert (
            row["relative_block_window_final_save_vs_snapshot"][
                "matching_fraction"
            ]
            < 0.85
        )
        assert len(row["blaze_t0_policy_scalars"]) == 27
