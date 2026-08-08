#!/usr/bin/env python3
"""Persistent checkpoint beam for generic video-to-input-tape recovery."""
import argparse
import concurrent.futures
import dataclasses
import json
import math
import os
import pathlib
import random
import re
import shutil
import time
import hashlib

from auto_search import (ACTION_KEYS, FeatureTape, Magma, Primitive,
                         gameplay_start, gui_primitives, primitive_candidates,
                         score_rollout, state_hash, targeted_attack_primitives,
                         write_json)


SCHEMA = 1
CHECKPOINT_SLOT_RE = re.compile(r"^beam-(\d{6})-(\d{2})$")


def prune_checkpoints(root, generation, live_slots, history=3):
    """Bound native checkpoint storage while retaining resumable live state."""
    if history <= 0 or not root.is_dir():
        return 0
    live_slots = set(live_slots)
    oldest = max(0, generation - history + 1)
    removed = 0
    for path in root.iterdir():
        match = CHECKPOINT_SLOT_RE.fullmatch(path.name)
        if (not match or not path.is_dir() or path.name in live_slots or
                int(match.group(1)) >= oldest):
            continue
        shutil.rmtree(path)
        removed += 1
    return removed


def append_jsonl(path, values):
    with path.open("a", encoding="utf-8") as stream:
        for value in values:
            stream.write(json.dumps(value, sort_keys=True,
                                    separators=(",", ":")) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


def read_nodes(path):
    if not path.exists():
        return {}
    with path.open(encoding="utf-8") as stream:
        values = [json.loads(line) for line in stream if line.strip()]
    return {value["id"]: value for value in values}


def materialize(nodes, node_id, output):
    chain = []
    while node_id is not None:
        node = nodes[node_id]
        chain.append(node)
        node_id = node.get("parent")
    chain.reverse()
    tick = 0
    temporary = output.with_suffix(output.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        for node in chain:
            for action in node["actions"]:
                row = {key: action[key] for key in ACTION_KEYS if key in action}
                row.update({"tick": tick, "type": "action"})
                stream.write(json.dumps(row, sort_keys=True,
                                        separators=(",", ":")) + "\n")
                tick += 1
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(output)
    return tick


def bootstrap_values(args):
    grid = []
    for x in range(-10, -5):
        for z in range(-10, -5):
            for yaw in range(0, 360, args.bootstrap_yaw_step):
                for pitch in (-25.0, 0.0, 25.0):
                    for fov in args.fovs:
                        grid.append(((x, z), float(fov), Primitive(
                            yaw_total=yaw - 180.0, pitch_total=pitch)))
    rng = random.Random(args.seed ^ 0x51A7)
    rng.shuffle(grid)
    if args.bootstrap_candidates and args.bootstrap_candidates < len(grid):
        grid = grid[:args.bootstrap_candidates]
    preferred = ((args.spawn_offset_x, args.spawn_offset_z), 70.0, Primitive())
    if preferred not in grid:
        grid.append(preferred)
    return grid


def diverse_bootstrap(ranked, width):
    selected = []
    offsets = set()
    # Preserve distinct fuzz offsets until later world landmarks resolve them.
    for value in ranked:
        identity = (value[1], value[2])
        if identity in offsets:
            continue
        selected.append(value)
        offsets.add(identity)
        if len(selected) == width:
            return selected
    for value in ranked:
        if value not in selected:
            selected.append(value)
        if len(selected) == width:
            break
    return selected


def bootstrap(args, features, frame, nodes_path):
    values = bootstrap_values(args)

    def evaluate(value):
        spawn, fov, primitive = value
        result = Magma(args, spawn, fov=fov).rollout(
            primitive.actions(1), render=True)
        score, terms = score_rollout(
            result, features.gray[frame], features.edges[frame],
            features.masks[frame], primitive,
            target_rgb=features.rgb[frame] if features.rgb is not None else None)
        return score, spawn, fov, primitive, result, terms

    with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.workers) as pool:
        ranked = list(pool.map(evaluate, values))
    ranked.sort(key=lambda value: value[0])
    # SpawnRandomizer tests all 5x5 fuzz cells. Preserve one hypothesis for
    # every cell across the featureless chunk-fade frames, even when the normal
    # steady-state beam is narrower; the first terrain landmark then resolves
    # them without an unjustified early guess.
    chosen = diverse_bootstrap(
        ranked, max(args.beam_width, 25 * len(args.fovs)))
    nodes = []
    for rank, (score, spawn, fov, primitive, _, _) in enumerate(chosen):
        actions = primitive.actions(1)
        slot = f"beam-000000-{rank:02d}"
        committed = Magma(args, spawn, fov=fov).rollout(
            actions, save_slot=slot, render=True)
        replay_score, terms = score_rollout(
            committed, features.gray[frame], features.edges[frame],
            features.masks[frame], primitive,
            target_rgb=features.rgb[frame] if features.rgb is not None else None)
        if abs(score - replay_score) > 1e-9:
            raise RuntimeError("non-deterministic bootstrap replay")
        final = committed["final"]
        node = {
            "schema": SCHEMA, "kind": "beam_node",
            "id": f"n000000-{rank:02d}", "parent": None,
            "generation": 0, "rank": rank, "slot": slot,
            "spawn_offset": list(spawn), "fov": fov, "actions": actions,
            "motion_counts": [0, 0, 0, 0, 0],
            "primitive": dataclasses.asdict(primitive),
            "endpoint_score": replay_score, "path_score": replay_score,
            "score_terms": terms, "state_hash": state_hash(final),
            "end": {key: final.get(key) for key in
                    ("x", "y", "z", "yaw", "pitch", "dimension", "health",
                     "food", "dead", "container", "inventory_ids",
                     "inventory_counts", "ray", "dig", "logs", "coal",
                     "blocks", "items", "gui", "cursor", "craft_grid",
                     "craft_result")},
        }
        nodes.append(node)
    append_jsonl(nodes_path, nodes)
    return nodes, len(ranked)


def _discrete_outcome(obs):
    return (
        obs.get("dimension"), obs.get("container"), obs.get("dead"),
        obs.get("gui"),
        round(float(obs.get("health", 20.0)) * 2),
        tuple(obs.get("inventory_ids", ())),
        tuple(obs.get("inventory_counts", ())),
        tuple(obs.get("cursor", ())),
        tuple(tuple(row) for row in obs.get("craft_grid", ())),
        tuple(obs.get("craft_result", ())),
        tuple((row[0], row[1]) for row in obs.get("items", ())
              if len(row) > 1),
    )


def select_diverse(ranked, width, horizon_ticks=10, attack_hint=False,
                   expected_gui=None):
    """Avoid filling the beam with near-identical children of one parent."""
    selected = []
    signatures = set()
    parent_counts = {}

    # Container, GUI, and dimension changes are sparse structural events.
    # Reserve their distinct simulated outcomes before inventory layouts can
    # fill the beam. This is what keeps ordinary place-then-use paths alive
    # at a video menu transition without knowing the container type in advance.
    structural_seen = set()
    structural_budget = max(2, width // 4)
    structural_ranked = sorted(
        ranked,
        key=lambda value: (
            0 if expected_gui is None or bool(value[4].get(
                "final", {}).get("gui")) == bool(expected_gui) else 1,
            value[0]))
    for value in structural_ranked:
        parent = value[2]
        final = value[4].get("final", {})
        before = parent.get("end", {})
        structural = (final.get("dimension"), final.get("container"),
                      bool(final.get("gui")), bool(final.get("dead")))
        prior = (before.get("dimension"), before.get("container"),
                 bool(before.get("gui")), bool(before.get("dead")))
        persistent_structure = (
            bool(final.get("container")) or
            final.get("dimension") not in (None, 0))
        if ((structural == prior and not persistent_structure) or
                structural in structural_seen):
            continue
        selected.append(value)
        structural_seen.add(structural)
        parent_counts[parent["id"]] = 1
        if (len(structural_seen) >= structural_budget or
                len(selected) >= width):
            break

    # Placing a carried block is commonly one horizon before opening its GUI.
    # Preserve ordinary-use outcomes that consume an inventory item even when
    # the changed world pixels are temporarily less similar. This is generic
    # transition evidence from the simulator, not a block identity shortcut.
    placement_seen = set()
    for value in ranked:
        parent = value[2]
        primitive = value[3]
        final = value[4].get("final", {})
        if not primitive.use or value in selected:
            continue
        before_counts = {}
        after_counts = {}
        for item, count in zip(parent.get("end", {}).get(
                "inventory_ids", ()), parent.get("end", {}).get(
                "inventory_counts", ())):
            if item and count:
                before_counts[item] = before_counts.get(item, 0) + count
        for item, count in zip(final.get("inventory_ids", ()),
                               final.get("inventory_counts", ())):
            if item and count:
                after_counts[item] = after_counts.get(item, 0) + count
        consumed = tuple(sorted(
            (item, amount - after_counts.get(item, 0))
            for item, amount in before_counts.items()
            if amount > after_counts.get(item, 0)))
        if not consumed or consumed in placement_seen:
            continue
        selected.append(value)
        placement_seen.add(consumed)
        parent_counts[parent["id"]] = 1
        if len(placement_seen) >= max(1, width // 4) or len(selected) >= width:
            break

    # A broken block becomes an EntityItem before pickup changes inventory.
    # Preserve pending rewards across checkpoint boundaries and pickup delay,
    # keyed by item/count rather than transient subpixel motion.
    pending_seen = set()
    for value in ranked:
        rows = value[4].get("final", {}).get("items", ())
        pending = tuple(sorted((int(row[0]), int(row[1])) for row in rows
                               if len(row) > 1 and row[0] and row[1]))
        if not pending or pending in pending_seen or value in selected:
            continue
        selected.append(value)
        pending_seen.add(pending)
        parent_counts[value[2]["id"]] = 1
        if len(pending_seen) >= max(1, width // 4) or len(selected) >= width:
            break

    def item_ids(obs):
        values = {item for item, count in zip(
            obs.get("inventory_ids", ()), obs.get("inventory_counts", ()))
                  if item and count}
        cursor = obs.get("cursor", ())
        if len(cursor) > 1 and cursor[0] and cursor[1]:
            values.add(cursor[0])
        values.update(row[0] for row in obs.get("craft_grid", ())
                      if len(row) > 1 and row[0] and row[1])
        result = obs.get("craft_result", ())
        if len(result) > 1 and result[0] and result[1]:
            values.add(result[0])
        return values

    def aimed_resource(obs):
        """Classify the engine ray target from observed resource positions."""
        ray = obs.get("ray", ())
        if len(ray) < 4 or not ray[0]:
            return 0
        target = tuple(map(int, ray[1:4]))
        for priority, key in ((2, "logs"), (1, "coal")):
            if any(len(row) >= 3 and tuple(map(int, row[:3])) == target
                   for row in obs.get(key, ())):
                return priority
        return 0

    # Resource gathering often needs several visually mediocre horizons after
    # the first pickup. Keep the strongest active-dig child for each carried
    # item family so "already acquired one, continue breaking the next" does
    # not compete only against empty-inventory attack lineages.
    digging_by_family = {}
    for value in ranked:
        primitive = value[3]
        final = value[4].get("final", {})
        dig = final.get("dig", ())
        family = tuple(sorted(item_ids(final)))
        if (not primitive.attack or not family or len(dig) < 2 or
                not dig[0]):
            continue
        # A faster-breaking dirt/plant target can show more progress than the
        # slower resource block this lineage needs. Prefer an engine ray that
        # actually intersects a dedicated resource observation, then compare
        # accumulated break progress. No item or recipe identity is assumed.
        evidence = (aimed_resource(final), float(dig[1]),
                    bool(final.get("ray", [0])[0]), -value[0])
        prior = digging_by_family.get(family)
        if prior is None or evidence > prior[0]:
            digging_by_family[family] = (evidence, value)
    for _, value in sorted(digging_by_family.values(), reverse=True,
                           key=lambda entry: entry[0])[:max(1, width // 4)]:
        if value in selected:
            continue
        selected.append(value)
        parent_counts[value[2]["id"]] = 1
        if len(selected) >= width:
            return selected[:width]

    # Recipe and pickup discoveries are sparse transitions whose pixels can
    # be worse than an idle branch at the instant they occur. Preserve one
    # child for each genuinely new item identity before ordinary inventory
    # diversity. This is generic search bookkeeping: no recipe or item ID is
    # encoded here, and the simulated outcome remains the sole evidence.
    created_seen = set()
    created_budget = max(1, width // 3)
    for value in ranked:
        parent = value[2]
        final = value[4].get("final", {})
        created = tuple(sorted(item_ids(final) -
                               item_ids(parent.get("end", {}))))
        if not created or created in created_seen or value in selected:
            continue
        selected.append(value)
        created_seen.add(created)
        parent_counts[parent["id"]] = 1
        if len(created_seen) >= created_budget or len(selected) >= width:
            break
    # Do not forget an acquired item just because later world frames contain
    # no strong HUD evidence. Reserve distinct non-empty inventory/crafting
    # states as long-horizon semantic hypotheses; visual score orders ties.
    persistent_seen = set()
    persistent_families = set()
    persistent_budget = max(2, width // 2)
    family_stages = set()

    def persistent_description(value):
        final = value[4].get("final", {})
        signature = (
            tuple(final.get("inventory_ids", ())),
            tuple(final.get("inventory_counts", ())),
            tuple(final.get("cursor", ())),
            tuple(tuple(row) for row in final.get("craft_grid", ())),
            tuple(final.get("craft_result", ())),
        )
        cursor = final.get("cursor", ())
        item_family = set(item for item, count in zip(
            final.get("inventory_ids", ()),
            final.get("inventory_counts", ())) if item and count)
        if len(cursor) > 1 and cursor[0] and cursor[1]:
            item_family.add(cursor[0])
        item_family.update(row[0] for row in final.get("craft_grid", ())
                           if len(row) > 1 and row[0] and row[1])
        result = final.get("craft_result", ())
        if len(result) > 1 and result[0] and result[1]:
            item_family.add(result[0])
        family = tuple(sorted(item_family))
        occupied = (sum(final.get("inventory_counts", ())) > 0 or
                    (len(cursor) > 1 and cursor[1] > 0) or
                    any(len(row) > 1 and row[1] > 0
                        for row in final.get("craft_grid", ())))
        stage = (3 if len(result) > 1 and result[1] > 0 else
                 2 if any(len(row) > 1 and row[1] > 0
                          for row in final.get("craft_grid", ())) else
                 1 if len(cursor) > 1 and cursor[1] > 0 else 0)
        return occupied, family, signature, stage

    # Cover different item identities before spending slots on count/layout
    # variants of the same item. This keeps wood and dirt hypotheses alive
    # together instead of four dirt counts crowding wood out.
    stage_order = {0: 0, 3: 1, 2: 2, 1: 3}
    incoming_family_parents = {}
    for value in ranked:
        _, family, _, _ = persistent_description(
            (None, None, None, None,
             {"final": value[2].get("end", {})}))
        if family:
            incoming_family_parents.setdefault(family, set()).add(
                value[2]["id"])
    persistent_ranked = sorted(
        ranked,
        key=lambda value: (
            0 if expected_gui is None or bool(value[4].get(
                "final", {}).get("gui")) == bool(expected_gui) else 1,
            len(incoming_family_parents.get(
                persistent_description(value)[1], ())),
            stage_order[persistent_description(value)[3]], value[0]))
    for value in persistent_ranked:
        occupied, family, signature, _ = persistent_description(value)
        if (not occupied or family in persistent_families or
                value in selected):
            continue
        selected.append(value)
        persistent_families.add(family)
        persistent_seen.add(signature)
        parent_counts[value[2]["id"]] = 1
        if (len(persistent_seen) >= persistent_budget or
                len(selected) >= width):
            break
    # Within every retained item identity, keep the resource-rich variant as
    # well as the visually best one. Counts are future action capacity, and a
    # one-pixel HUD digit must not erase a successful extra pickup.
    if len(persistent_seen) < persistent_budget:
        for family in tuple(persistent_families):
            family_values = []
            for value in ranked:
                occupied, actual_family, signature, _ = \
                    persistent_description(value)
                if (occupied and actual_family == family and
                        signature not in persistent_seen and
                        value not in selected):
                    total = sum(value[4].get("final", {}).get(
                        "inventory_counts", ()))
                    family_values.append((total, -value[0], value,
                                          signature))
            if not family_values:
                continue
            _, _, value, signature = max(family_values,
                                          key=lambda entry: entry[:2])
            selected.append(value)
            persistent_seen.add(signature)
            parent_counts[value[2]["id"]] = 1
            if (len(persistent_seen) >= persistent_budget or
                    len(selected) >= width):
                break
    # Cover each manipulation stage per item family. GUI crafting necessarily
    # passes inventory -> cursor -> grid -> result; every intermediate can look
    # worse than idle for a frame but is the only path to a later result item.
    if len(persistent_seen) < persistent_budget:
        for wanted_stage in (3, 2, 1):
            for family in tuple(persistent_families):
                for value in ranked:
                    occupied, actual_family, signature, stage = \
                        persistent_description(value)
                    key = (family, wanted_stage)
                    if (not occupied or actual_family != family or
                            stage != wanted_stage or key in family_stages or
                            signature in persistent_seen or
                            value in selected):
                        continue
                    selected.append(value)
                    persistent_seen.add(signature)
                    family_stages.add(key)
                    parent_counts[value[2]["id"]] = 1
                    break
                if (len(persistent_seen) >= persistent_budget or
                        len(selected) >= width):
                    break
            if (len(persistent_seen) >= persistent_budget or
                    len(selected) >= width):
                break
    if len(persistent_seen) < persistent_budget:
        for value in sorted(
                ranked,
                key=lambda item: (-sum(item[4].get("final", {}).get(
                    "inventory_counts", ())), item[0])):
            occupied, _, signature, _ = persistent_description(value)
            if (not occupied or signature in persistent_seen or
                    value in selected):
                continue
            selected.append(value)
            persistent_seen.add(signature)
            parent_counts[value[2]["id"]] = 1
            if (len(persistent_seen) >= persistent_budget or
                    len(selected) >= width):
                break
    # A tiny HUD item-count change is easy for a world-pixel score to drown
    # out, yet it is decisive evidence that an attack/use branch succeeded.
    # Reserve a bounded part of the beam for genuinely new discrete outcomes.
    novel_outcomes = set()
    novelty_budget = max(1, width // 3)
    for value in ranked:
        _, _, parent, primitive, result, _ = value
        final = result.get("final", {})
        outcome = _discrete_outcome(final)
        if outcome == _discrete_outcome(parent.get("end", {})):
            continue
        signature = (outcome, primitive.attack, primitive.use)
        if signature in novel_outcomes or value in selected:
            continue
        selected.append(value)
        novel_outcomes.add(signature)
        signatures.add((
            parent["id"], primitive.forward, primitive.strafe,
            primitive.sprint, bool(primitive.jump_period),
            primitive.attack, primitive.use,
            primitive.open_inventory, primitive.close_container,
            primitive.inv_slot, primitive.inv_button, primitive.inv_type,
            primitive.inv_sequence,
            round(primitive.yaw_total / 5.0),
            round(primitive.pitch_total / 5.0)))
        parent_counts[parent["id"]] = parent_counts.get(parent["id"], 0) + 1
        if (len(novel_outcomes) >= novelty_budget or
                len(selected) >= width):
            break
    if len(selected) >= width:
        return selected[:width]
    # Breaking a bare log spans several half-second horizons. A pure endpoint
    # pixel beam sees the swing but can discard it one segment before the
    # inventory changes. Retain a few distinct sustained-attack lineages until
    # their ordinary simulation outcome becomes visible.
    def attack_evidence(value):
        final = value[4].get("final", {})
        ray = final.get("ray", [0])
        dig = final.get("dig", [0, 0.0])
        parent_inv = sum(value[2].get("end", {}).get(
            "inventory_counts", ()))
        final_inv = sum(final.get("inventory_counts", ()))
        return (final_inv > parent_inv, bool(dig[0]),
                float(dig[1]) if len(dig) > 1 else 0.0,
                bool(ray[0]) if ray else False,
                value[2].get("motion_counts", [0, 0, 0, 0, 0])[3] +
                horizon_ticks)

    attack_values = sorted(
        (value for value in ranked if value[3].attack),
        key=attack_evidence, reverse=True)
    attack_parents = set()
    for value in attack_values:
        parent = value[2]
        primitive = value[3]
        if parent["id"] in attack_parents or value in selected:
            continue
        selected.append(value)
        attack_parents.add(parent["id"])
        signatures.add((
            parent["id"], primitive.forward, primitive.strafe,
            primitive.sprint, bool(primitive.jump_period),
            primitive.attack, primitive.use,
            primitive.open_inventory, primitive.close_container,
            primitive.inv_slot, primitive.inv_button, primitive.inv_type,
            primitive.inv_sequence,
            round(primitive.yaw_total / 5.0),
            round(primitive.pitch_total / 5.0)))
        parent_counts[parent["id"]] = parent_counts.get(parent["id"], 0) + 1
        attack_budget = max(1, width // (2 if attack_hint else 4))
        if (len(attack_parents) >= attack_budget or
                len(selected) >= width):
            break
    if len(selected) >= width:
        return selected[:width]
    passes = (1, 2, width)
    for parent_limit in passes:
        for value in ranked:
            _, _, parent, primitive, _, _ = value
            signature = (
                parent["id"], primitive.forward, primitive.strafe,
                primitive.sprint, bool(primitive.jump_period),
                primitive.attack, primitive.use,
                primitive.open_inventory, primitive.close_container,
                primitive.inv_slot, primitive.inv_button, primitive.inv_type,
                primitive.inv_sequence,
                round(primitive.yaw_total / 5.0),
                round(primitive.pitch_total / 5.0))
            if signature in signatures:
                continue
            if parent_counts.get(parent["id"], 0) >= parent_limit:
                continue
            selected.append(value)
            signatures.add(signature)
            parent_counts[parent["id"]] = parent_counts.get(parent["id"], 0) + 1
            if len(selected) >= width:
                return selected[:width]
    return selected[:width]


def select_third_person_diverse(ranked, width, horizon_ticks=10):
    """Preserve control ambiguity while the source camera is F5-offset."""
    selected = []
    families = set()
    described = []
    for value in ranked:
        primitive = value[3]
        parent = value[2]
        prior = parent.get("motion_counts", [0, 0, 0, 0, 0])
        ticks = horizon_ticks
        cumulative = (
            prior[0] + primitive.forward * ticks,
            prior[1] + primitive.sprint * ticks,
            prior[2] + bool(primitive.jump_period) * ticks,
            prior[3] + primitive.attack * ticks,
            prior[4] + bool(primitive.use) * ticks,
        )
        strata = {
            "camera_hypothesis": (
                tuple(parent.get("spawn_offset", ())),
                int(parent.get("fov", 70))),
            "fov": int(parent.get("fov", 70)),
            "forward_history": max(-10, min(10, cumulative[0] // 10)),
            "sprint_history": max(0, min(10, cumulative[1] // 10)),
            "attack_history": max(0, min(10, cumulative[3] // 10)),
        }
        # Coarse cumulative bins retain "kept sprinting" and "kept still"
        # lineages, not merely one candidate using each key in the latest
        # half-second.
        history = tuple(max(-4, min(4, round(value / 20)))
                        for value in cumulative)
        final = value[4].get("final", {})
        carried = set(item for item, count in zip(
            final.get("inventory_ids", ()), final.get("inventory_counts", ()))
                      if item and count)
        cursor = final.get("cursor", ())
        if len(cursor) > 1 and cursor[0] and cursor[1]:
            carried.add(cursor[0])
        carried.update(row[0] for row in final.get("craft_grid", ())
                       if len(row) > 1 and row[0] and row[1])
        result = final.get("craft_result", ())
        if len(result) > 1 and result[0] and result[1]:
            carried.add(result[0])
        strata["items"] = tuple(sorted(carried))
        transient_gui_items = (
            bool(final.get("gui")) or
            (len(cursor) > 1 and bool(cursor[1])) or
            any(len(row) > 1 and row[1]
                for row in final.get("craft_grid", ())) or
            (len(result) > 1 and bool(result[1])))
        strata["completed_items"] = (
            () if transient_gui_items else strata["items"])
        strata["items_motion"] = (
            strata["completed_items"],
            bool(primitive.forward or primitive.strafe),
            bool(primitive.sprint))
        family = (history, primitive.forward, primitive.strafe, primitive.sprint,
                  bool(primitive.jump_period), primitive.attack,
                  bool(primitive.use))
        described.append((value, family, strata))

    # Cover each latent FOV before spending slots on motion histories. Then
    # preserve both extremes of each cumulative control axis. Unlike greedy
    # set cover, this cannot spend the whole budget on finely spaced forward
    # bins before it encounters the correct, temporarily unobservable FOV.
    def add_best(axis, wanted, allow_family=False):
        if len(selected) >= width:
            return
        for value, family, strata in described:
            if (strata[axis] != wanted or value in selected or
                    (family in families and not allow_family)):
                continue
            selected.append(value)
            families.add(family)
            return

    # F5 obscures the hand and weakens HUD evidence. Retain each simulated
    # item-family and its motion alternatives before spending the widened beam
    # on camera calibration. Otherwise many old spawn-fuzz hypotheses can
    # consume every slot while the only successful inventory lineage idles.
    item_frequency = {}
    for _, _, strata in described:
        items = strata["completed_items"]
        if items:
            item_frequency[items] = item_frequency.get(items, 0) + 1
    item_order = sorted(item_frequency,
                        key=lambda items: (item_frequency[items], items))
    for items in item_order:
        add_best("completed_items", items, allow_family=True)
        item_motions = sorted({entry[2]["items_motion"]
                               for entry in described
                               if entry[2]["items_motion"][0] == items})
        for items_motion in item_motions:
            add_best("items_motion", items_motion, allow_family=True)
    for hypothesis in sorted(
            {entry[2]["camera_hypothesis"] for entry in described}):
        add_best("camera_hypothesis", hypothesis, allow_family=True)
    for fov in sorted({entry[2]["fov"] for entry in described}):
        add_best("fov", fov)
    for axis in ("forward_history", "sprint_history", "attack_history"):
        observed = {entry[2][axis] for entry in described}
        for wanted in sorted({min(observed), max(observed)}):
            add_best(axis, wanted)
    if len(selected) >= width:
        return selected[:width]
    if len(selected) == width:
        return selected
    for value, family, _ in described:
        if family in families:
            continue
        selected.append(value)
        families.add(family)
        if len(selected) == width:
            return selected
    for value in select_diverse(ranked, width):
        if value not in selected:
            selected.append(value)
        if len(selected) == width:
            break
    return selected


def run(args):
    features = FeatureTape(args.features)
    args.workspace.mkdir(parents=True, exist_ok=True)
    args.checkpoints.mkdir(parents=True, exist_ok=True)
    args.render_scratch = args.workspace / "render_scratch"
    nodes_path = args.workspace / "beam_nodes.jsonl"
    state_path = args.workspace / "beam_state.json"
    progress_path = args.workspace / "progress.jsonl"
    actions_path = args.workspace / "actions.jsonl"

    if args.restart:
        for path in (nodes_path, state_path, progress_path, actions_path):
            if path.exists():
                path.unlink()
    nodes = read_nodes(nodes_path)
    if state_path.exists():
        state = json.loads(state_path.read_text())
        if (args.rewind_generation is not None and
                args.rewind_generation < state["generation"]):
            generation = args.rewind_generation
            rewind = sorted(
                (node for node in nodes.values()
                 if node["generation"] == generation),
                key=lambda node: node["rank"])
            if not rewind:
                raise RuntimeError(f"no nodes at rewind generation {generation}")
            rewind_frame = FeatureTape(args.features).frame_for_tick(
                state["video_start_frame"], generation * args.horizon_ticks)
            rewind_cameras = {
                (tuple(node["spawn_offset"]), node["fov"])
                for node in rewind
            }
            rewind_width = (
                max(args.beam_width, 25 * len(args.fovs)) if generation == 0
                else min(96, max(args.beam_width * 2,
                                 len(rewind_cameras) + 12))
                if features.third_person[rewind_frame]
                else args.beam_width)
            state.update({
                "generation": generation,
                "tick": 1 + generation * args.horizon_ticks,
                "video_frame": rewind_frame,
                "live": [node["id"] for node in rewind[:rewind_width]],
                "status": "searching",
            })
            write_json(state_path, state)
        live = [nodes[node_id] for node_id in state["live"]]
    else:
        frame = gameplay_start(features)
        live, evaluated = bootstrap(args, features, frame, nodes_path)
        nodes.update({node["id"]: node for node in live})
        state = {
            "schema": SCHEMA, "generation": 0, "tick": 1,
            "video_start_frame": frame, "video_frame": frame,
            "live": [node["id"] for node in live], "status": "searching",
        }
        write_json(state_path, state)
        best = min(live, key=lambda node: node["path_score"])
        materialize(nodes, best["id"], actions_path)
        print(json.dumps({
            "bootstrap": True, "evaluated": evaluated,
            "beam": [{"id": node["id"], "score": node["path_score"],
                      "spawn_offset": node["spawn_offset"],
                      "yaw": node["end"]["yaw"]} for node in live],
        }, sort_keys=True), flush=True)

    completed = 0
    while state["video_frame"] < len(features.edges) - 1:
        if args.max_generations and completed >= args.max_generations:
            break
        started = time.monotonic()
        begin = state["video_frame"]
        end = features.frame_for_tick(begin, args.horizon_ticks)
        third_person = bool(features.third_person[end])
        source_gui = bool(features.gui[end])
        gui_transition = source_gui != bool(features.gui[begin])
        camera_transition = third_person != bool(features.third_person[begin])
        alpha = (1.0 if camera_transition or gui_transition else
                 0.25 if third_person else 0.28)
        target_mask = features.masks[end].copy()
        if third_person:
            target_mask[10:29, 22:42] = 0
        per_parent = max(8 if third_person else 4,
                         math.ceil(args.candidates / len(live)))
        work = []
        for parent_index, parent in enumerate(live):
            if (source_gui or bool(features.gui[begin]) or
                    bool(parent["end"].get("gui"))):
                primitives = gui_primitives(
                    parent_index, per_parent,
                    opening=source_gui and not bool(parent["end"].get("gui")),
                    closing=not source_gui and bool(parent["end"].get("gui")),
                    obs=parent.get("end", {}), phase=state["generation"])
            else:
                primitives = primitive_candidates(
                    features, begin, end, per_parent,
                    args.seed ^ (state["generation"] << 10) ^ parent_index)
            if bool(features.attack_hint[end]) and not source_gui:
                aimed = targeted_attack_primitives(
                    parent.get("end", {}), max(2, per_parent // 2))
                primitives = list(dict.fromkeys(aimed + primitives))[:per_parent]
            elif not source_gui and parent.get("end", {}).get("logs"):
                aimed = targeted_attack_primitives(
                    parent.get("end", {}), max(1, per_parent // 4))
                primitives = list(dict.fromkeys(aimed + primitives))[:per_parent]
            for primitive in primitives:
                work.append((parent, primitive))

        def evaluate(value):
            parent, primitive = value
            magma = Magma(args, tuple(parent["spawn_offset"]), parent["slot"],
                          fov=parent["fov"])
            result = magma.rollout(
                primitive.actions(args.horizon_ticks), render=True,
                third_person=third_person)
            endpoint, terms = score_rollout(
                result, features.gray[end], features.edges[end],
                target_mask, primitive, third_person=third_person,
                target_rgb=(features.rgb[end]
                            if features.rgb is not None else None))
            # Exponential history keeps continuity while allowing a branch to
            # recover from one occluded, third-person, or menu frame.
            path_score = ((1.0 - alpha) * parent["path_score"] +
                          alpha * endpoint)
            return path_score, endpoint, parent, primitive, result, terms

        with concurrent.futures.ThreadPoolExecutor(
                max_workers=args.workers) as pool:
            ranked = list(pool.map(evaluate, work))
        ranked.sort(key=lambda value: value[0])
        latent_cameras = {
            (tuple(parent["spawn_offset"]), parent["fov"])
            for parent in live
        }
        # An F5 frame does not observe the first-person camera origin. Keep
        # every unresolved spawn-fuzz/FOV pair plus room for distinct control
        # histories until the source returns to first person.
        active_width = (min(96, max(args.beam_width * 2,
                                    len(latent_cameras) + 12))
                        if third_person else args.beam_width)
        chosen = (select_third_person_diverse(
                      ranked, active_width, args.horizon_ticks)
                  if third_person else select_diverse(
                      ranked, active_width, args.horizon_ticks,
                      bool(features.attack_hint[end]), source_gui))
        generation = state["generation"] + 1

        def commit_winner(indexed):
            rank, value = indexed
            path_score, endpoint, parent, primitive, _, _ = value
            actions = primitive.actions(args.horizon_ticks)
            slot = f"beam-{generation:06d}-{rank:02d}"
            magma = Magma(args, tuple(parent["spawn_offset"]), parent["slot"],
                          fov=parent["fov"])
            committed = magma.rollout(
                actions, save_slot=slot, render=True,
                third_person=third_person)
            if "final" not in committed:
                raise RuntimeError(
                    f"winner replay failed for {parent['id']} -> {slot}: "
                    f"{committed.get('error', committed)}")
            replay_endpoint, terms = score_rollout(
                committed, features.gray[end], features.edges[end],
                target_mask, primitive, third_person=third_person,
                target_rgb=(features.rgb[end]
                            if features.rgb is not None else None))
            if abs(endpoint - replay_endpoint) > 1e-9:
                evaluated_rgb = value[4].get("render_rgb")
                committed_rgb = committed.get("render_rgb")
                def digest(frame):
                    return (hashlib.sha256(frame.tobytes()).hexdigest()[:16]
                            if frame is not None else None)
                changed = (int((evaluated_rgb != committed_rgb).any(axis=2).sum())
                           if evaluated_rgb is not None and
                           committed_rgb is not None else None)
                evaluated_state = state_hash(value[4].get("final", {}))
                committed_state = state_hash(committed.get("final", {}))
                # Parallel software renders can disagree on a handful of
                # downsampled edge pixels even when the complete searchable
                # gameplay state is identical. Commit the replay score in
                # that measured noise floor; never mask state drift or a
                # material frame difference.
                if evaluated_state != committed_state or changed is None or changed > 4:
                    raise RuntimeError(
                        "non-deterministic beam winner replay: "
                        f"generation={generation} rank={rank} parent={parent['id']} "
                        f"primitive={dataclasses.asdict(primitive)} "
                        f"score={endpoint:.17g} replay={replay_endpoint:.17g} "
                        f"rgb={digest(evaluated_rgb)}/{digest(committed_rgb)} "
                        f"changed_pixels={changed} "
                        f"state={evaluated_state}/{committed_state}")
            final = committed["final"]
            prior_motion = parent.get("motion_counts", [0, 0, 0, 0, 0])
            motion_counts = [
                prior_motion[0] + primitive.forward * args.horizon_ticks,
                prior_motion[1] + primitive.sprint * args.horizon_ticks,
                prior_motion[2] + int(bool(primitive.jump_period)) *
                                  args.horizon_ticks,
                prior_motion[3] + primitive.attack * args.horizon_ticks,
                prior_motion[4] + int(bool(primitive.use)) *
                                  args.horizon_ticks,
            ]
            node = {
                "schema": SCHEMA, "kind": "beam_node",
                "id": f"n{generation:06d}-{rank:02d}",
                "parent": parent["id"], "generation": generation,
                "rank": rank, "slot": slot,
                "spawn_offset": parent["spawn_offset"], "fov": parent["fov"],
                "actions": actions, "motion_counts": motion_counts,
                "primitive": dataclasses.asdict(primitive),
                "endpoint_score": replay_endpoint,
                "path_score": ((1.0 - alpha) * parent["path_score"] +
                               alpha * replay_endpoint),
                "score_terms": terms, "state_hash": state_hash(final),
                "end": {key: final.get(key) for key in
                        ("x", "y", "z", "yaw", "pitch", "dimension",
                         "health", "food", "dead", "container",
                         "inventory_ids", "inventory_counts", "ray", "dig",
                         "logs", "coal", "blocks", "items", "gui", "cursor",
                         "craft_grid", "craft_result")},
            }
            return node

        # The selected slots are disjoint native checkpoint destinations.
        # Replay-verify them concurrently just like candidate evaluation; the
        # ordered map keeps stable ranks and deterministic ledger output.
        with concurrent.futures.ThreadPoolExecutor(
                max_workers=args.workers) as pool:
            children = list(pool.map(commit_winner, enumerate(chosen)))
        append_jsonl(nodes_path, children)
        nodes.update({node["id"]: node for node in children})
        live = children
        state.update({
            "generation": generation,
            "tick": state["tick"] + args.horizon_ticks,
            "video_frame": end, "live": [node["id"] for node in live],
            "status": "searching",
        })
        write_json(state_path, state)
        best = min(live, key=lambda node: node["path_score"])
        tape_ticks = materialize(nodes, best["id"], actions_path)
        if tape_ticks != state["tick"]:
            raise RuntimeError(f"materialized {tape_ticks} != state tick "
                               f"{state['tick']}")
        pruned = prune_checkpoints(
            args.checkpoints, generation, (node["slot"] for node in live),
            getattr(args, "checkpoint_history", 3))
        progress = {
            "generation": generation, "tick": state["tick"],
            "video_s": end / features.fps,
            "source_pct": 100.0 * end / len(features.edges),
            "evaluated": len(ranked),
            "rollouts_per_s": len(ranked) /
                              max(time.monotonic() - started, 1e-9),
            "best": {"id": best["id"], "path_score": best["path_score"],
                     "endpoint_score": best["endpoint_score"],
                     "spawn_offset": best["spawn_offset"],
                     "end": {key: best["end"][key] for key in
                             ("x", "y", "z", "yaw", "pitch", "dimension",
                              "health", "food", "dead")}},
            "beam_scores": [node["path_score"] for node in live],
            "third_person": third_person,
            "camera_transition": camera_transition,
            "gui": source_gui,
            "gui_transition": gui_transition,
            "checkpoints_pruned": pruned,
        }
        append_jsonl(progress_path, [progress])
        print(json.dumps(progress, sort_keys=True), flush=True)
        completed += 1
    if state["video_frame"] >= len(features.edges) - 1:
        state["status"] = "source_exhausted"
        write_json(state_path, state)
    return state


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--features", type=pathlib.Path, required=True)
    parser.add_argument("--workspace", type=pathlib.Path, required=True)
    parser.add_argument("--checkpoints", type=pathlib.Path, required=True)
    parser.add_argument("--game", type=pathlib.Path,
                        default=pathlib.Path("magma/magma_game"))
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--spawn-offset-x", type=int, default=-7)
    parser.add_argument("--spawn-offset-z", type=int, default=-10)
    parser.add_argument("--view-distance", type=int, default=2)
    parser.add_argument("--fovs", type=float, nargs="+",
                        default=[70.0, 90.0, 110.0])
    parser.add_argument("--workers", type=int, default=32)
    parser.add_argument("--candidates", type=int, default=192)
    parser.add_argument("--beam-width", type=int, default=8)
    parser.add_argument("--horizon-ticks", type=int, default=10)
    parser.add_argument("--checkpoint-history", type=int, default=3,
                        help="native checkpoint generations to retain; "
                             "0 keeps all")
    parser.add_argument("--bootstrap-candidates", type=int, default=0,
                        help="0 means the exhaustive fuzz/yaw/pitch grid")
    parser.add_argument("--bootstrap-yaw-step", type=int, default=30)
    parser.add_argument("--max-generations", type=int, default=0)
    parser.add_argument("--rewind-generation", type=int)
    parser.add_argument("--restart", action="store_true")
    args = parser.parse_args()
    args.game = args.game.resolve()
    args.features = args.features.resolve()
    args.workspace = args.workspace.resolve()
    args.checkpoints = args.checkpoints.resolve()
    if (args.workers < 1 or args.candidates < 1 or args.beam_width < 1 or
            args.horizon_ticks < 1 or args.bootstrap_candidates < 0 or
            args.checkpoint_history < 0 or
            args.bootstrap_yaw_step < 1 or 360 % args.bootstrap_yaw_step or
            any(value < 30 or value > 110 for value in args.fovs)):
        parser.error("invalid positive search dimensions")
    if args.rewind_generation is not None and args.rewind_generation < 0:
        parser.error("--rewind-generation must be non-negative")
    print(json.dumps(run(args), sort_keys=True))


if __name__ == "__main__":
    main()
