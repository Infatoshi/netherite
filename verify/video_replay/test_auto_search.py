import importlib.util
import pathlib

import numpy as np


HERE = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "auto_search", HERE / "auto_search.py")
search = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(search)


def test_chamfer_identity_and_translation():
    edge = np.zeros((search.CAM_H, search.CAM_W), dtype=np.uint8)
    edge[8:25, 20] = 1
    edge[24, 20:42] = 1
    mask = np.ones_like(edge)
    assert search.chamfer_cost(edge, mask, edge) == 0.0
    shifted = np.roll(edge, 2, axis=1)
    cost = search.chamfer_cost(edge, mask, shifted)
    assert 0.0 < cost < 0.5


def test_primitive_actions_are_complete_and_bounded():
    primitive = search.Primitive(
        forward=1, sprint=1, jump_period=3,
        yaw_total=20.0, pitch_total=-10.0)
    rows = primitive.actions(10)
    assert len(rows) == 10
    assert abs(sum(row["dyaw"] for row in rows) - 20.0) < 1e-9
    assert abs(sum(row["dpitch"] for row in rows) + 10.0) < 1e-9
    assert [row["jump"] for row in rows].count(1) == 4


def test_state_hash_ignores_camera_payload():
    base = {"t": 1, "x": 2.0, "inventory_ids": [1], "cam": [1, 2]}
    changed = dict(base, cam=[9, 9])
    assert search.state_hash(base) == search.state_hash(changed)


def test_gray_edge_shape():
    gray = np.zeros((search.CAM_H, search.CAM_W), dtype=np.uint8)
    gray[10:25, 12:40] = 200
    edge = search._gray_edges(gray)
    assert edge.shape == gray.shape
    assert edge.any()


def test_score_uses_spatial_rgb_to_reject_wrong_colored_scene():
    gray = np.full((search.CAM_H, search.CAM_W), 100, dtype=np.uint8)
    edge = np.zeros_like(gray)
    edge[8:28, 20] = 1
    mask = np.ones_like(gray)
    target_rgb = np.zeros((search.CAM_H, search.CAM_W, 3), dtype=np.uint8)
    target_rgb[..., 1] = 150
    matching = {"final": {"health": 20}, "render_gray": gray,
                "render_rgb": target_rgb.copy()}
    wrong = {"final": {"health": 20}, "render_gray": gray,
             "render_rgb": np.full_like(target_rgb, (120, 40, 120))}
    good, _ = search.score_rollout(
        matching, gray, edge, mask, search.Primitive(),
        target_rgb=target_rgb)
    bad, _ = search.score_rollout(
        wrong, gray, edge, mask, search.Primitive(),
        target_rgb=target_rgb)
    assert bad > good + 0.15


def test_small_candidate_budget_still_contains_video_conditioned_turns():
    class Features:
        @staticmethod
        def motion(begin, end):
            return 12.0, -4.0

    values = search.primitive_candidates(Features(), 0, 1, 8, 7)
    assert len(values) == 8
    assert any(value.yaw_total == 12.0 and value.pitch_total == -4.0
               for value in values)
    assert any(value.forward == 1 and value.sprint == 1 and
               value.yaw_total != 0 for value in values)


def test_targeted_attack_aims_at_nearby_log_with_ordinary_mouse_delta():
    obs = {"x": 0.0, "y": 64.0, "z": 0.0, "yaw": 0.0, "pitch": 0.0,
           "logs": [[1, 65, 2]], "coal": [], "blocks": []}
    values = search.targeted_attack_primitives(obs)
    assert len(values) == 1
    assert values[0].attack == 1
    assert values[0].yaw_total < 0
    assert abs(values[0].pitch_total) < 10


def test_gui_primitives_open_and_emit_single_tick_clicks():
    values = search.gui_primitives(0, 5, opening=True)
    assert values[0].open_inventory == 1
    clicked = next(value for value in values if value.inv_slot >= 0)
    rows = clicked.actions(10)
    assert sum("inv_slot" in row for row in rows) == 1


