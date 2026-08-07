#!/usr/bin/env python3
"""check_jar_models.py - parse vanilla 1.11.2 block models from the MC jar and
assert geometry contracts that pixel-diff cannot see.

Golden source: minecraft-1.11.2.jar assets/minecraft/models/block/*.json
(NOT another hand-port). Fails hard if waterlily is anything but a zero-
thickness horizontal plane, if cross plants are not crosses, etc.

This is the structural half of the "open C, think it's Java" bar. Pair with
test_model_oracle.c (BmBlock table) and test_mesh_models (emitted verts).
"""
from __future__ import annotations

import json
import struct
import sys
import zipfile
from pathlib import Path

_HERE = Path(__file__).resolve().parent
# Prefer local gradle-cache jar; fall back to Forge workspace (anvil path).
JAR_CANDIDATES = [
    Path.home() / ".gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar",
    Path.home() / ".gradle/caches/minecraft/net/minecraft/minecraft_merged/1.11.2/minecraft_merged-1.11.2.jar",
    _HERE.parents[1] / "java/Minecraft/run/gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar",
]

# name -> expectations derived once from jar; re-checked every run against jar.
CONTRACTS = {
    "waterlily": {
        "zero_thickness_y": 0.25,  # from[1] == to[1] == 0.25
        "faces_only": {"up", "down"},
        "no_side_faces": True,
        "ambientocclusion": False,
    },
    "cross": {  # parent used by tallgrass etc - we check waterlily primarily
    },
    "tall_grass": {
        "parent_contains": "cross",
    },
    "fern": {
        "parent_contains": "cross",
    },
    "reeds": {
        "parent_contains": "cross",
    },
    "flower_dandelion": {
        "parent_contains": "cross",
    },
}


def find_jar() -> Path:
    for p in JAR_CANDIDATES:
        if p.is_file():
            return p
    raise SystemExit("FAIL: no minecraft-1.11.2.jar under ~/.gradle/caches/minecraft")


def load_model(z: zipfile.ZipFile, name: str) -> dict:
    path = f"assets/minecraft/models/block/{name}.json"
    try:
        raw = z.read(path)
    except KeyError as e:
        raise SystemExit(f"FAIL: {path} missing from jar") from e
    return json.loads(raw.decode("utf-8"))


def load_json(z: zipfile.ZipFile, path: str) -> dict:
    try:
        return json.loads(z.read(path).decode("utf-8"))
    except KeyError as e:
        raise SystemExit(f"FAIL: {path} missing from jar") from e


