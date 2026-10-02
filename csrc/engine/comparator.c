/* BlockRedstoneComparator's container input and World.func_147453_f
 * (comparator.h). Everything the comparator reads that World's power queries
 * do not zero with config.yaml redstone = off: the override of the block
 * behind it (or behind a normal cube there) and a redstone wire's own
 * metadata (always 0 in a world where nothing powers a wire). */
#include "comparator.h"

#include "blockcb.h"
#include "blocks.h"
#include "blockwl.h"
#include "env.h"
#include "items.h"
#include "ticks.h"
#include "tileentity.h"
#include "world.h"

enum {
    CMP_WIRE = 55, CMP_REPEATER_OFF = 93, CMP_REPEATER_ON = 94, CMP_OFF = 149, CMP_ON = 150,
    CMP_CHEST = 54, CMP_TRAPPED_CHEST = 146, CMP_FURNACE = 61, CMP_LIT_FURNACE = 62,
    CMP_DISPENSER = 23, CMP_DROPPER = 158, CMP_HOPPER = 154, CMP_BREWING_STAND = 117,
    CMP_CAULDRON = 118, CMP_JUKEBOX = 84, CMP_END_FRAME = 120, CMP_DETECTOR_RAIL = 28,
    CMP_COMMAND_BLOCK = 137, CMP_RECORD_13 = 2256,
};

/* Direction.offsetX / offsetZ */
static const int OFFX[4] = {0, -1, 0, 1};
static const int OFFZ[4] = {1, 0, -1, 0};

static int block_at(struct world *w, int x, int y, int z)
{
    return world_get_block(w, x, y, z) & 4095;
}

int comparator_is(int id)
{
    id &= 4095;
    return id == CMP_OFF || id == CMP_ON;
}

static int is_diode(int id)
{
    id &= 4095;
    return id == CMP_REPEATER_OFF || id == CMP_REPEATER_ON || id == CMP_OFF || id == CMP_ON;
}

int comparator_has_override(int id)
{
    switch (id & 4095)
    {
    case CMP_CHEST: case CMP_TRAPPED_CHEST: case CMP_FURNACE: case CMP_LIT_FURNACE:
    case CMP_DISPENSER: case CMP_DROPPER: case CMP_HOPPER: case CMP_BREWING_STAND:
    case CMP_CAULDRON: case CMP_JUKEBOX: case CMP_END_FRAME: case CMP_DETECTOR_RAIL:
    case CMP_COMMAND_BLOCK:
        return 1;
    default:
        return 0;
    }
}

/* Container.calcRedstoneFromInventory over the inventory's slot arrays in its
 * getStackInSlot order (an InventoryLargeChest is its upper chest's slots,
 * then its lower's): each stack's fill against min(getInventoryStackLimit,
 * getMaxStackSize), summed in float in slot order. Every inventory here has
 * the stack limit 64. */
static int calc_redstone(const struct te_stack *const *parts, const int *sizes, int nparts)
{
    int filled = 0, size = 0;
    float sum = 0.0F;

    for (int p = 0; p < nparts; ++p)
    {
        for (int i = 0; i < sizes[p]; ++i)
        {
            const struct te_stack *s = &parts[p][i];

            if (s->item < 0) continue;

            int max = ITEMS[s->item & 4095].max_stack_size;
            int limit = max < 64 ? max : 64;

            sum += (float)s->count / (float)limit;
            ++filled;
        }

        size += sizes[p];
    }

    sum /= (float)size;

    float v = sum * 14.0F;
    int f = (int)v;

    if (v < (float)f) --f;   /* MathHelper.floor_float */
    return f + (filled > 0 ? 1 : 0);
}

/* BlockChest.func_149953_o: an ocelot sitting on the chest. config.yaml
 * ocelots = off keeps them out of every spawn list and /summon refuses them,
 * so the entity scan finds none. */
