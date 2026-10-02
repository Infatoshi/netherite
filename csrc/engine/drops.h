/* Minecraft 1.7.10 block drops: Block.dropBlockAsItemWithChance,
 * Block.dropBlockAsItem_do, Block.dropXpOnBlockBreak and the per-block
 * getItemDropped / quantityDropped(WithBonus) / damageDropped overrides.
 *
 * One call is one break of one block: the world Random is seeded to the same
 * opseed the oracle probe used and the shared Math.random stream is advanced
 * exactly as the EntityItem and EntityXPOrb constructors do, so the items, the
 * experience and where each entity lands come out bit for bit.
 *
 * Player-dependent paths (silk touch, shears, harvestBlock's player argument,
 * canHarvestBlock) are not here: the probe calls dropBlockAsItemWithChance
 * directly, so no player is in the picture. The blocks whose drop is not a
 * function of block, meta and Random (air, monster_egg, piston_head,
 * piston_extension) answer -1.
 *
 * The fortune levels are the 0..4 the probe sweeps. Java's int shifts and sums
 * here do not wrap inside that range; a level of 32 or more is not covered.
 */
#ifndef NETHERITE_DROPS_H
#define NETHERITE_DROPS_H

#include <stdint.h>

#include "jrand.h"

enum drop_kind { DROP_ITEM = 0, DROP_XP = 1 };

/* One spawned entity, as the probe records it: an item with its stack, or an
 * experience orb with its value. Position, motion and yaw are the raw values,
 * hover the raw hoverStart; the Math.random draws behind all four are spent
 * on the caller's Math.random stream. */
struct drop_ent {
    int kind;
    int item;      /* registry id, or -1 when the game has no id for the drop */
    int damage;
    int count;
    int xp;
    double x, y, z;
    double mx, my, mz;
    float yaw;
    float hover;
};

/* A case makes at most 40 items (lapis ore at fortune 4), so this is slack. */
#define DROPS_MAX_ENTS 256

/* One break: the block, the metadata the world holds for it, the fortune
 * level, the drop location and the world Random's seed. math is the Math.random
 * stream, shared across a whole sweep and advanced by four draws per entity.
 * out_seed, when not NULL, takes the world Random's state after the break, so
 * a caller whose break is not the last draw of its case (the dig state
 * machine) can hand the stream on. */
struct drop_case {
    int block, meta, fortune;
    int x, y, z;
    int64_t opseed;
    jrand *math;
    uint64_t *out_seed;
};

/* Writes the entities a break spawns to out and returns how many. A block that
 * drops nothing (a fluid, glass, a portal) returns 0; a block with no drop
 * path here returns -1. */
int drops_break(const struct drop_case *cs, struct drop_ent *out, int cap);

/* The same break against a world Random the caller owns (the placement paths,
 * where the stream is not reseeded per break). The advanced state is written
 * back, so consecutive breaks share it. */
int drops_break_stream(const struct drop_case *cs, jrand *w, struct drop_ent *out, int cap);

/* BlockSkull.breakBlock: the skull item at the tile entity's type, through
 * dropBlockAsItem_do against the caller's world Random. */
int drops_skull_break_stream(const struct drop_case *cs, jrand *w, int type, struct drop_ent *out, int cap);

/* One item entity for a caller that already knows the item and the damage,
 * through dropBlockAsItem_do against the caller's world Random. */
int drops_item_break_stream(const struct drop_case *cs, jrand *w, int item, int damage, int with_roll,
                            struct drop_ent *out, int cap);

/* The same break over a caller-owned world Random, continuing *wr in place
 * instead of seeding it, with the caller's drop chance (1.0f is the always
 * passes roll the drops probe uses; the roll is drawn either way). */
int drops_break_live(const struct drop_case *cs, jrand *wr, float chance,
                     struct drop_ent *out, int cap);

/* EntityXPOrb.getXPSplit: the largest split of amount, 1 below the first step. */
int drops_xp_split(int amount);

/* The item Item.getItemFromBlock(block) resolves to, or -1 when the block has
 * no item (fire, portal, piston_extension ...) or is not a registered block. */
int drops_block_item(int block);

#endif