/* StructureComponent's block helpers and StructureStart.generateStructure,
 * ported from net.minecraft.world.gen.structure; see structure_blocks.h for
 * the API the structure lanes use. Every write goes through World.setBlock
 * with flags 2, the way the helpers vanilla runs here do. */
#include "structure_blocks.h"
#include "env.h"

#include "blocks.h"
#include "blockcb.h"
#include "tileentity.h"
#include "spill.h"
#include "det.h"

#include <stdlib.h>
#include <string.h>


static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

static int class_of(int id, const char *name)
{
    return strcmp(BLOCKS[id & 4095].class_name, name) == 0;
}

/* ------------------------------------------------------------ entities */

void sc_ent_free(struct sc_ent *e)
{
    for (int i = 0; i < e->nslots; ++i)
    {
        e->slots[i].tag = 0;
    }

    e->nslots = 0;
}

void sc_free_ents(struct sc_ctx *c)
{
    for (int i = 0; i < c->nents; ++i) sc_ent_free(&c->ents[i]);
    free(c->ents);
    c->ents = NULL;
    c->nents = 0;
    c->cap = 0;
}

static void sc_add_ent(struct sc_ctx *c, struct sc_ent e)
{
    if (c->nents == c->cap)
    {
        c->cap = c->cap ? 2 * c->cap : 4;
        c->ents = realloc(c->ents, (size_t)c->cap * sizeof *c->ents);
        if (!c->ents) abort();
    }

    c->ents[c->nents++] = e;

    /* the moment the piece hands the entity over is World.spawnEntityInWorld's
     * call in the Java piece, so a caller that records entities takes it here
     * and not from the context's list, which the step frees (structure.h) */
    if (populate_on_ent != NULL) populate_on_ent(populate_on_ent_ctx, &e);
}

/* Det.nextEntityId, Det.newRandom and Det.uuid, the draws an Entity
 * constructor makes, in its order: the id, the rand, the UUID. */
static int entity_construct(struct sc_ctx *c, int kind, struct sc_ent *out)
{
    if (c->det == NULL) return 0;

    memset(out, 0, sizeof *out);
    out->kind = kind;
    out->entity_id = det_next_entity_id_role(c->det, det_role(c->det));

    det_rng erand = det_new_random_role(c->det, det_role(c->det));
    out->rand_state = det_rng_state(&erand);

    det_uuid_role(c->det, det_role(c->det), &out->uuid_msb, &out->uuid_lsb);
    return 1;
}

/* A corridor's chest cart: the constructor's Det draws (the id, the rand, the
 * UUID), then the position it was built at and the loot slots, which move in. */
int sc_cart_new(struct sc_ctx *c, double x, double y, double z, struct loot_stack *slots)
{
    struct sc_ent e;

    if (!entity_construct(c, SC_CART, &e)) return 0;

    e.x = x;
    e.y = y;
    e.z = z;
    e.nslots = CART_SLOTS;

    for (int i = 0; i < CART_SLOTS; ++i) e.slots[i] = slots[i];
    sc_add_ent(c, e);
    return 1;
}

/* The witch a swamp hut spawns: the EntityWitch constructor's Det draws, the
 * position setLocationAndAngles gives it and onSpawnWithEgg's modifier UUID.
 * The record keeps the streams from before the constructor, so a caller that
 * spawns it (the replay) rebuilds the living over the same draws; the egg's
 * gaussian comes from the witch's own Random. */
int sc_witch_new(struct sc_ctx *c, double x, double y, double z)
{
    struct sc_ent e;

    if (c->det == NULL) return 0;

    int role = det_role(c->det);
    det_rng pre_seeder = c->det->seeder[role], pre_math = c->det->math[role];
    int32_t pre_next_id = c->det->next_id[role];

    if (!entity_construct(c, SC_WITCH, &e)) return 0;

    e.pre_seeder = pre_seeder;
    e.pre_math = pre_math;
    e.pre_next_id = pre_next_id;

    /* EntityLivingBase's constructor: field_70770_ap, field_70769_ao and the
     * rotationYaw the head keeps (setLocationAndAngles zeroes the body's) */
    det_math_random_role(c->det, role);
    det_math_random_role(c->det, role);
    e.yaw = (float)(det_math_random_role(c->det, role) * 3.141592653589793 * 2.0);

    /* EntityLiving.onSpawnWithEgg (the witch does not override it): the
     * follow-range attribute gets a "Random spawn bonus" modifier, and the
     * AttributeModifier constructor draws its own UUID, two more seeder longs.
     * The modifier's amount comes from the entity's own Random (nextGaussian
     * after the size argument is evaluated), which nothing else ever reads. */
    int64_t attr_msb, attr_lsb;

    det_uuid_role(c->det, det_role(c->det), &attr_msb, &attr_lsb);

    e.x = x;
    e.y = y;
    e.z = z;

    sc_add_ent(c, e);
    return 1;
}

