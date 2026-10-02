/* Block breaking speed and harvestability, vanilla 1.7.10. See harvest.h for
 * what each function is. The block ids the tool classes name are looked up once
 * by registry name, and the material indices the pickaxe and the sword compare
 * against are looked up in the generated MATERIALS table the same way, so
 * nothing here is a transcribed number. */
#include "harvest.h"

#include <string.h>

/* The block ids the vanilla tool classes name in their block sets and their
 * harvest tests, resolved in harvest_init. */
struct harvest_ids {
    int web, redstone_wire, tripwire, wool;
    int obsidian, diamond_block, diamond_ore, emerald_ore, emerald_block;
    int gold_block, gold_ore, iron_block, iron_ore, lapis_block, lapis_ore;
    int redstone_ore, lit_redstone_ore, snow_layer, snow;
    int material_rock, material_iron, material_anvil, material_wood, material_plants,
        material_vine, material_leaves, material_coral, material_field_151572_C;
};

static struct harvest_ids ids;

static int block_named(const char *name)
{
    return harvest_block_id(name);
}

static int material_named(const char *name)
{
    for (int i = 0; i < (int)(sizeof MATERIALS / sizeof MATERIALS[0]); ++i)
        if (!strcmp(MATERIALS[i].name, name)) return i;
    return -1;
}

/* before main: the same for every environment */
__attribute__((constructor)) static void init(void)
{
    ids.web = block_named("minecraft:web");
    ids.redstone_wire = block_named("minecraft:redstone_wire");
    ids.tripwire = block_named("minecraft:tripwire");
    ids.wool = block_named("minecraft:wool");
    ids.obsidian = block_named("minecraft:obsidian");
    ids.diamond_block = block_named("minecraft:diamond_block");
    ids.diamond_ore = block_named("minecraft:diamond_ore");
    ids.emerald_ore = block_named("minecraft:emerald_ore");
    ids.emerald_block = block_named("minecraft:emerald_block");
    ids.gold_block = block_named("minecraft:gold_block");
    ids.gold_ore = block_named("minecraft:gold_ore");
    ids.iron_block = block_named("minecraft:iron_block");
    ids.iron_ore = block_named("minecraft:iron_ore");
    ids.lapis_block = block_named("minecraft:lapis_block");
    ids.lapis_ore = block_named("minecraft:lapis_ore");
    ids.redstone_ore = block_named("minecraft:redstone_ore");
    ids.lit_redstone_ore = block_named("minecraft:lit_redstone_ore");
    ids.snow_layer = block_named("minecraft:snow_layer");
    ids.snow = block_named("minecraft:snow");
    ids.material_rock = material_named("rock");
    ids.material_iron = material_named("iron");
    ids.material_anvil = material_named("anvil");
    ids.material_wood = material_named("wood");
    ids.material_plants = material_named("plants");
    ids.material_vine = material_named("vine");
    ids.material_leaves = material_named("leaves");
    /* Material.coral is in the ItemSword test but no registered 1.7.10 block
     * uses it, so this is -1 and the comparison never fires. */
    ids.material_coral = material_named("coral");
    ids.material_field_151572_C = material_named("field_151572_C");
}

int harvest_block_id(const char *name)
{
    for (int id = 0; id < (int)(sizeof BLOCKS / sizeof BLOCKS[0]); ++id)
        if (BLOCKS[id].exists && !strcmp(BLOCKS[id].name, name)) return id;
    return -1;
}

float block_hardness(int block_id)
{
    if (block_id < 0 || block_id >= (int)(sizeof BLOCKS / sizeof BLOCKS[0])) return 0.0f;
    return BLOCKS[block_id].hardness;
}

/* ItemPickaxe's Set: the blocks a pickaxe has its efficiency on, under the
 * material test the override also applies. */
static int pickaxe_set(int block_id)
{
    switch (block_id)
    {
        case 4:   /* cobblestone */
        case 43:  /* double_stone_slab */
        case 44:  /* stone_slab */
        case 1:   /* stone */
        case 24:  /* sandstone */
        case 48:  /* mossy_cobblestone */
        case 15:  /* iron_ore */
        case 42:  /* iron_block */
        case 16:  /* coal_ore */
        case 41:  /* gold_block */
        case 14:  /* gold_ore */
        case 56:  /* diamond_ore */
        case 57:  /* diamond_block */
        case 79:  /* ice */
        case 87:  /* netherrack */
        case 21:  /* lapis_ore */
        case 22:  /* lapis_block */
        case 73:  /* redstone_ore */
        case 74:  /* lit_redstone_ore */
        case 66:  /* rail */
        case 28:  /* detector_rail */
        case 27:  /* golden_rail */
        case 157: /* activator_rail */
            return 1;
        default:
            return 0;
    }
}

/* ItemAxe's Set. */
static int axe_set(int block_id)
{
    switch (block_id)
    {
        case 5:   /* planks */
        case 47:  /* bookshelf */
        case 17:  /* log */
        case 162: /* log2 */
        case 54:  /* chest */
        case 86:  /* pumpkin */
        case 91:  /* lit_pumpkin */
            return 1;
        default:
            return 0;
    }
}

/* ItemSpade's Set. */
static int spade_set(int block_id)
{
    switch (block_id)
    {
        case 2:   /* grass */
        case 3:   /* dirt */
        case 12:  /* sand */
        case 13:  /* gravel */
        case 78:  /* snow_layer */
        case 80:  /* snow */
        case 82:  /* clay */
        case 60:  /* farmland */
        case 88:  /* soul_sand */
        case 110: /* mycelium */
            return 1;
        default:
            return 0;
    }
}

