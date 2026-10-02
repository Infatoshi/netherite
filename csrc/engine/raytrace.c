#include "raytrace.h"
#include "jmath.h"
#include "env.h"
#include "collide.h"
#include "blocks.h"

#include <math.h>
#include <string.h>

/* Vec3.getIntermediateWithXValue and friends, on plain doubles: null outside
 * [0, 1] or when the axis delta squared is under 1.0000000116860974E-7. */
static int intermediate(double *out, double ax, double ay, double az,
                        double bx, double by, double bz,
                        double target, int axis)
{
    double dx = bx - ax, dy = by - ay, dz = bz - az;
    double d = axis == 0 ? dx : (axis == 1 ? dy : dz);

    if (d * d < 1.0000000116860974E-7) return 0;

    double t = (target - (axis == 0 ? ax : (axis == 1 ? ay : az))) / d;

    if (!(t >= 0.0 && t <= 1.0)) return 0;

    out[0] = ax + dx * t;
    out[1] = ay + dy * t;
    out[2] = az + dz * t;
    return 1;
}

static double sq_dist(double ax, double ay, double az, double bx, double by, double bz)
{
    double dx = bx - ax, dy = by - ay, dz = bz - az;
    return dx * dx + dy * dy + dz * dz;
}

/* Block.canCollideCheck(meta, stopOnLiquid). */
static int can_collide_check(int id, int meta, int stop_on_liquid)
{
    if (id == 0) return 0;

    const char *cls = BLOCKS[id & 4095].class_name;

    if (strcmp(cls, "BlockLiquid") == 0 || strcmp(cls, "BlockStaticLiquid") == 0 ||
        strcmp(cls, "BlockDynamicLiquid") == 0)
    {
        return stop_on_liquid && meta == 0;
    }

    return BLOCKS[id & 4095].collidable != 0;
}

/* BlockFence.func_149826_e and BlockWall.func_150091_e share a body: the
 * neighbor connects when it is the same object, or a fence gate, or an opaque
 * material whose render shape is a normal block. */
static int connects(struct world *w, int self_id, int nx, int ny, int nz)
{
    int n = world_get_block(w, nx, ny, nz) & 4095;
    if (n == self_id) return 1;
    if (n == 107) return 1; /* Blocks.fence_gate */
    if (!BLOCKS[n].exists) return 0;
    return BLOCKS[n].material != 0 && MATERIALS[BLOCKS[n].material].is_opaque && BLOCKS[n].normal_block &&
           BLOCKS[n].material != 16;
}

/* setBlockBounds(x0, y0, z0, x1, y1, z1): the six floats, widened. */
static void set6(double *b, float x0, float y0, float z0, float x1, float y1, float z1)
{
    b[0] = x0; b[1] = y0; b[2] = z0;
    b[3] = x1; b[4] = y1; b[5] = z1;
}

/* BlockPane.func_150098_a: an opaque block (Block.opaque, fixed at
 * construction), the pane itself, glass, stained glass, the stained pane or
 * any other BlockPane. */
static int pane_connects(struct world *w, int self_id, int x, int y, int z)
{
    int n = world_get_block(w, x, y, z) & 4095;
    if (n == self_id || n == 20 || n == 95 || n == 160 || n == 101 || n == 102) return 1;
    return BLOCKS[n].exists && BLOCKS[n].opaque_cube;
}

/* BlockVine.func_150093_a: renderAsNormalBlock and a material that blocks
 * movement. */
static int vine_support(int id)
{
    id &= 4095;
    return BLOCKS[id].exists && BLOCKS[id].normal_block &&
           MATERIALS[BLOCKS[id].material].blocks_movement;
}

/* The block's ray bounds after setBlockBoundsBasedOnState (or the bounds a
 * collisionRayTrace override sets before calling Block's), as doubles in
 * min x, y, z, max x, y, z order. A state vanilla's setter leaves alone keeps
 * whatever the shared Block object last held (a ladder with metadata 0, 1 or
 * 6 up, a button or a piston with direction 6 or 7, the moving piston); those
 * keep the constructor bounds here, which is what an untouched Block holds.
 * BlockPortal's metadata 0 (Teleporter.makePortal's) picks its axis from the
 * x neighbours and, in a server world, writes it back with flag 2. */
