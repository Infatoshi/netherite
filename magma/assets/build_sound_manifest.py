#!/usr/bin/env python3
"""Build a compact hash manifest for the represented 1.11 sound events."""

import json
import os
from pathlib import Path
import sys


EVENTS = [
    None,
    "entity.chicken.hurt", "entity.chicken.death",
    "entity.pig.hurt", "entity.pig.death",
    "entity.cow.hurt", "entity.cow.death",
    "entity.sheep.hurt", "entity.sheep.death", "entity.sheep.shear",
    "entity.chicken.egg", "item.bucket.fill", "item.armor.equip_generic",
    "entity.pig.saddle", "entity.lightning.thunder",
    "entity.lightning.impact", "entity.firework.launch",
    "entity.firework.blast", "entity.firework.blast_far",
    "entity.firework.large_blast", "entity.firework.large_blast_far",
    "entity.firework.twinkle", "entity.firework.twinkle_far",
    "block.wood.break", "block.gravel.break", "block.grass.break",
    "block.stone.break", "block.metal.break", "block.glass.break",
    "block.cloth.break", "block.sand.break", "block.snow.break",
    "block.ladder.break", "block.anvil.break", "block.slime.break",
    "block.wood.place", "block.gravel.place", "block.grass.place",
    "block.stone.place", "block.metal.place", "block.glass.place",
    "block.cloth.place", "block.sand.place", "block.snow.place",
    "block.ladder.place", "block.anvil.place", "block.slime.place",
    "block.wood.hit", "block.gravel.hit", "block.grass.hit",
    "block.stone.hit", "block.metal.hit", "block.glass.hit",
    "block.cloth.hit", "block.sand.hit", "block.snow.hit",
    "block.ladder.hit", "block.anvil.hit", "block.slime.hit",
    "entity.player.small_fall", "entity.player.big_fall",
    "block.wood.fall", "block.gravel.fall", "block.grass.fall",
    "block.stone.fall", "block.metal.fall", "block.glass.fall",
    "block.cloth.fall", "block.sand.fall", "block.snow.fall",
    "block.ladder.fall", "block.anvil.fall", "block.slime.fall",
    "block.wood.step", "block.gravel.step", "block.grass.step",
    "block.stone.step", "block.metal.step", "block.glass.step",
    "block.cloth.step", "block.sand.step", "block.snow.step",
    "block.ladder.step", "block.anvil.step", "block.slime.step",
    "entity.player.swim", "entity.player.splash",
    "entity.bobber.splash", "block.dispenser.dispense",
    "block.dispenser.fail", "block.dispenser.launch",
    "entity.endereye.launch", "entity.firework.shoot",
    "block.iron_door.open", "block.wooden_door.open",
    "block.wooden_trapdoor.open", "block.fence_gate.open",
    "block.fire.extinguish", "block.iron_door.close",
    "block.wooden_door.close", "block.wooden_trapdoor.close",
    "block.fence_gate.close", "entity.ghast.warn", "entity.ghast.shoot",
    "entity.enderdragon.shoot", "entity.blaze.shoot",
    "entity.zombie.attack_door_wood", "entity.zombie.attack_iron_door",
    "entity.zombie.break_door_wood", "entity.wither.break_block",
    "entity.wither.shoot", "entity.bat.takeoff", "entity.zombie.infect",
    "entity.zombie_villager.converted", "entity.zombie_villager.cure",
    "block.anvil.destroy",
    "block.anvil.use", "block.anvil.land", "block.portal.travel",
    "block.chorus_flower.grow", "block.chorus_flower.death",
    "block.brewing_stand.brew", "block.iron_trapdoor.close",
    "block.iron_trapdoor.open", "entity.splash_potion.break",
    "entity.enderdragon_fireball.explode", "block.end_gateway.spawn",
    "entity.enderdragon.growl", "entity.villager.yes", "entity.villager.no",
    None,
    "record.13", "record.cat", "record.blocks", "record.chirp",
    "record.far", "record.mall", "record.mellohi", "record.stal",
    "record.strad", "record.ward", "record.11", "record.wait",
    "entity.player.attack.knockback", "entity.player.attack.sweep",
    "entity.player.attack.crit", "entity.player.attack.strong",
    "entity.player.attack.weak", "entity.player.attack.nodamage",
    "entity.zombie.hurt", "entity.zombie.death",
    "entity.zombie_villager.hurt", "entity.zombie_villager.death",
    "entity.zombie_pig.hurt", "entity.zombie_pig.death",
    "entity.skeleton.hurt", "entity.skeleton.death",
    "entity.wither_skeleton.hurt", "entity.wither_skeleton.death",
    "entity.creeper.hurt", "entity.creeper.death",
    "entity.spider.hurt", "entity.spider.death",
    "entity.endermen.hurt", "entity.endermen.death",
    "entity.endermen.teleport",
    "entity.blaze.hurt", "entity.blaze.death",
    "entity.ghast.hurt", "entity.ghast.death",
    "entity.slime.hurt", "entity.slime.death",
    "entity.small_slime.hurt", "entity.small_slime.death",
    "entity.magmacube.hurt", "entity.magmacube.death",
    "entity.small_magmacube.hurt", "entity.small_magmacube.death",
    "entity.silverfish.hurt", "entity.silverfish.death",
    "entity.villager.hurt", "entity.villager.death",
    "block.lava.extinguish",
    "block.note.harp", "block.note.basedrum", "block.note.snare",
    "block.note.hat", "block.note.bass", "entity.witch.ambient",
    "entity.witch.throw",
    "entity.witch.drink", "entity.witch.hurt", "entity.witch.death",
    "entity.hostile.splash",
    "entity.hostile.small_fall", "entity.hostile.big_fall",
    "entity.generic.small_fall", "entity.generic.big_fall",
    "entity.arrow.shoot", "entity.arrow.hit",
    "entity.wolf.hurt", "entity.wolf.death",
    "entity.cat.hurt", "entity.cat.death",
    "entity.shulker.ambient", "entity.shulker.close",
    "entity.shulker.death", "entity.shulker.hurt",
    "entity.shulker.hurt_closed", "entity.shulker.open",
    "entity.shulker.shoot", "entity.shulker.teleport",
    "entity.shulker_bullet.hit", "entity.shulker_bullet.hurt",
    "entity.vindication_illager.hurt", "entity.vindication_illager.death",
    "entity.evocation_illager.hurt", "entity.evocation_illager.death",
    "entity.evocation_illager.prepare_attack",
    "entity.evocation_illager.prepare_summon",
    "entity.evocation_illager.prepare_wololo",
    "entity.evocation_illager.cast_spell",
    "entity.evocation_fangs.attack",
    "entity.vindication_illager.ambient",
    "entity.evocation_illager.ambient",
    "entity.vex.ambient", "entity.vex.charge",
    "entity.vex.hurt", "entity.vex.death",
    "entity.guardian.ambient", "entity.guardian.ambient_land",
    "entity.guardian.attack", "entity.guardian.death",
    "entity.guardian.death_land", "entity.guardian.flop",
    "entity.guardian.hurt", "entity.guardian.hurt_land",
    "entity.elder_guardian.ambient", "entity.elder_guardian.ambient_land",
    "entity.elder_guardian.curse", "entity.elder_guardian.death",
    "entity.elder_guardian.death_land", "entity.elder_guardian.flop",
    "entity.elder_guardian.hurt", "entity.elder_guardian.hurt_land",
    "entity.irongolem.attack", "entity.irongolem.hurt",
    "entity.irongolem.death", "entity.irongolem.step",
    "entity.wither.ambient", "entity.wither.hurt",
    "entity.wither.death", "entity.wither.spawn",
    "entity.horse.ambient", "entity.horse.angry", "entity.horse.armor",
    "entity.horse.breathe", "entity.horse.death", "entity.horse.eat",
    "entity.horse.gallop", "entity.horse.hurt", "entity.horse.jump",
    "entity.horse.land", "entity.horse.saddle", "entity.horse.step",
    "entity.horse.step_wood", "entity.donkey.ambient",
    "entity.donkey.angry", "entity.donkey.chest", "entity.donkey.death",
    "entity.donkey.hurt", "entity.mule.ambient", "entity.mule.chest",
    "entity.mule.death", "entity.mule.hurt",
    "entity.skeleton_horse.ambient", "entity.skeleton_horse.death",
    "entity.skeleton_horse.hurt", "entity.zombie_horse.ambient",
    "entity.zombie_horse.death", "entity.zombie_horse.hurt",
    "entity.llama.ambient", "entity.llama.angry", "entity.llama.chest",
    "entity.llama.death", "entity.llama.eat", "entity.llama.hurt",
    "entity.llama.spit", "entity.llama.step", "entity.llama.swag",
    "entity.armorstand.break", "entity.armorstand.fall",
    "entity.armorstand.hit", "entity.armorstand.place",
    "item.armor.equip_leather", "item.armor.equip_chain",
    "item.armor.equip_iron", "item.armor.equip_gold",
    "item.armor.equip_diamond", "item.armor.equip_elytra",
    "entity.generic.extinguish_fire",
    "entity.painting.break", "entity.painting.place",
    "entity.leashknot.break", "entity.leashknot.place",
    "entity.itemframe.add_item", "entity.itemframe.break",
    "entity.itemframe.place", "entity.itemframe.remove_item",
    "entity.itemframe.rotate_item",
    "entity.egg.throw", "entity.snowball.throw",
    "entity.experience_bottle.throw", "entity.enderpearl.throw",
    "entity.splash_potion.throw", "entity.lingeringpotion.throw",
    "block.enderchest.open", "block.enderchest.close",
    "block.shulker_box.open", "block.shulker_box.close",
    "block.chest.open", "block.chest.close",
    "entity.bat.ambient", "entity.bat.hurt", "entity.bat.death",
    "entity.mooshroom.shear",
    "entity.snowman.ambient", "entity.snowman.hurt",
    "entity.snowman.death", "entity.snowman.shoot",
    "entity.endermite.ambient", "entity.endermite.hurt",
    "entity.endermite.death", "entity.endermite.step",
    "entity.husk.ambient", "entity.husk.hurt",
    "entity.husk.death", "entity.husk.step",
    "entity.stray.ambient", "entity.stray.hurt",
    "entity.stray.death", "entity.stray.step",
    "entity.polar_bear.ambient", "entity.polar_bear.baby_ambient",
    "entity.polar_bear.hurt", "entity.polar_bear.death",
    "entity.polar_bear.step", "entity.polar_bear.warning",
    "entity.rabbit.ambient", "entity.rabbit.attack",
    "entity.rabbit.death", "entity.rabbit.hurt", "entity.rabbit.jump",
    "entity.player.burp", "item.chorus_fruit.teleport",
]