/* A village house's villager: the Entity constructor's Det draws (the id, the
 * rand, the UUID), then EntityLivingBase's three Det.mathRandom draws
 * (field_70770_ap, field_70769_ao, rotationYaw), then the position
 * setLocationAndAngles set and the profession. */
int sc_villager_new(struct sc_ctx *c, double x, double y, double z, int profession)
{
    struct sc_ent e;

    if (!entity_construct(c, SC_VILLAGER, &e)) return 0;

    det_math_random_role(c->det, det_role(c->det));
    det_math_random_role(c->det, det_role(c->det));
    /* EntityLivingBase's rotationYaw = rotationYawHead draw; the spawn's
     * setLocationAndAngles zeroes the yaw, the head keeps it */
    e.yaw = (float)(det_math_random_role(c->det, det_role(c->det)) * 3.141592653589793 * 2.0);
    e.x = x;
    e.y = y;
    e.z = z;
    e.profession = profession;
    sc_add_ent(c, e);
    return 1;
}

/* ------------------------------------------------------------ offsets */

int sc_x_with_offset(const struct piece *p, int tx, int tz)
{
    switch (p->coord_base_mode)
    {
        case 0:
        case 2: return p->bb.minX + tx;
        case 1: return p->bb.maxX - tz;
        case 3: return p->bb.minX + tz;
        default: return tx;
    }
}

int sc_y_with_offset(const struct piece *p, int ty)
{
    return p->coord_base_mode == -1 ? ty : ty + p->bb.minY;
}

int sc_z_with_offset(const struct piece *p, int tx, int tz)
{
    switch (p->coord_base_mode)
    {
        case 0: return p->bb.minZ + tz;
        case 1:
        case 3: return p->bb.minZ + tx;
        case 2: return p->bb.maxZ - tz;
        default: return tz;
    }
}

/* World.isBlockNormalCubeDefault and doesBlockHaveSolidTopSurface live in
 * blockcb.c, beside the torch and rail bodies that are their only callers
 * here. */

static int block_is_liquid(int id)
{
    return MATERIALS[BLOCKS[id & 4095].material].is_liquid;
}

/* ------------------------------------------------------------- orientation
 * func_151555_a. The instanceof BlockDirectional test is the class family:
 * BlockBed, BlockCocoa, BlockFenceGate, BlockPumpkin, BlockRedstoneDiode and
 * its two subclasses (BlockDispenser is a BlockContainer, not directional). */
static int is_directional(int id)
{
    const char *c = BLOCKS[id & 4095].class_name;

    return !strcmp(c, "BlockBed") || !strcmp(c, "BlockCocoa") || !strcmp(c, "BlockFenceGate")
        || !strcmp(c, "BlockPumpkin") || !strcmp(c, "BlockRedstoneDiode")
        || !strcmp(c, "BlockRedstoneRepeater") || !strcmp(c, "BlockRedstoneComparator");
}

