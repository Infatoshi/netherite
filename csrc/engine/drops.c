/* Minecraft 1.7.10 block drops, checked against the oracle's DropsProbe dump.
 *
 * The Java this reproduces, and where each piece lives:
 *   Block.dropBlockAsItemWithChance  the count draw, the chance roll, the item
 *   Block.dropBlockAsItem_do         the three nextFloat offsets, the EntityItem
 *   Block.dropXpOnBlockBreak         the split loop, the EntityXPOrb
 *   Block.quantityDropped(WithBonus), getItemDropped, damageDropped
 *                                    the overrides, block id by block id below
 *   EntityItem / EntityXPOrb         the four Math.random draws per entity
 *
 * The decompiled literals in those constructors are not the doubles they look
 * like: 0.20000000298023224D is (double)0.2f and 0.10000000149011612D is
 * (double)0.1f, the values the decompiler prints for the widened float
 * constants. The orb's motionY is the one place the true 0.2D appears. Both
 * matter: using the plain doubles puts the motion one float ulp out.
 *
 * Java evaluates operands left to right and C does not, so every expression
 * that draws more than once from one generator is split into statements in the
 * order the Java source spells it.
 */
#include "drops.h"

#include <string.h>

#include "items.h"

/* (double)0.2f and (double)0.1f, the constants EntityItem and EntityXPOrb use. */
#define D02 0.20000000298023224
#define D01 0.10000000149011612
/* ... except the orb's motionY, where the source has 0.2D. */
#define D02_TRUE 0.2

#define DROP_PI 3.141592653589793

/* The items the drop overrides name, resolved once from the registry. */
enum {
    D_COBBLESTONE, D_DIRT, D_PLANKS, D_SAPLING, D_FLINT, D_COAL, D_DIAMOND,
    D_EMERALD, D_DYE, D_QUARTZ, D_REDSTONE, D_STRING, D_WHEAT_SEEDS, D_WHEAT,
    D_MELON, D_MELON_SEEDS, D_PUMPKIN_SEEDS, D_GLOWSTONE_DUST, D_APPLE, D_BOOK,
    D_CLAY_BALL, D_SNOWBALL, D_REEDS, D_SIGN, D_WOODEN_DOOR, D_IRON_DOOR,
    D_BED, D_BREWING_STAND, D_CAULDRON, D_COMPARATOR, D_REPEATER,
    D_FLOWER_POT, D_SKULL, D_CARROT, D_POTATO, D_POISONOUS_POTATO,
    D_NETHER_WART, D_OBSIDIAN,
    D_COUNT
};

static const char *const DROP_NAMES[D_COUNT] = {
    "minecraft:cobblestone", "minecraft:dirt", "minecraft:planks",
    "minecraft:sapling", "minecraft:flint", "minecraft:coal",
    "minecraft:diamond", "minecraft:emerald", "minecraft:dye",
    "minecraft:quartz", "minecraft:redstone", "minecraft:string",
    "minecraft:wheat_seeds", "minecraft:wheat", "minecraft:melon",
    "minecraft:melon_seeds", "minecraft:pumpkin_seeds",
    "minecraft:glowstone_dust", "minecraft:apple", "minecraft:book",
    "minecraft:clay_ball", "minecraft:snowball", "minecraft:reeds",
    "minecraft:sign", "minecraft:wooden_door", "minecraft:iron_door",
    "minecraft:bed", "minecraft:brewing_stand", "minecraft:cauldron",
    "minecraft:comparator", "minecraft:repeater", "minecraft:flower_pot",
    "minecraft:skull", "minecraft:carrot", "minecraft:potato",
    "minecraft:poisonous_potato", "minecraft:nether_wart",
    "minecraft:obsidian",
};

static int ITEM[D_COUNT];
/* Item.getItemFromBlock's map, block id to item id, -1 when there is none. */
static int BLOCK_ITEM[4096];

static int find_item(const char *name)
{
    for (int i = 0; i < 4096; ++i)
    {
        if (ITEMS[i].exists && strcmp(ITEMS[i].name, name) == 0) return i;
    }

    return -1;
}