/* ItemPickaxe.func_150897_b: which blocks a pickaxe of this item's harvest
 * level can harvest. The block ids are the ones ItemPickaxe names, the rest is
 * by material. */
static int pickaxe_level(int item_id, int block_id)
{
    int level = TOOL_MATERIALS[ITEMS[item_id].tool_material].harvest_level;
    int mat = BLOCKS[block_id].material;

    if (block_id == ids.obsidian) return level == 3;
    if (block_id == ids.diamond_block || block_id == ids.diamond_ore) return level >= 2;
    if (block_id == ids.emerald_ore || block_id == ids.emerald_block) return level >= 2;
    if (block_id == ids.gold_block || block_id == ids.gold_ore) return level >= 2;
    if (block_id == ids.iron_block || block_id == ids.iron_ore) return level >= 1;
    if (block_id == ids.lapis_block || block_id == ids.lapis_ore) return level >= 1;
    if (block_id == ids.redstone_ore || block_id == ids.lit_redstone_ore) return level >= 2;
    if (mat == ids.material_rock || mat == ids.material_iron || mat == ids.material_anvil) return 1;
    return 0;
}

/* ItemTool.func_150893_a: the material's efficiency when the block is in the
 * class's Set, else 1.0. The three subclasses that override it test the block's
 * material instead of the Set, and their names come first. */
float item_str_vs_block(int item_id, int block_id)
{
    if (block_id < 0 || block_id >= (int)(sizeof BLOCKS / sizeof BLOCKS[0])) return 1.0f;
    const struct item_def *it = &ITEMS[item_id];
    const struct block_def *b = &BLOCKS[block_id];
    int mat = b->material;

    const char *cls = it->class_name;

    if (it->kind == ITEM_SWORD)
    {
        if (block_id == ids.web) return 15.0f;
        if (mat == ids.material_plants || mat == ids.material_vine || mat == ids.material_coral
            || mat == ids.material_leaves || mat == ids.material_field_151572_C) return 1.5f;
        return 1.0f;
    }

    /* ItemShears extends Item, not ItemTool, so it is an ITEM_PLAIN in the
     * registry; its two special speeds come before the tool-kind test. */
    if (!strcmp(cls, "ItemShears"))
    {
        if (block_id == ids.web || mat == ids.material_leaves) return 15.0f;
        if (block_id == ids.wool) return 5.0f;
        return 1.0f;
    }

    if (it->kind != ITEM_TOOL) return 1.0f;

    if (!strcmp(cls, "ItemPickaxe"))
    {
        if (mat == ids.material_iron || mat == ids.material_anvil || mat == ids.material_rock) return it->efficiency;
        if (pickaxe_set(block_id)) return it->efficiency;
    }
    else if (!strcmp(cls, "ItemAxe"))
    {
        if (mat == ids.material_wood || mat == ids.material_plants || mat == ids.material_vine) return it->efficiency;
        if (axe_set(block_id)) return it->efficiency;
    }
    else if (!strcmp(cls, "ItemSpade"))
    {
        if (spade_set(block_id)) return it->efficiency;
    }
    return 1.0f;
}

int item_can_harvest(int item_id, int block_id)
{
    if (block_id < 0 || block_id >= (int)(sizeof BLOCKS / sizeof BLOCKS[0])) return 0;
    const struct item_def *it = &ITEMS[item_id];

    const char *cls = it->class_name;

    if (it->kind == ITEM_SWORD) return block_id == ids.web;
    if (!strcmp(cls, "ItemShears")) return block_id == ids.web || block_id == ids.redstone_wire || block_id == ids.tripwire;
    if (it->kind != ITEM_TOOL) return 0;

    if (!strcmp(cls, "ItemPickaxe")) return pickaxe_level(item_id, block_id);
    if (!strcmp(cls, "ItemSpade")) return block_id == ids.snow_layer || block_id == ids.snow;
    return 0;
}

float inventory_str_vs_block(const struct harvest_player *p, int block_id)
{
    if (p->held_item == 0) return 1.0f;
    return item_str_vs_block(p->held_item, block_id);
}

int can_harvest_block(const struct harvest_player *p, int block_id)
{
    if (block_id < 0 || block_id >= (int)(sizeof BLOCKS / sizeof BLOCKS[0])) return 0;
    if (MATERIALS[BLOCKS[block_id].material].tool_not_required) return 1;
    if (p->held_item == 0) return 0;
    return item_can_harvest(p->held_item, block_id);
}

float player_str_vs_block(const struct harvest_player *p, int block_id)
{
    float s = inventory_str_vs_block(p, block_id);

    if (s > 1.0f && p->held_enchant > 0 && p->held_item != 0)
    {
        /* (lvl*lvl + 1). Vanilla also tests !func_150998_b(block) && var3 <= 1.0F
         * before the smaller bonus, which can never hold inside var3 > 1.0F, so
         * the full bonus always lands. */
        float bonus = (float)(p->held_enchant * p->held_enchant + 1);
        s += bonus;
    }

    if (p->haste > 0) s *= 1.0f + (float)p->haste * 0.2f;
    if (p->fatigue > 0) s *= 1.0f - (float)p->fatigue * 0.2f;
    if (p->in_water) s /= 5.0f;
    if (!p->on_ground) s /= 5.0f;
    return s;
}

float player_relative_block_hardness(const struct harvest_player *p, int block_id)
{
    float h = block_hardness(block_id);

    if (h < 0.0f) return 0.0f;
    if (!can_harvest_block(p, block_id)) return player_str_vs_block(p, block_id) / h / 100.0f;
    return player_str_vs_block(p, block_id) / h / 30.0f;
}