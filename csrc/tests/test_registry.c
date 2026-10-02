/* Gate: the generated block and item registries against values read out of the
 * 1.7.10 source by hand, so a generator that drops or misreads a field fails
 * here instead of somewhere in population, interaction or physics. Each value
 * is what the game stores after construction, not what a setter was passed
 * (stone's setResistance(10) stores 30; obsidian's setHardness(50) leaves that). */
#include "../engine/blocks.h"
#include "../engine/items.h"

#include <stdio.h>
#include <string.h>

static int bad;

static void check(int ok, const char *what)
{
    if (!ok)
    {
        printf("FAIL %s\n", what);
        ++bad;
    }
}

static int find_block(const char *name)
{
    for (int id = 0; id < (int)(sizeof BLOCKS / sizeof BLOCKS[0]); ++id)
        if (BLOCKS[id].exists && !strcmp(BLOCKS[id].name, name)) return id;
    return -1;
}

static int find_item(const char *name)
{
    for (int id = 0; id < (int)(sizeof ITEMS / sizeof ITEMS[0]); ++id)
        if (ITEMS[id].exists && !strcmp(ITEMS[id].name, name)) return id;
    return -1;
}

int main(void)
{
    int stone = find_block("minecraft:stone");
    check(stone >= 0, "stone is registered");
    if (stone >= 0)
    {
        check(BLOCKS[stone].hardness == 1.5f, "stone hardness 1.5");
        check(BLOCKS[stone].resistance == 30.0f, "stone resistance 30 (setResistance(10) * 3)");
    }

    int obsidian = find_block("minecraft:obsidian");
    check(obsidian >= 0, "obsidian is registered");
    if (obsidian >= 0) check(BLOCKS[obsidian].hardness == 50.0f, "obsidian hardness 50");

    int ice = find_block("minecraft:ice");
    check(ice >= 0, "ice is registered");
    if (ice >= 0) check(BLOCKS[ice].slipperiness == 0.98f, "ice slipperiness 0.98");

    int water = find_block("minecraft:water");
    check(water >= 0, "water is registered");
    if (water >= 0)
    {
        int m = BLOCKS[water].material;
        check(m >= 0 && m < (int)(sizeof MATERIALS / sizeof MATERIALS[0]), "water's material index is in range");
        if (m >= 0 && m < (int)(sizeof MATERIALS / sizeof MATERIALS[0]))
        {
            check(MATERIALS[m].is_liquid == 1, "water's material is a liquid");
            check(!strcmp(MATERIALS[m].name, "water"), "water's material is named water");
        }
    }

    int torch = find_block("minecraft:torch");
    check(torch >= 0, "torch is registered");
    if (torch >= 0) check(BLOCKS[torch].opaque_cube == 0, "torch is not an opaque cube");

    int pick = find_item("minecraft:iron_pickaxe");
    check(pick >= 0, "iron pickaxe is registered");
    if (pick >= 0)
    {
        check(ITEMS[pick].kind == ITEM_TOOL, "iron pickaxe is a tool");
        check(ITEMS[pick].max_damage == 250, "iron pickaxe max damage 250");
        check(ITEMS[pick].efficiency == 6.0f, "iron pickaxe efficiency 6");
    }

    int bread = find_item("minecraft:bread");
    check(bread >= 0, "bread is registered");
    if (bread >= 0)
    {
        check(ITEMS[bread].kind == ITEM_FOOD, "bread is food");
        check(ITEMS[bread].heal_amount == 5, "bread heals 5");
    }

    int chest = find_item("minecraft:diamond_chestplate");
    check(chest >= 0, "diamond chestplate is registered");
    if (chest >= 0)
    {
        check(ITEMS[chest].kind == ITEM_ARMOR, "diamond chestplate is armor");
        check(ITEMS[chest].damage_reduce == 8, "diamond chestplate reduces 8");
    }

    printf("%s block/item registry\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}