SPECIAL_EVENTS = [
    "ambient.cave", "music.menu", "music.game", "music.creative",
    "music.credits", "music.nether", "music.dragon", "music.end",
]


def find_index() -> Path:
    override = os.environ.get("MC_ASSET_INDEX")
    candidates = []
    if override:
        candidates.append(Path(override))
    root = Path(__file__).resolve().parents[2]
    candidates.extend([
        root / "java/Minecraft/run/gradle/caches/minecraft/assets/indexes/1.11.json",
        root / "java/Minecraft/.gradle/minecraft/assets/indexes/1.11.json",
    ])
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit("Minecraft 1.11 asset index not found; run bootstrap_oracle.sh")


def resolve(entries, name, volume=1.0, pitch=1.0, stream=False, seen=()):
    if name in seen:
        raise ValueError(f"recursive sound event: {name}")
    entry = entries.get(name)
    if not entry:
        raise KeyError(f"missing sound event: {name}")
    result = []
    for value in entry.get("sounds", []):
        if isinstance(value, str):
            value = {"name": value}
        child = value["name"]
        child_volume = volume * float(value.get("volume", 1.0))
        child_pitch = pitch * float(value.get("pitch", 1.0))
        child_stream = stream or bool(value.get("stream", False))
        weight = int(value.get("weight", 1))
        if value.get("type") == "event":
            for row in resolve(
                    entries, child, child_volume, child_pitch,
                    child_stream, seen + (name,)):
                result.append(
                    (row[0], row[1], row[2], row[3] * weight, row[4]))
        else:
            result.append(
                (child, child_volume, child_pitch, weight, child_stream))
    return result