/* before main: the same for every environment */
__attribute__((constructor)) static void drops_init(void)
{
    for (int i = 0; i < 4096; ++i) BLOCK_ITEM[i] = -1;

    for (int i = 0; i < 4096; ++i)
    {
        const struct item_def *d = &ITEMS[i];

        if (!d->exists || d->kind != ITEM_BLOCK || d->block_id < 0) continue;
        BLOCK_ITEM[d->block_id] = i;
    }

    for (int i = 0; i < D_COUNT; ++i) ITEM[i] = find_item(DROP_NAMES[i]);

}

int drops_block_item(int block)
{
    if (block < 0 || block >= 4096) return -1;

    return BLOCK_ITEM[block];
}

int drops_xp_split(int amount)
{
    if (amount >= 2477) return 2477;
    if (amount >= 1237) return 1237;
    if (amount >= 617) return 617;
    if (amount >= 307) return 307;
    if (amount >= 149) return 149;
    if (amount >= 73) return 73;
    if (amount >= 37) return 37;
    if (amount >= 17) return 17;
    if (amount >= 7) return 7;
    if (amount >= 3) return 3;
    return 1;
}

struct drop_ctx {
    jrand *math;             /* the shared Math.random stream */
    struct drop_ent *out;
    int n, cap;
    int x, y, z;             /* the block the break is at */
    int meta, fortune;
    float chance;            /* the drop chance the loop rolls against, 1.0f unless live */
    jrand w;                 /* the world Random, seeded to the case's opseed */
};

static struct drop_ent *push(struct drop_ctx *c)
{
    if (c->n >= c->cap || c->n >= DROPS_MAX_ENTS) return 0;

    struct drop_ent *e = &c->out[c->n++];
    e->damage = 0;
    e->count = 1;
    e->xp = 0;
    e->mx = e->my = e->mz = 0.0;
    e->yaw = 0.0f;
    return e;
}

/* Room for one more entity, checked before anything is drawn so a full buffer
 * leaves the generators where it found them. */
static int room(struct drop_ctx *c)
{
    return c->n < c->cap && c->n < DROPS_MAX_ENTS;
}

/* Block.dropBlockAsItem_do: the three world-Random offsets, then the
 * EntityItem constructor's Math.random draws (hoverStart, rotationYaw,
 * motionX, motionZ). */
static void emit_item(struct drop_ctx *c, int item, int damage)
{
    float f = 0.7f;
    double dx, dy, dz;
    struct drop_ent *e;

    if (!room(c)) return;

    dx = (double)(jr_float(&c->w) * f) + (double)(1.0f - f) * 0.5;
    dy = (double)(jr_float(&c->w) * f) + (double)(1.0f - f) * 0.5;
    dz = (double)(jr_float(&c->w) * f) + (double)(1.0f - f) * 0.5;
    e = push(c);

    e->hover = (float)(jr_double(c->math) * DROP_PI * 2.0);   /* hoverStart */

    e->kind = DROP_ITEM;
    e->item = item;
    e->damage = damage;
    e->count = 1;
    e->x = (double)c->x + dx;
    e->y = (double)c->y + dy;
    e->z = (double)c->z + dz;
    e->yaw = (float)(jr_double(c->math) * 360.0);
    e->mx = (double)(float)(jr_double(c->math) * D02 - D01);
    e->my = D02;
    e->mz = (double)(float)(jr_double(c->math) * D02 - D01);
}

/* Block.dropXpOnBlockBreak: getXPSplit's loop at 0.5 above every axis, then
 * EntityXPOrb's Math.random draws (rotationYaw, motionX, motionY, motionZ). */