static void block_bounds(struct world *w, int x, int y, int z, double *b)
{
    int id = world_get_block(w, x, y, z) & 4095;
    const struct block_def *d = &BLOCKS[id];
    b[0] = d->min_x; b[1] = d->min_y; b[2] = d->min_z;
    b[3] = d->max_x; b[4] = d->max_y; b[5] = d->max_z;
    int meta;

    switch (id)
    {
    case 26: /* BlockBed.func_149978_e */
        set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.5625F, 1.0F);
        return;

    case 27: case 28: case 66: case 157: /* BlockRailBase */
        meta = world_get_meta(w, x, y, z);
        if (meta >= 2 && meta <= 5) set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.625F, 1.0F);
        else set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.125F, 1.0F);
        return;

    case 29: case 33: /* BlockPistonBase */
        meta = world_get_meta(w, x, y, z);
        if ((meta & 8) != 0)
        {
            switch (meta & 7)
            {
            case 0: set6(b, 0.0F, 0.25F, 0.0F, 1.0F, 1.0F, 1.0F); break;
            case 1: set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.75F, 1.0F); break;
            case 2: set6(b, 0.0F, 0.0F, 0.25F, 1.0F, 1.0F, 1.0F); break;
            case 3: set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.75F); break;
            case 4: set6(b, 0.25F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F); break;
            case 5: set6(b, 0.0F, 0.0F, 0.0F, 0.75F, 1.0F, 1.0F); break;
            }
        }
        else set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
        return;

    case 34: /* BlockPistonExtension */
        switch (world_get_meta(w, x, y, z) & 7)
        {
        case 0: set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.25F, 1.0F); break;
        case 1: set6(b, 0.0F, 0.75F, 0.0F, 1.0F, 1.0F, 1.0F); break;
        case 2: set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.25F); break;
        case 3: set6(b, 0.0F, 0.0F, 0.75F, 1.0F, 1.0F, 1.0F); break;
        case 4: set6(b, 0.0F, 0.0F, 0.0F, 0.25F, 1.0F, 1.0F); break;
        case 5: set6(b, 0.75F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F); break;
        }
        return;

    case 120: /* BlockEndPortalFrame: the 13/16 its collision boxes left */
        if (nw_env->collide.item_bounds[120]) set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.8125F, 1.0F);
        return;

    case 43: case 125: /* the double slabs: BlockSlab with field_150004_a */
        set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
        return;

    case 44: case 126:
        if ((world_get_meta(w, x, y, z) & 8) != 0) set6(b, 0.0F, 0.5F, 0.0F, 1.0F, 1.0F, 1.0F);
        else set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.5F, 1.0F);
        return;

    case 50: case 75: case 76:
    {
        /* BlockTorch.collisionRayTrace's shapes, by metadata & 7 (the
         * redstone torches inherit it) */
        int var7 = world_get_meta(w, x, y, z) & 7;
        float var8 = 0.15F;

        if (var7 == 1) set6(b, 0.0F, 0.2F, 0.5F - var8, var8 * 2.0F, 0.8F, 0.5F + var8);
        else if (var7 == 2) set6(b, 1.0F - var8 * 2.0F, 0.2F, 0.5F - var8, 1.0F, 0.8F, 0.5F + var8);
        else if (var7 == 3) set6(b, 0.5F - var8, 0.2F, 0.0F, 0.5F + var8, 0.8F, var8 * 2.0F);
        else if (var7 == 4) set6(b, 0.5F - var8, 0.2F, 1.0F - var8 * 2.0F, 0.5F + var8, 0.8F, 1.0F);
        else
        {
            var8 = 0.1F;
            set6(b, 0.5F - var8, 0.0F, 0.5F - var8, 0.5F + var8, 0.6F, 0.5F + var8);
        }
        return;
    }

    case 54: case 146: /* BlockChest: the double-chest insets */
        if ((world_get_block(w, x, y, z - 1) & 4095) == id)
            set6(b, 0.0625F, 0.0F, 0.0F, 0.9375F, 0.875F, 0.9375F);
        else if ((world_get_block(w, x, y, z + 1) & 4095) == id)
            set6(b, 0.0625F, 0.0F, 0.0625F, 0.9375F, 0.875F, 1.0F);
        else if ((world_get_block(w, x - 1, y, z) & 4095) == id)
            set6(b, 0.0F, 0.0F, 0.0625F, 0.9375F, 0.875F, 0.9375F);
        else if ((world_get_block(w, x + 1, y, z) & 4095) == id)
            set6(b, 0.0625F, 0.0F, 0.0625F, 1.0F, 0.875F, 0.9375F);
        else
            set6(b, 0.0625F, 0.0F, 0.0625F, 0.9375F, 0.875F, 0.9375F);
        /* setBlockBoundsBasedOnState writes the block object's bounds */
        collide_chest_bounds_set(id, b);
        return;

    case 68: /* BlockSign, the wall sign (the standing one never changes) */
    {
        float var6 = 0.28125F, var7 = 0.78125F, var8 = 0.0F, var9 = 1.0F, var10 = 0.125F;
        meta = world_get_meta(w, x, y, z);
        set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
        if (meta == 2) set6(b, var8, var6, 1.0F - var10, var9, var7, 1.0F);
        if (meta == 3) set6(b, var8, var6, 0.0F, var9, var7, var10);
        if (meta == 4) set6(b, 1.0F - var10, var6, var8, 1.0F, var7, var9);
        if (meta == 5) set6(b, 0.0F, var6, var8, var10, var7, var9);
        return;
    }

    case 64: case 71:
    {
        /* BlockDoor.func_150012_g and func_150011_b: the thin collision
         * plane depends on the lower facing and the upper hinge bit. */
        meta = world_get_meta(w, x, y, z);
        int lower = (meta & 8) ? world_get_meta(w, x, y - 1, z) : meta;
        int upper = (meta & 8) ? meta : world_get_meta(w, x, y + 1, z);
        int dir = lower & 3, open = (lower & 4) != 0, hinge = (upper & 1) != 0;
        const float thick = 0.1875F;
        set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
        if (dir == 0)
        {
            if (!open) b[3] = thick;
            else if (!hinge) b[5] = thick;
            else b[2] = 1.0F - thick;
        }
        else if (dir == 1)
        {
            if (!open) b[5] = thick;
            else if (!hinge) b[0] = 1.0F - thick;
            else b[3] = thick;
        }
        else if (dir == 2)
        {
            if (!open) b[0] = 1.0F - thick;
            else if (!hinge) b[2] = 1.0F - thick;
            else b[5] = thick;
        }
        else
        {
            if (!open) b[2] = 1.0F - thick;
            else if (!hinge) b[3] = thick;
            else b[0] = 1.0F - thick;
        }
        return;
    }

    case 65: /* BlockLadder.func_149797_b, over the shared bounds */
    {
        float f[6];
        collide_ladder_bounds(world_get_meta(w, x, y, z), f);
        set6(b, f[0], f[1], f[2], f[3], f[4], f[5]);
        return;
    }

    case 69: /* BlockLever */
    {
        int var5 = world_get_meta(w, x, y, z) & 7;
        float var6 = 0.1875F;

        if (var5 == 1) set6(b, 0.0F, 0.2F, 0.5F - var6, var6 * 2.0F, 0.8F, 0.5F + var6);
        else if (var5 == 2) set6(b, 1.0F - var6 * 2.0F, 0.2F, 0.5F - var6, 1.0F, 0.8F, 0.5F + var6);
        else if (var5 == 3) set6(b, 0.5F - var6, 0.2F, 0.0F, 0.5F + var6, 0.8F, var6 * 2.0F);
        else if (var5 == 4) set6(b, 0.5F - var6, 0.2F, 1.0F - var6 * 2.0F, 0.5F + var6, 0.8F, 1.0F);
        else if (var5 != 5 && var5 != 6)
        {
            var6 = 0.25F;
            set6(b, 0.5F - var6, 0.4F, 0.5F - var6, 0.5F + var6, 1.0F, 0.5F + var6);
        }
        else
        {
            var6 = 0.25F;
            set6(b, 0.5F - var6, 0.0F, 0.5F - var6, 0.5F + var6, 0.6F, 0.5F + var6);
        }
        return;
    }

    case 70: case 72: case 147: case 148:
    {
        /* BlockBasePressurePlate.func_150063_b: func_150060_c is 15 for a
         * stone or wooden plate at metadata 1, the metadata itself for the
         * weighted ones */
        meta = world_get_meta(w, x, y, z);
        int power = (id == 70 || id == 72) ? (meta == 1 ? 15 : 0) : meta;
        float var3 = 0.0625F;
        if (power > 0) set6(b, var3, 0.0F, var3, 1.0F - var3, 0.03125F, 1.0F - var3);
        else set6(b, var3, 0.0F, var3, 1.0F - var3, 0.0625F, 1.0F - var3);
        return;
    }

    case 77: case 143:
    {
        /* BlockButton.func_150043_b: the button occupies only a small
         * rectangle on its support face. */
        meta = world_get_meta(w, x, y, z);
        int var2 = meta & 7;
        float var4 = 0.375F, var5 = 0.625F, var6 = 0.1875F;
        float var7 = (meta & 8) > 0 ? 0.0625F : 0.125F;

        if (var2 == 1) set6(b, 0.0F, var4, 0.5F - var6, var7, var5, 0.5F + var6);
        else if (var2 == 2) set6(b, 1.0F - var7, var4, 0.5F - var6, 1.0F, var5, 0.5F + var6);
        else if (var2 == 3) set6(b, 0.5F - var6, var4, 0.0F, 0.5F + var6, var5, var7);
        else if (var2 == 4) set6(b, 0.5F - var6, var4, 1.0F - var7, 0.5F + var6, var5, 1.0F);
        return;
    }

    case 78: /* BlockSnow.func_150154_b */
    {
        int var2 = world_get_meta(w, x, y, z) & 7;
        float var3 = (float)(2 * (1 + var2)) / 16.0F;
        set6(b, 0.0F, 0.0F, 0.0F, 1.0F, var3, 1.0F);
        return;
    }

    case 85: case 113:
    {
        int var5 = connects(w, id, x, y, z - 1);
        int var6 = connects(w, id, x, y, z + 1);
        int var7 = connects(w, id, x - 1, y, z);
        int var8 = connects(w, id, x + 1, y, z);
        float x0 = var7 ? 0.0f : 0.375f, x1 = var8 ? 1.0f : 0.625f;
        float z0 = var5 ? 0.0f : 0.375f, z1 = var6 ? 1.0f : 0.625f;
        set6(b, x0, 0.0f, z0, x1, 1.0f, z1);
        /* setBlockBoundsBasedOnState writes the block object's bounds */
        collide_fence_bounds_set(id, x0, 0.0f, z0, x1, 1.0f, z1);
        return;
    }

    case 90: /* BlockPortal */
    {
        int var5 = world_get_meta(w, x, y, z) & 3;
        if (var5 == 0)
        {
            var5 = (world_get_block(w, x - 1, y, z) & 4095) != 90 &&
                   (world_get_block(w, x + 1, y, z) & 4095) != 90 ? 2 : 1;
            /* a server world keeps the axis it picked: Teleporter.makePortal's
             * meta-0 blocks get it the first time a ray trace crosses them */
            if (!w->is_remote) world_set_meta(w, x, y, z, var5, 2);
        }
        float var6 = 0.125F, var7 = 0.125F;
        if (var5 == 1) var6 = 0.5F;
        if (var5 == 2) var7 = 0.5F;
        set6(b, 0.5F - var6, 0.0F, 0.5F - var7, 0.5F + var6, 1.0F, 0.5F + var7);
        return;
    }

    case 92: /* BlockCake */
    {
        meta = world_get_meta(w, x, y, z);
        float var6 = 0.0625F;
        float var7 = (float)(1 + meta * 2) / 16.0F;
        set6(b, var7, 0.0F, var6, 1.0F - var6, 0.5F, 1.0F - var6);
        return;
    }

    case 96: /* BlockTrapDoor.func_150117_b */
    {
        meta = world_get_meta(w, x, y, z);
        float var2 = 0.1875F;
        if ((meta & 8) != 0) set6(b, 0.0F, 1.0F - var2, 0.0F, 1.0F, 1.0F, 1.0F);
        else set6(b, 0.0F, 0.0F, 0.0F, 1.0F, var2, 1.0F);
        if ((meta & 4) != 0)
        {
            if ((meta & 3) == 0) set6(b, 0.0F, 0.0F, 1.0F - var2, 1.0F, 1.0F, 1.0F);
            if ((meta & 3) == 1) set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, var2);
            if ((meta & 3) == 2) set6(b, 1.0F - var2, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
            if ((meta & 3) == 3) set6(b, 0.0F, 0.0F, 0.0F, var2, 1.0F, 1.0F);
        }
        return;
    }

    case 101: case 102: case 160: /* BlockPane: iron bars, the panes */
    {
        float var5 = 0.4375F, var6 = 0.5625F, var7 = 0.4375F, var8 = 0.5625F;
        int var9 = pane_connects(w, id, x, y, z - 1);
        int var10 = pane_connects(w, id, x, y, z + 1);
        int var11 = pane_connects(w, id, x - 1, y, z);
        int var12 = pane_connects(w, id, x + 1, y, z);

        if ((!var11 || !var12) && (var11 || var12 || var9 || var10))
        {
            if (var11 && !var12) var5 = 0.0F;
            else if (!var11 && var12) var6 = 1.0F;
        }
        else
        {
            var5 = 0.0F;
            var6 = 1.0F;
        }

        if ((!var9 || !var10) && (var11 || var12 || var9 || var10))
        {
            if (var9 && !var10) var7 = 0.0F;
            else if (!var9 && var10) var8 = 1.0F;
        }
        else
        {
            var7 = 0.0F;
            var8 = 1.0F;
        }

        set6(b, var5, 0.0F, var7, var6, 1.0F, var8);
        /* setBlockBoundsBasedOnState writes the pane object's bounds */
        collide_pane_bounds_set(id, var5, 0.0F, var7, var6, 1.0F, var8);
        return;
    }

    case 104: case 105: /* BlockStem: field_149756_F set, then read back */
    {
        double top = (double)((float)(world_get_meta(w, x, y, z) * 2 + 2) / 16.0F);
        float var5 = 0.125F;
        set6(b, 0.5F - var5, 0.0F, 0.5F - var5, 0.5F + var5, (float)top, 0.5F + var5);
        return;
    }

    case 106: /* BlockVine */
    {
        int var6 = world_get_meta(w, x, y, z);
        float var7 = 1.0F, var8 = 1.0F, var9 = 1.0F, var10 = 0.0F, var11 = 0.0F, var12 = 0.0F;
        int var13 = var6 > 0;

        if ((var6 & 2) != 0)
        {
            var10 = fmaxf(var10, 0.0625F);
            var7 = 0.0F; var8 = 0.0F; var11 = 1.0F; var9 = 0.0F; var12 = 1.0F;
            var13 = 1;
        }
        if ((var6 & 8) != 0)
        {
            var7 = fminf(var7, 0.9375F);
            var10 = 1.0F; var8 = 0.0F; var11 = 1.0F; var9 = 0.0F; var12 = 1.0F;
            var13 = 1;
        }
        if ((var6 & 4) != 0)
        {
            var12 = fmaxf(var12, 0.0625F);
            var9 = 0.0F; var7 = 0.0F; var10 = 1.0F; var8 = 0.0F; var11 = 1.0F;
            var13 = 1;
        }
        if ((var6 & 1) != 0)
        {
            var9 = fminf(var9, 0.9375F);
            var12 = 1.0F; var7 = 0.0F; var10 = 1.0F; var8 = 0.0F; var11 = 1.0F;
            var13 = 1;
        }
        if (!var13 && vine_support(world_get_block(w, x, y + 1, z)))
        {
            var8 = fminf(var8, 0.9375F);
            var11 = 1.0F; var7 = 0.0F; var10 = 1.0F; var9 = 0.0F; var12 = 1.0F;
        }
        set6(b, var7, var8, var9, var10, var11, var12);
        return;
    }

    case 107: /* BlockFenceGate */
        meta = world_get_meta(w, x, y, z) & 3;
        if (meta != 2 && meta != 0) set6(b, 0.375F, 0.0F, 0.0F, 0.625F, 1.0F, 1.0F);
        else set6(b, 0.0F, 0.0F, 0.375F, 1.0F, 1.0F, 0.625F);
        return;

    case 119: /* BlockEndPortal: 1/16 high, which the shared object keeps
               * for its pool box after */
        nw_env->collide.item_bounds[119] = 1;
        set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.0625F, 1.0F);
        return;

    case 127: /* BlockCocoa */
    {
        meta = world_get_meta(w, x, y, z);
        int var6 = meta & 3;
        int var7 = (meta & 12) >> 2;
        int var8 = 4 + var7 * 2;
        int var9 = 5 + var7 * 2;
        float var10 = (float)var8 / 2.0F;

        switch (var6)
        {
        case 0:
            set6(b, (8.0F - var10) / 16.0F, (12.0F - (float)var9) / 16.0F, (15.0F - (float)var8) / 16.0F,
                 (8.0F + var10) / 16.0F, 0.75F, 0.9375F);
            break;
        case 1:
            set6(b, 0.0625F, (12.0F - (float)var9) / 16.0F, (8.0F - var10) / 16.0F,
                 (1.0F + (float)var8) / 16.0F, 0.75F, (8.0F + var10) / 16.0F);
            break;
        case 2:
            set6(b, (8.0F - var10) / 16.0F, (12.0F - (float)var9) / 16.0F, 0.0625F,
                 (8.0F + var10) / 16.0F, 0.75F, (1.0F + (float)var8) / 16.0F);
            break;
        case 3:
            set6(b, (15.0F - (float)var8) / 16.0F, (12.0F - (float)var9) / 16.0F, (8.0F - var10) / 16.0F,
                 0.9375F, 0.75F, (8.0F + var10) / 16.0F);
            break;
        }
        return;
    }

    case 131: /* BlockTripWireHook */
    {
        int var5 = world_get_meta(w, x, y, z) & 3;
        float var6 = 0.1875F;
        if (var5 == 3) set6(b, 0.0F, 0.2F, 0.5F - var6, var6 * 2.0F, 0.8F, 0.5F + var6);
        else if (var5 == 1) set6(b, 1.0F - var6 * 2.0F, 0.2F, 0.5F - var6, 1.0F, 0.8F, 0.5F + var6);
        else if (var5 == 0) set6(b, 0.5F - var6, 0.2F, 0.0F, 0.5F + var6, 0.8F, var6 * 2.0F);
        else set6(b, 0.5F - var6, 0.2F, 1.0F - var6 * 2.0F, 0.5F + var6, 0.8F, 1.0F);
        return;
    }

    case 132: /* BlockTripWire */
        meta = world_get_meta(w, x, y, z);
        if ((meta & 2) != 2) set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.09375F, 1.0F);
        else if ((meta & 4) != 4) set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.5F, 1.0F);
        else set6(b, 0.0F, 0.0625F, 0.0F, 1.0F, 0.15625F, 1.0F);
        return;

    case 139:
    {
        int var5 = connects(w, id, x, y, z - 1);
        int var6 = connects(w, id, x, y, z + 1);
        int var7 = connects(w, id, x - 1, y, z);
        int var8 = connects(w, id, x + 1, y, z);
        float var9 = 0.25f, var10 = 0.75f, var11 = 0.25f, var12 = 0.75f, var13 = 1.0f;
        if (var5) var11 = 0.0f;
        if (var6) var12 = 1.0f;
        if (var7) var9 = 0.0f;
        if (var8) var10 = 1.0f;
        if (var5 && var6 && !var7 && !var8) {
            var13 = 0.8125f; var9 = 0.3125f; var10 = 0.6875f;
        } else if (!var5 && !var6 && var7 && var8) {
            var13 = 0.8125f; var11 = 0.3125f; var12 = 0.6875f;
        }
        set6(b, var9, 0.0f, var11, var10, var13, var12);
        return;
    }

    case 144: /* BlockSkull */
        switch (world_get_meta(w, x, y, z) & 7)
        {
        case 2: set6(b, 0.25F, 0.25F, 0.5F, 0.75F, 0.75F, 1.0F); break;
        case 3: set6(b, 0.25F, 0.25F, 0.0F, 0.75F, 0.75F, 0.5F); break;
        case 4: set6(b, 0.5F, 0.25F, 0.25F, 1.0F, 0.75F, 0.75F); break;
        case 5: set6(b, 0.0F, 0.25F, 0.25F, 0.5F, 0.75F, 0.75F); break;
        default: set6(b, 0.25F, 0.0F, 0.25F, 0.75F, 0.5F, 0.75F); break;
        }
        return;

    case 145: /* BlockAnvil */
        meta = world_get_meta(w, x, y, z) & 3;
        if (meta != 3 && meta != 1) set6(b, 0.125F, 0.0F, 0.0F, 0.875F, 1.0F, 1.0F);
        else set6(b, 0.0F, 0.0F, 0.125F, 1.0F, 1.0F, 0.875F);
        return;

    case 117: /* BlockBrewingStand: the object's bounds, the full cube until
               * the first collision query leaves its base (collide.c) */
        set6(b, 0.0F, 0.0F, 0.0F, 1.0F, nw_env->collide.brewing_base ? 0.125F : 1.0F, 1.0F);
        return;

    case 151: /* BlockDaylightDetector */
        set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 0.375F, 1.0F);
        return;

    case 154: case 175: /* BlockHopper, BlockDoublePlant */
        set6(b, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F);
        return;

    case 171: /* BlockCarpet.func_150089_b */
        set6(b, 0.0F, 0.0F, 0.0F, 1.0F, (float)(1 * (1 + 0)) / 16.0F, 1.0F);
        return;
    }
}

