import importlib.util
import pathlib
import sys


HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
SPEC = importlib.util.spec_from_file_location(
    "beam_search", HERE / "beam_search.py")
beam = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(beam)


def test_checkpoint_pruning_is_bounded_and_name_safe(tmp_path):
    for name in ("beam-000001-00", "beam-000007-00",
                 "beam-000008-00", "beam-000009-00", "do-not-touch"):
        (tmp_path / name).mkdir()
    removed = beam.prune_checkpoints(
        tmp_path, 9, ["beam-000007-00"], history=2)
    assert removed == 1
    assert not (tmp_path / "beam-000001-00").exists()
    assert (tmp_path / "beam-000007-00").is_dir()
    assert (tmp_path / "beam-000008-00").is_dir()
    assert (tmp_path / "beam-000009-00").is_dir()
    assert (tmp_path / "do-not-touch").is_dir()


def test_materialize_follows_only_selected_ancestry(tmp_path):
    nodes = {
        "a": {"id": "a", "parent": None, "actions": [{"forward": 1}]},
        "b": {"id": "b", "parent": "a", "actions": [{"jump": 1}]},
        "x": {"id": "x", "parent": "a", "actions": [{"strafe": 1}]},
    }
    output = tmp_path / "actions.jsonl"
    assert beam.materialize(nodes, "b", output) == 2
    text = output.read_text()
    assert '"forward":1' in text
    assert '"jump":1' in text
    assert '"strafe":1' not in text


def test_bootstrap_selection_retains_distinct_offsets():
    primitive = beam.Primitive()
    ranked = [
        (0.1, (-7, -7), 70.0, primitive, {}, {}),
        (0.2, (-7, -7), 70.0, primitive, {}, {}),
        (0.3, (-8, -7), 70.0, primitive, {}, {}),
    ]
    selected = beam.diverse_bootstrap(ranked, 2)
    assert [value[1] for value in selected] == [(-7, -7), (-8, -7)]


def test_third_person_selection_preserves_movement_families():
    parent = {"id": "p"}
    idle = beam.Primitive()
    sprint = beam.Primitive(forward=1, sprint=1)
    ranked = [
        (0.1, 0.1, parent, idle, {}, {}),
        (0.2, 0.2, parent, idle, {}, {}),
        (0.3, 0.3, parent, sprint, {}, {}),
    ]
    selected = beam.select_third_person_diverse(ranked, 2)
    assert [value[3] for value in selected] == [idle, sprint]


def test_third_person_selection_cannot_discard_fov_or_sprint_history():
    idle = beam.Primitive()
    sprint = beam.Primitive(forward=1, sprint=1)
    ranked = []
    for index in range(30):
        parent = {
            "id": f"near-{index}", "fov": 70, "spawn_offset": [-7, -7],
            "motion_counts": [index * 10, 0, 0, 0, 0],
        }
        ranked.append((index / 100.0, 0.1, parent, idle, {}, {}))
    wide = {
        "id": "wide-sprinting", "fov": 110, "spawn_offset": [-9, -9],
        "motion_counts": [100, 100, 0, 0, 0],
    }
    ranked.append((0.9, 0.9, wide, sprint, {}, {}))
    selected = beam.select_third_person_diverse(ranked, 8)
    assert {value[2]["fov"] for value in selected} == {70, 110}
    assert any(value[2]["id"] == "wide-sprinting" for value in selected)


def test_third_person_selection_preserves_each_camera_hypothesis():
    primitive = beam.Primitive()
    ranked = []
    for index, (spawn, fov) in enumerate(
            [((-7, -7), 70), ((-7, -7), 110), ((-9, -9), 70)]):
        parent = {"id": str(index), "spawn_offset": list(spawn), "fov": fov}
        ranked.append((index / 10.0, 0.1, parent, primitive, {}, {}))
    selected = beam.select_third_person_diverse(ranked, 3)
    assert {(tuple(value[2]["spawn_offset"]), value[2]["fov"])
            for value in selected} == {
                ((-7, -7), 70), ((-7, -7), 110), ((-9, -9), 70)}


