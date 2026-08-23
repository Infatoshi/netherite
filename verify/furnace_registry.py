#!/usr/bin/env python3
"""Derive furnace recipes, fuel times, item ids, and food stats from 1.11.2 oracle-src.

Parses:
  java/oracle-src/net/minecraft/item/Item.java            registerItem / registerItemBlock
  java/oracle-src/net/minecraft/block/Block.java          registerBlock
  java/oracle-src/net/minecraft/init/Items.java           field -> registry name
  java/oracle-src/net/minecraft/init/Blocks.java          field -> registry name
  java/oracle-src/net/minecraft/item/crafting/FurnaceRecipes.java
  java/oracle-src/net/minecraft/tileentity/TileEntityFurnace.java  getItemBurnTime
  java/oracle-src/net/minecraft/item/ItemFood.java        + ItemSoup / ItemAppleGold /
                                                          ItemSeedFood / ItemChorusFruit /
                                                          ItemFishFood

Run from repo root (or via make):
  uv run --no-project python verify/furnace_registry.py
  uv run --no-project python verify/furnace_registry.py --check DUMP.json
  uv run --no-project python verify/furnace_registry.py --c-header > out/verify/furnace_registry_expect.h

DUMP.json is written by magma/tests/test_furnace_registry.c --dump.
Never hand-edit the derived table.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ORACLE = ROOT / "java" / "oracle-src" / "net" / "minecraft"
WILDCARD = 32767


def _read(rel: str) -> str:
    p = ORACLE / rel
    if not p.is_file():
        raise SystemExit(f"MISSING JAVA {p} (rsync java/oracle-src from ~/dev/netherite)")
    return p.read_text(encoding="utf-8", errors="replace")


def parse_item_ids(src: str) -> dict[str, dict]:
    """registerItem(id, \"name\", ...) and registerItemBlock via Block.getIdFromBlock.

    Item.java:1726-1728 registerItem(int, String, Item).
    Item-block ids equal the block id (Item.java:1722).
    """
    out: dict[str, dict] = {}
    for m in re.finditer(
        r'registerItem\(\s*(\d+)\s*,\s*"([^"]+)"', src
    ):
        i, name = int(m.group(1)), m.group(2)
        line = src[: m.start()].count("\n") + 1
        out[name] = {"id": i, "line": line, "file": "item/Item.java"}
    return out


def parse_block_ids(src: str) -> dict[str, dict]:
    """registerBlock(id, \"name\", ...) Block.java:2402+."""
    out: dict[str, dict] = {}
    for m in re.finditer(
        r'registerBlock\(\s*(\d+)\s*,\s*"([^"]+)"', src
    ):
        i, name = int(m.group(1)), m.group(2)
        line = src[: m.start()].count("\n") + 1
        out[name] = {"id": i, "line": line, "file": "block/Block.java"}
    return out


def parse_init_fields(src: str, getter: str) -> dict[str, str]:
    """FIELD = getRegisteredX(\"name\") plus casts."""
    out: dict[str, str] = {}
    pat = re.compile(
        rf"(\w+)\s*=\s*(?:\([^)]+\)\s*)?{re.escape(getter)}\(\"([^\"]+)\"\)"
    )
    for m in pat.finditer(src):
        out[m.group(1)] = m.group(2)
    return out


def parse_enum_dye() -> dict[str, int]:
    src = _read("item/EnumDyeColor.java")
    # GREEN(13, 2, ...) second ctor arg is dyeDamage (EnumDyeColor.java:35-52)
    out: dict[str, int] = {}
    for m in re.finditer(
        r"(\w+)\s*\(\s*\d+\s*,\s*(\d+)\s*,\s*\"", src
    ):
        out[m.group(1)] = int(m.group(2))
    return out


def parse_stonebrick_metas() -> dict[str, int]:
    src = _read("block/BlockStoneBrick.java")
    out: dict[str, int] = {}
    for m in re.finditer(
        r"(\w+)\s*\(\s*(\d+)\s*,", src
    ):
        out[m.group(1)] = int(m.group(2))
    return out


def resolve_item(
    token: str,
    items_field: dict[str, str],
    item_ids: dict[str, dict],
    blocks_field: dict[str, str],
    block_ids: dict[str, dict],
) -> tuple[int, str]:
    """Items.FOO / Blocks.FOO / Items.field_191525_da -> (id, cite)."""
    token = token.strip()
    if token.startswith("Items."):
        field = token[len("Items.") :]
        if field not in items_field:
            raise SystemExit(f"unknown Items.{field}")
        name = items_field[field]
        if name not in item_ids:
            raise SystemExit(f"Items.{field} -> {name} not in registerItem")
        rec = item_ids[name]
        return rec["id"], f"Item.java:{rec['line']} {name}"
    if token.startswith("Blocks."):
        field = token[len("Blocks.") :]
        if field not in blocks_field:
            raise SystemExit(f"unknown Blocks.{field}")
        name = blocks_field[field]
        if name not in block_ids:
            raise SystemExit(f"Blocks.{field} -> {name} not in registerBlock")
        rec = block_ids[name]
        return rec["id"], f"Block.java:{rec['line']} {name}"
    raise SystemExit(f"unresolved token {token}")


def _split_top(s: str) -> list[str]:
    parts, buf, depth = [], [], 0
    for ch in s:
        if ch == "(":
            depth += 1
            buf.append(ch)
        elif ch == ")":
            depth -= 1
            buf.append(ch)
        elif ch == "," and depth == 0:
            parts.append("".join(buf).strip())
            buf = []
        else:
            buf.append(ch)
    if buf:
        parts.append("".join(buf).strip())
    return parts


def parse_itemstack(expr: str, dyes: dict[str, int], sb: dict[str, int]) -> tuple[str, int, int]:
    """new ItemStack(X) or new ItemStack(X, count, meta) -> (token, count, meta)."""
    expr = expr.strip()
    m = re.match(r"new ItemStack\((.*)\)\s*$", expr, flags=re.S)
    if not m:
        raise SystemExit(f"not an ItemStack: {expr!r}")
    parts = _split_top(m.group(1))
    tok = parts[0].strip()
    if len(parts) == 1:
        return tok, 1, 0
    if len(parts) == 3:
        return tok, int(parts[1]), eval_meta(parts[2], dyes, sb)
    raise SystemExit(f"ItemStack arity {len(parts)}: {expr!r}")


def parse_recipes(
    src: str,
    items_field: dict[str, str],
    item_ids: dict[str, dict],
    blocks_field: dict[str, str],
    block_ids: dict[str, dict],
    dyes: dict[str, int],
    sb: dict[str, int],
) -> list[dict]:
    """FurnaceRecipes constructor, registration order (FurnaceRecipes.java:31-91)."""
    recipes: list[dict] = []
    start = src.find("private FurnaceRecipes()")
    end = src.find("public void addSmeltingRecipeForBlock")
    ctor = src[start:end]

    def item_id(tok: str) -> tuple[int, str]:
        return resolve_item(tok, items_field, item_ids, blocks_field, block_ids)

    def add(in_item, in_meta, out_item, out_count, out_meta, xp, cite):
        recipes.append(
            {
                "in_item": int(in_item),
                "in_meta": int(in_meta),
                "out_item": int(out_item),
                "out_count": int(out_count),
                "out_meta": int(out_meta),
                "xp": float(xp),
                "cite": cite,
            }
        )

    pos = 0
    while True:
        m = re.search(
            r"this\.(addSmeltingRecipeForBlock|addSmelting|addSmeltingRecipe)\s*\(",
            ctor[pos:],
        )
        if not m:
            break
        abs_pos = pos + m.start()
        line = src[: src.find("private FurnaceRecipes()")].count("\n") + ctor[:abs_pos].count("\n") + 1
        kind = m.group(1)
        # match the call's parenthesized args
        i = pos + m.end() - 1  # at '('
        depth, j = 0, i
        while j < len(ctor):
            if ctor[j] == "(":
                depth += 1
            elif ctor[j] == ")":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        args = _split_top(ctor[i + 1 : j])
        pos = j + 1

        if kind == "addSmeltingRecipeForBlock":
            in_id, in_cite = item_id(args[0])
            out_tok, out_count, out_meta = parse_itemstack(args[1], dyes, sb)
            out_id, out_cite = item_id(out_tok)
            xp = float(args[2].rstrip("F"))
            add(in_id, WILDCARD, out_id, out_count, out_meta, xp,
                f"FurnaceRecipes.java:{line} ({in_cite} -> {out_cite})")
        elif kind == "addSmelting":
            in_id, in_cite = item_id(args[0])
            out_tok, out_count, out_meta = parse_itemstack(args[1], dyes, sb)
            out_id, out_cite = item_id(out_tok)
            xp = float(args[2].rstrip("F"))
            add(in_id, WILDCARD, out_id, out_count, out_meta, xp,
                f"FurnaceRecipes.java:{line} ({in_cite} -> {out_cite})")
        else:
            # FishType loop (FurnaceRecipes.java:55-61): two cookable metas.
            if "getMetadata()" in args[0]:
                if not any(r.get("cite", "").endswith("COD") for r in recipes):
                    fish = item_ids["fish"]["id"]
                    cooked = item_ids["cooked_fish"]["id"]
                    add(fish, 0, cooked, 1, 0, 0.35,
                        "FurnaceRecipes.java:55-61 ItemFishFood.FishType.COD")
                    add(fish, 1, cooked, 1, 1, 0.35,
                        "FurnaceRecipes.java:55-61 ItemFishFood.FishType.SALMON")
                continue
            in_tok, _in_count, in_meta = parse_itemstack(args[0], dyes, sb)
            out_tok, out_count, out_meta = parse_itemstack(args[1], dyes, sb)
            in_id, in_cite = item_id(in_tok)
            out_id, out_cite = item_id(out_tok)
            xp = float(args[2].rstrip("F"))
            add(in_id, in_meta, out_id, out_count, out_meta, xp,
                f"FurnaceRecipes.java:{line} ({in_cite} -> {out_cite})")

    return recipes


def eval_meta(expr: str, dyes: dict[str, int], sb: dict[str, int]) -> int:
    expr = expr.strip()
    if expr.isdigit() or (expr[0] == "-" and expr[1:].isdigit()):
        return int(expr)
    m = re.fullmatch(r"EnumDyeColor\.(\w+)\.getDyeDamage\(\)", expr)
    if m:
        return dyes[m.group(1)]
    m = re.fullmatch(r"BlockStoneBrick\.(\w+)", expr)
    if m:
        # DEFAULT_META / CRACKED_META
        key = m.group(1)
        if key.endswith("_META"):
            key = key[: -len("_META")]
        return sb[key]
    raise SystemExit(f"unresolved meta expr {expr!r}")


WOOD_SUPER_CLASSES = {
    "BlockPlanks",
    "BlockOldLog",
    "BlockNewLog",
    "BlockLog",
    "BlockWoodSlab",
    "BlockWorkbench",
    "BlockChest",
    "BlockBookshelf",
    "BlockNote",
    "BlockJukebox",
    "BlockStandingSign",
    "BlockWallSign",
    "BlockBanner",
    "BlockFenceGate",
    "BlockDaylightDetector",
    "BlockFence",  # only when Material.WOOD (iron fence is BlockPane)
}


def wood_material_block_ids(block_src: str, block_ids: dict[str, dict]) -> list[int]:
    """Block.getDefaultState().getMaterial() == Material.WOOD (TileEntityFurnace.java:354).

    Direct Material.WOOD ctor args on the registerBlock expression, plus classes
    whose constructor always passes Material.WOOD, plus BlockStairs cloned from
    a wood block's default state.
    """
    ids: set[int] = set()
    # Per-line registerBlock
    for m in re.finditer(
        r'registerBlock\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*(.*?)\);',
        block_src,
        flags=re.S,
    ):
        bid = int(m.group(1))
        expr = m.group(3)
        if re.search(r"(?<!SoundType\.)Material\.WOOD", expr):
            ids.add(bid)
            continue
        for cls in WOOD_SUPER_CLASSES:
            if re.search(rf"new {cls}\b", expr):
                if cls == "BlockFence" and "Material.WOOD" not in expr:
                    continue
                ids.add(bid)
                break

    # Named locals: Block block1 = (new BlockPlanks()) ...
    local_is_wood: dict[str, bool] = {}
    for m in re.finditer(
        r"Block (\w+)\s*=\s*\(?\s*new (\w+)\b", block_src
    ):
        var, cls = m.group(1), m.group(2)
        local_is_wood[var] = cls in WOOD_SUPER_CLASSES or cls == "BlockPlanks"
    # registerBlock(id, name, blockN) / new BlockStairs(blockN.getDefaultState())
    for m in re.finditer(
        r'registerBlock\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*(.*?)\);',
        block_src,
        flags=re.S,
    ):
        bid = int(m.group(1))
        expr = re.sub(r"\s+", " ", m.group(3))
        sm = re.search(r"new BlockStairs\(\s*(\w+)\.", expr)
        if sm and local_is_wood.get(sm.group(1)):
            ids.add(bid)
        vm = re.fullmatch(r"(\w+)", expr.strip())
        if vm and local_is_wood.get(vm.group(1)):
            ids.add(bid)

    # Double wooden slab / wooden slab: BlockWoodSlab
    for name in ("double_wooden_slab", "wooden_slab", "planks", "log", "log2"):
        if name in block_ids:
            ids.add(block_ids[name]["id"])
    return sorted(ids)


def parse_food(item_src: str, item_ids: dict[str, dict]) -> list[dict]:
    """ItemFood / ItemSoup / ItemAppleGold / ItemSeedFood / ItemChorusFruit / ItemFishFood."""
    foods: list[dict] = []

    def add(name, hunger, sat, potion_prob=None, extra=None, cite=""):
        rec = item_ids[name]
        row = {
            "name": name,
            "id": rec["id"],
            "hunger": int(hunger),
            "saturation": float(sat),
            "potion_prob": None if potion_prob is None else float(potion_prob),
            "cite": cite or f"Item.java:{rec['line']}",
        }
        if extra:
            row.update(extra)
        foods.append(row)

    # ItemFood(amount, saturation, wolf) plus optional setPotionEffect on the statement.
    for m in re.finditer(
        r'registerItem\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*\(new ItemFood\(\s*(\d+)\s*,\s*([0-9.]+)F\s*,\s*(true|false)\)([^;]*);',
        item_src,
    ):
        name = m.group(2)
        hunger = int(m.group(3))
        sat = float(m.group(4))
        rest = m.group(6) or ""
        line = item_src[: m.start()].count("\n") + 1
        prob = None
        pm = re.search(r"setPotionEffect\([^;]*?,\s*([0-9.]+)F\)", rest)
        if pm:
            prob = float(pm.group(1))
        add(name, hunger, sat, prob, cite=f"Item.java:{line}")

    # ItemFood(amount, wolf) -> saturation 0.6F (ItemFood.java:40-42)
    for m in re.finditer(
        r'registerItem\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*\(new ItemFood\(\s*(\d+)\s*,\s*(true|false)\)',
        item_src,
    ):
        name = m.group(2)
        if any(f["name"] == name for f in foods):
            continue
        line = item_src[: m.start()].count("\n") + 1
        add(name, int(m.group(3)), 0.6, cite=f"Item.java:{line} ItemFood.java:40-42")

    # ItemSoup(heal) -> ItemFood(heal, false) sat 0.6, maxStack 1 (ItemSoup.java:9-12)
    for m in re.finditer(
        r'registerItem\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*\(new ItemSoup\(\s*(\d+)\)',
        item_src,
    ):
        name = m.group(2)
        line = item_src[: m.start()].count("\n") + 1
        add(name, int(m.group(3)), 0.6, extra={"soup": True, "max_stack": 1},
            cite=f"Item.java:{line} ItemSoup.java:9-12")

    # ItemAppleGold(amount, sat, wolf) (Item.java:1563)
    for m in re.finditer(
        r'registerItem\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*\(new ItemAppleGold\(\s*(\d+)\s*,\s*([0-9.]+)F\s*,\s*(true|false)\)',
        item_src,
    ):
        name = m.group(2)
        line = item_src[: m.start()].count("\n") + 1
        add(name, int(m.group(3)), float(m.group(4)), potion_prob=None,
            extra={"golden_apple": True, "potion_override": True},
            cite=f"Item.java:{line} ItemAppleGold.java:15-18,43-61")

    # ItemSeedFood (Item.java:1634-1635)
    for m in re.finditer(
        r'registerItem\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*\(new ItemSeedFood\(\s*(\d+)\s*,\s*([0-9.]+)F',
        item_src,
    ):
        name = m.group(2)
        line = item_src[: m.start()].count("\n") + 1
        add(name, int(m.group(3)), float(m.group(4)),
            cite=f"Item.java:{line} ItemSeedFood.java:17-21")

    # ItemChorusFruit
    for m in re.finditer(
        r'registerItem\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*\(new ItemChorusFruit\(\s*(\d+)\s*,\s*([0-9.]+)F',
        item_src,
    ):
        name = m.group(2)
        line = item_src[: m.start()].count("\n") + 1
        add(name, int(m.group(3)), float(m.group(4)), extra={"chorus": True},
            cite=f"Item.java:{line} ItemChorusFruit.java:12-15")

    # ItemFishFood: per FishType (ItemFishFood.java:77-81)
    # COD 2/0.1 cooked 5/0.6; SALMON 2/0.1 cooked 6/0.8; CLOWN 1/0.1; PUFFER 1/0.1
    fish_id = item_ids["fish"]["id"]
    cooked_id = item_ids["cooked_fish"]["id"]
    foods.append({
        "name": "fish", "id": fish_id, "meta": 0, "hunger": 2, "saturation": 0.1,
        "potion_prob": None,
        "cite": "ItemFishFood.java:77 COD / Item.java:%d" % item_ids["fish"]["line"],
    })
    foods.append({
        "name": "fish", "id": fish_id, "meta": 1, "hunger": 2, "saturation": 0.1,
        "potion_prob": None, "cite": "ItemFishFood.java:78 SALMON",
    })
    foods.append({
        "name": "fish", "id": fish_id, "meta": 2, "hunger": 1, "saturation": 0.1,
        "potion_prob": None, "cite": "ItemFishFood.java:79 CLOWNFISH",
    })
    foods.append({
        "name": "fish", "id": fish_id, "meta": 3, "hunger": 1, "saturation": 0.1,
        "potion_prob": None, "pufferfish": True, "cite": "ItemFishFood.java:80 PUFFERFISH",
    })
    foods.append({
        "name": "cooked_fish", "id": cooked_id, "meta": 0, "hunger": 5, "saturation": 0.6,
        "potion_prob": None, "cite": "ItemFishFood.java:77 COD cooked",
    })
    foods.append({
        "name": "cooked_fish", "id": cooked_id, "meta": 1, "hunger": 6, "saturation": 0.8,
        "potion_prob": None, "cite": "ItemFishFood.java:78 SALMON cooked",
    })
    return foods


def java_burn_time(
    item_id: int,
    *,
    item_ids: dict[str, dict],
    block_ids: dict[str, dict],
    wood_ids: set[int],
    wooden_tools: set[int],
    wooden_swords: set[int],
    wooden_hoes: set[int],
    doors_except_iron: set[int],
    boats: set[int],
) -> int:
    """TileEntityFurnace.getItemBurnTime nested ternary (TileEntityFurnace.java:340-355)."""
    slab = block_ids["wooden_slab"]["id"]
    wool = block_ids["wool"]["id"]
    carpet = block_ids["carpet"]["id"]
    ladder = block_ids["ladder"]["id"]
    wbutton = block_ids["wooden_button"]["id"]
    coal_block = block_ids["coal_block"]["id"]
    stick = item_ids["stick"]["id"]
    bow = item_ids["bow"]["id"]
    rod = item_ids["fishing_rod"]["id"]
    sign = item_ids["sign"]["id"]
    coal = item_ids["coal"]["id"]
    lava = item_ids["lava_bucket"]["id"]
    sapling = block_ids["sapling"]["id"]
    bowl = item_ids["bowl"]["id"]
    blaze = item_ids["blaze_rod"]["id"]
    iron_door = item_ids["iron_door"]["id"]

    if item_id == slab:
        return 150
    if item_id == wool:
        return 100
    if item_id == carpet:
        return 67
    if item_id == ladder:
        return 300
    if item_id == wbutton:
        return 100
    if item_id in wood_ids:
        return 300
    if item_id == coal_block:
        return 16000
    if item_id in wooden_tools:
        return 200
    if item_id in wooden_swords:
        return 200
    if item_id in wooden_hoes:
        return 200
    if item_id == stick:
        return 100
    if item_id == bow or item_id == rod:
        return 300
    if item_id == sign:
        return 200
    if item_id == coal:
        return 1600
    if item_id == lava:
        return 20000
    if item_id == sapling or item_id == bowl:
        return 100
    if item_id == blaze:
        return 2400
    if item_id in doors_except_iron and item_id != iron_door:
        return 200
    if item_id in boats:
        return 400
    return 0


def build_table() -> dict:
    item_src = _read("item/Item.java")
    block_src = _read("block/Block.java")
    items_init = _read("init/Items.java")
    blocks_init = _read("init/Blocks.java")
    recipes_src = _read("item/crafting/FurnaceRecipes.java")

    item_ids = parse_item_ids(item_src)
    # Item-blocks share the block numeric id.
    block_ids = parse_block_ids(block_src)
    for name, rec in block_ids.items():
        item_ids.setdefault(name, {**rec, "file": "block/Block.java (item-block)"})

    items_field = parse_init_fields(items_init, "getRegisteredItem")
    blocks_field = parse_init_fields(blocks_init, "getRegisteredBlock")
    dyes = parse_enum_dye()
    sb = parse_stonebrick_metas()
    recipes = parse_recipes(
        recipes_src, items_field, item_ids, blocks_field, block_ids, dyes, sb
    )
    wood_ids = wood_material_block_ids(block_src, block_ids)
    foods = parse_food(item_src, item_ids)

    wooden_tools = {
        item_ids[n]["id"]
        for n in ("wooden_shovel", "wooden_pickaxe", "wooden_axe")
    }
    wooden_swords = {item_ids["wooden_sword"]["id"]}
    wooden_hoes = {item_ids["wooden_hoe"]["id"]}
    doors_except_iron = {
        item_ids[n]["id"]
        for n in (
            "wooden_door",
            "spruce_door",
            "birch_door",
            "jungle_door",
            "acacia_door",
            "dark_oak_door",
        )
    }
    boats = {
        item_ids[n]["id"]
        for n in (
            "boat",
            "spruce_boat",
            "birch_boat",
            "jungle_boat",
            "acacia_boat",
            "dark_oak_boat",
        )
    }

    fuel_probe_names = [
        "coal", "stick", "lava_bucket", "blaze_rod", "bowl", "sign", "bow",
        "fishing_rod", "sapling", "wooden_slab", "wool", "carpet", "ladder",
        "wooden_button", "coal_block", "planks", "log", "log2", "crafting_table",
        "chest", "fence", "wooden_sword", "wooden_pickaxe", "wooden_hoe",
        "wooden_door", "iron_door", "boat", "spruce_boat", "diamond",
        "iron_ingot", "bucket",
    ]
    fuels = []
    for name in fuel_probe_names:
        rec = item_ids.get(name) or block_ids.get(name)
        if not rec:
            raise SystemExit(f"fuel probe missing {name}")
        burn = java_burn_time(
            rec["id"],
            item_ids=item_ids,
            block_ids=block_ids,
            wood_ids=set(wood_ids),
            wooden_tools=wooden_tools,
            wooden_swords=wooden_swords,
            wooden_hoes=wooden_hoes,
            doors_except_iron=doors_except_iron,
            boats=boats,
        )
        fuels.append({"name": name, "id": rec["id"], "burn": burn,
                      "cite": f"{rec.get('file','?')}:{rec['line']}"})

    empty_bucket = item_ids["bucket"]
    water_bucket = item_ids["water_bucket"]
    lava_bucket = item_ids["lava_bucket"]
    milk_bucket = item_ids["milk_bucket"]

    return {
        "cite": {
            "recipes": "item/crafting/FurnaceRecipes.java:31-91",
            "fuel": "tileentity/TileEntityFurnace.java:340-355",
            "cook": "tileentity/TileEntityFurnace.java:274-277 getCookTime=200",
            "container": "item/Item.java:1566-1569 lava_bucket.setContainerItem(bucket)",
            "bucket_stack": "item/Item.java:1566-1567 empty setMaxStackSize(16); ItemBucket.java:32 filled=1",
            "fill": "item/ItemBucket.java:117-140 fillBucket",
            "food_finish": "item/ItemFood.java:49-70",
            "hotbar": "entity/player/InventoryPlayer.java:162-185",
        },
        "items": {n: item_ids[n] for n in (
            "lava_bucket", "bucket", "water_bucket", "milk_bucket",
            "fish", "cooked_fish", "beef", "cooked_beef", "porkchop",
            "coal", "iron_ingot", "gold_ingot", "blaze_rod", "stick",
            "potato", "baked_potato", "chorus_fruit", "chorus_fruit_popped",
            "iron_nugget", "gold_nugget", "dye", "emerald", "quartz",
            "redstone", "netherbrick", "brick", "clay_ball",
        ) if n in item_ids},
        "blocks": {n: block_ids[n] for n in (
            "iron_ore", "gold_ore", "diamond_ore", "coal_ore", "redstone_ore",
            "lapis_ore", "quartz_ore", "emerald_ore", "sand", "cobblestone",
            "stone", "stonebrick", "clay", "cactus", "log", "log2",
            "netherrack", "sponge", "furnace", "planks",
        ) if n in block_ids},
        "recipes": recipes,
        "wood_material_block_ids": wood_ids,
        "fuels": fuels,
        "foods": foods,
        "buckets": {
            "empty_id": empty_bucket["id"],
            "empty_max": 16,
            "empty_cite": "Item.java:1566",
            "water_id": water_bucket["id"],
            "lava_id": lava_bucket["id"],
            "milk_id": milk_bucket["id"],
            "filled_max": 1,
            "filled_cite": "ItemBucket.java:32 ItemBucketMilk.java:17",
        },
        "cook_ticks": 200,
        "coal_burn": 1600,
        "lava_burn": 20000,
    }


def emit_c_header(table: dict) -> str:
    lines = [
        "/* generated by verify/furnace_registry.py from java/oracle-src; do not hand-edit */",
        "#ifndef FURNACE_REGISTRY_EXPECT_H",
        "#define FURNACE_REGISTRY_EXPECT_H",
        f"#define FRE_NRECIPES {len(table['recipes'])}",
        f"#define FRE_NFUELS {len(table['fuels'])}",
        f"#define FRE_NFOODS {len(table['foods'])}",
        f"#define FRE_NWOOD {len(table['wood_material_block_ids'])}",
        f"#define FRE_COOK_TICKS {table['cook_ticks']}",
        "typedef struct { int in_item, in_meta, out_item, out_count, out_meta; float xp; } FreRecipe;",
        "typedef struct { int id; int burn; } FreFuel;",
        "typedef struct { int id; int meta; int hunger; float saturation; float potion_prob; int has_potion; } FreFood;",
        "static const FreRecipe FRE_RECIPES[FRE_NRECIPES] = {",
    ]
    for r in table["recipes"]:
        lines.append(
            f"    {{{r['in_item']}, {r['in_meta']}, {r['out_item']}, "
            f"{r['out_count']}, {r['out_meta']}, {r['xp']:.6f}f}},"
        )
    lines.append("};")
    lines.append("static const FreFuel FRE_FUELS[FRE_NFUELS] = {")
    for f in table["fuels"]:
        lines.append(f"    {{{f['id']}, {f['burn']}}}, /* {f['name']} */")
    lines.append("};")
    lines.append("static const int FRE_WOOD_IDS[FRE_NWOOD] = {")
    ids = table["wood_material_block_ids"]
    for i in range(0, len(ids), 12):
        chunk = ", ".join(str(x) for x in ids[i : i + 12])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("static const FreFood FRE_FOODS[FRE_NFOODS] = {")
    for f in table["foods"]:
        meta = int(f.get("meta", 0))
        prob = f.get("potion_prob")
        has = 0 if prob is None else 1
        pv = 0.0 if prob is None else float(prob)
        lines.append(
            f"    {{{f['id']}, {meta}, {f['hunger']}, {f['saturation']:.6f}f, {pv:.6f}f, {has}}}, /* {f['name']} */"
        )
    lines.append("};")
    b = table["buckets"]
    lines.append(f"#define FRE_BUCKET {b['empty_id']}")
    lines.append(f"#define FRE_BUCKET_MAX {b['empty_max']}")
    lines.append(f"#define FRE_WATER_BUCKET {b['water_id']}")
    lines.append(f"#define FRE_LAVA_BUCKET {b['lava_id']}")
    lines.append(f"#define FRE_MILK_BUCKET {b['milk_id']}")
    lines.append(f"#define FRE_FILLED_BUCKET_MAX {b['filled_max']}")
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def check_dump(table: dict, dump_path: Path) -> int:
    dump = json.loads(dump_path.read_text())
    fails = 0

    def fail(msg: str) -> None:
        nonlocal fails
        fails += 1
        print(f"FAIL: {msg}", file=sys.stderr)

    crec = dump.get("recipes", [])
    if len(crec) != len(table["recipes"]):
        fail(f"recipe count C={len(crec)} Java={len(table['recipes'])}")
    n = min(len(crec), len(table["recipes"]))
    for i in range(n):
        j, c = table["recipes"][i], crec[i]
        for k in ("in_item", "in_meta", "out_item", "out_count", "out_meta"):
            if int(c[k]) != int(j[k]):
                fail(f"recipe[{i}].{k} C={c[k]} Java={j[k]} {j['cite']}")
        if abs(float(c["xp"]) - float(j["xp"])) > 1e-6:
            fail(f"recipe[{i}].xp C={c['xp']} Java={j['xp']} {j['cite']}")
    for f in table["fuels"]:
        got = None
        for cf in dump.get("fuels", []):
            if int(cf["id"]) == f["id"]:
                got = int(cf["burn"])
                break
        if got is None:
            fail(f"fuel {f['name']} id={f['id']} missing from C dump")
        elif got != f["burn"]:
            fail(f"fuel {f['name']} id={f['id']} C={got} Java={f['burn']} {f['cite']}")
    if dump.get("bucket_empty_max") != table["buckets"]["empty_max"]:
        fail(f"empty bucket max C={dump.get('bucket_empty_max')} Java=16")
    if dump.get("bucket_lava_max") != table["buckets"]["filled_max"]:
        fail(f"lava bucket max C={dump.get('bucket_lava_max')} Java=1")
    if fails:
        print(f"furnace_registry: FAIL ({fails})", file=sys.stderr)
        return 1
    print(
        f"furnace_registry: PASS recipes={len(table['recipes'])} "
        f"fuels={len(table['fuels'])} foods={len(table['foods'])}"
    )
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--c-header", action="store_true")
    ap.add_argument("--check", metavar="DUMP.json")
    args = ap.parse_args()
    table = build_table()
    if args.check:
        return check_dump(table, Path(args.check))
    if args.c_header:
        sys.stdout.write(emit_c_header(table))
        return 0
    if args.json:
        json.dump(table, sys.stdout, indent=2)
        sys.stdout.write("\n")
        return 0
    print(f"recipes {len(table['recipes'])}  fuels {len(table['fuels'])}  "
          f"foods {len(table['foods'])}  wood_blocks {len(table['wood_material_block_ids'])}")
    print("spot ids:",
          f"lava_bucket={table['items']['lava_bucket']['id']}",
          f"fish={table['items']['fish']['id']}",
          f"beef={table['items']['beef']['id']}",
          f"bucket_max={table['buckets']['empty_max']}")
    for r in table["recipes"][:8]:
        print(f"  {r['in_item']}:{r['in_meta']} -> {r['out_item']} x{r['out_count']} "
              f"meta={r['out_meta']} xp={r['xp']}  {r['cite']}")
    print("  ...")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