void raytrace_block_bounds(struct world *w, int x, int y, int z, double *b)
{
    block_bounds(w, x, y, z, b);
}

/* Block.collisionRayTrace's body over the bounds b: the six face crossings,
 * the nearest inside-bounds one wins (ties keep the earlier candidate, which
 * is what the reference equality in the decompile does), the face from which
 * one. */
static int bounds_ray_trace(const double *b, int bx, int by, int bz,
                            double sx, double sy, double sz,
                            double ex, double ey, double ez,
                            struct rt_mop *out)
{
    double min_x = b[0], min_y = b[1], min_z = b[2], max_x = b[3], max_y = b[4], max_z = b[5];
    double vx0 = sx - (double)bx, vy0 = sy - (double)by, vz0 = sz - (double)bz;
    double vx1 = ex - (double)bx, vy1 = ey - (double)by, vz1 = ez - (double)bz;

    double v7[3] = {0}, v8[3] = {0}, v9[3] = {0}, v10[3] = {0}, v11[3] = {0}, v12[3] = {0};
    int ok7 = intermediate(v7, vx0, vy0, vz0, vx1, vy1, vz1, min_x, 0);
    int ok8 = intermediate(v8, vx0, vy0, vz0, vx1, vy1, vz1, max_x, 0);
    int ok9 = intermediate(v9, vx0, vy0, vz0, vx1, vy1, vz1, min_y, 1);
    int ok10 = intermediate(v10, vx0, vy0, vz0, vx1, vy1, vz1, max_y, 1);
    int ok11 = intermediate(v11, vx0, vy0, vz0, vx1, vy1, vz1, min_z, 2);
    int ok12 = intermediate(v12, vx0, vy0, vz0, vx1, vy1, vz1, max_z, 2);