def test_third_person_selection_preserves_hidden_inventory_family():
    idle = beam.Primitive()
    ranked = []
    for index in range(20):
        parent = {"id": f"empty-{index}", "spawn_offset": [-7, -7],
                  "fov": 70, "motion_counts": [0, 0, 0, 0, 0]}
        result = {"final": {"inventory_ids": [0],
                            "inventory_counts": [0]}}
        ranked.append((index / 100.0, 0.1, parent, idle, result, {}))
    table_parent = {"id": "table", "spawn_offset": [-7, -7], "fov": 70,
                    "motion_counts": [0, 0, 0, 0, 0]}
    table = {"final": {"inventory_ids": [58], "inventory_counts": [1]}}
    ranked.append((0.9, 0.9, table_parent, idle, table, {}))
    selected = beam.select_third_person_diverse(ranked, 8)
    assert any(value[4] is table for value in selected)


def test_third_person_inventory_keeps_moving_hypothesis():
    idle = beam.Primitive()
    sprint = beam.Primitive(forward=1, sprint=1)
    parent = {"id": "table", "spawn_offset": [-7, -7], "fov": 70,
              "motion_counts": [0, 0, 0, 0, 0]}
    table = {"final": {"inventory_ids": [58], "inventory_counts": [1]}}
    ranked = [(0.1, 0.1, parent, idle, table, {}),
              (0.9, 0.9, parent, sprint, table, {})]
    for index in range(20):
        empty_parent = {"id": f"e{index}",
                        "spawn_offset": [-7 - index, -7],
                        "fov": 70, "motion_counts": [0, 0, 0, 0, 0]}
        empty = {"final": {"inventory_ids": [0],
                           "inventory_counts": [0]}}
        ranked.insert(1, (0.2 + index / 100.0, 0.2,
                          empty_parent, idle, empty, {}))
    selected = beam.select_third_person_diverse(ranked, 8)
    assert any(value[4] is table and value[3] == sprint
               for value in selected)


def test_third_person_rare_item_gets_motion_before_common_families():
    idle = beam.Primitive()
    sprint = beam.Primitive(forward=1, sprint=1)
    ranked = []
    for item in range(1, 8):
        for copy in range(3):
            parent = {"id": f"common-{item}-{copy}",
                      "spawn_offset": [-7, -7], "fov": 70,
                      "motion_counts": [0, 0, 0, 0, 0]}
            result = {"final": {"inventory_ids": [item],
                                "inventory_counts": [1]}}
            ranked.append((item / 100.0, 0.1, parent, idle, result, {}))
    rare_parent = {"id": "rare", "spawn_offset": [-7, -7], "fov": 70,
                   "motion_counts": [0, 0, 0, 0, 0]}
    rare = {"final": {"inventory_ids": [58], "inventory_counts": [1]}}
    ranked.extend([(0.8, 0.8, rare_parent, idle, rare, {}),
                   (0.9, 0.9, rare_parent, sprint, rare, {})])
    selected = beam.select_third_person_diverse(ranked, 8)
    assert any(value[4] is rare and value[3] == sprint
               for value in selected)


def test_first_person_selection_reserves_discrete_inventory_outcome():
    parent = {
        "id": "p", "end": {
            "dimension": 0, "container": 0, "dead": 0, "health": 20,
            "inventory_ids": [0], "inventory_counts": [0],
        },
    }
    idle = beam.Primitive()
    attack = beam.Primitive(attack=1)
    unchanged = {"final": dict(parent["end"])}
    acquired = {"final": dict(parent["end"], inventory_ids=[17],
                               inventory_counts=[1])}
    ranked = [(index / 100.0, 0.1, parent, idle, unchanged, {})
              for index in range(20)]
    ranked.append((0.9, 0.9, parent, attack, acquired, {}))
    selected = beam.select_diverse(ranked, 6)
    assert any(value[4] is acquired for value in selected)


def test_first_person_selection_retains_sustained_attack_lineage():
    idle = beam.Primitive()
    attack = beam.Primitive(attack=1)
    unchanged = {"final": {"health": 20, "inventory_ids": [0],
                            "inventory_counts": [0]}}
    ranked = []
    for index in range(20):
        parent = {"id": f"idle-{index}", "end": unchanged["final"]}
        ranked.append((index / 100.0, 0.1, parent, idle, unchanged, {}))
    attacker = {"id": "attacker", "end": unchanged["final"],
                "motion_counts": [0, 0, 0, 30, 0]}
    ranked.append((0.9, 0.9, attacker, attack, unchanged, {}))
    selected = beam.select_diverse(ranked, 6)
    assert any(value[2]["id"] == "attacker" for value in selected)