int sc_orient_meta(int id, int meta, int mode)
{
    if (id == SCB_RAIL)
    {
        if (mode == 1 || mode == 3)
        {
            if (meta == 1) return 0;
            return 1;
        }
    }
    else if (id != 64 && id != 71) /* wooden_door, iron_door */
    {
        if (class_of(id, "BlockStairs"))
        {
            if (mode == 0)
            {
                if (meta == 2) return 3;
                if (meta == 3) return 2;
            }
            else if (mode == 1)
            {
                if (meta == 0) return 2;
                if (meta == 1) return 3;
                if (meta == 2) return 0;
                if (meta == 3) return 1;
            }
            else if (mode == 3)
            {
                if (meta == 0) return 2;
                if (meta == 1) return 3;
                if (meta == 2) return 1;
                if (meta == 3) return 0;
            }
        }
        else if (id == 65) /* ladder */
        {
            if (mode == 0)
            {
                if (meta == 2) return 3;
                if (meta == 3) return 2;
            }
            else if (mode == 1)
            {
                if (meta == 2) return 4;
                if (meta == 3) return 5;
                if (meta == 4) return 2;
                if (meta == 5) return 3;
            }
            else if (mode == 3)
            {
                if (meta == 2) return 5;
                if (meta == 3) return 4;
                if (meta == 4) return 2;
                if (meta == 5) return 3;
            }
        }
        else if (id == 77) /* stone_button */
        {
            if (mode == 0)
            {
                if (meta == 3) return 4;
                if (meta == 4) return 3;
            }
            else if (mode == 1)
            {
                if (meta == 3) return 1;
                if (meta == 4) return 2;
                if (meta == 2) return 3;
                if (meta == 1) return 4;
            }
            else if (mode == 3)
            {
                if (meta == 3) return 2;
                if (meta == 4) return 1;
                if (meta == 2) return 3;
                if (meta == 1) return 4;
            }
        }
        else if (id != 131 && !is_directional(id)) /* tripwire_hook */
        {
            if (id == 33 || id == 29 || id == 69 || id == 23) /* piston, sticky_piston, lever, dispenser */
            {
                if (mode == 0)
                {
                    if (meta == 2 || meta == 3) return meta == 2 ? 3 : 2;
                }
                else if (mode == 1)
                {
                    if (meta == 2) return 4;
                    if (meta == 3) return 5;
                    if (meta == 4) return 2;
                    if (meta == 5) return 3;
                }
                else if (mode == 3)
                {
                    if (meta == 2) return 5;
                    if (meta == 3) return 4;
                    if (meta == 4) return 2;
                    if (meta == 5) return 3;
                }
            }
        }
        else if (mode == 0)
        {
            if (meta == 0 || meta == 2) return meta == 0 ? 2 : 0;
        }
        else if (mode == 1)
        {
            if (meta == 2) return 1;
            if (meta == 0) return 3;
            if (meta == 1) return 2;
            if (meta == 3) return 0;
        }
        else if (mode == 3)
        {
            if (meta == 2) return 3;
            if (meta == 0) return 1;
            if (meta == 1) return 2;
            if (meta == 3) return 0;
        }
    }
    else if (mode == 0)
    {
        if (meta == 0) return 2;
        if (meta == 2) return 0;
    }
    else
    {
        if (mode == 1) return (meta + 1) & 3;
        if (mode == 3) return (meta + 3) & 3;
    }

    return meta;
}

/* ------------------------------------------------------------- the helpers
 *
 * isVecInside over the step chunk box, the guard the Java helpers carry. */
static int in_step_box(const struct sc_ctx *c, int x, int y, int z)
{
    return x >= c->box.minX && x <= c->box.maxX && z >= c->box.minZ && z <= c->box.maxZ && y >= c->box.minY
        && y <= c->box.maxY;
}

void sc_place(struct sc_ctx *c, const struct piece *p, int id, int meta, int tx, int ty, int tz)
{
    int x = sc_x_with_offset(p, tx, tz), y = sc_y_with_offset(p, ty), z = sc_z_with_offset(p, tx, tz);

    if (in_step_box(c, x, y, z)) world_set_block(c->w, x, y, z, id, meta, 2);
}

int sc_block_at(struct sc_ctx *c, const struct piece *p, int tx, int ty, int tz)
{
    int x = sc_x_with_offset(p, tx, tz), y = sc_y_with_offset(p, ty), z = sc_z_with_offset(p, tx, tz);

    return in_step_box(c, x, y, z) ? world_get_block(c->w, x, y, z) : SCB_AIR;
}

int sc_in_step_box(const struct sc_ctx *c, int x, int y, int z)
{
    return in_step_box(c, x, y, z);
}

/* fillWithBlocks (func_151549_a): y outer, then x, then z, the interior to
 * `inner` and every face to `outer`; with `replace`, a cell already air stays. */