    /* isVecInsideYZBounds(v7/v8), XZBounds(v9/v10), XYBounds(v11/v12) */
    if (ok7 && !(v7[1] >= min_y && v7[1] <= max_y && v7[2] >= min_z && v7[2] <= max_z)) ok7 = 0;
    if (ok8 && !(v8[1] >= min_y && v8[1] <= max_y && v8[2] >= min_z && v8[2] <= max_z)) ok8 = 0;
    if (ok9 && !(v9[0] >= min_x && v9[0] <= max_x && v9[2] >= min_z && v9[2] <= max_z)) ok9 = 0;
    if (ok10 && !(v10[0] >= min_x && v10[0] <= max_x && v10[2] >= min_z && v10[2] <= max_z)) ok10 = 0;
    if (ok11 && !(v11[0] >= min_x && v11[0] <= max_x && v11[1] >= min_y && v11[1] <= max_y)) ok11 = 0;
    if (ok12 && !(v12[0] >= min_x && v12[0] <= max_x && v12[1] >= min_y && v12[1] <= max_y)) ok12 = 0;

    int picked = -1;
    double best[3] = {0.0, 0.0, 0.0}, bestd = 0.0;
    const int oks[6] = {ok7, ok8, ok9, ok10, ok11, ok12};
    const double *vs[6] = {v7, v8, v9, v10, v11, v12};