def normalized_event_name(name):
    return name.split(":", 1)[1] if name.startswith("minecraft:") else name


def accessor_seed_key(name):
    value = 0xcbf29ce484222325
    for byte in ("minecraft:" + name).encode("utf-8"):
        value ^= byte
        value = (value * 0x100000001b3) & 0xffffffffffffffff
    return value


def event_closure(entries, roots):
    result = set()
    pending = [name for name in roots if name is not None]
    while pending:
        name = normalized_event_name(pending.pop())
        if name in result:
            continue
        entry = entries.get(name)
        if entry is None:
            raise KeyError(f"missing sound event: {name}")
        result.add(name)
        for value in entry.get("sounds", []):
            if isinstance(value, str):
                continue
            if value.get("type") == "event":
                pending.append(value["name"])
    return sorted(result)


def event_weight(entries, name, cache, visiting=()):
    name = normalized_event_name(name)
    if name in cache:
        return cache[name]
    if name in visiting:
        raise ValueError(f"recursive sound event: {name}")
    total = 0
    for value in entries[name].get("sounds", []):
        if isinstance(value, str):
            total += 1
        elif value.get("type") == "event":
            # SoundHandler's anonymous event accessor delegates getWeight()
            # directly.  The weight/volume/pitch fields on an event reference
            # are deliberately ignored by vanilla 1.11.2.
            total += event_weight(
                entries, value["name"], cache, visiting + (name,))
        else:
            total += int(value.get("weight", 1))
    cache[name] = total
    return total