def test_gui_primitives_always_reach_occupied_inventory_slot():
    obs = {"inventory_counts": [0, 0, 1] + [0] * 33}
    values = search.gui_primitives(7, 8, obs=obs)
    assert any(value.inv_slot == 2 for value in values)


def test_gui_opening_searches_inventory_and_world_use_paths():
    obs = {"inventory_counts": [0] * 8 + [1] + [0] * 27}
    values = search.gui_primitives(0, 12, opening=True, obs=obs)
    assert any(value.open_inventory for value in values)
    assert {value.use for value in values} >= {1, 2}
    assert any(value.use == 2 and value.hotbar == 8 and value.forward == -1
               for value in values)


def test_gui_opening_keeps_resource_completion_hypothesis():
    obs = {"x": 0.0, "y": 64.0, "z": 0.0, "yaw": 0.0, "pitch": 0.0,
           "logs": [[0, 66, 2]], "inventory_counts": [2] + [0] * 35}
    values = search.gui_primitives(0, 16, opening=True, obs=obs)
    assert any(value.attack for value in values)


def test_targeted_attack_prefers_resource_over_nearer_generic_block():
    obs = {"x": 0.5, "y": 64.0, "z": 0.5, "yaw": 0.0, "pitch": 0.0,
           "logs": [[0, 65, 4]], "coal": [],
           "blocks": [[2, 0, 65, 2]]}
    values = search.targeted_attack_primitives(obs, 1)
    assert len(values) == 1
    # The farther log is straight ahead, while the nearer generic block is
    # also ahead but requires a steeper downward pitch.
    assert abs(values[0].pitch_total) < 10.0


def test_gui_macro_schedules_multiple_ordinary_clicks_in_one_horizon():
    primitive = search.Primitive(
        inv_sequence=((8, 0, 0), (36, 1, 0), (37, 1, 0), (45, 0, 0)))
    rows = primitive.actions(10)
    clicks = [(row["inv_slot"], row["inv_button"], row["inv_type"])
              for row in rows if "inv_slot" in row]
    assert clicks == list(primitive.inv_sequence)


def test_gui_recipe_macro_empties_cursor_before_result_transfer():
    obs = {"inventory_counts": [3] + [0] * 35,
           "inventory_ids": [17] + [0] * 35,
           "cursor": [0, 0, 0], "craft_result": [0, 0, 0]}
    values = search.gui_primitives(0, 32, obs=obs)
    macro = next(value for value in values if value.inv_sequence[:2] ==
                 ((0, 0, 0), (36, 1, 0)))
    assert macro.inv_sequence == ((0, 0, 0), (36, 1, 0),
                                  (0, 0, 0), (45, 0, 1))


def test_table_search_contains_generic_two_ingredient_t_layout():
    obs = {"container": 1,
           "inventory_counts": [3, 2] + [0] * 34,
           "inventory_ids": [101, 202] + [0] * 34}
    macros = search._two_ingredient_recipe_macros(obs)
    expected = ((0, 0, 0), (36, 1, 0), (37, 1, 0), (38, 1, 0),
                (1, 0, 0), (40, 1, 0), (43, 1, 0), (45, 0, 1))
    assert any(value.inv_sequence == expected for value in macros)


def test_dark_textured_world_frame_is_not_a_loading_pause(tmp_path):
    shape = (2, search.CAM_H, search.CAM_W)
    gray = np.full(shape, 25, dtype=np.uint8)
    edges = np.zeros(shape, dtype=np.uint8)
    edges[1, ::2, ::2] = 1
    masks = np.ones(shape, dtype=np.uint8)
    path = tmp_path / "features.npz"
    np.savez(path, fps=np.array([10.0]), gray=gray, edges=edges,
             masks=masks, shifts=np.zeros((2, 2), dtype=np.int8),
             usable=np.ones(2, dtype=np.uint8))
    features = search.FeatureTape(path)
    assert features.loading.tolist() == [1, 0]