def test_attack_hint_prioritizes_engine_validated_digging():
    idle = beam.Primitive()
    attack = beam.Primitive(attack=1)
    end = {"health": 20, "inventory_ids": [0], "inventory_counts": [0]}
    ranked = []
    for index in range(8):
        parent = {"id": f"p{index}", "end": end,
                  "motion_counts": [0, 0, 0, 0, 0]}
        blind = {"final": dict(end, ray=[0], dig=[0, 0.0])}
        ranked.append((index / 100.0, 0.1, parent, idle, blind, {}))
    target_parent = {"id": "target", "end": end,
                     "motion_counts": [0, 0, 0, 0, 0]}
    digging = {"final": dict(end, ray=[17], dig=[1, 0.6])}
    ranked.append((0.9, 0.9, target_parent, attack, digging, {}))
    selected = beam.select_diverse(ranked, 4, attack_hint=True)
    assert any(value[4] is digging for value in selected)


def test_digging_with_existing_resource_survives_other_attack_parents():
    idle = beam.Primitive()
    attack = beam.Primitive(attack=1)
    ranked = []
    for index in range(20):
        parent = {"id": f"empty-{index}", "end": {},
                  "motion_counts": [0, 0, 0, 20, 0]}
        result = {"final": {"inventory_ids": [0],
                            "inventory_counts": [0],
                            "ray": [2], "dig": [1, 0.9]}}
        ranked.append((index / 100.0, 0.1, parent, attack, result, {}))
    wood_parent = {"id": "wood", "end": {
        "inventory_ids": [17], "inventory_counts": [1]},
        "motion_counts": [0, 0, 0, 20, 0]}
    wood_dig = {"final": {"inventory_ids": [17],
                          "inventory_counts": [1],
                          "ray": [17], "dig": [1, 0.2]}}
    ranked.append((0.9, 0.9, wood_parent, attack, wood_dig, {}))
    selected = beam.select_diverse(ranked, 6)
    assert any(value[4] is wood_dig for value in selected)


def test_resource_ray_beats_faster_progress_on_unrelated_block():
    attack = beam.Primitive(attack=1)
    parent = {"id": "wood", "end": {
        "inventory_ids": [17], "inventory_counts": [2]},
        "motion_counts": [0, 0, 0, 20, 0]}
    dirt = {"final": {
        "inventory_ids": [17], "inventory_counts": [2],
        "logs": [[53, 70, -182]],
        "ray": [2, 52, 67, -183], "dig": [1, 0.8]}}
    log = {"final": {
        "inventory_ids": [17], "inventory_counts": [2],
        "logs": [[53, 70, -182]],
        "ray": [17, 53, 70, -182], "dig": [1, 0.2]}}
    ranked = [(0.1, 0.1, parent, attack, dirt, {}),
              (0.9, 0.9, parent, attack, log, {})]
    selected = beam.select_diverse(ranked, 1)
    assert selected[0][4] is log


def test_pending_item_drop_survives_pickup_delay():
    idle = beam.Primitive()
    ranked = []
    for index in range(20):
        parent = {"id": f"empty-{index}", "end": {}}
        ranked.append((index / 100.0, 0.1, parent, idle,
                       {"final": {"items": []}}, {}))
    drop_parent = {"id": "drop", "end": {
        "inventory_ids": [17], "inventory_counts": [1]}}
    pending = {"final": {"inventory_ids": [17], "inventory_counts": [1],
                          "items": [[17, 1, 10, 1.5, 65.0, 2.5]]}}
    ranked.append((0.9, 0.9, drop_parent, idle, pending, {}))
    selected = beam.select_diverse(ranked, 6)
    assert any(value[4] is pending for value in selected)


def test_use_consumption_survives_before_container_opens():
    idle = beam.Primitive()
    use = beam.Primitive(use=2, hotbar=4)
    parent = {"id": "place", "end": {
        "inventory_ids": [58], "inventory_counts": [1], "gui": 0,
        "container": 0}}
    unchanged = {"final": dict(parent["end"])}
    placed = {"final": {"inventory_ids": [0], "inventory_counts": [0],
                         "gui": 0, "container": 0}}
    ranked = [(index / 100.0, 0.1, parent, idle, unchanged, {})
              for index in range(10)]
    ranked.append((0.9, 0.9, parent, use, placed, {}))
    selected = beam.select_diverse(ranked, 4, expected_gui=True)
    assert any(value[4] is placed for value in selected)