static int ocelot_on(struct world *w, int x, int y, int z)
{
    (void)w; (void)x; (void)y; (void)z;
    return 0;
}

static int chest_te_slots(struct world *w, int x, int y, int z, const struct te_stack **out)
{
    struct tile_entity *te = world_tile_entity(w, x, y, z);

    if (te == NULL || te->kind != TE_CHEST) return 0;
    *out = te->u.chest.slots;
    return 1;
}

/* BlockChest.getComparatorInputOverride: calcRedstoneFromInventory over
 * func_149951_m, which is null (0) for a chest under a normal cube or an
 * ocelot, or beside a same-kind chest that is; otherwise the chest, wrapped in
 * an InventoryLargeChest for each same-kind neighbour in -x, +x, -z, +z order
 * (the -x and -z halves go first). */
static int chest_override(struct world *w, int x, int y, int z)
{
    int self = block_at(w, x, y, z);
    const struct te_stack *parts[5];
    int sizes[5];
    int n = 0;

    if (!chest_te_slots(w, x, y, z, &parts[0])) return 0;
    if (BLOCKS[block_at(w, x, y + 1, z)].normal_cube) return 0;
    if (ocelot_on(w, x, y, z)) return 0;

    for (int d = 0; d < 4; ++d)
    {
        static const int DX[4] = {-1, 1, 0, 0}, DZ[4] = {0, 0, -1, 1};
        int nx = x + DX[d], nz = z + DZ[d];

        if (block_at(w, nx, y, nz) == self &&
            (BLOCKS[block_at(w, nx, y + 1, nz)].normal_cube || ocelot_on(w, nx, y, nz))) return 0;
    }

    sizes[0] = CHEST_SLOTS;
    n = 1;

    for (int d = 0; d < 4; ++d)
    {
        static const int DX[4] = {-1, 1, 0, 0}, DZ[4] = {0, 0, -1, 1};
        int nx = x + DX[d], nz = z + DZ[d];
        const struct te_stack *other;

        if (block_at(w, nx, y, nz) != self) continue;
        if (!chest_te_slots(w, nx, y, nz, &other)) return 0;   /* the cast of a missing entity */

        if (DX[d] < 0 || DZ[d] < 0)
        {
            for (int i = n; i > 0; --i) { parts[i] = parts[i - 1]; sizes[i] = sizes[i - 1]; }
            parts[0] = other;
            sizes[0] = CHEST_SLOTS;
        }
        else
        {
            parts[n] = other;
            sizes[n] = CHEST_SLOTS;
        }

        ++n;
    }

    return calc_redstone(parts, sizes, n);
}

int comparator_override(struct world *w, int x, int y, int z)
{
    int id = block_at(w, x, y, z);
    struct tile_entity *te;
    const struct te_stack *parts[1];
    int sizes[1];

    switch (id)
    {
    case CMP_CHEST:
    case CMP_TRAPPED_CHEST:
        return chest_override(w, x, y, z);

    case CMP_FURNACE:
    case CMP_LIT_FURNACE:
        te = world_tile_entity(w, x, y, z);
        if (te == NULL || te->kind != TE_FURNACE) return 0;
        parts[0] = te->u.furnace.slots;
        sizes[0] = 3;
        return calc_redstone(parts, sizes, 1);

    case CMP_DISPENSER:
    case CMP_DROPPER:
        te = world_tile_entity(w, x, y, z);
        if (te == NULL || te->kind != TE_DISPENSER) return 0;
        parts[0] = te->u.dispenser.slots;
        sizes[0] = DISPENSER_SLOTS;
        return calc_redstone(parts, sizes, 1);

    case CMP_HOPPER:
        te = world_tile_entity(w, x, y, z);
        if (te == NULL || te->kind != TE_HOPPER) return 0;
        parts[0] = te->u.hopper.slots;
        sizes[0] = 5;
        return calc_redstone(parts, sizes, 1);

    case CMP_BREWING_STAND:
        te = world_tile_entity(w, x, y, z);
        if (te == NULL || te->kind != TE_BREWING_STAND) return 0;
        parts[0] = te->u.brewing.slots;
        sizes[0] = 4;
        return calc_redstone(parts, sizes, 1);

    case CMP_CAULDRON:
        /* BlockCauldron.func_150027_b: the level is the metadata */
        return world_get_meta(w, x, y, z);

    case CMP_JUKEBOX:
        /* the record's id + 1 - record_13's */
        te = world_tile_entity(w, x, y, z);
        if (te == NULL || te->kind != TE_JUKEBOX || te->u.jukebox.item < 0) return 0;
        return te->u.jukebox.item + 1 - CMP_RECORD_13;

    case CMP_END_FRAME:
        /* BlockEndPortalFrame.func_150020_b: the eye bit */
        return (world_get_meta(w, x, y, z) & 4) != 0 ? 15 : 0;

    default:
        /* BlockRailDetector counts minecarts on a powered rail (config.yaml
         * minecarts = off: none is ever placed or spawned) and
         * BlockCommandBlock a command block's success count (creative only):
         * both answer 0 in this game */
        return 0;
    }
}