    for (int i = 0; i < 6; ++i)
    {
        if (!oks[i]) continue;
        double d = sq_dist(vx0, vy0, vz0, vs[i][0], vs[i][1], vs[i][2]);
        if (picked < 0 || d < bestd)
        {
            picked = i;
            best[0] = vs[i][0]; best[1] = vs[i][1]; best[2] = vs[i][2];
            bestd = d;
        }
    }

    if (picked < 0) return 0;

    static const int side[6] = {4, 5, 0, 1, 2, 3};
    out->hit = 1;
    out->type = side[picked];
    out->face = side[picked];
    out->x = bx;
    out->y = by;
    out->z = bz;
    out->hx = best[0] + (double)bx;
    out->hy = best[1] + (double)by;
    out->hz = best[2] + (double)bz;
    return 1;
}

/* The stair blocks (BlockStairs.func_150148_a's instanceof). */
static int is_stairs(int id)
{
    switch (id & 4095)
    {
    case 53: case 67: case 108: case 109: case 114: case 128: case 134: case 135:
    case 136: case 156: case 163: case 164:
        return 1;
    }
    return 0;
}

/* BlockStairs.collisionRayTrace: Block's trace over each of the eight half
 * cubes (field_150153_O = octant, x from bit 0, y from bit 1, z from bit 2),
 * the two octants field_150150_a names for the facing and the upside-down bit
 * dropped, then the hit whose hitVec lies farthest from the END point, first
 * one on a tie, and none at distance 0. */
