/* WorldGenDungeons, the population feature that carves a cobblestone room,
 * fills its chests from the dungeon loot table and sets a mob spawner at the
 * room's centre. Ported literally from
 * oracle/src/world/gen/feature/WorldGenDungeons.java, with World.getBlock's
 * material solidity (MATERIALS through block_def.material) and World.isAirBlock
 * as the feature reads them.
 *
 * The chest and the spawner are the first blocks the port places that hold a
 * tile entity: world_set_block makes them (and BlockChest.onBlockAdded writes
 * the chest's facing metadata inside func_150807_a), and generateChestContents
 * fills the chest's 27 slots through the loot module.
 *
 * Java evaluates left to right, so the two chest-attempt draws stay in one
 * statement each and the book stack's enchantment draws happen after the chest
 * block is written and before the contents are drawn. */
#include "features.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blocks.h"
#include "loot.h"
#include "tileentity.h"

/* TileEntityChest's slot count, what generateChestContents fills. */
#define CHEST_SLOTS 27

/* Blocks.cobblestone / mossy_cobblestone. The air material is read off the
 * table's own air block, so no index of MATERIALS is written down. */
enum { BLK_COBBLESTONE = 4, BLK_MOSSY_COBBLESTONE = 48 };

/* The count WeightedRandomChestContent.generateChestContents is called with. */
#define DUNGEON_LOOT_COUNT 8

static int mat_solid(int id)
{
    return MATERIALS[BLOCKS[id & 4095].material].is_solid;
}

/* World.isAirBlock: the block's material is the one Material.air uses. */
static int mat_is_air(int id)
{
    return BLOCKS[id & 4095].material == BLOCKS[BLK_AIR].material;
}

/* WorldGenDungeons.pickMobSpawner. */
static const char *pick_mob_spawner(jrand *r)
{
    int v = jr_int_n(r, 4);

    return v == 0 ? "Skeleton" : (v == 1 ? "Zombie" : (v == 2 ? "Zombie" : "Spider"));
}

static char *own_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);

    if (d != NULL) memcpy(d, s, n);
    return d;
}

int feature_dungeons(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;
    (void)count;

    const struct loot_table *table = loot_table_by_name("dungeon");
    const int var6 = 3;
    int var7 = jr_int_n(r, 2) + 2;
    int var8 = jr_int_n(r, 2) + 2;
    int var9 = 0;
    int var10, var11, var12;

    /* the shape check: a solid floor one below and a solid ceiling var6 above
     * the whole box, and one to five rim cells at y with open air above them */
    for (var10 = x - var7 - 1; var10 <= x + var7 + 1; ++var10)
    {
        for (var11 = y - 1; var11 <= y + var6 + 1; ++var11)
        {
            for (var12 = z - var8 - 1; var12 <= z + var8 + 1; ++var12)
            {
                int id = world_get_block(w, var10, var11, var12);
                int solid = mat_solid(id);

                if (var11 == y - 1 && !solid) return 0;

                if (var11 == y + var6 + 1 && !solid) return 0;

                if ((var10 == x - var7 - 1 || var10 == x + var7 + 1 || var12 == z - var8 - 1 ||
                     var12 == z + var8 + 1) && var11 == y && mat_is_air(id) &&
                    mat_is_air(world_get_block(w, var10, var11 + 1, var12)))
                    ++var9;
            }
        }
    }

    if (var9 < 1 || var9 > 5) return 0;

    /* the room: the shell of cobblestone with a mossy floor, the inside air */
    for (var10 = x - var7 - 1; var10 <= x + var7 + 1; ++var10)
    {
        for (var11 = y + var6; var11 >= y - 1; --var11)
        {
            for (var12 = z - var8 - 1; var12 <= z + var8 + 1; ++var12)
            {
                if (var10 != x - var7 - 1 && var11 != y - 1 && var12 != z - var8 - 1 &&
                    var10 != x + var7 + 1 && var11 != y + var6 + 1 && var12 != z + var8 + 1)
                {
                    world_set_block(w, var10, var11, var12, BLK_AIR, 0, 3);
                }
                else if (var11 >= 0 && !mat_solid(world_get_block(w, var10, var11 - 1, var12)))
                {
                    world_set_block(w, var10, var11, var12, BLK_AIR, 0, 3);
                }
                else if (mat_solid(world_get_block(w, var10, var11, var12)))
                {
                    if (var11 == y - 1 && jr_int_n(r, 4) != 0)
                        world_set_block(w, var10, var11, var12, BLK_MOSSY_COBBLESTONE, 0, 2);
                    else
                        world_set_block(w, var10, var11, var12, BLK_COBBLESTONE, 0, 2);
                }
            }
        }
    }

    /* up to two chests, three tries each, where exactly one side is solid */
    var10 = 0;

    while (var10 < 2)
    {
        var11 = 0;

        for (;;)
        {
            int placed = 0;

            if (var11 < 3)
            {
                var12 = x + jr_int_n(r, var7 * 2 + 1) - var7;

                int var14 = z + jr_int_n(r, var8 * 2 + 1) - var8;

                if (mat_is_air(world_get_block(w, var12, y, var14)))
                {
                    int var15 = 0;

                    if (mat_solid(world_get_block(w, var12 - 1, y, var14))) ++var15;

                    if (mat_solid(world_get_block(w, var12 + 1, y, var14))) ++var15;

                    if (mat_solid(world_get_block(w, var12, y, var14 - 1))) ++var15;

                    if (mat_solid(world_get_block(w, var12, y, var14 + 1))) ++var15;

                    if (var15 == 1)
                    {
                        world_set_block(w, var12, y, var14, BLK_CHEST, 0, 2);

                        /* func_92080_a(field_111189_a, {enchanted_book
                         * .func_92114_b(rand)}): the appended book stack is
                         * built from this feature's Random here, before the
                         * chest's contents are drawn */
                        struct loot_book book;
                        memset(&book, 0, sizeof book);
                        loot_book_build(r, &table->entries[table->n - 1], &book);

                        struct tile_entity *te = world_tile_entity(w, var12, y, var14);

                        if (te != NULL)
                        {
                            for (int i = 0; i < CHEST_SLOTS; ++i)
                            {
                                te->u.chest.slots[i].tag = 0;
                            }

                            loot_generate_contents(r, table, &book,
                                (struct loot_stack *)te->u.chest.slots, CHEST_SLOTS, DUNGEON_LOOT_COUNT);
                        }

                        loot_book_free(&book);
                        placed = 1;
                    }
                }

                if (!placed)
                {
                    ++var11;
                    continue;
                }
            }

            break;
        }

        ++var10;
    }

    world_set_block(w, x, y, z, BLK_MOB_SPAWNER, 0, 2);

    struct tile_entity *spawner = world_tile_entity(w, x, y, z);

    if (spawner != NULL)
    {
        free(spawner->u.spawner.mob_id);
        spawner->u.spawner.mob_id = own_str(pick_mob_spawner(r));
    }
    else
    {
        fprintf(stderr, "Failed to fetch mob spawner entity at (%d, %d, %d)\n", x, y, z);
    }

    return 1;
}