/* BlockRedstoneComparator.func_149903_h: BlockRedstoneDiode's input (the
 * indirect power behind, 0 with redstone off, or a wire's own strength),
 * replaced by the override of the block behind, or of the block behind a
 * normal cube there while the input is under 15. */
static int input(struct world *w, int x, int y, int z, int meta)
{
    int d = meta & 3;
    int ix = x + OFFX[d], iz = z + OFFZ[d];
    int b = block_at(w, ix, y, iz);
    int v = b == CMP_WIRE ? world_get_meta(w, ix, y, iz) : 0;

    if (comparator_has_override(b))
    {
        v = comparator_override(w, ix, y, iz);
    }
    else if (v < 15 && BLOCKS[b].normal_cube)
    {
        ix += OFFX[d];
        iz += OFFZ[d];
        b = block_at(w, ix, y, iz);

        if (comparator_has_override(b)) v = comparator_override(w, ix, y, iz);
    }

    return v;
}

/* BlockRedstoneDiode.func_149913_i with the comparator's func_149908_a
 * (canProvidePower): a wire's strength; every other power source answers
 * World.isBlockProvidingPowerTo, 0 with redstone off. */
static int side_one(struct world *w, int x, int y, int z)
{
    return block_at(w, x, y, z) == CMP_WIRE ? world_get_meta(w, x, y, z) : 0;
}

/* BlockRedstoneDiode.func_149902_h: the larger side input. */
static int side_input(struct world *w, int x, int y, int z, int meta)
{
    int a, b;

    switch (meta & 3)
    {
    case 0:
    case 2:
        a = side_one(w, x - 1, y, z);
        b = side_one(w, x + 1, y, z);
        return a > b ? a : b;
    default:
        a = side_one(w, x, y, z + 1);
        b = side_one(w, x, y, z - 1);
        return a > b ? a : b;
    }
}

/* func_149970_j: the input, less the side input in subtract mode (bit 4). */
static int strength(struct world *w, int x, int y, int z, int meta)
{
    int in = input(w, x, y, z, meta);

    if ((meta & 4) != 4) return in;

    int v = in - side_input(w, x, y, z, meta);
    return v > 0 ? v : 0;
}

int comparator_strength(struct world *w, int x, int y, int z, int meta)
{
    return strength(w, x, y, z, meta);
}

int comparator_should_power(struct world *w, int x, int y, int z, int meta)
{
    int in = input(w, x, y, z, meta);

    if (in >= 15) return 1;
    if (in == 0) return 0;

    int side = side_input(w, x, y, z, meta);
    return side == 0 ? 1 : in >= side;
}