void sc_fill(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ, int maxX, int maxY, int maxZ,
             int outer_id, int inner_id, int replace)
{
    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            for (int z = minZ; z <= maxZ; ++z)
            {
                if (!replace || sc_block_at(c, p, x, y, z) != SCB_AIR)
                {
                    if (y != minY && y != maxY && x != minX && x != maxX && z != minZ && z != maxZ)
                        sc_place(c, p, inner_id, 0, x, y, z);
                    else
                        sc_place(c, p, outer_id, 0, x, y, z);
                }
            }
        }
    }
}

/* fillWithMetadataBlocks (func_151556_a). */
void sc_fill_meta(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ, int maxX, int maxY, int maxZ,
                  int outer_id, int outer_meta, int inner_id, int inner_meta, int replace)
{
    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            for (int z = minZ; z <= maxZ; ++z)
            {
                if (!replace || sc_block_at(c, p, x, y, z) != SCB_AIR)
                {
                    if (y != minY && y != maxY && x != minX && x != maxX && z != minZ && z != maxZ)
                        sc_place(c, p, inner_id, inner_meta, x, y, z);
                    else
                        sc_place(c, p, outer_id, outer_meta, x, y, z);
                }
            }
        }
    }
}

void sc_fill_air(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ, int maxX, int maxY, int maxZ)
{
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x)
            for (int z = minZ; z <= maxZ; ++z)
                sc_place(c, p, SCB_AIR, 0, x, y, z);
}

/* randomlyFillWithBlocks (func_151551_a): the float draws happen wherever the
 * cell lands, the writes only inside the step chunk box. */
void sc_fill_random(struct sc_ctx *c, const struct piece *p, float chance, int minX, int minY, int minZ,
                    int maxX, int maxY, int maxZ, int outer_id, int inner_id, int replace)
{
    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            for (int z = minZ; z <= maxZ; ++z)
            {
                if (jr_float(c->rand) <= chance && (!replace || sc_block_at(c, p, x, y, z) != SCB_AIR))
                {
                    if (y != minY && y != maxY && x != minX && x != maxX && z != minZ && z != maxZ)
                        sc_place(c, p, inner_id, 0, x, y, z);
                    else
                        sc_place(c, p, outer_id, 0, x, y, z);
                }
            }
        }
    }
}

/* randomlyPlaceBlock (func_151552_a): the draw happens wherever the position
 * lands, the write only inside the step chunk box. */
void sc_random_place(struct sc_ctx *c, const struct piece *p, float chance, int tx, int ty, int tz, int id, int meta)
{
    if (jr_float(c->rand) < chance) sc_place(c, p, id, meta, tx, ty, tz);
}

/* fillWithRandomizedBlocks: y outer, then x, then z; the selector runs per
 * cell that passes the replace check, carrying the on-face flag, and its
 * block goes in raw (no metadata offset). */
void sc_fill_randomized(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ,
                        int maxX, int maxY, int maxZ, int replace, sc_select_fn select, void *ud)
{
    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            for (int z = minZ; z <= maxZ; ++z)
            {
                if (!replace || sc_block_at(c, p, x, y, z) != SCB_AIR)
                {
                    int id = SCB_AIR, meta = 0;

                    select(ud, y == minY || y == maxY || x == minX || x == maxX || z == minZ || z == maxZ,
                           &id, &meta);
                    sc_place(c, p, id, meta, x, y, z);
                }
            }
        }
    }
}

/* func_151547_a, the room's dome carve: y outer, then x, then z, floats. */
void sc_fill_dome(struct sc_ctx *c, const struct piece *p, int minX, int minY, int minZ, int maxX, int maxY, int maxZ,
                  int id, int replace)
{
    float sx = (float)(maxX - minX + 1);
    float sy = (float)(maxY - minY + 1);
    float sz = (float)(maxZ - minZ + 1);
    float cx = (float)minX + sx / 2.0f;
    float cz = (float)minZ + sz / 2.0f;

    for (int y = minY; y <= maxY; ++y)
    {
        float dy = (float)(y - minY) / sy;

        for (int x = minX; x <= maxX; ++x)
        {
            float dx = ((float)x - cx) / (sx * 0.5f);

            for (int z = minZ; z <= maxZ; ++z)
            {
                float dz = ((float)z - cz) / (sz * 0.5f);

                if (!replace || sc_block_at(c, p, x, y, z) != SCB_AIR)
                {
                    float r = dx * dx + dy * dy + dz * dz;

                    if (r <= 1.05f) sc_place(c, p, id, 0, x, y, z);
                }
            }
        }
    }
}