def main():
    index_path = find_index()
    asset_root = index_path.parent.parent
    index = json.loads(index_path.read_text())
    objects = index["objects"]
    sounds_hash = objects["minecraft/sounds.json"]["hash"]
    sounds_path = asset_root / "objects" / sounds_hash[:2] / sounds_hash
    entries = json.loads(sounds_path.read_text())
    node_names = event_closure(entries, EVENTS[1:] + SPECIAL_EVENTS)
    node_by_name = {name: index for index, name in enumerate(node_names)}
    weight_cache = {}
    variants = []
    edges = []
    nodes = []
    for name in node_names:
        start = len(edges)
        total = event_weight(entries, name, weight_cache)
        for raw in entries[name].get("sounds", []):
            value = {"name": raw} if isinstance(raw, str) else raw
            logical = value["name"]
            if value.get("type") == "event":
                target_name = normalized_event_name(logical)
                edges.append((node_by_name[target_name], -1,
                              weight_cache[target_name]))
                continue
            key = f"minecraft/sounds/{logical}.ogg"
            digest = objects.get(key, {}).get("hash")
            if not digest:
                raise KeyError(f"missing indexed asset: {key}")
            variant = len(variants)
            weight = int(value.get("weight", 1))
            variants.append((
                digest, float(value.get("volume", 1.0)),
                float(value.get("pitch", 1.0)), weight,
                bool(value.get("stream", False))))
            edges.append((-1, variant, weight))
        nodes.append((start, len(edges) - start, total))
    roots = [-1 if event is None else
             node_by_name[normalized_event_name(event)] for event in EVENTS]
    special_roots = [
        node_by_name[normalized_event_name(event)] for event in SPECIAL_EVENTS]
    if len(roots) != len(EVENTS):
        raise AssertionError("sound enum and manifest lengths differ")

    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
        "assets/sound_manifest.h")
    lines = [
        "/* Generated from the owned Minecraft 1.11 asset index. Do not commit. */",
        "#ifndef MAGMA_ASSETS_SOUND_MANIFEST_H",
        "#define MAGMA_ASSETS_SOUND_MANIFEST_H",
        "typedef struct {",
        "    const char *hash; float volume, pitch; int weight, stream;",
        "} GmSoundAssetVariant;",
        "typedef struct { int target, variant, weight; } GmSoundAssetEdge;",
        "typedef struct { int start, count, total_weight; } GmSoundAssetNode;",
        "typedef GmSoundAssetNode GmSoundAssetSpan; /* root compatibility */",
        f"#define GM_SOUND_ASSET_VARIANT_COUNT {len(variants)}",
        f"#define GM_SOUND_ASSET_NODE_COUNT {len(nodes)}",
        "static const GmSoundAssetVariant gm_sound_asset_variants[] = {",
    ]
    def c_float(value):
        text = f"{value:.9g}"
        if "." not in text and "e" not in text.lower():
            text += ".0"
        return text + "F"

    lines.extend(
        f'    {{"{digest}", {c_float(volume)}, {c_float(pitch)}, '
        f'{weight}, {int(stream)}}},'
        for digest, volume, pitch, weight, stream in variants
    )
    lines.extend(["};", "static const GmSoundAssetEdge gm_sound_asset_edges[] = {"])
    lines.extend(f"    {{{target}, {variant}, {weight}}},"
                 for target, variant, weight in edges)
    lines.extend(["};", "static const GmSoundAssetNode gm_sound_asset_nodes[] = {"])
    lines.extend(f"    {{{start}, {count}, {total}}},"
                 for start, count, total in nodes)
    lines.extend(["};", "static const uint64_t gm_sound_asset_seed_keys[] = {"])
    lines.extend(f"    UINT64_C(0x{accessor_seed_key(name):016x}),"
                 for name in node_names)
    lines.extend(["};", "static const int gm_sound_asset_roots[GM_SOUND_COUNT] = {"])
    lines.extend(f"    {root}," for root in roots)
    lines.extend(["};", "static const GmSoundAssetSpan gm_sound_asset_spans[GM_SOUND_COUNT] = {"])
    lines.extend(
        "    {0, 0, 0}," if root < 0 else
        f"    {{{nodes[root][0]}, {nodes[root][1]}, {nodes[root][2]}}},"
        for root in roots)
    lines.extend([
        "};",
        f"#define GM_SPECIAL_SOUND_COUNT {len(special_roots)}",
        "static const int gm_special_sound_asset_roots[GM_SPECIAL_SOUND_COUNT] = {",
    ])
    lines.extend(f"    {root}," for root in special_roots)
    lines.extend(["};", "#endif", ""])
    output.write_text("\n".join(lines))
    if len(sys.argv) > 2:
        spec = Path(sys.argv[2])
        spec_lines = ["# netherite.sound_selector.v1"]
        spec_lines.extend(
            f"node\t{index}\t{name}\t{accessor_seed_key(name):016x}"
            for index, name in enumerate(node_names))
        spec_lines.extend(
            f"variant\t{index}\t{digest}\t{volume:.9g}\t{pitch:.9g}\t"
            f"{weight}\t{int(stream)}"
            for index, (digest, volume, pitch, weight, stream)
            in enumerate(variants))
        spec_lines.extend(
            f"edge\t{node_index}\t{target}\t{variant}\t{weight}"
            for node_index, node in enumerate(nodes)
            for target, variant, weight in
            edges[node[0]:node[0] + node[1]])
        spec_lines.extend(
            f"root\t{sound}\t{root}" for sound, root in enumerate(roots))
        spec.write_text("\n".join(spec_lines) + "\n")
    event_count = sum(event is not None for event in EVENTS[1:])
    print(f"sound manifest: {event_count} roots, {len(nodes)} accessors, "
          f"{len(variants)} variants")


if __name__ == "__main__":
    main()