def test_selection_preserves_existing_inventory_across_later_frames():
    idle = beam.Primitive()
    ranked = []
    for index in range(8):
        parent = {"id": f"empty-{index}", "end": {}}
        result = {"final": {"inventory_ids": [0],
                             "inventory_counts": [0]}}
        ranked.append((index / 100.0, 0.1, parent, idle, result, {}))
    wood_parent = {"id": "wood", "end": {}}
    wood = {"final": {"inventory_ids": [17], "inventory_counts": [1]}}
    ranked.append((0.9, 0.9, wood_parent, idle, wood, {}))
    selected = beam.select_diverse(ranked, 4)
    assert any(value[4] is wood for value in selected)


def test_inventory_family_diversity_keeps_wood_beside_more_dirt():
    idle = beam.Primitive()
    ranked = []
    for count in (4, 3, 2, 1):
        parent = {"id": f"dirt-{count}", "end": {}}
        dirt = {"final": {"inventory_ids": [3],
                           "inventory_counts": [count]}}
        ranked.append(((5 - count) / 100.0, 0.1, parent, idle, dirt, {}))
    parent = {"id": "wood", "end": {}}
    wood = {"final": {"inventory_ids": [17], "inventory_counts": [1]}}
    ranked.append((0.9, 0.9, parent, idle, wood, {}))
    selected = beam.select_diverse(ranked, 6)
    assert any(value[4] is wood for value in selected)


def test_item_family_retains_highest_resource_count():
    idle = beam.Primitive()
    parent = {"id": "wood", "end": {}}
    one = {"final": {"inventory_ids": [17], "inventory_counts": [1]}}
    three = {"final": {"inventory_ids": [17], "inventory_counts": [3]}}
    ranked = [(0.1, 0.1, parent, idle, one, {}),
              (0.9, 0.9, parent, idle, three, {})]
    for index in range(10):
        ranked.insert(1, (0.2 + index / 100.0, 0.2,
                          {"id": f"empty-{index}", "end": {}}, idle,
                          {"final": {"inventory_ids": [0],
                                     "inventory_counts": [0]}}, {}))
    selected = beam.select_diverse(ranked, 4)
    assert any(value[4] is three for value in selected)


def test_same_item_on_cursor_and_grid_does_not_crowd_other_item():
    idle = beam.Primitive()
    ranked = []
    dirt_states = [
        {"inventory_ids": [3], "inventory_counts": [1]},
        {"inventory_ids": [0], "inventory_counts": [0],
         "cursor": [3, 1, 0]},
        {"inventory_ids": [0], "inventory_counts": [0],
         "craft_grid": [[3, 1, 0]]},
    ]
    for index, final in enumerate(dirt_states):
        ranked.append((index / 100.0, 0.1, {"id": f"d{index}", "end": {}},
                       idle, {"final": final}, {}))
    wood = {"final": {"inventory_ids": [17], "inventory_counts": [1]}}
    ranked.append((0.9, 0.9, {"id": "wood", "end": {}}, idle, wood, {}))
    selected = beam.select_diverse(ranked, 6)
    assert any(value[4] is wood for value in selected)


def test_crafting_retention_keeps_second_state_of_same_item_family():
    idle = beam.Primitive()
    inv_wood = {"final": {"inventory_ids": [17],
                           "inventory_counts": [1]}}
    cursor_wood = {"final": {"inventory_ids": [0],
                              "inventory_counts": [0],
                              "cursor": [17, 1, 0]}}
    ranked = [
        (0.1, 0.1, {"id": "inv", "end": {}}, idle, inv_wood, {}),
        (0.9, 0.9, {"id": "cursor", "end": {}}, idle, cursor_wood, {}),
    ]
    for index in range(8):
        ranked.append((0.2 + index / 100.0, 0.2,
                       {"id": f"e{index}", "end": {}}, idle,
                       {"final": {"inventory_ids": [0],
                                   "inventory_counts": [0]}}, {}))
    selected = beam.select_diverse(ranked, 6)
    assert any(value[4] is inv_wood for value in selected)
    assert any(value[4] is cursor_wood for value in selected)


def test_selection_reserves_each_new_recipe_result_identity():
    idle = beam.Primitive()
    parent_end = {"inventory_ids": [5], "inventory_counts": [4]}
    parent = {"id": "planks", "end": parent_end}
    ranked = []
    for index in range(12):
        unchanged = {"final": dict(parent_end)}
        ranked.append((index / 100.0, 0.1, parent, idle, unchanged, {}))
    discoveries = []
    for index, item in enumerate((72, 280, 58)):
        result = {"final": {"inventory_ids": [item],
                            "inventory_counts": [1]}}
        discoveries.append(result)
        ranked.append((0.7 + index / 10.0, 0.7, parent, idle, result, {}))
    selected = beam.select_diverse(ranked, 9)
    assert all(any(value[4] is result for value in selected)
               for result in discoveries)