/* func_151554_b: the continuous fill down from a position, to y 2. */
void sc_fill_down(struct sc_ctx *c, const struct piece *p, int id, int meta, int tx, int ty, int tz)
{
    int x = sc_x_with_offset(p, tx, tz), y = sc_y_with_offset(p, ty), z = sc_z_with_offset(p, tx, tz);

    if (!in_step_box(c, x, y, z)) return;

    while ((world_get_block(c->w, x, y, z) == SCB_AIR || block_is_liquid(world_get_block(c->w, x, y, z))) && y > 1)
    {
        world_set_block(c->w, x, y, z, id, meta, 2);
        --y;
    }
}

/* clearCurrentPositionBlocksUpwards. */
void sc_clear_upwards(struct sc_ctx *c, const struct piece *p, int tx, int ty, int tz)
{
    int x = sc_x_with_offset(p, tx, tz), y = sc_y_with_offset(p, ty), z = sc_z_with_offset(p, tx, tz);

    if (!in_step_box(c, x, y, z)) return;

    while (world_get_block(c->w, x, y, z) != SCB_AIR && y < 255)
    {
        world_set_block(c->w, x, y, z, SCB_AIR, 0, 2);
        ++y;
    }
}

/* isLiquidInStructureBoundingBox, clamped to the step chunk box, the three
 * face scans in the order Java does them. */
int sc_is_liquid_in(const struct sc_ctx *c, const struct piece *p)
{
    int x0 = imax(p->bb.minX - 1, c->box.minX);
    int y0 = imax(p->bb.minY - 1, c->box.minY);
    int z0 = imax(p->bb.minZ - 1, c->box.minZ);
    int x1 = imin(p->bb.maxX + 1, c->box.maxX);
    int y1 = imin(p->bb.maxY + 1, c->box.maxY);
    int z1 = imin(p->bb.maxZ + 1, c->box.maxZ);

    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            if (block_is_liquid(world_get_block(c->w, x, y0, z))
                || block_is_liquid(world_get_block(c->w, x, y1, z))) return 1;

    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            if (block_is_liquid(world_get_block(c->w, x, y, z0))
                || block_is_liquid(world_get_block(c->w, x, y, z1))) return 1;

    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y)
            if (block_is_liquid(world_get_block(c->w, x0, y, z))
                || block_is_liquid(world_get_block(c->w, x1, y, z))) return 1;

    return 0;
}

/* generateStructureChestContents. */
int sc_chest_contents(struct sc_ctx *c, const struct piece *p, const struct loot_table *table,
                      const struct loot_book *book, int tx, int ty, int tz, int count)
{
    int x = sc_x_with_offset(p, tx, tz), y = sc_y_with_offset(p, ty), z = sc_z_with_offset(p, tx, tz);

    if (!in_step_box(c, x, y, z)) return 0;
    if (world_get_block(c->w, x, y, z) == SCB_CHEST) return 0;

    world_set_block(c->w, x, y, z, SCB_CHEST, 0, 2);
    struct tile_entity *te = world_tile_entity(c->w, x, y, z);

    if (te != NULL) loot_generate_contents(c->rand, table, book, (struct loot_stack *)te->u.chest.slots, CHEST_SLOTS, count);

    return 1;
}

/* generateStructureDispenserContents. The dispenser block's own onBlockAdded
 * (BlockDispenser.func_149938_m) rewrites its metadata from the neighbours, so
 * the block the caller asked for is not always the metadata the world keeps;
 * the tile entity is made on demand by the write path, and TileEntityDispenser's
 * field initializer spends one Det seeder draw, exactly as Java's does. */