static int stairs_ray_trace(struct world *w, int bx, int by, int bz,
                            double sx, double sy, double sz,
                            double ex, double ey, double ez,
                            struct rt_mop *out)
{
    static const int empty[8][2] = {{2, 6}, {3, 7}, {2, 3}, {6, 7}, {0, 4}, {1, 5}, {0, 1}, {4, 5}};
    int meta = world_get_meta(w, bx, by, bz);

    collide_stairs_traced(world_get_block(w, bx, by, bz));
    const int *skip = empty[(meta & 3) + ((meta & 4) == 4 ? 4 : 0)];
    struct rt_mop hits[8];
    int have[8];

    for (int i = 0; i < 8; ++i)
    {
        double b[6];
        set6(b, 0.5F * (float)(i % 2), 0.5F * (float)(i / 2 % 2), 0.5F * (float)(i / 4 % 2),
             0.5F + 0.5F * (float)(i % 2), 0.5F + 0.5F * (float)(i / 2 % 2), 0.5F + 0.5F * (float)(i / 4 % 2));
        have[i] = bounds_ray_trace(b, bx, by, bz, sx, sy, sz, ex, ey, ez, &hits[i]);
    }
    have[skip[0]] = 0;
    have[skip[1]] = 0;

    int chosen = -1;
    double far = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        if (!have[i]) continue;
        double dx = ex - hits[i].hx, dy = ey - hits[i].hy, dz = ez - hits[i].hz;
        double d = dx * dx + dy * dy + dz * dz;
        if (nw_env->cfg.raytrace_negative_stairs_nearest ? (chosen < 0 || d < far) : d > far)
        {
            chosen = i;
            far = d;
        }
    }
    if (chosen < 0) return 0;
    *out = hits[chosen];
    return 1;
}