static void emit_xp(struct drop_ctx *c, int amount)
{
    while (amount > 0)
    {
        int v = drops_xp_split(amount);
        struct drop_ent *e;

        amount -= v;

        if (!room(c)) return;

        e = push(c);
        e->kind = DROP_XP;
        e->item = -1;
        e->count = 0;
        e->xp = v;
        e->x = (double)c->x + 0.5;
        e->y = (double)c->y + 0.5;
        e->z = (double)c->z + 0.5;
        e->yaw = (float)(jr_double(c->math) * 360.0);
        e->mx = (double)((float)(jr_double(c->math) * D02 - D01) * 2.0f);
        e->my = (double)((float)(jr_double(c->math) * D02_TRUE) * 2.0f);
        e->mz = (double)((float)(jr_double(c->math) * D02 - D01) * 2.0f);
    }
}

/* Block.dropBlockAsItemWithChance's loop for a block whose getItemDropped does
 * not draw: quantityDroppedWithBonus items, each an ItemStack(item, 1, damage)
 * behind the chance roll (1.0, so the roll always passes and always draws). */
static void plain(struct drop_ctx *c, int count, int item, int damage)
{
    for (int i = 0; i < count; ++i)
    {
        float roll = jr_float(&c->w);

        if (c->chance >= 1.0f || roll <= c->chance)
        {
            if (item >= 0) emit_item(c, item, damage);
        }
    }
}

static void base(struct drop_ctx *c, int block)
{
    plain(c, 1, drops_block_item(block), 0);
}

/* BlockOre.quantityDroppedWithBonus: when the block drops something other than
 * itself and fortune is on, nextInt(fortune + 2) comes first and then
 * quantityDropped, whose own draw (lapis only) follows it. */
static int ore_count(struct drop_ctx *c, int lapis)
{
    int factor = 1, count;

    if (c->fortune > 0)
    {
        int v = jr_int_n(&c->w, c->fortune + 2) - 1;

        if (v < 0) v = 0;
        factor = v + 1;
    }

    count = lapis ? 4 + jr_int_n(&c->w, 5) : 1;
    return count * factor;
}

/* BlockLeaves.dropBlockAsItemWithChance, with the two subclasses' choices:
 * BlockOldLeaf raises the sapling roll for the jungle meta and drops apples
 * from the oak one; BlockNewLeaf drops apples from the dark oak one and
 * offsets the sapling damage by 4. */
static void leaves(struct drop_ctx *c, int old)
{
    int meta = c->meta, fortune = c->fortune;
    int variant = meta & 3;
    int chance = 20, apple = 200;

    if (old && variant == 3) chance = 40;         /* BlockOldLeaf.func_150123_b */

    if (fortune > 0)
    {
        chance -= 2 << fortune;
        if (chance < 10) chance = 10;
    }

    if (jr_int_n(&c->w, chance) == 0)
    {
        emit_item(c, ITEM[D_SAPLING], old ? variant : variant + 4);
    }

    if (fortune > 0)
    {
        apple -= 10 << fortune;
        if (apple < 40) apple = 40;
    }

    /* func_150124_c: the apple roll is only drawn for that subclass's one
     * meta, so the other three do not spend it. */
    if (variant == (old ? 0 : 1))
    {
        if (jr_int_n(&c->w, apple) == 0) emit_item(c, ITEM[D_APPLE], 0);
    }
}

/* BlockCrops.dropBlockAsItemWithChance: the Block drop at fortune 0, then, for
 * a ripe crop, 3 + fortune rolls of nextInt(15) against the meta, each a seed. */
static void crops(struct drop_ctx *c, int crop_item, int seed_item)
{
    plain(c, 1, crop_item, 0);

    if (c->meta >= 7)
    {
        int n = 3 + c->fortune;

        for (int i = 0; i < n; ++i)
        {
            if (jr_int_n(&c->w, 15) <= c->meta) emit_item(c, seed_item, 0);
        }
    }
}