def test_completed_inventory_beats_transient_recipe_layouts():
    idle = beam.Primitive()
    ranked = []
    for index, item in enumerate((72, 280, 143, 5, 17, 3)):
        transient = {"final": {
            "inventory_ids": [item], "inventory_counts": [1],
            "craft_grid": [[5, 1, 0]], "craft_result": [item, 1, 0],
        }}
        parent = {"id": f"transient-{item}", "end": transient["final"]}
        ranked.append((index / 100.0, 0.1, parent, idle, transient, {}))
    table = {"final": {"inventory_ids": [58], "inventory_counts": [1]}}
    table_parent = {"id": "table", "end": table["final"]}
    ranked.append((0.9, 0.9, table_parent, idle, table, {}))
    selected = beam.select_diverse(ranked, 6)
    assert any(value[4] is table for value in selected)


def test_expected_closed_gui_retains_closed_item_branch():
    idle = beam.Primitive()
    parent = {"id": "table", "end": {
        "inventory_ids": [58], "inventory_counts": [1], "gui": 1}}
    opened = {"final": dict(parent["end"])}
    closed = {"final": dict(parent["end"], gui=0)}
    ranked = [(0.1, 0.1, parent, idle, opened, {}),
              (0.9, 0.9, parent, beam.Primitive(close_container=1),
               closed, {})]
    for item in range(1, 9):
        for copy in range(2):
            end = {"inventory_ids": [item], "inventory_counts": [1],
                   "gui": 0}
            ranked.insert(1, (0.2 + item / 100.0, 0.2,
                              {"id": f"common-{item}-{copy}", "end": end},
                              idle, {"final": end}, {}))
    selected = beam.select_diverse(ranked, 4, expected_gui=False)
    assert len(selected) == 4
    assert any(value[4] is closed for value in selected)


def test_container_transition_precedes_inventory_reservations():
    idle = beam.Primitive()
    ranked = []
    for item in range(1, 20):
        parent = {"id": f"item-{item}", "end": {
            "dimension": 0, "container": 0, "gui": 1,
            "inventory_ids": [item], "inventory_counts": [1]}}
        result = {"final": dict(parent["end"])}
        ranked.append((item / 100.0, 0.1, parent, idle, result, {}))
    parent = {"id": "table", "end": {
        "dimension": 0, "container": 0, "gui": 0,
        "inventory_ids": [58], "inventory_counts": [1]}}
    opened = {"final": {"dimension": 0, "container": 1, "gui": 1,
                         "inventory_ids": [0], "inventory_counts": [0]}}
    ranked.append((0.9, 0.9, parent, beam.Primitive(use=2), opened, {}))
    selected = beam.select_diverse(ranked, 6, expected_gui=True)
    assert any(value[4] is opened for value in selected)


def test_open_container_persists_after_transition_tick():
    idle = beam.Primitive()
    container_end = {"dimension": 0, "container": 1, "gui": 1,
                     "inventory_ids": [0], "inventory_counts": [0]}
    parent = {"id": "container", "end": container_end}
    continuing = {"final": dict(container_end)}
    ranked = []
    for index in range(20):
        empty_parent = {"id": f"empty-{index}", "end": {
            "dimension": 0, "container": 0, "gui": 1}}
        empty = {"final": dict(empty_parent["end"])}
        ranked.append((index / 100.0, 0.1, empty_parent, idle, empty, {}))
    ranked.append((0.9, 0.9, parent, idle, continuing, {}))
    selected = beam.select_diverse(ranked, 6, expected_gui=True)
    assert any(value[4] is continuing for value in selected)


def test_structural_retention_prefers_expected_gui_close():
    idle = beam.Primitive()
    end = {"dimension": 0, "container": 1, "gui": 1}
    parent = {"id": "container", "end": end}
    opened = {"final": dict(end)}
    closed = {"final": dict(end, gui=0)}
    ranked = [(0.1, 0.1, parent, idle, opened, {}),
              (0.9, 0.9, parent, beam.Primitive(close_container=1),
               closed, {})]
    selected = beam.select_diverse(ranked, 1, expected_gui=False)
    assert selected[0][4] is closed