int raytrace_collision(struct world *w, int bx, int by, int bz,
                       double sx, double sy, double sz,
                       double ex, double ey, double ez,
                       struct rt_mop *out)
{
    if (is_stairs(world_get_block(w, bx, by, bz)))
        return stairs_ray_trace(w, bx, by, bz, sx, sy, sz, ex, ey, ez, out);

    double b[6];
    block_bounds(w, bx, by, bz, b);
    return bounds_ray_trace(b, bx, by, bz, sx, sy, sz, ex, ey, ez, out);
}

/* The walk's per-block call. */
static int collision_ray_trace(struct world *w, int bx, int by, int bz,
                               double sx, double sy, double sz,
                               double ex, double ey, double ez,
                               struct rt_mop *out)
{
    return raytrace_collision(w, bx, by, bz, sx, sy, sz, ex, ey, ez, out);
}

static int isnan_(double v)
{
    return v != v;
}

int raytrace_blocks(struct world *w, double sx, double sy, double sz,
                    double ex, double ey, double ez,
                    int stop_on_liquid, int ignore_no_bbox, int return_last,
                    struct rt_mop *out)
{
    if (isnan_(sx) || isnan_(sy) || isnan_(sz)) return 0;
    if (isnan_(ex) || isnan_(ey) || isnan_(ez)) return 0;