/* The per-block dispatch, over a ctx whose w and math the caller owns. */
static int drops_dispatch(struct drop_ctx *c, int block)
{
    int meta = c->meta, fortune = c->fortune;

    switch (block)
    {
        case 0:  /* BlockAir.dropBlockAsItemWithChance is empty */
        case 34: /* BlockPistonMoving / BlockPistonExtension need a piston TE */
        case 36:
        case 97: /* BlockSilverfish spawns a silverfish and drops nothing */
            return -1;

        /* Blocks with no drop: quantityDropped 0, or an empty override. The
         * count is drawn before the loop, so these spend no world draw. */
        case 8: case 9: case 10: case 11:      /* BlockLiquid: water, lava */
        case 20:                               /* BlockGlass */
        case 51:                               /* BlockFire */
        case 78:                               /* BlockSnow layer */
        case 79:                               /* BlockIce */
        case 90:                               /* BlockPortal */
        case 92:                               /* BlockCake */
        case 95:                               /* BlockStainedGlass */
        case 106:                              /* BlockVine */
        case 119:                              /* BlockEndPortal */
        case 137:                              /* BlockCommandBlock */
        case 144:                              /* BlockSkull's override is empty */
        case 174:                              /* BlockPackedIce */
            return c->n;

        case 1: plain(c, 1, drops_block_item(4), 0); break;         /* BlockStone -> cobblestone */
        case 2: plain(c, 1, ITEM[D_DIRT], 0); break;                /* BlockGrass -> dirt */
        case 110: plain(c, 1, ITEM[D_DIRT], 0); break;              /* BlockMycelium -> dirt */
        case 60: plain(c, 1, ITEM[D_DIRT], 0); break;               /* BlockFarmland -> dirt */

        case 5: plain(c, 1, ITEM[D_PLANKS], meta); break;           /* BlockWood.damageDropped */
        case 6:                                                      /* BlockSapling.damageDropped */
        {
            int d = meta & 7;

            if (d > 5) d = 5;
            plain(c, 1, ITEM[D_SAPLING], d);
            break;
        }

        case 12: plain(c, 1, drops_block_item(12), meta); break;    /* BlockSand */
        case 24: plain(c, 1, drops_block_item(24), meta); break;    /* BlockSandStone */
        case 98: plain(c, 1, drops_block_item(98), meta); break;    /* BlockStoneBrick */
        case 139: plain(c, 1, drops_block_item(139), meta); break;  /* BlockWall */
        case 159: plain(c, 1, drops_block_item(159), meta); break;  /* BlockColored */
        case 160:                                                    /* BlockStainedGlassPane: BlockPane with the no-drop flag, like glass_pane */
            plain(c, 1, -1, 0);
            break;
        case 171: plain(c, 1, drops_block_item(171), meta); break;  /* BlockCarpet */
        case 70: case 72: case 147: case 148:                        /* the pressure plates */
            plain(c, 1, drops_block_item(block), 0);
            break;
        case 35: case 37: case 38:                                   /* BlockColored, BlockFlower */
            plain(c, 1, drops_block_item(block), meta);
            break;

        case 17: case 170:                                           /* BlockLog, BlockHay: BlockRotatedPillar */
            plain(c, 1, drops_block_item(block), meta & 3);
            break;

        case 13:                                                     /* BlockGravel: fortune up to flint */
        {
            int f = fortune > 3 ? 3 : fortune;
            float roll = jr_float(&c->w);

            /* Block's loop: the flint nextInt inside getItemDropped runs only
             * when the roll passes. */
            if (c->chance >= 1.0f || roll <= c->chance)
            {
                emit_item(c, jr_int_n(&c->w, 10 - f * 3) == 0 ? ITEM[D_FLINT] : drops_block_item(13), 0);
            }

            break;
        }

        case 14: case 15:                                            /* gold and iron ore drop themselves */
            plain(c, 1, drops_block_item(block), 0);
            break;

        case 16:                                                     /* BlockOre: coal */
            plain(c, ore_count(c, 0), ITEM[D_COAL], 0);
            emit_xp(c, jr_int_n(&c->w, 3));
            break;

        case 56: case 129:                                           /* BlockOre: diamond, emerald */
            plain(c, ore_count(c, 0), block == 56 ? ITEM[D_DIAMOND] : ITEM[D_EMERALD], 0);
            emit_xp(c, 3 + jr_int_n(&c->w, 5));
            break;

        case 21:                                                     /* BlockOre: lapis lazuli */
            plain(c, ore_count(c, 1), ITEM[D_DYE], 4);
            emit_xp(c, 2 + jr_int_n(&c->w, 4));
            break;

        case 153:                                                    /* BlockOre: nether quartz */
            plain(c, ore_count(c, 0), ITEM[D_QUARTZ], 0);
            emit_xp(c, 2 + jr_int_n(&c->w, 4));
            break;

        case 18: leaves(c, 1); break;                               /* BlockOldLeaf */
        case 161: leaves(c, 0); break;                              /* BlockNewLeaf */
        case 162: plain(c, 1, drops_block_item(162), meta & 3); break;  /* BlockNewLog */

        case 26:                                                     /* BlockBed: the head half drops nothing */
            if ((meta & 8) == 0) plain(c, 1, ITEM[D_BED], 0);
            break;

        case 30: case 132:                                           /* BlockWeb, BlockTripWire -> string */
            plain(c, 1, ITEM[D_STRING], 0);
            break;

        case 31:                                                     /* BlockTallGrass */
        {
            int count = 1 + jr_int_n(&c->w, fortune * 2 + 1);         /* quantityDroppedWithBonus */

            for (int i = 0; i < count; ++i)
            {
                float roll = jr_float(&c->w);                          /* the chance roll */

                /* the seeds nextInt inside getItemDropped runs only when the
                 * roll passes */
                if (c->chance >= 1.0f || roll <= c->chance)
                {
                    if (jr_int_n(&c->w, 8) == 0) emit_item(c, ITEM[D_WHEAT_SEEDS], 0);
                }
            }

            break;
        }

        case 32: case 102: case 120:                                 /* getItemDropped null, quantity 1 */
            plain(c, 1, -1, 0);
            break;

        case 43: case 44:                                            /* BlockStoneSlab -> stone_slab */
            plain(c, block == 43 ? 2 : 1, drops_block_item(44), meta & 7);
            break;

        case 125: case 126:                                          /* BlockWoodSlab -> wooden_slab */
            plain(c, block == 125 ? 2 : 1, drops_block_item(126), meta & 7);
            break;

        case 47: plain(c, 3, ITEM[D_BOOK], 0); break;               /* BlockBookshelf */
        case 55: case 73: case 74:                                   /* redstone dust */
            if (block == 55)
            {
                plain(c, 1, ITEM[D_REDSTONE], 0);                   /* BlockRedstoneWire */
            }
            else
            {
                /* BlockRedstoneOre: 4 + nextInt(2), plus nextInt(fortune + 1) */
                int count = 4 + jr_int_n(&c->w, 2);

                count += jr_int_n(&c->w, fortune + 1);
                plain(c, count, ITEM[D_REDSTONE], 0);
                emit_xp(c, 1 + jr_int_n(&c->w, 5));
            }

            break;

        case 52:                                                     /* BlockMobSpawner: xp only */
        {
            int xp = 15 + jr_int_n(&c->w, 15);

            xp += jr_int_n(&c->w, 15);
            emit_xp(c, xp);
            break;
        }

        case 59:                                                     /* BlockCrops: wheat */
            crops(c, meta == 7 ? ITEM[D_WHEAT] : ITEM[D_WHEAT_SEEDS], ITEM[D_WHEAT_SEEDS]);
            break;

        case 141:                                                    /* BlockCarrot */
            crops(c, ITEM[D_CARROT], ITEM[D_CARROT]);
            break;

        case 142:                                                    /* BlockPotato */
            crops(c, ITEM[D_POTATO], ITEM[D_POTATO]);

            if (meta >= 7 && jr_int_n(&c->w, 50) == 0) emit_item(c, ITEM[D_POISONOUS_POTATO], 0);

            break;

        case 61: base(c, 61); break;                                /* BlockFurnace */
        case 62: plain(c, 1, drops_block_item(61), 0); break;       /* lit furnace -> the furnace item */

        case 63: case 68: plain(c, 1, ITEM[D_SIGN], 0); break;      /* BlockSign */
        case 64: case 71:                                            /* BlockDoor: the top half drops nothing */
            plain(c, 1, (meta & 8) != 0 ? -1 : (block == 64 ? ITEM[D_WOODEN_DOOR] : ITEM[D_IRON_DOOR]), 0);
            break;

        case 75: case 76: plain(c, 1, drops_block_item(76), 0); break;   /* BlockRedstoneTorch */
        case 80: plain(c, 4, ITEM[D_SNOWBALL], 0); break;           /* BlockSnowBlock */
        case 82: plain(c, 4, ITEM[D_CLAY_BALL], 0); break;          /* BlockClay */
        case 83: plain(c, 1, ITEM[D_REEDS], 0); break;              /* BlockReed */
        case 84: base(c, 84); break;                                /* BlockJukebox (fortune forced 0) */

        case 89:                                                     /* BlockGlowstone */
        {
            int count = 2 + jr_int_n(&c->w, 3);

            count += jr_int_n(&c->w, fortune + 1);
            if (count < 1) count = 1;
            if (count > 4) count = 4;
            plain(c, count, ITEM[D_GLOWSTONE_DUST], 0);
            break;
        }

        case 99: case 100:                                           /* BlockHugeMushroom: brown or red mushroom */
        {
            int count = jr_int_n(&c->w, 10) - 7;

            if (count < 0) count = 0;
            plain(c, count, drops_block_item(block == 99 ? 39 : 40), 0);
            break;
        }

        case 103:                                                    /* BlockMelon */
        {
            int count = 3 + jr_int_n(&c->w, 5);

            count += jr_int_n(&c->w, fortune + 1);
            if (count > 9) count = 9;
            plain(c, count, ITEM[D_MELON], 0);
            break;
        }

        case 104: case 105:                                          /* BlockStem */
        {
            plain(c, 1, -1, 0);

            for (int i = 0; i < 3; ++i)
            {
                if (jr_int_n(&c->w, 15) <= meta)
                {
                    emit_item(c, block == 104 ? ITEM[D_PUMPKIN_SEEDS] : ITEM[D_MELON_SEEDS], 0);
                }
            }

            break;
        }

        case 115:                                                    /* BlockNetherWart */
        {
            int count = 1;

            if (meta >= 3)
            {
                count = 2 + jr_int_n(&c->w, 3);

                if (fortune > 0) count += jr_int_n(&c->w, fortune + 1);
            }

            /* The override never calls Block's loop, so there is no chance
             * roll: three world draws per item, not four. */
            for (int i = 0; i < count; ++i) emit_item(c, ITEM[D_NETHER_WART], 0);

            break;
        }

        case 117: plain(c, 1, ITEM[D_BREWING_STAND], 0); break;     /* BlockBrewingStand */
        case 118: plain(c, 1, ITEM[D_CAULDRON], 0); break;          /* BlockCauldron */

        case 123: case 124: plain(c, 1, drops_block_item(123), 0); break;  /* BlockRedstoneLight */

        case 127:                                                    /* BlockCocoa: dye, no other draws */
        {
            int age = (meta & 12) >> 2;
            int count = age >= 2 ? 3 : 1;

            for (int i = 0; i < count; ++i) emit_item(c, ITEM[D_DYE], 3);
            break;
        }

        case 130: plain(c, 8, ITEM[D_OBSIDIAN], 0); break;          /* BlockEnderChest */
        case 140: plain(c, 1, ITEM[D_FLOWER_POT], 0); break;        /* BlockFlowerPot */
        case 145: plain(c, 1, drops_block_item(145), meta >> 2); break;  /* BlockAnvil */

        case 149: case 150: plain(c, 1, ITEM[D_COMPARATOR], 0); break;   /* BlockRedstoneComparator */
        case 93: case 94: plain(c, 1, ITEM[D_REPEATER], 0); break;       /* BlockRedstoneRepeater */

        case 155:                                                    /* BlockQuartz.damageDropped */
        {
            int d = (meta == 3 || meta == 4) ? 2 : meta;

            plain(c, 1, drops_block_item(155), d);
            break;
        }

        case 175:                                                    /* BlockDoublePlant */
        {
            int item = -1, damage = 0;

            if ((meta & 8) == 0)
            {
                int variant = meta & 7;

                if (variant != 2 && variant != 3)
                {
                    item = drops_block_item(175);
                    damage = variant;
                }
            }

            plain(c, 1, item, damage);
            break;
        }

        default:
            base(c, block);
            break;
    }

    return c->n;
}