int sc_dispenser_contents(struct sc_ctx *c, const struct piece *p, const struct loot_table *table,
                          const struct loot_book *book, int tx, int ty, int tz, int dir, int count)
{
    int x = sc_x_with_offset(p, tx, tz), y = sc_y_with_offset(p, ty), z = sc_z_with_offset(p, tx, tz);

    if (!in_step_box(c, x, y, z)) return 0;
    if (world_get_block(c->w, x, y, z) == SCB_DISPENSER) return 0;

    world_set_block(c->w, x, y, z, SCB_DISPENSER, sc_orient_meta(SCB_DISPENSER, dir, p->coord_base_mode), 2);

    /* the dispensers' construction, after the write's own: the block's write
     * made the tile entity, the constructor's Det.newRandom is spent right
     * after (TileEntityDispenser's field initializer) */
    if (c->det != NULL) det_new_random_role(c->det, det_role(c->det));

    struct tile_entity *te = world_tile_entity(c->w, x, y, z);

    if (te != NULL) loot_generate_dispenser(c->rand, table, book, (struct loot_stack *)te->u.dispenser.slots, DISPENSER_SLOTS, count);

    return 1;
}

/* StructureStart.generateStructure: the components that meet the step chunk
 * box, in list order; one whose addComponentParts returns false is dropped
 * for good. Returns how many components ran. The block-callback environment
 * (World.rand, Det's OTHER role, the item-drop recorder) is this context's
 * for the run and cleared after, the way the oracle probe parks it. */
static void env_drop_item(void *ctx, int entity_id, uint64_t rand_state, int64_t uuid_msb, int64_t uuid_lsb,
                          double x, double y, double z, float yaw, float hover,
                          double motion_x, double motion_z, int item, int damage, int count)
{
    struct sc_ctx *c = ctx;
    struct sc_ent e;

    memset(&e, 0, sizeof e);
    e.kind = SC_ITEM;
    e.entity_id = entity_id;
    e.rand_state = rand_state;
    e.uuid_msb = uuid_msb;
    e.uuid_lsb = uuid_lsb;
    e.x = x;
    e.y = y;
    e.z = z;
    e.yaw = yaw;
    e.hover = hover;
    e.motion_x = motion_x;
    e.motion_z = motion_z;
    e.item = item;
    e.damage = damage;
    e.count = count;
    sc_add_ent(c, e);
}

/* A chest a piece's write replaced (a mineshaft corridor through a
 * dungeon): BlockChest.breakBlock's spill, spawned where the Java piece's
 * setBlock spawns it, among the step's other entities. */
static void env_spill_item(void *ctx, const struct spill_item *s)
{
    struct sc_ctx *c = ctx;
    struct sc_ent e;

    memset(&e, 0, sizeof e);
    e.kind = SC_ITEM;
    e.spilled = 1;
    e.entity_id = s->entity_id;
    e.rand_state = s->rand_state;
    e.uuid_msb = s->uuid_msb;
    e.uuid_lsb = s->uuid_lsb;
    e.x = s->x;
    e.y = s->y;
    e.z = s->z;
    e.yaw = s->yaw;
    e.hover = s->hover;
    e.motion_x = s->mx;
    e.motion_y = s->my;
    e.motion_z = s->mz;
    e.item = s->item;
    e.damage = s->damage;
    e.count = s->count;
    e.tag = s->tag;
    sc_add_ent(c, e);
}

int sc_generate(struct sc_ctx *c, struct start *s)
{
    int ran = 0;
    int i = 0;

    /* the pieces run under this context's environment; the caller's comes back
     * when they are done (the probes have none of it set, so they see the same
     * empty environment either way) */
    struct blockcb_env saved_env = nw_env->blockcb.env;

    nw_env->blockcb.env.world_rand = c->world_rand;
    nw_env->blockcb.env.det = c->det;
    nw_env->blockcb.env.item_drop = env_drop_item;
    nw_env->blockcb.env.ctx = c;
    nw_env->blockcb.env.item_spill = c->det != NULL ? env_spill_item : NULL;
    nw_env->blockcb.env.spill_role = c->det != NULL ? det_role(c->det) : DET_OTHER;

    while (i < s->n)
    {
        struct piece *p = s->pieces[i];

        if (bbox_intersects(&p->bb, &c->box) && !c->piece_fn(c, p))
        {
            for (int j = i + 1; j < s->n; ++j) s->pieces[j - 1] = s->pieces[j];

            --s->n;
        }
        else
        {
            ++ran;
            ++i;
        }
    }

    nw_env->blockcb.env = saved_env;
    return ran;
}