def main() -> int:
    jar = find_jar()
    fails = 0
    print(f"jar-models: reading {jar}")

    with zipfile.ZipFile(jar, "r") as z:
        # --- waterlily: the swamp bug that seed-7 walk exposed ---
        m = load_model(z, "waterlily")
        els = m.get("elements") or []
        if len(els) != 1:
            print(f"FAIL: waterlily: expected 1 element, got {len(els)}")
            fails += 1
        else:
            el = els[0]
            fr, to = el["from"], el["to"]
            if fr[1] != to[1] or abs(fr[1] - 0.25) > 1e-6:
                print(f"FAIL: waterlily: want zero-thickness plane at y=0.25, got from={fr} to={to}")
                fails += 1
            else:
                print("ok waterlily: plane at y=0.25")
            faces = set((el.get("faces") or {}).keys())
            if faces != {"up", "down"}:
                print(f"FAIL: waterlily: faces={faces} want {{up, down}} only")
                fails += 1
            else:
                print("ok waterlily: up+down only")
            if m.get("ambientocclusion", True) is not False:
                print("FAIL: waterlily: ambientocclusion should be false")
                fails += 1
            else:
                print("ok waterlily: ambientocclusion false")
            # footprint full XZ
            if fr[0] != 0 or fr[2] != 0 or to[0] != 16 or to[2] != 16:
                print(f"FAIL: waterlily: want full XZ 0..16, got from={fr} to={to}")
                fails += 1

        # --- cross plants: parent chain must include "cross" ---
        for name in ("tall_grass", "fern", "reeds", "flower_dandelion"):
            try:
                bm = load_model(z, name)
            except SystemExit:
                # some names differ slightly across versions
                alt = {
                    "tall_grass": "tallgrass",
                    "flower_dandelion": "dandelion",
                }.get(name)
                if alt:
                    bm = load_model(z, alt)
                    name = alt
                else:
                    raise
            parent = bm.get("parent", "")
            if "cross" not in parent and "cross" not in json.dumps(bm):
                # follow one parent hop
                if parent.startswith("block/"):
                    pm = load_model(z, parent.split("/", 1)[1])
                    parent2 = pm.get("parent", "")
                    if "cross" not in parent and "cross" not in parent2:
                        print(f"FAIL: {name}: parent={parent!r} not a cross model")
                        fails += 1
                        continue
                else:
                    print(f"FAIL: {name}: parent={parent!r} not a cross model")
                    fails += 1
                    continue
            print(f"ok {name}: cross plant")

        # --- double plants: six lower/upper species, with contextual upper type ---
        double_plants = {
            "sunflower": ("double_sunflower_bottom", "double_sunflower_top", False),
            "syringa": ("double_syringa_bottom", "double_syringa_top", False),
            "double_grass": ("double_grass_bottom", "double_grass_top", True),
            "double_fern": ("double_fern_bottom", "double_fern_top", True),
            "double_rose": ("double_rose_bottom", "double_rose_top", False),
            "paeonia": ("double_paeonia_bottom", "double_paeonia_top", False),
        }
        for state_name, (lower_name, upper_name, tinted) in double_plants.items():
            state = load_json(z, f"assets/minecraft/blockstates/{state_name}.json")
            variants = state.get("variants") or {}
            if variants.get("half=lower", {}).get("model") != lower_name \
                    or variants.get("half=upper", {}).get("model") != upper_name:
                print(f"FAIL: {state_name}: lower/upper models {variants}")
                fails += 1
                continue
            for half_name in (lower_name, upper_name):
                model = load_model(z, half_name)
                if half_name == "double_sunflower_top":
                    continue
                want_parent = "block/tinted_cross" if tinted else "block/cross"
                if model.get("parent") != want_parent:
                    print(f"FAIL: {half_name}: parent={model.get('parent')!r} want {want_parent!r}")
                    fails += 1
                    continue
            print(f"ok {state_name}: distinct lower/upper {'tinted ' if tinted else ''}models")

        sunflower = load_model(z, "double_sunflower_top")
        sunflower_els = sunflower.get("elements") or []
        sunflower_ok = (
            sunflower.get("ambientocclusion") is False
            and len(sunflower_els) == 3
            and sunflower_els[0].get("from") == [0.8, 0, 8]
            and sunflower_els[0].get("to") == [15.2, 8, 8]
            and sunflower_els[1].get("from") == [8, 0, 0.8]
            and sunflower_els[1].get("to") == [8, 8, 15.2]
            and sunflower_els[2].get("from") == [9.6, -1, 1]
            and sunflower_els[2].get("to") == [9.6, 15, 15]
            and sunflower_els[2].get("rotation", {}).get("angle") == 22.5
            and set(sunflower_els[2].get("faces") or {}) == {"west", "east"}
        )
        if not sunflower_ok:
            print("FAIL: double_sunflower_top: stem/head geometry contract")
            fails += 1
        else:
            print("ok double_sunflower_top: half-height stem + tilted two-sided head")

        # --- fire: supported-from-below multipart + dual animated layers ---
        fire_state = load_json(z, "assets/minecraft/blockstates/fire.json")
        multipart = fire_state.get("multipart") or []
        apply_counts = [len(part.get("apply") or []) for part in multipart]
        if len(multipart) != 6 or apply_counts[:5] != [2, 4, 4, 4, 4]:
            print(f"FAIL: fire multipart len/counts={len(multipart)}/{apply_counts}")
            fails += 1
        else:
            print("ok fire: floor + four supported fallback selectors")

        floor = load_model(z, "fire_floor")
        floor_els = floor.get("elements") or []
        floor_ok = (
            floor.get("ambientocclusion") is False
            and len(floor_els) == 4
            and all(el.get("shade") is False for el in floor_els)
            and all(el["to"][1] == 22.4 for el in floor_els)
            and sorted(el["rotation"]["angle"] for el in floor_els)
                == [-22.5, -22.5, 22.5, 22.5]
        )
        if not floor_ok:
            print("FAIL: fire_floor geometry/AO/shade contract")
            fails += 1
        else:
            print("ok fire_floor: four tilted shade=false quads")

        for name, mirrored in (("fire_side", False), ("fire_side_alt", True)):
            side = load_model(z, name)
            els = side.get("elements") or []
            uvs = [] if not els else [face.get("uv") for face in els[0]["faces"].values()]
            want_uv = [16, 0, 0, 16] if mirrored else [0, 0, 16, 16]
            ok = (
                side.get("ambientocclusion") is False
                and len(els) == 1
                and els[0].get("shade") is False
                and set(els[0].get("faces", {})) == {"north", "south"}
                and all(uv == want_uv for uv in uvs)
                and els[0]["to"][1] == 22.4
            )
            if not ok:
                print(f"FAIL: {name}: two-sided plane contract")
                fails += 1
            else:
                print(f"ok {name}: two-sided {'mirrored ' if mirrored else ''}plane")

        for layer in (0, 1):
            base = f"assets/minecraft/textures/blocks/fire_layer_{layer}.png"
            png = z.read(base)
            width, height = struct.unpack(">II", png[16:24])
            meta = load_json(z, base + ".mcmeta")["animation"]
            if (width, height) != (16, 512):
                print(f"FAIL: fire_layer_{layer}: size={(width, height)}")
                fails += 1
            elif layer == 0 and meta.get("frames") != list(range(16, 32)) + list(range(16)):
                print("FAIL: fire_layer_0: unexpected animation order")
                fails += 1
            elif layer == 1 and meta:
                print(f"FAIL: fire_layer_1: expected default frame order, got {meta}")
                fails += 1
            else:
                print(f"ok fire_layer_{layer}: 32 physical frames")

        # --- dimension portals: axis-dependent panel, not a translucent cube ---
        portal_state = load_json(z, "assets/minecraft/blockstates/portal.json")
        variants = portal_state.get("variants") or {}
        if variants.get("axis=x", {}).get("model") != "portal_ns" \
                or variants.get("axis=z", {}).get("model") != "portal_ew":
            print(f"FAIL: portal: unexpected axis variants {variants}")
            fails += 1
        else:
            print("ok portal: metadata axis selects ns/ew model")
        portal_specs = {
            "portal_ns": ([0, 0, 6], [16, 16, 10], {"north", "south"}),
            "portal_ew": ([6, 0, 0], [10, 16, 16], {"east", "west"}),
        }
        for name, (want_from, want_to, want_faces) in portal_specs.items():
            model = load_model(z, name)
            els = model.get("elements") or []
            ok = (
                len(els) == 1
                and els[0].get("from") == want_from
                and els[0].get("to") == want_to
                and set((els[0].get("faces") or {}).keys()) == want_faces
            )
            if not ok:
                print(f"FAIL: {name}: expected 4/16 two-face panel")
                fails += 1
            else:
                print(f"ok {name}: 4/16 two-face panel")

        # --- End portal frame: 13/16 base plus optional eye box ---
        empty = load_model(z, "end_portal_frame_empty")
        filled = load_model(z, "end_portal_frame_filled")
        empty_els = empty.get("elements") or []
        filled_els = filled.get("elements") or []
        base_ok = (
            len(empty_els) == 1
            and empty_els[0].get("from") == [0, 0, 0]
            and empty_els[0].get("to") == [16, 13, 16]
            and all(empty_els[0]["faces"][f].get("uv") == [0, 3, 16, 16]
                    for f in ("north", "south", "west", "east"))
        )
        eye_ok = (
            len(filled_els) == 2
            and filled_els[1].get("from") == [4, 13, 4]
            and filled_els[1].get("to") == [12, 16, 12]
        )
        if not base_ok or not eye_ok:
            print("FAIL: end_portal_frame: base/eye geometry or side UV")
            fails += 1
        else:
            print("ok end_portal_frame: 13/16 base + metadata eye")

        # --- iron bars: exact multipart model family and mipped-cutout planes ---
        bars_state = load_json(z, "assets/minecraft/blockstates/iron_bars.json")
        bars_parts = bars_state.get("multipart") or []
        applied = [part.get("apply", {}).get("model") for part in bars_parts]
        want_applied = [
            "iron_bars_post_ends", "iron_bars_post",
            "iron_bars_cap", "iron_bars_cap", "iron_bars_cap_alt",
            "iron_bars_cap_alt", "iron_bars_side", "iron_bars_side",
            "iron_bars_side_alt", "iron_bars_side_alt",
        ]
        if applied != want_applied:
            print(f"FAIL: iron_bars: unexpected multipart models {applied}")
            fails += 1
        else:
            print("ok iron_bars: post/cap/side multipart selectors")
        bars_contracts = {
            "iron_bars_post_ends": (2, 4),
            "iron_bars_post": (2, 4),
            "iron_bars_cap": (2, 4),
            "iron_bars_cap_alt": (2, 4),
            "iron_bars_side": (4, 7),
            "iron_bars_side_alt": (4, 9),
        }
        for name, (want_elements, want_quads) in bars_contracts.items():
            model = load_model(z, name)
            elements = model.get("elements") or []
            quads = sum(len(el.get("faces") or {}) for el in elements)
            all_planes = all(any(a == b for a, b in zip(el["from"], el["to"]))
                             for el in elements)
            require_plane_elements = name not in {
                "iron_bars_side", "iron_bars_side_alt",
            }
            if (model.get("ambientocclusion") is not False
                    or len(elements) != want_elements or quads != want_quads
                    or (require_plane_elements and not all_planes)):
                print(f"FAIL: {name}: elements/quads/geometry/AO contract")
                fails += 1
            else:
                print(f"ok {name}: {want_quads} model quads")
        north_culls = [face.get("cullface")
                       for el in load_model(z, "iron_bars_side")["elements"]
                       for face in el.get("faces", {}).values()
                       if face.get("cullface")]
        south_culls = [face.get("cullface")
                       for el in load_model(z, "iron_bars_side_alt")["elements"]
                       for face in el.get("faces", {}).values()
                       if face.get("cullface")]
        if north_culls != ["north"] or south_culls != ["south"]:
            print(f"FAIL: iron_bars: side cullfaces {north_culls}/{south_culls}")
            fails += 1
        else:
            print("ok iron_bars: directional connection cullfaces")

        # --- torch: standing/wall plane models, exact rotations, shade=false ---
        torch_state = load_json(z, "assets/minecraft/blockstates/torch.json")
        torch_variants = torch_state.get("variants") or {}
        torch_state_ok = (
            torch_variants.get("facing=up") == {"model": "normal_torch"}
            and torch_variants.get("facing=east") == {"model": "normal_torch_wall"}
            and torch_variants.get("facing=south") == {
                "model": "normal_torch_wall", "y": 90}
            and torch_variants.get("facing=west") == {
                "model": "normal_torch_wall", "y": 180}
            and torch_variants.get("facing=north") == {
                "model": "normal_torch_wall", "y": 270}
        )
        if not torch_state_ok:
            print(f"FAIL: torch: blockstate rotations {torch_variants}")
            fails += 1
        else:
            print("ok torch: standing and four wall rotations")
        for name, wall in (("torch", False), ("torch_wall", True)):
            model = load_model(z, name)
            elements = model.get("elements") or []
            faces = [face for el in elements for face in (el.get("faces") or {}).values()]
            rotations = [el.get("rotation") for el in elements]
            rotation_ok = (all(r is None for r in rotations) if not wall else all(
                r == {"origin": [0, 3.5, 8], "axis": "z", "angle": -22.5}
                for r in rotations))
            if (model.get("ambientocclusion") is not False or len(elements) != 3
                    or len(faces) != 6 or not all(el.get("shade") is False
                                                  for el in elements)
                    or not rotation_ok):
                print(f"FAIL: {name}: planes/AO/shade/rotation contract")
                fails += 1
            else:
                print(f"ok {name}: six shade=false {'tilted ' if wall else ''}quads")

        # --- sanity: stone is a full cube (elements or parent cube_all) ---
        stone = load_model(z, "stone")
        parent = stone.get("parent", "")
        if "cube" not in parent and not stone.get("elements"):
            print(f"FAIL: stone: unexpected model parent={parent!r}")
            fails += 1
        else:
            print(f"ok stone: parent={parent or 'elements'}")

        # --- snow_height2: thin box y 0..2 ---
        sn = load_model(z, "snow_height2")
        els = sn.get("elements") or []
        if not els:
            print("FAIL: snow_height2: no elements")
            fails += 1
        else:
            fr, to = els[0]["from"], els[0]["to"]
            if fr[1] != 0 or to[1] != 2:
                print(f"FAIL: snow_height2: want y 0..2 got from={fr} to={to}")
                fails += 1
            else:
                print("ok snow_height2: y 0..2 thin box")

        # --- cactus: multi-element inset sides ---
        cact = load_model(z, "cactus")
        els = cact.get("elements") or []
        if len(els) < 3:
            print(f"FAIL: cactus: expected multi-element model, got {len(els)}")
            fails += 1
        else:
            print(f"ok cactus: {len(els)} elements")

        # --- vine_1: thin plane ---
        vine = load_model(z, "vine_1")
        els = vine.get("elements") or []
        if not els or els[0]["from"][2] != els[0]["to"][2]:
            print("FAIL: vine_1: expected zero-thickness plane in Z")
            fails += 1
        else:
            print("ok vine_1: thin plane")

    if fails:
        print(f"JAR_MODELS FAIL ({fails})")
        return 1
    print("JAR_MODELS PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
