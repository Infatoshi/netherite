#!/usr/bin/env python3
"""Convert the owned 1.11.2 igloo NBT templates to shared C data.

Run: uv run --no-project --with nbtlib python assets/build_igloo_templates.py
"""
import gzip
import io
import os
import zipfile

import nbtlib

from mc_jar import find_jar


HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.abspath(os.path.join(HERE, "../../blaze/core/igloo_templates.h"))
PREFIX = "assets/minecraft/structures/igloo/igloo_"
NAMES = ("top", "middle", "bottom")

FACING_META = {
    "down": 0, "up": 1, "north": 2, "south": 3, "west": 4, "east": 5,
}
HORIZONTAL_META = {"south": 0, "west": 1, "north": 2, "east": 3}
STAIRS_META = {"east": 0, "west": 1, "south": 2, "north": 3}
TRAPDOOR_META = {"north": 0, "south": 1, "west": 2, "east": 3}
TORCH_META = {"east": 1, "west": 2, "south": 3, "north": 4, "up": 5}
COLORS = {
    name: index for index, name in enumerate((
        "white", "orange", "magenta", "light_blue", "yellow", "lime",
        "pink", "gray", "silver", "cyan", "purple", "blue", "brown",
        "green", "red", "black"))
}


def legacy_state(entry):
    name = str(entry["Name"])
    prop = {str(k): str(v) for k, v in entry.get("Properties", {}).items()}
    fixed = {
        "minecraft:air": (0, 0),
        "minecraft:stone": (1, {"stone": 0, "smooth_andesite": 6}.get(
            prop.get("variant", "stone"), -1)),
        "minecraft:web": (30, 0),
        "minecraft:crafting_table": (58, 0),
        "minecraft:ice": (79, 0),
        "minecraft:snow": (80, 0),
        "minecraft:monster_egg": (97, {
            "stone_brick": 2, "mossy_brick": 3, "chiseled_brick": 5,
        }.get(prop.get("variant"), -1)),
        "minecraft:stonebrick": (98, {
            "stonebrick": 0, "mossy_stonebrick": 1,
            "cracked_stonebrick": 2, "chiseled_stonebrick": 3,
        }.get(prop.get("variant"), -1)),
        "minecraft:iron_bars": (101, 0),
        "minecraft:brewing_stand": (117, sum(
            1 << i for i in range(3)
            if prop.get("has_bottle_%d" % i) == "true")),
        "minecraft:cauldron": (118, int(prop.get("level", "0"))),
        "minecraft:wooden_slab": (126,
            (8 if prop.get("half") == "top" else 0)
            | {"spruce": 1}.get(prop.get("variant"), -1)),
        "minecraft:flower_pot": (140, 0),
        "minecraft:structure_block": (255, 0),
    }
    if name in fixed:
        result = fixed[name]
    elif name == "minecraft:furnace":
        result = (61, FACING_META[prop["facing"]])
    elif name == "minecraft:wall_sign":
        result = (68, FACING_META[prop["facing"]])
    elif name == "minecraft:redstone_torch":
        result = (76, TORCH_META[prop["facing"]])
    elif name == "minecraft:torch":
        result = (50, TORCH_META[prop["facing"]])
    elif name == "minecraft:chest":
        result = (54, FACING_META[prop["facing"]])
    elif name == "minecraft:ladder":
        result = (65, FACING_META[prop["facing"]])
    elif name == "minecraft:bed":
        result = (26, HORIZONTAL_META[prop["facing"]]
                  | (8 if prop["part"] == "head" else 0)
                  | (4 if prop.get("occupied") == "true" else 0))
    elif name == "minecraft:trapdoor":
        result = (96, TRAPDOOR_META[prop["facing"]]
                  | (4 if prop["open"] == "true" else 0)
                  | (8 if prop["half"] == "top" else 0))
    elif name == "minecraft:spruce_stairs":
        result = (134, STAIRS_META[prop["facing"]]
                  | (4 if prop["half"] == "top" else 0))
    elif name == "minecraft:carpet":
        result = (171, COLORS[prop["color"]])
    else:
        raise ValueError("unmapped igloo state: %r %r" % (name, prop))
    if result[1] < 0:
        raise ValueError("unmapped igloo metadata: %r %r" % (name, prop))
    return result


def tile_kind(tag):
    if tag is None:
        return 0
    return {
        "Furnace": 1, "Sign": 2, "Chest": 3, "Structure": 4,
        "Cauldron": 5, "FlowerPot": 6,
    }.get(str(tag.get("id", "")), 0)