int drops_break(const struct drop_case *cs, struct drop_ent *out, int cap)
{
    struct drop_ctx c;

    c.math = cs->math;
    c.out = out;
    c.n = 0;
    c.cap = cap;
    c.x = cs->x;
    c.y = cs->y;
    c.z = cs->z;
    c.meta = cs->meta;
    c.fortune = cs->fortune;
    c.chance = 1.0f;
    jr_seed(&c.w, cs->opseed);

    drops_dispatch(&c, cs->block);

    if (cs->out_seed != NULL) *cs->out_seed = c.w.seed;
    return c.n;
}

/* The same break over a caller-owned world Random: an explosion's drop
 * continues the world's stream in place instead of seeding it, with the
 * caller's drop chance (1.0F / explosionSize). `chance` is 1.0f for the
 * chance-always-passes path; the roll is always drawn either way. */
int drops_break_live(const struct drop_case *cs, jrand *wr, float chance,
                     struct drop_ent *out, int cap)
{
    struct drop_ctx c;

    c.math = cs->math;
    c.out = out;
    c.n = 0;
    c.cap = cap;
    c.x = cs->x;
    c.y = cs->y;
    c.z = cs->z;
    c.meta = cs->meta;
    c.fortune = cs->fortune;
    c.chance = chance;
    c.w = *wr;