/* BlockRedstoneDiode.func_149912_i: a diode in front not facing this way. */
static int front_diode(struct world *w, int x, int y, int z, int meta)
{
    int d = meta & 3;
    int fx = x - OFFX[d], fz = z - OFFZ[d];

    if (!is_diode(block_at(w, fx, y, fz))) return 0;
    return (world_get_meta(w, fx, y, fz) & 3) != d;
}

static struct tile_entity *comparator_te(struct world *w, int x, int y, int z)
{
    struct tile_entity *te = world_tile_entity(w, x, y, z);

    return te != NULL && te->kind == TE_COMPARATOR ? te : NULL;
}

void comparator_on_neighbor(struct world *w, int x, int y, int z, int id)
{
    if (ticks_scheduled_this_tick(x, y, z, id)) return;

    int meta = world_get_meta(w, x, y, z);
    int v7 = strength(w, x, y, z, meta);
    struct tile_entity *te = comparator_te(w, x, y, z);

    if (te == NULL) return;

    int v8 = te->u.comparator.out_signal;
    int powered = (id & 4095) == CMP_ON || (meta & 8) != 0;

    if (v7 != v8 || powered != comparator_should_power(w, x, y, z, meta))
    {
        /* func_149901_b(0) is 2 */
        int priority = front_diode(w, x, y, z, meta) ? -1 : 0;

        ticks_bwl_sched_priority(w, x, y, z, id & 4095, 2, priority);
    }
}

void comparator_refresh(struct world *w, int x, int y, int z, int id)
{
    int meta = world_get_meta(w, x, y, z);
    int v7 = strength(w, x, y, z, meta);
    struct tile_entity *te = comparator_te(w, x, y, z);

    if (te == NULL) return;

    int v8 = te->u.comparator.out_signal;

    te->u.comparator.out_signal = v7;   /* TileEntityComparator.func_145995_a */

    if (v8 != v7 || (meta & 4) != 4)
    {
        int v9 = comparator_should_power(w, x, y, z, meta);
        int v10 = (id & 4095) == CMP_ON || (meta & 8) != 0;

        if (v10 && !v9) world_set_meta(w, x, y, z, meta & -9, 2);
        else if (!v10 && v9) world_set_meta(w, x, y, z, meta | 8, 2);

        blockcb_diode_notify(w, x, y, z, id & 4095);
    }
}

void comparator_update_tick(struct world *w, int x, int y, int z, int id)
{
    if ((id & 4095) == CMP_ON)
        world_set_block(w, x, y, z, CMP_OFF, world_get_meta(w, x, y, z) | 8, 4);

    comparator_refresh(w, x, y, z, id);
}

void comparator_notify(struct world *w, int x, int y, int z, int block)
{
    if ((block & 4095) == 0 || w->is_remote) return;
    BWL_CALL(BWL_CMP_NOTIFY, w, x, y, z, block & 4095);
}

void comparator_notify_emit(struct world *w, int x, int y, int z, int block)
{
    if ((block & 4095) == 0 || w->is_remote) return;
    BWL_EMIT(BWL_CMP_NOTIFY, w, x, y, z, block & 4095);
}

int comparator_notify_step(struct bwl_frame *f)
{
    struct world *w = f->op.w;
    int x = f->op.x, y = f->op.y, z = f->op.z;

    while (f->i < 4)
    {
        int d = f->i++;
        int nx = x + OFFX[d], nz = z + OFFZ[d];
        int b = block_at(w, nx, y, nz);

        if (comparator_is(b))
        {
            BWL_EMIT(BWL_NEIGHBOR, w, nx, y, nz, f->op.a[0], 0);
            return 0;
        }

        if (BLOCKS[b].normal_cube)
        {
            nx += OFFX[d];
            nz += OFFZ[d];

            if (comparator_is(block_at(w, nx, y, nz)))
            {
                BWL_EMIT(BWL_NEIGHBOR, w, nx, y, nz, f->op.a[0], 0);
                return 0;
            }
        }
    }

    return 1;
}