def c_float(value):
    text = "%.9g" % value
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def main():
    templates = []
    with zipfile.ZipFile(find_jar()) as jar:
        for name in NAMES:
            member = PREFIX + name + ".nbt"
            root = nbtlib.File.parse(io.BytesIO(gzip.decompress(jar.read(member))))
            palette = [legacy_state(state) for state in root["palette"]]
            blocks = []
            markers = []
            for block in root["blocks"]:
                x, y, z = map(int, block["pos"])
                block_id, meta = palette[int(block["state"])]
                tag = block.get("nbt")
                kind = tile_kind(tag)
                blocks.append((x, y, z, block_id, meta, kind))
                if kind == 4 and str(tag.get("mode", "")) == "DATA":
                    marker = {"chest": 1}.get(str(tag.get("metadata", "")), 0)
                    if marker:
                        markers.append((x, y, z, marker))
            entities = []
            for entity in root["entities"]:
                tag = entity["nbt"]
                entity_id = str(tag.get("id", ""))
                kind = {"minecraft:villager": 1,
                        "minecraft:zombie_villager": 2}.get(entity_id, 0)
                if not kind:
                    raise ValueError("unmapped igloo entity: %r" % entity_id)
                x, y, z = map(float, entity["pos"])
                vx, vy, vz = map(float, tag["Motion"])
                yaw, pitch = map(float, tag["Rotation"])
                entities.append((
                    x, y, z, vx, vy, vz, float(tag["Health"]), yaw, pitch,
                    int(tag.get("ConversionTime", -1)), int(tag["Fire"]),
                    int(tag["Air"]), int(tag.get("Profession", -1)), kind,
                    int(tag.get("PersistenceRequired", 0)),
                    int(tag.get("OnGround", 0))))
            templates.append((name, tuple(map(int, root["size"])),
                              blocks, markers, entities))

    with open(OUT_H, "w", encoding="utf-8") as out:
        out.write("/* GENERATED by magma/assets/build_igloo_templates.py. */\n")
        out.write("#ifndef MC_IGLOO_TEMPLATES_H\n#define MC_IGLOO_TEMPLATES_H\n\n")
        out.write('#include "mc.h"\n\n')
        out.write("typedef struct { unsigned char x,y,z,id,meta,tile; } IgBlock;\n")
        out.write("typedef struct { unsigned char x,y,z,kind; } IgMarker;\n")
        out.write("typedef struct {\n"
                  "    double x,y,z,vx,vy,vz;\n"
                  "    float health,yaw,pitch;\n"
                  "    int conversion_time;\n"
                  "    short fire,air;\n"
                  "    signed char profession;\n"
                  "    unsigned char kind,persistence,on_ground;\n"
                  "} IgEntity;\n")
        out.write("\nMC_HD static inline int ig_template_block_count(int index) {\n"
                  "    static const unsigned short counts[3] = {")
        out.write(",".join(str(len(template[2])) for template in templates))
        out.write("};\n    return index >= 0 && index < 3 ? counts[index] : 0;\n}\n\n")
        out.write("MC_HD static inline IgBlock ig_template_block_at("
                  "int template_index, int index) {\n")
        out.write("    static const unsigned short offsets[3] = {")
        offset = 0
        offsets = []
        for template in templates:
            offsets.append(offset)
            offset += len(template[2])
        out.write(",".join(map(str, offsets)))
        out.write("};\n    static const IgBlock blocks[%d] = {\n" % offset)
        for _, _, blocks, _, _ in templates:
            for values in blocks:
                out.write("{%d,%d,%d,%d,%d,%d}," % values)
        out.write("\n    };\n    IgBlock empty = {0,0,0,0,0,0};\n"
                  "    if (template_index < 0 || template_index >= 3"
                  " || index < 0 || index >= ig_template_block_count(template_index))"
                  " return empty;\n"
                  "    return blocks[offsets[template_index] + index];\n}\n\n")

        out.write("MC_HD static inline int ig_template_marker_count(int index) {\n"
                  "    static const unsigned char counts[3] = {")
        out.write(",".join(str(len(template[3])) for template in templates))
        out.write("};\n    return index >= 0 && index < 3 ? counts[index] : 0;\n}\n\n")
        all_markers = [value for template in templates for value in template[3]]
        out.write("MC_HD static inline IgMarker ig_template_marker_at("
                  "int template_index, int index) {\n"
                  "    static const IgMarker markers[%d] = {" % len(all_markers))
        for values in all_markers:
            out.write("{%d,%d,%d,%d}," % values)
        out.write("};\n    IgMarker empty = {0,0,0,0};\n"
                  "    if (template_index != 2 || index < 0"
                  " || index >= ig_template_marker_count(template_index)) return empty;\n"
                  "    return markers[index];\n}\n\n")

        out.write("MC_HD static inline int ig_template_entity_count(int index) {\n"
                  "    static const unsigned char counts[3] = {")
        out.write(",".join(str(len(template[4])) for template in templates))
        out.write("};\n    return index >= 0 && index < 3 ? counts[index] : 0;\n}\n\n")
        all_entities = [value for template in templates for value in template[4]]
        out.write("MC_HD static inline IgEntity ig_template_entity_at("
                  "int template_index, int index) {\n"
                  "    static const IgEntity entities[%d] = {" % len(all_entities))
        for values in all_entities:
            x, y, z, vx, vy, vz, health, yaw, pitch, conversion, fire, air, \
                profession, kind, persistence, on_ground = values
            out.write("{%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,"
                      "%s,%s,%s,%d,%d,%d,%d,%d,%d,%d}," % (
                          x, y, z, vx, vy, vz, c_float(health),
                          c_float(yaw), c_float(pitch),
                          conversion, fire, air, profession, kind,
                          persistence, on_ground))
        out.write("};\n    IgEntity empty = {0};\n"
                  "    if (template_index != 2 || index < 0"
                  " || index >= ig_template_entity_count(template_index)) return empty;\n"
                  "    return entities[index];\n}\n\n#endif\n")
    print("igloo templates: %d blocks, %d markers, %d entities" % (
        sum(len(t[2]) for t in templates), sum(len(t[3]) for t in templates),
        sum(len(t[4]) for t in templates)))
    print("wrote:", OUT_H)


if __name__ == "__main__":
    main()