    drops_dispatch(&c, cs->block);

    *wr = c.w;
    return c.n;
}
/* The placement paths' break: the world Random the caller owns, the chance
 * always passes (and is drawn), the state written back so consecutive breaks
 * share the stream. */
int drops_break_stream(const struct drop_case *cs, jrand *w, struct drop_ent *out, int cap)
{
    return drops_break_live(cs, w, 1.0f, out, cap);
}

/* BlockSkull.breakBlock: one skull item at the tile entity's type, through
 * dropBlockAsItem_do directly (no chance roll and no quantity draws). The
 * world Random is the caller's (the placement paths' stream). */
int drops_skull_break_stream(const struct drop_case *cs, jrand *w, int type, struct drop_ent *out, int cap)
{
    struct drop_ctx c;

    c.math = cs->math;
    c.out = out;
    c.n = 0;
    c.cap = cap;
    c.x = cs->x;
    c.y = cs->y;
    c.z = cs->z;
    c.meta = cs->meta;
    c.fortune = 0;
    c.chance = 1.0f;
    c.w = *w;

    emit_item(&c, ITEM[D_SKULL], type);

    *w = c.w;
    return c.n;
}

/* One item entity for a caller that already knows the item and the damage
 * (the door's door item, a block's own drop): dropBlockAsItem_do against the
 * caller's world Random. with_roll adds the dropBlockAsItemWithChance chance
 * float the break's caller draws before dropBlockAsItem_do (a 1.0 chance
 * always passes, and is drawn either way). */
int drops_item_break_stream(const struct drop_case *cs, jrand *w, int item, int damage, int with_roll,
                            struct drop_ent *out, int cap)
{
    struct drop_ctx c;

    c.math = cs->math;
    c.out = out;
    c.n = 0;
    c.cap = cap;
    c.x = cs->x;
    c.y = cs->y;
    c.z = cs->z;
    c.meta = cs->meta;
    c.fortune = 0;
    c.chance = 1.0f;
    c.w = *w;

    if (with_roll) (void)jr_float(&c.w);

    emit_item(&c, item, damage);

    *w = c.w;
    return c.n;
}