    int var6 = mh_floor(ex), var7 = mh_floor(ey), var8 = mh_floor(ez);
    int var9 = mh_floor(sx), var10 = mh_floor(sy), var11 = mh_floor(sz);
    struct rt_mop var40;
    memset(&var40, 0, sizeof var40);
    var40.type = RT_MISS;
    int have40 = 0;

    int id = world_get_block(w, var9, var10, var11) & 4095;
    int meta = world_get_meta(w, var9, var10, var11);
    struct rt_mop first;

    if ((!ignore_no_bbox || collide_pool_box(w, var9, var10, var11, &(struct aabb){0})) &&
        can_collide_check(id, meta, stop_on_liquid))
    {
        if (collision_ray_trace(w, var9, var10, var11, sx, sy, sz, ex, ey, ez, &first))
        {
            *out = first;
            return 1;
        }
    }

    int var13 = 200;

    while (var13-- >= 0)
    {
        if (isnan_(sx) || isnan_(sy) || isnan_(sz)) return 0;

        if (var9 == var6 && var10 == var7 && var11 == var8)
        {
            if (return_last && have40)
            {
                *out = var40;
                return 1;
            }

            return 0;
        }

        int var41 = 1, var15 = 1, var16 = 1;
        double var17 = 999.0, var19 = 999.0, var21 = 999.0;

        if (var6 > var9) var17 = (double)var9 + 1.0;
        else if (var6 < var9) var17 = (double)var9 + 0.0;
        else var41 = 0;

        if (var7 > var10) var19 = (double)var10 + 1.0;
        else if (var7 < var10) var19 = (double)var10 + 0.0;
        else var15 = 0;

        if (var8 > var11) var21 = (double)var11 + 1.0;
        else if (var8 < var11) var21 = (double)var11 + 0.0;
        else var16 = 0;

        double var23 = 999.0, var25 = 999.0, var27 = 999.0;
        double var29 = ex - sx, var31 = ey - sy, var33 = ez - sz;

        if (var41) var23 = (var17 - sx) / var29;
        if (var15) var25 = (var19 - sy) / var31;
        if (var16) var27 = (var21 - sz) / var33;

        int var42;

        if (var23 < var25 && var23 < var27)
        {
            var42 = var6 > var9 ? 4 : 5;
            sx = var17;
            sy += var31 * var23;
            sz += var33 * var23;
        }
        else if (var25 < var27)
        {
            var42 = var7 > var10 ? 0 : 1;
            sx += var29 * var25;
            sy = var19;
            sz += var33 * var25;
        }
        else
        {
            var42 = var8 > var11 ? 2 : 3;
            sx += var29 * var27;
            sy += var31 * var27;
            sz = var21;
        }

        var9 = mh_floor(sx);
        if (var42 == 5) --var9;
        var10 = mh_floor(sy);
        if (var42 == 1) --var10;
        var11 = mh_floor(sz);
        if (var42 == 3) --var11;

        id = world_get_block(w, var9, var10, var11) & 4095;
        meta = world_get_meta(w, var9, var10, var11);

        if (!ignore_no_bbox || collide_pool_box(w, var9, var10, var11, &(struct aabb){0}))
        {
            if (can_collide_check(id, meta, stop_on_liquid))
            {
                struct rt_mop hit;

                if (collision_ray_trace(w, var9, var10, var11, sx, sy, sz, ex, ey, ez, &hit))
                {
                    *out = hit;
                    return 1;
                }
            }
            else
            {
                var40.hit = 1;
                var40.type = RT_MISS;
                var40.face = var42;
                var40.x = var9;
                var40.y = var10;
                var40.z = var11;
                var40.hx = sx;
                var40.hy = sy;
                var40.hz = sz;
                have40 = 1;
            }
        }
    }

    if (return_last && have40)
    {
        *out = var40;
        return 1;
    }

    return 0;
}

int raytrace_trace(struct world *w, double sx, double sy, double sz,
                   double ex, double ey, double ez, struct rt_mop *out)
{
    return raytrace_blocks(w, sx, sy, sz, ex, ey, ez, 0, 0, 0, out);
}
