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
    "entity.zombie_villager.converted", "block.anvil.destroy",
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


def main():
    index_path = find_index()
    asset_root = index_path.parent.parent
    index = json.loads(index_path.read_text())
    objects = index["objects"]
    sounds_hash = objects["minecraft/sounds.json"]["hash"]
    sounds_path = asset_root / "objects" / sounds_hash[:2] / sounds_hash
    entries = json.loads(sounds_path.read_text())
    variants = []
    spans = [(0, 0, 0)]
    for event in EVENTS[1:]:
        start = len(variants)
        total = 0
        if event is None:
            spans.append((start, 0, 0))
            continue
        for logical, volume, pitch, weight, stream in resolve(entries, event):
            key = f"minecraft/sounds/{logical}.ogg"
            digest = objects.get(key, {}).get("hash")
            if not digest:
                raise KeyError(f"missing indexed asset: {key}")
            variants.append((digest, volume, pitch, weight, stream))
            total += weight
        spans.append((start, len(variants) - start, total))
    if len(spans) != len(EVENTS):
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
        "typedef struct { int start, count, total_weight; } GmSoundAssetSpan;",
        f"#define GM_SOUND_ASSET_VARIANT_COUNT {len(variants)}",
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
    lines.extend([
        "};",
        "static const GmSoundAssetSpan gm_sound_asset_spans[GM_SOUND_COUNT] = {",
    ])
    lines.extend(f"    {{{start}, {count}, {total}}}," for start, count, total in spans)
    lines.extend(["};", "#endif", ""])
    output.write_text("\n".join(lines))
    event_count = sum(event is not None for event in EVENTS[1:])
    print(f"sound manifest: {event_count} events, {len(variants)} variants")


if __name__ == "__main__":
    main()
