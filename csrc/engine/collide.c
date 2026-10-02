/* Block.addCollisionBoxesToList for the 1.7.10 blocks the move probe's shape
 * table places, plus the plain-cube default every other block uses.
 *
 * Vanilla keeps the bounds in six mutable fields on the block object, and each
 * addCollisionBoxesToList calls setBlockBounds once per box it wants. Blocks
 * that override getCollisionBoundingBoxFromPool (snow, cactus, cake, soul sand,
 * lily pad, fence gate, wall, ladder, trapdoor) compute the box inside that
 * call instead; Block.addCollisionBoxesToList just calls it on the bounds the
 * override left. Stairs call super three times with three different bounds, a
 * fence up to three times, a pane twice.
 *
 * The port drops the shared mutable state and builds an explicit box per call,
 * which is what the Java sequence computes. Every float expression is kept in
 * float: (float)(p_x + 1) - 0.0625F rounds in float in Java and only then
 * extends to double, so folding it into a double expression would lose the
 * last bit of some cactus and cake boxes. */
#include "collide.h"
#include "env.h"
#include "blocks.h"
#include "world.h"

#include <stdlib.h>
#include <string.h>

/* Block.getCollisionBoundingBoxFromPool from literally-set bounds, widened the
 * way Java's implicit float to double conversion does. */
static struct aabb from_bounds(float b0, float b1, float b2, float b3, float b4, float b5, int x, int y, int z)
{
    return aabb_make((double)x + (double)b0, (double)y + (double)b1, (double)z + (double)b2,
                     (double)x + (double)b3, (double)y + (double)b4, (double)z + (double)b5);
}

/* BlockFence's bounds are one object's mutable fields: every
 * addCollisionBoxesToList and setBlockBoundsBasedOnState leaves them at the
 * fence it last saw (max Y 1.0), and getCollisionBoundingBoxFromPool reports
 * them for any fence. [0] the oak fence (85), [1] the nether brick fence
 * (113); the constructor's full cube until the first call. */
#define fence_bounds (nw_env->collide.fence_bounds)

void collide_env_init(float fence[2][6], float chest[2][6], float ladder[6])
{
    for (int i = 0; i < 2; ++i)
    {
        fence[i][0] = fence[i][1] = fence[i][2] = 0.0F;
        fence[i][3] = fence[i][4] = fence[i][5] = 1.0F;
        /* BlockChest's constructor bounds */
        float *c = chest[i];
        c[0] = 0.0625F; c[1] = 0.0F; c[2] = 0.0625F; c[3] = 0.9375F; c[4] = 0.875F; c[5] = 0.9375F;
    }
    /* BlockLadder's constructor leaves Block's full cube */
    float *l = ladder;
    l[0] = l[1] = l[2] = 0.0F;
    l[3] = l[4] = l[5] = 1.0F;
}

void collide_ladder_bounds(int meta, float b[6])
{
    float var3 = 0.125F, *l = nw_env->collide.ladder_bounds;
    float v[4][6] = {{0.0F, 0.0F, 1.0F - var3, 1.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, var3},
                     {1.0F - var3, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 0.0F, var3, 1.0F, 1.0F}};
    if (meta >= 2 && meta <= 5) memcpy(l, v[meta - 2], sizeof v[0]);
    memcpy(b, l, 6 * sizeof *b);
}

void collide_chest_bounds_set(int id, const double *b)
{
    float *c = nw_env->collide.chest_bounds[id == 146 ? 1 : 0];
    for (int i = 0; i < 6; ++i) c[i] = (float)b[i];
}

/* Each BlockStairs object's bounds are shared too. addCollisionBoxesToList
 * ends on the full cube; collisionRayTrace sets field_150152_N for good
 * (nothing clears it) and leaves the last octant's bounds, octant 7, which
 * setBlockBoundsBasedOnState then keeps. getCollisionBoundingBoxFromPool
 * (Block's own, BlockStairs has none) reports whichever the last call left:
 * 1 here for the octant, 0 (the constructor's full cube) otherwise. */
#define stairs_octant (nw_env->collide.stairs_octant)

/* The BlockBrewingStand object's bounds: the constructor's full cube (0)
 * until the first addCollisionBoxesToList, which ends on
 * setBlockBoundsForItemRender's 1/8 high base (1) for good. Block's
 * getCollisionBoundingBoxFromPool and the ray trace
 * (setBlockBoundsBasedOnState is Block's empty one) read them. */
#define brewing_base (nw_env->collide.brewing_base)

void collide_stairs_traced(int id)
{
    stairs_octant[id & 4095] = 1;
}

int collide_stairs_octant(int id)
{
    return stairs_octant[id & 4095];
}

void collide_fence_bounds_set(int id, float x0, float y0, float z0, float x1, float y1, float z1)
{
    float *b = fence_bounds[id == 113 ? 1 : 0];
    b[0] = x0; b[1] = y0; b[2] = z0; b[3] = x1; b[4] = y1; b[5] = z1;
}

static int pane_slot(int id)
{
    return id == 101 ? 0 : id == 102 ? 1 : 2;
}

void collide_pane_bounds_set(int id, float x0, float y0, float z0, float x1, float y1, float z1)
{
    float *b = nw_env->collide.pane_bounds[pane_slot(id)];
    b[0] = x0; b[1] = y0; b[2] = z0; b[3] = x1; b[4] = y1; b[5] = z1;
    nw_env->collide.pane_set[pane_slot(id)] = 1;
}

/* BlockPane.addCollisionBoxesToList's setBlockBounds, then super's one box */
static void pane_box(struct collide_list *l, const struct aabb *query, int id, int x, int y, int z,
                     float x0, float z0, float x1, float z1);

static void add_query(struct collide_list *l, const struct aabb *query, struct aabb box)
{
    if (query != NULL && aabb_intersects(query, &box))
    {
        collide_list_push(l, &box);
    }
}

static int block_id(struct world *w, int x, int y, int z)
{
    return world_get_block(w, x, y, z) & 4095;
}

static int block_meta(struct world *w, int x, int y, int z)
{
    return world_get_meta(w, x, y, z) & 15;
}

static int is_stairs(int id)
{
    const char *n = BLOCKS[id & 4095].class_name;
    return n != NULL && strcmp(n, "BlockStairs") == 0;
}

/* BlockStairs.func_150146_f: a neighbor block is the same stair shape when it
 * is a stair and its metadata is exactly this one's. */
static int same_stairs(struct world *w, int x, int y, int z, int meta)
{
    return is_stairs(block_id(w, x, y, z)) && block_meta(w, x, y, z) == meta;
}

/* BlockFence.func_149826_e and BlockWall.func_150091_e share a body: the
 * neighbor connects when it is the same object, or a fence gate, or an opaque
 * material whose render shape is a normal block. */
static int connects(struct world *w, int self_id, int nx, int ny, int nz)
{
    int n = block_id(w, nx, ny, nz);

    if (n == self_id) return 1;
    if (n == 107) return 1; /* Blocks.fence_gate */
    if (!BLOCKS[n].exists) return 0;

    return BLOCKS[n].material != 0 && MATERIALS[BLOCKS[n].material].is_opaque && BLOCKS[n].normal_block &&
           BLOCKS[n].material != 16; /* Material.field_151572_C */
}

/* BlockPane.func_150098_a: an opaque cube, this pane, glass, stained glass, a
 * stained pane, or any pane. */
static int pane_connects(struct world *w, int nx, int ny, int nz)
{
    int n = block_id(w, nx, ny, nz);

    if (!BLOCKS[n].exists) return 0;
    if (BLOCKS[n].opaque_cube) return 1;
    if (n == 20 || n == 95 || n == 160 || n == 102 || n == 101) return 1;
    if (BLOCKS[n].class_name != NULL && strcmp(BLOCKS[n].class_name, "BlockPane") == 0) return 1;
    if (BLOCKS[n].class_name != NULL && strcmp(BLOCKS[n].class_name, "BlockStainedGlassPane") == 0) return 1;
    return 0;
}

/* BlockStairs.func_150145_f. The six out floats are the bounds it leaves and
 * the return is var13, whether addCollisionBoxesToList also asks for the
 * corner box. */
static int stairs_mid(struct world *w, int x, int y, int z, int meta, float *b)
{
    int var6 = meta & 3;
    float var7 = 0.5F;
    float var8 = 1.0F;

    if ((meta & 4) != 0)
    {
        var7 = 0.0F;
        var8 = 0.5F;
    }

    float var9 = 0.0F;
    float var10 = 1.0F;
    float var11 = 0.0F;
    float var12 = 0.5F;
    int var13 = 1;

    if (var6 == 0)
    {
        var9 = 0.5F;
        var12 = 1.0F;

        int nid = block_id(w, x + 1, y, z);
        int nmeta = block_meta(w, x + 1, y, z);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 3 && !same_stairs(w, x, y, z + 1, meta)) { var12 = 0.5F; var13 = 0; }
            else if (var16 == 2 && !same_stairs(w, x, y, z - 1, meta)) { var11 = 0.5F; var13 = 0; }
        }
    }
    else if (var6 == 1)
    {
        var10 = 0.5F;
        var12 = 1.0F;

        int nid = block_id(w, x - 1, y, z);
        int nmeta = block_meta(w, x - 1, y, z);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 3 && !same_stairs(w, x, y, z + 1, meta)) { var12 = 0.5F; var13 = 0; }
            else if (var16 == 2 && !same_stairs(w, x, y, z - 1, meta)) { var11 = 0.5F; var13 = 0; }
        }
    }
    else if (var6 == 2)
    {
        var11 = 0.5F;
        var12 = 1.0F;

        int nid = block_id(w, x, y, z + 1);
        int nmeta = block_meta(w, x, y, z + 1);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 1 && !same_stairs(w, x + 1, y, z, meta)) { var10 = 0.5F; var13 = 0; }
            else if (var16 == 0 && !same_stairs(w, x - 1, y, z, meta)) { var9 = 0.5F; var13 = 0; }
        }
    }
    else if (var6 == 3)
    {
        int nid = block_id(w, x, y, z - 1);
        int nmeta = block_meta(w, x, y, z - 1);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 1 && !same_stairs(w, x + 1, y, z, meta)) { var10 = 0.5F; var13 = 0; }
            else if (var16 == 0 && !same_stairs(w, x - 1, y, z, meta)) { var9 = 0.5F; var13 = 0; }
        }
    }

    b[0] = var9;
    b[1] = var7;
    b[2] = var11;
    b[3] = var10;
    b[4] = var8;
    b[5] = var12;
    return var13;
}

/* BlockStairs.func_150144_g: the corner box. Returns whether it wants one and
 * leaves the bounds in b. */
static int stairs_corner(struct world *w, int x, int y, int z, int meta, float *b)
{
    int var6 = meta & 3;
    float var7 = 0.5F;
    float var8 = 1.0F;

    if ((meta & 4) != 0)
    {
        var7 = 0.0F;
        var8 = 0.5F;
    }

    float var9 = 0.0F;
    float var10 = 0.5F;
    float var11 = 0.5F;
    float var12 = 1.0F;
    int var13 = 0;

    if (var6 == 0)
    {
        int nid = block_id(w, x - 1, y, z);
        int nmeta = block_meta(w, x - 1, y, z);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 3 && !same_stairs(w, x, y, z - 1, meta)) { var11 = 0.0F; var12 = 0.5F; var13 = 1; }
            else if (var16 == 2 && !same_stairs(w, x, y, z + 1, meta)) { var11 = 0.5F; var12 = 1.0F; var13 = 1; }
        }
    }
    else if (var6 == 1)
    {
        int nid = block_id(w, x + 1, y, z);
        int nmeta = block_meta(w, x + 1, y, z);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            var9 = 0.5F;
            var10 = 1.0F;
            int var16 = nmeta & 3;

            if (var16 == 3 && !same_stairs(w, x, y, z - 1, meta)) { var11 = 0.0F; var12 = 0.5F; var13 = 1; }
            else if (var16 == 2 && !same_stairs(w, x, y, z + 1, meta)) { var11 = 0.5F; var12 = 1.0F; var13 = 1; }
        }
    }
    else if (var6 == 2)
    {
        int nid = block_id(w, x, y, z - 1);
        int nmeta = block_meta(w, x, y, z - 1);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            var11 = 0.0F;
            var12 = 0.5F;
            int var16 = nmeta & 3;

            if (var16 == 1 && !same_stairs(w, x - 1, y, z, meta)) { var13 = 1; }
            else if (var16 == 0 && !same_stairs(w, x + 1, y, z, meta)) { var9 = 0.5F; var10 = 1.0F; var13 = 1; }
        }
    }
    else if (var6 == 3)
    {
        int nid = block_id(w, x, y, z + 1);
        int nmeta = block_meta(w, x, y, z + 1);

        if (is_stairs(nid) && (meta & 4) == (nmeta & 4))
        {
            int var16 = nmeta & 3;

            if (var16 == 1 && !same_stairs(w, x - 1, y, z, meta)) { var13 = 1; }
            else if (var16 == 0 && !same_stairs(w, x + 1, y, z, meta)) { var9 = 0.5F; var10 = 1.0F; var13 = 1; }
        }
    }

    b[0] = var9;
    b[1] = var7;
    b[2] = var11;
    b[3] = var10;
    b[4] = var8;
    b[5] = var12;
    return var13;
}

/* BlockCocoa.setBlockBoundsBasedOnState's bounds from a metadata: the pod's
 * six relative bounds. BlockCocoa.getCollisionBoundingBoxFromPool runs this
 * and then returns the block's own bounds, so the pod is what the collision
 * list and the pool box see. */
static void cocoa_bounds(int meta, float *b)
{
    int facing = meta & 3;
    int size = (meta & 12) >> 2;
    int var8 = 4 + size * 2;
    int var9 = 5 + size * 2;
    float var10 = (float)var8 / 2.0F;

    switch (facing)
    {
        case 0:
            b[0] = (8.0F - var10) / 16.0F; b[1] = (12.0F - (float)var9) / 16.0F; b[2] = (15.0F - (float)var8) / 16.0F;
            b[3] = (8.0F + var10) / 16.0F; b[4] = 0.75F; b[5] = 0.9375F;
            break;

        case 1:
            b[0] = 0.0625F; b[1] = (12.0F - (float)var9) / 16.0F; b[2] = (8.0F - var10) / 16.0F;
            b[3] = (1.0F + (float)var8) / 16.0F; b[4] = 0.75F; b[5] = (8.0F + var10) / 16.0F;
            break;

        case 2:
            b[0] = (8.0F - var10) / 16.0F; b[1] = (12.0F - (float)var9) / 16.0F; b[2] = 0.0625F;
            b[3] = (8.0F + var10) / 16.0F; b[4] = 0.75F; b[5] = (1.0F + (float)var8) / 16.0F;
            break;

        default:
            b[0] = (15.0F - (float)var8) / 16.0F; b[1] = (12.0F - (float)var9) / 16.0F; b[2] = (8.0F - var10) / 16.0F;
            b[3] = 0.9375F; b[4] = 0.75F; b[5] = (8.0F + var10) / 16.0F;
            break;
    }
}

/* BlockSkull.setBlockBoundsBasedOnState: the floor skull (1, and every
 * other value) or the wall skull's facing (2 to 5), metadata & 7. */
static void skull_bounds(int meta, float *b)
{
    static const float B[6][6] = {
        {0.25F, 0.0F, 0.25F, 0.75F, 0.5F, 0.75F},
        {0.25F, 0.25F, 0.5F, 0.75F, 0.75F, 1.0F},
        {0.25F, 0.25F, 0.0F, 0.75F, 0.75F, 0.5F},
        {0.5F, 0.25F, 0.25F, 1.0F, 0.75F, 0.75F},
        {0.0F, 0.25F, 0.25F, 0.5F, 0.75F, 0.75F},
    };
    int m = meta & 7;
    int k = m >= 2 && m <= 5 ? m - 1 : 0;

    for (int i = 0; i < 6; ++i) b[i] = B[k][i];
}

void collide_list_clear(struct collide_list *l)
{
    l->n = 0;
    l->impure = 0;
}

void collide_list_push(struct collide_list *l, const struct aabb *b)
{
    if (l->box == NULL)
    {
        l->box = l->first;
        l->cap = COLLIDE_MAX_BOXES;
    }
    if (l->n == l->cap)
    {
        struct aabb *grown = slab_take((size_t)l->cap * 2 * sizeof *grown);
        memcpy(grown, l->box, (size_t)l->n * sizeof *grown);
        if (l->box != l->first) slab_give(l->box, (size_t)l->cap * sizeof *l->box);
        l->box = grown;
        l->cap *= 2;
    }
    l->box[l->n++] = *b;
}


static struct aabb door_bounds(struct world *w, int x, int y, int z)
{
    int var5 = block_meta(w, x, y, z);
    int var6 = (var5 & 8) != 0;
    int var7 = var6 ? block_meta(w, x, y - 1, z) : var5;
    int var8 = var6 ? var5 : block_meta(w, x, y + 1, z);
    int var9 = (var8 & 1) != 0;
    int state = (var7 & 7) | (var6 ? 8 : 0) | (var9 ? 16 : 0);

    float var2 = 0.1875F;
    int var3 = state & 3;
    int var4 = (state & 4) != 0;
    int is_hinge_right = (state & 16) != 0;

    float b0 = 0.0F, b1 = 0.0F, b2 = 0.0F, b3 = 1.0F, b4 = 1.0F, b5 = 1.0F;

    if (var3 == 0)
    {
        if (var4)
        {
            if (!is_hinge_right) { b0 = 0.0F; b1 = 0.0F; b2 = 0.0F; b3 = 1.0F; b4 = 1.0F; b5 = var2; }
            else                 { b0 = 0.0F; b1 = 0.0F; b2 = 1.0F - var2; b3 = 1.0F; b4 = 1.0F; b5 = 1.0F; }
        }
        else
        {
            b0 = 0.0F; b1 = 0.0F; b2 = 0.0F; b3 = var2; b4 = 1.0F; b5 = 1.0F;
        }
    }
    else if (var3 == 1)
    {
        if (var4)
        {
            if (!is_hinge_right) { b0 = 1.0F - var2; b1 = 0.0F; b2 = 0.0F; b3 = 1.0F; b4 = 1.0F; b5 = 1.0F; }
            else                 { b0 = 0.0F; b1 = 0.0F; b2 = 0.0F; b3 = var2; b4 = 1.0F; b5 = 1.0F; }
        }
        else
        {
            b0 = 0.0F; b1 = 0.0F; b2 = 0.0F; b3 = 1.0F; b4 = 1.0F; b5 = var2;
        }
    }
    else if (var3 == 2)
    {
        if (var4)
        {
            if (!is_hinge_right) { b0 = 0.0F; b1 = 0.0F; b2 = 1.0F - var2; b3 = 1.0F; b4 = 1.0F; b5 = 1.0F; }
            else                 { b0 = 0.0F; b1 = 0.0F; b2 = 0.0F; b3 = 1.0F; b4 = 1.0F; b5 = var2; }
        }
        else
        {
            b0 = 1.0F - var2; b1 = 0.0F; b2 = 0.0F; b3 = 1.0F; b4 = 1.0F; b5 = 1.0F;
        }
    }
    else if (var3 == 3)
    {
        if (var4)
        {
            if (!is_hinge_right) { b0 = 0.0F; b1 = 0.0F; b2 = 0.0F; b3 = var2; b4 = 1.0F; b5 = 1.0F; }
            else                 { b0 = 1.0F - var2; b1 = 0.0F; b2 = 0.0F; b3 = 1.0F; b4 = 1.0F; b5 = 1.0F; }
        }
        else
        {
            b0 = 0.0F; b1 = 0.0F; b2 = 1.0F - var2; b3 = 1.0F; b4 = 1.0F; b5 = 1.0F;
        }
    }

    return from_bounds(b0, b1, b2, b3, b4, b5, x, y, z);
}

void collide_add_boxes(struct world *w, int x, int y, int z, const struct aabb *query, struct collide_list *l)
{
    collide_add_boxes_id(w, x, y, z, block_id(w, x, y, z), query, l);
}

void collide_add_boxes_id(struct world *w, int x, int y, int z, int id, const struct aabb *query, struct collide_list *l)
{
    if (!BLOCKS[id].collidable) return;

    switch (id)
    {
    case 26: /* BlockBed.getCollisionBoundingBoxFromPool -> func_149978_e */
        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 0.5625F, 1.0F, x, y, z));
        return;

    case 54: case 146: /* Block's own, over the chest object's shared bounds (the
                        * last ray trace over a chest set them) */
    {
        l->impure = 1;
        const float *b = nw_env->collide.chest_bounds[id == 146 ? 1 : 0];
        add_query(l, query, from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z));
        return;
    }

    case 127: /* BlockCocoa: the pod's own box, not the full cube */
    {
        float b[6];
        cocoa_bounds(block_meta(w, x, y, z), b);
        add_query(l, query, from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z));
        return;
    }

    case 30: /* BlockWeb.getCollisionBoundingBoxFromPool: null */
    case 106: /* BlockVine.getCollisionBoundingBoxFromPool: null */
    case 9: case 8: case 10: case 11: /* BlockLiquid: null */
    case 0: /* BlockAir.getCollisionBoundingBoxFromPool: null */
    /* the rest of the null overrides, by class:
     * BlockBush (and BlockSapling, TallGrass, DeadBush, Flower, Mushroom,
     * Crops, Stem, NetherWart, DoublePlant), BlockReed, BlockTorch and
     * BlockRedstoneTorch, BlockRedstoneWire, BlockRailBase, BlockSign,
     * BlockLever, BlockButton, BlockBasePressurePlate, BlockTripWire and
     * BlockTripWireHook, BlockPortal, BlockEndPortal, BlockPistonMoving */
    case 6: case 31: case 32: case 37: case 38: case 39: case 40: case 59:
    case 83: case 104: case 105: case 115: case 141: case 142: case 175:
    case 27: case 28: case 66: case 157:
    case 50: case 75: case 76: case 55: case 63: case 68: case 69:
    case 77: case 143: case 70: case 72: case 147: case 148:
    case 131: case 132: case 90: case 119: case 36:
        return;

    case 43: case 44: case 125: case 126: /* BlockStoneSlab / BlockWoodSlab */
    {
        /* setBlockBoundsBasedOnState: the flipped (top) half is metadata bit 8.
         * The double slab (43, 125) has field_150004_a set and always reports
         * the full cube. */
        int full = id == 43 || id == 125;
        int top = !full && (block_meta(w, x, y, z) & 8) != 0;

        if (full) add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, x, y, z));
        else if (top) add_query(l, query, from_bounds(0.0F, 0.5F, 0.0F, 1.0F, 1.0F, 1.0F, x, y, z));
        else add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 0.5F, 1.0F, x, y, z));
        return;
    }

    case 53: case 67: case 108: case 109: case 114: case 128: case 134: case 135: case 136:
    case 156: case 163: case 164: /* BlockStairs.addCollisionBoxesToList */
    {
        l->impure = 1;
        int meta = block_meta(w, x, y, z);
        float b[6];

        /* func_150147_e: the bottom half unless metadata bit 4 is set */
        float by_lo = (meta & 4) != 0 ? 0.5F : 0.0F;
        float by_hi = (meta & 4) != 0 ? 1.0F : 0.5F;

        add_query(l, query, from_bounds(0.0F, by_lo, 0.0F, 1.0F, by_hi, 1.0F, x, y, z));

        int want_corner = stairs_mid(w, x, y, z, meta, b);
        add_query(l, query, from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z));

        if (want_corner && stairs_corner(w, x, y, z, meta, b))
            add_query(l, query, from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z));
        /* then setBlockBounds(0, 0, 0, 1, 1, 1) */
        stairs_octant[id] = 0;
        return;
    }

    case 60: /* BlockFarmland: its own full-cube override */
        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, x, y, z));
        return;

    case 64: case 71: /* BlockDoor.getCollisionBoundingBoxFromPool */
        add_query(l, query, door_bounds(w, x, y, z));
        return;

    case 65: /* BlockLadder.getCollisionBoundingBoxFromPool -> func_149797_b */
    {
        l->impure = 1;
        float b[6];
        collide_ladder_bounds(block_meta(w, x, y, z), b);
        add_query(l, query, from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z));
        return;
    }

    case 78: /* BlockSnow.getCollisionBoundingBoxFromPool */
    {
        int var5 = block_meta(w, x, y, z) & 7;
        float var6 = 0.125F;

        add_query(l, query, aabb_make((double)x + 0.0, (double)y + 0.0, (double)z + 0.0,
                                      (double)x + 1.0, (double)((float)y + (float)var5 * var6), (double)z + 1.0));
        return;
    }

    case 81: /* BlockCactus.getCollisionBoundingBoxFromPool */
    {
        float var5 = 0.0625F;

        add_query(l, query, aabb_make((double)((float)x + var5), (double)y, (double)((float)z + var5),
                                      (double)((float)(x + 1) - var5), (double)((float)(y + 1) - var5),
                                      (double)((float)(z + 1) - var5)));
        return;
    }

    case 85: case 113: /* BlockFence.addCollisionBoxesToList */
    {
        l->impure = 1;
        int var8 = connects(w, id, x, y, z - 1);
        int var9 = connects(w, id, x, y, z + 1);
        int var10 = connects(w, id, x - 1, y, z);
        int var11 = connects(w, id, x + 1, y, z);
        float var12 = 0.375F;
        float var13 = 0.625F;
        float var14 = 0.375F;
        float var15 = 0.625F;

        if (var8) var14 = 0.0F;
        if (var9) var15 = 1.0F;
        if (var8 || var9) add_query(l, query, from_bounds(var12, 0.0F, var14, var13, 1.5F, var15, x, y, z));

        var14 = 0.375F;
        var15 = 0.625F;
        if (var10) var12 = 0.0F;
        if (var11) var13 = 1.0F;
        if (var10 || var11 || (!var8 && !var9))
            add_query(l, query, from_bounds(var12, 0.0F, var14, var13, 1.5F, var15, x, y, z));

        /* the trailing setBlockBounds(var12, 0, var14, var13, 1, var15) leaves
         * the object's bounds for the next caller, not this one's box list */
        if (var8) var14 = 0.0F;
        if (var9) var15 = 1.0F;
        collide_fence_bounds_set(id, var12, 0.0F, var14, var13, 1.0F, var15);
        return;
    }

    case 88: /* BlockSoulSand.getCollisionBoundingBoxFromPool */
    {
        float var5 = 0.125F;

        add_query(l, query, aabb_make((double)x, (double)y, (double)z, (double)(x + 1),
                                      (double)((float)(y + 1) - var5), (double)(z + 1)));
        return;
    }

    case 92: /* BlockCake.getCollisionBoundingBoxFromPool */
    {
        int var5 = block_meta(w, x, y, z);
        float var6 = 0.0625F;
        float var7 = (float)(1 + var5 * 2) / 16.0F;
        float var8 = 0.5F;

        add_query(l, query, aabb_make((double)((float)x + var7), (double)y, (double)((float)z + var6),
                                      (double)((float)(x + 1) - var6), (double)((float)y + var8 - var6),
                                      (double)((float)(z + 1) - var6)));
        return;
    }

    case 96: /* BlockTrapDoor.getCollisionBoundingBoxFromPool -> func_150117_b.
              * The override calls setBlockBounds once for the open/closed slab
              * and then once more for the open door's side, so a door with
              * bit 4 set reports only the side box and a closed one only the
              * slab. */
    {
        float var2 = 0.1875F;
        int meta = block_meta(w, x, y, z);
        float b0 = 0.0F, b1 = 0.0F, b2 = 0.0F, b3 = 1.0F, b4 = var2, b5 = 1.0F;

        if ((meta & 8) != 0)
        {
            b1 = 1.0F - var2;
            b4 = 1.0F;
        }

        if ((meta & 4) != 0)
        {
            if ((meta & 3) == 0) { b0 = 0.0F; b1 = 0.0F; b2 = 1.0F - var2; b3 = 1.0F; b4 = 1.0F; b5 = 1.0F; }
            if ((meta & 3) == 1) { b0 = 0.0F; b1 = 0.0F; b2 = 0.0F; b3 = 1.0F; b4 = 1.0F; b5 = var2; }
            if ((meta & 3) == 2) { b0 = 1.0F - var2; b1 = 0.0F; b2 = 0.0F; b3 = 1.0F; b4 = 1.0F; b5 = 1.0F; }
            if ((meta & 3) == 3) { b0 = 0.0F; b1 = 0.0F; b2 = 0.0F; b3 = var2; b4 = 1.0F; b5 = 1.0F; }
        }

        add_query(l, query, from_bounds(b0, b1, b2, b3, b4, b5, x, y, z));
        return;
    }

    case 101: case 102: case 160: /* BlockPane.addCollisionBoxesToList */
    {
        l->impure = 1;
        int var8 = pane_connects(w, x, y, z - 1);
        int var9 = pane_connects(w, x, y, z + 1);
        int var10 = pane_connects(w, x - 1, y, z);
        int var11 = pane_connects(w, x + 1, y, z);

        if ((!var10 || !var11) && (var10 || var11 || var8 || var9))
        {
            if (var10 && !var11) pane_box(l, query, id, x, y, z, 0.0F, 0.4375F, 0.5F, 0.5625F);
            else if (!var10 && var11) pane_box(l, query, id, x, y, z, 0.5F, 0.4375F, 1.0F, 0.5625F);
        }
        else
        {
            pane_box(l, query, id, x, y, z, 0.0F, 0.4375F, 1.0F, 0.5625F);
        }

        if ((!var8 || !var9) && (var10 || var11 || var8 || var9))
        {
            if (var8 && !var9) pane_box(l, query, id, x, y, z, 0.4375F, 0.0F, 0.5625F, 0.5F);
            else if (!var8 && var9) pane_box(l, query, id, x, y, z, 0.4375F, 0.5F, 0.5625F, 1.0F);
        }
        else
        {
            pane_box(l, query, id, x, y, z, 0.4375F, 0.0F, 0.5625F, 1.0F);
        }
        return;
    }

    case 107: /* BlockFenceGate.getCollisionBoundingBoxFromPool */
    {
        int var5 = block_meta(w, x, y, z);

        if ((var5 & 4) != 0) return; /* isFenceGateOpen */

        if (var5 != 2 && var5 != 0)
            add_query(l, query, from_bounds(0.375F, 0.0F, 0.0F, 0.625F, 1.5F, 1.0F, x, y, z));
        else
            add_query(l, query, from_bounds(0.0F, 0.0F, 0.375F, 1.0F, 1.5F, 0.625F, x, y, z));
        return;
    }

    case 111: /* BlockLilyPad.getCollisionBoundingBoxFromPool */
        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, (float)1 / 64.0F, 1.0F, x, y, z));
        return;

    case 139: /* BlockWall.getCollisionBoundingBoxFromPool -> setBlockBoundsBasedOnState */
    {
        l->impure = 1;
        int var5 = connects(w, id, x, y, z - 1);
        int var6 = connects(w, id, x, y, z + 1);
        int var7 = connects(w, id, x - 1, y, z);
        int var8 = connects(w, id, x + 1, y, z);
        float var9 = 0.25F;
        float var10 = 0.75F;
        float var11 = 0.25F;
        float var12 = 0.75F;
        float var13 = 1.0F;

        if (var5) var11 = 0.0F;
        if (var6) var12 = 1.0F;
        if (var7) var9 = 0.0F;
        if (var8) var10 = 1.0F;

        if (var5 && var6 && !var7 && !var8)
        {
            var13 = 0.8125F;
            var9 = 0.3125F;
            var10 = 0.6875F;
        }
        else if (!var5 && !var6 && var7 && var8)
        {
            var13 = 0.8125F;
            var11 = 0.3125F;
            var12 = 0.6875F;
        }

        /* BlockWall.getCollisionBoundingBoxFromPool sets field_149756_F = 1.5D after
         * setBlockBoundsBasedOnState, so the box's max Y is 1.5 regardless of
         * the height the bounds pass computed (var13 still matters for the
         * narrow post-and-rail form through the bounds themselves). */
        add_query(l, query, aabb_make((double)x + (double)var9, (double)y + 0.0, (double)z + (double)var11,
                                      (double)x + (double)var10, (double)y + 1.5, (double)z + (double)var12));
        (void)var13;
        return;
    }

    case 118: /* BlockCauldron.addCollisionBoxesToList: the base, then the
               * four walls, each through super with its own setBlockBounds */
    {
        float var8 = 0.125F;

        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 0.3125F, 1.0F, x, y, z));
        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, var8, 1.0F, 1.0F, x, y, z));
        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, var8, x, y, z));
        add_query(l, query, from_bounds(1.0F - var8, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, x, y, z));
        add_query(l, query, from_bounds(0.0F, 0.0F, 1.0F - var8, 1.0F, 1.0F, 1.0F, x, y, z));
        return;
    }

    case 117: /* BlockBrewingStand.addCollisionBoxesToList: the centre rod,
               * then the base (setBlockBoundsForItemRender), which stays */
        l->impure = 1;
        add_query(l, query, from_bounds(0.4375F, 0.0F, 0.4375F, 0.5625F, 0.875F, 0.5625F, x, y, z));
        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 0.125F, 1.0F, x, y, z));
        brewing_base = 1;
        return;

    case 154: /* BlockHopper.addCollisionBoxesToList: the 10/16 base, then
               * the four walls; it ends at the full-cube bounds again */
    {
        float var8 = 0.125F;

        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 0.625F, 1.0F, x, y, z));
        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, var8, 1.0F, 1.0F, x, y, z));
        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, var8, x, y, z));
        add_query(l, query, from_bounds(1.0F - var8, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, x, y, z));
        add_query(l, query, from_bounds(0.0F, 0.0F, 1.0F - var8, 1.0F, 1.0F, 1.0F, x, y, z));
        return;
    }

    case 145: /* BlockAnvil: Block's box over the singleton's bounds, which the
               * oracle's anvil pin (SPEC.md) sets from this anvil's metadata
               * first: inset 1/8 across its facing */
        if ((block_meta(w, x, y, z) & 3) != 3 && (block_meta(w, x, y, z) & 3) != 1)
            add_query(l, query, from_bounds(0.125F, 0.0F, 0.0F, 0.875F, 1.0F, 1.0F, x, y, z));
        else
            add_query(l, query, from_bounds(0.0F, 0.0F, 0.125F, 1.0F, 1.0F, 0.875F, x, y, z));
        return;

    case 144: /* BlockSkull.getCollisionBoundingBoxFromPool: its own
               * setBlockBoundsBasedOnState first, then super */
    {
        float b[6];

        skull_bounds(block_meta(w, x, y, z), b);
        add_query(l, query, from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z));
        return;
    }

    case 120: /* BlockEndPortalFrame.addCollisionBoxesToList: the 13/16 base,
               * then the eye on top when the frame holds one (meta & 4) */
        l->impure = 1;
        add_query(l, query, from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 0.8125F, 1.0F, x, y, z));
        if ((block_meta(w, x, y, z) & 4) != 0)
            add_query(l, query, from_bounds(0.3125F, 0.8125F, 0.3125F, 0.6875F, 1.0F, 0.6875F, x, y, z));
        /* then setBlockBoundsForItemRender: the object keeps the 13/16 */
        nw_env->collide.item_bounds[120] = 1;
        return;

    case 171: /* BlockCarpet.getCollisionBoundingBoxFromPool */
    {
        /* the override's max Y is (float)var5 * 0.0625F with var5 a local byte
         * 0, not the field func_150089_b set, so the collision box is flat at
         * the block's own y. The visual and in-bounds box a carpet reports
         * through setBlockBoundsBasedOnState is 1/16; the collision one is 0. */
        add_query(l, query, aabb_make((double)x, (double)y, (double)z, (double)(x + 1), (double)y, (double)(z + 1)));
        return;
    }

    default:
        break;
    }

    /* Block.addCollisionBoxesToList with the bounds the constructor left
     * (setBlockBounds), the path every plain cube takes. The registry carries
     * them, and every other shape in the table (glass, sponge, water, lava,
     * fire, nether brick fence) lands here too. */
    add_query(l, query, from_bounds(BLOCKS[id].min_x, BLOCKS[id].min_y, BLOCKS[id].min_z,
                                    BLOCKS[id].max_x, BLOCKS[id].max_y, BLOCKS[id].max_z, x, y, z));
}

int collide_entity_effect(struct world *w, int x, int y, int z, int id, int meta)
{
    (void)meta;

    switch (id & 4095)
    {
    case 30: /* BlockWeb.onEntityCollidedWithBlock: setInWeb() */
        return COLLIDE_EFFECT_WEB;

    case 88: /* BlockSoulSand.onEntityCollidedWithBlock: motionX/Z *= 0.4 */
        return COLLIDE_EFFECT_SOUL_SAND;

    case 81: /* BlockCactus.onEntityCollidedWithBlock: attackEntityFrom(cactus,
              * 1). The probe entity has no attackEntityFrom override and
              * Entity's empty body leaves no state the probe records (see the
              * report); item_entity.c applies it through Entity.attack_from. */
        return COLLIDE_EFFECT_CACTUS;

    case 70: /* BlockPressurePlate (stone, Sensitivity.mobs) and the wooden
              * one (Sensitivity.everything): onEntityCollidedWithBlock runs
              * func_150062_a, which only acts while the plate is up */
    case 72:
    case 147: /* BlockPressurePlateWeighted (gold, iron): the same
               * onEntityCollidedWithBlock, acting while the plate is at 0 */
    case 148:
        return COLLIDE_EFFECT_PLATE;

    case 132: /* BlockTripWire.onEntityCollidedWithBlock: func_150140_e on
               * the server while the wire is not tripped; world state like
               * the plate's, so it takes the same hook */
        return COLLIDE_EFFECT_PLATE;

    case 118: /* BlockCauldron.onEntityCollidedWithBlock */
        return COLLIDE_EFFECT_CAULDRON;

    default:
        /* Block.onEntityCollidedWithBlock and BlockFarmland's are empty */
        (void)w;
        (void)x;
        (void)y;
        (void)z;
        return 0;
    }
}
/* Block.getCollisionBoundingBoxFromPool for the block at (x, y, z). The
 * overrides that report one box are the same shapes addCollisionBoxesToList
 * handles above; the pool form is the single box without the fence, pane and
 * wall multi-box passes (a fence reports the bounds the last fence left, a
 * stair the bottom-or-top half before the second box is added, a wall the one
 * box with max Y 1.5). Null for the blocks whose override returns null. */
int collide_pool_box(struct world *w, int x, int y, int z, struct aabb *out)
{
    return collide_pool_box_as(w, block_id(w, x, y, z), x, y, z, out);
}

/* The pool box block id would report at (x, y, z), whatever the cell holds
 * now (World.canPlaceEntityOnSide asks the block about to be placed; the
 * metadata its override reads is the cell's current one). */
int collide_pool_box_as(struct world *w, int id, int x, int y, int z, struct aabb *out)
{
    switch (id)
    {
    case 0: /* BlockAir: null */
    case 8: case 9: case 10: case 11: /* BlockLiquid: null */
    case 30: /* BlockWeb: null */
    case 51: /* BlockFire: null */
    case 106: /* BlockVine: null */
    case 6: case 31: case 32: case 37: case 38: case 39: case 40: case 59:
    case 83: case 104: case 105: case 115: case 141: case 142: case 175:
    case 27: case 28: case 66: case 157:
    case 50: case 75: case 76: case 55: case 63: case 68: case 69:
    case 77: case 143: case 70: case 72: case 147: case 148:
    case 131: case 132: case 90: case 36:
        return 0;

    case 119:
        /* BlockEndPortal keeps Block's pool box over the shared object's
         * fields: the constructor's full cube until a ray trace runs its
         * setBlockBoundsBasedOnState (1/16 high; it has no block renderer
         * to do it) */
        *out = nw_env->collide.item_bounds[119] ? from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 0.0625F, 1.0F, x, y, z)
                                                : from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, x, y, z);
        return 1;

    case 85: case 113: /* Block's own: the shared bounds the last fence left */
    {
        const float *b = fence_bounds[id == 113 ? 1 : 0];
        *out = from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z);
        return 1;
    }

    case 117: /* Block's own, over the brewing stand object's bounds */
        *out = from_bounds(0.0F, 0.0F, 0.0F, 1.0F, brewing_base ? 0.125F : 1.0F, 1.0F, x, y, z);
        return 1;

    case 26: /* BlockBed.getCollisionBoundingBoxFromPool -> func_149978_e */
        *out = from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 0.5625F, 1.0F, x, y, z);
        return 1;

    case 127: /* BlockCocoa, see cocoa_bounds */
    {
        float b[6];
        cocoa_bounds(block_meta(w, x, y, z), b);
        *out = from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z);
        return 1;
    }

    case 53: case 67: case 108: case 109: case 114: case 128: case 134: case 135: case 136:
    case 156: case 163: case 164: /* BlockStairs: Block's, over the shared bounds */
        if (stairs_octant[id]) *out = from_bounds(0.5F, 0.5F, 0.5F, 1.0F, 1.0F, 1.0F, x, y, z);
        else *out = from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, x, y, z);
        return 1;

    case 64: case 71: /* BlockDoor.getCollisionBoundingBoxFromPool */
        *out = door_bounds(w, x, y, z);
        return 1;

    case 65: /* BlockLadder.getCollisionBoundingBoxFromPool -> func_149797_b */
    {
        float b[6];
        collide_ladder_bounds(block_meta(w, x, y, z), b);
        *out = from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z);
        return 1;
    }

    case 78: /* BlockSnow.getCollisionBoundingBoxFromPool */
    {
        int var5 = block_meta(w, x, y, z) & 7;
        float var6 = 0.125F;
        *out = aabb_make((double)x + 0.0, (double)y + 0.0, (double)z + 0.0,
                         (double)x + 1.0, (double)((float)y + (float)var5 * var6), (double)z + 1.0);
        return 1;
    }

    case 81: /* BlockCactus.getCollisionBoundingBoxFromPool */
    {
        float var5 = 0.0625F;
        *out = aabb_make((double)((float)x + var5), (double)y, (double)((float)z + var5),
                         (double)((float)(x + 1) - var5), (double)((float)(y + 1) - var5),
                         (double)((float)(z + 1) - var5));
        return 1;
    }

    case 88: /* BlockSoulSand.getCollisionBoundingBoxFromPool */
    {
        float var5 = 0.125F;
        *out = aabb_make((double)x, (double)y, (double)z, (double)(x + 1),
                         (double)((float)(y + 1) - var5), (double)(z + 1));
        return 1;
    }

    case 92: /* BlockCake.getCollisionBoundingBoxFromPool */
    {
        int var5 = block_meta(w, x, y, z);
        float var6 = 0.0625F;
        float var7 = (float)(1 + var5 * 2) / 16.0F;
        float var8 = 0.5F;
        *out = aabb_make((double)((float)x + var7), (double)y, (double)((float)z + var6),
                         (double)((float)(x + 1) - var6), (double)((float)y + var8 - var6),
                         (double)((float)(z + 1) - var6));
        return 1;
    }

    case 96: /* BlockTrapDoor.getCollisionBoundingBoxFromPool -> func_150117_b */
    {
        float var2 = 0.1875F;
        int meta = block_meta(w, x, y, z);
        float b0 = 0.0F, b1 = 0.0F, b2 = 0.0F, b3 = 1.0F, b4 = var2, b5 = 1.0F;

        if ((meta & 8) != 0)
        {
            b1 = 1.0F - var2;
            b4 = 1.0F;
        }

        if ((meta & 4) != 0)
        {
            if ((meta & 3) == 0) { b0 = 0.0F; b1 = 0.0F; b2 = 1.0F - var2; b3 = 1.0F; b4 = 1.0F; b5 = 1.0F; }
            if ((meta & 3) == 1) { b0 = 0.0F; b1 = 0.0F; b2 = 0.0F; b3 = 1.0F; b4 = 1.0F; b5 = var2; }
            if ((meta & 3) == 2) { b0 = 1.0F - var2; b1 = 0.0F; b2 = 0.0F; b3 = 1.0F; b4 = 1.0F; b5 = 1.0F; }
            if ((meta & 3) == 3) { b0 = 0.0F; b1 = 0.0F; b2 = 0.0F; b3 = var2; b4 = 1.0F; b5 = 1.0F; }
        }

        *out = from_bounds(b0, b1, b2, b3, b4, b5, x, y, z);
        return 1;
    }

    case 107: /* BlockFenceGate.getCollisionBoundingBoxFromPool: null when open */
    {
        int var5 = block_meta(w, x, y, z);

        if ((var5 & 4) != 0) return 0;

        if (var5 != 2 && var5 != 0)
            *out = from_bounds(0.375F, 0.0F, 0.0F, 0.625F, 1.5F, 1.0F, x, y, z);
        else
            *out = from_bounds(0.0F, 0.0F, 0.375F, 1.0F, 1.5F, 0.625F, x, y, z);
        return 1;
    }

    case 111: /* BlockLilyPad.getCollisionBoundingBoxFromPool */
        *out = from_bounds(0.0F, 0.0F, 0.0F, 1.0F, (float)1 / 64.0F, 1.0F, x, y, z);
        return 1;

    case 101: case 102: case 160: /* Block's own, over the pane object's shared bounds */
        if (!nw_env->collide.pane_set[pane_slot(id)]) goto constructor;
        {
            const float *b = nw_env->collide.pane_bounds[pane_slot(id)];
            *out = from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z);
        }
        return 1;

    case 139: /* BlockWall.getCollisionBoundingBoxFromPool:
                 setBlockBoundsBasedOnState, then field_149756_F = 1.5D */
    {
        int var5 = connects(w, id, x, y, z - 1);
        int var6 = connects(w, id, x, y, z + 1);
        int var7 = connects(w, id, x - 1, y, z);
        int var8 = connects(w, id, x + 1, y, z);
        float var9 = 0.25F, var10 = 0.75F, var11 = 0.25F, var12 = 0.75F;

        if (var5) var11 = 0.0F;
        if (var6) var12 = 1.0F;
        if (var7) var9 = 0.0F;
        if (var8) var10 = 1.0F;

        if (var5 && var6 && !var7 && !var8)
        {
            var9 = 0.3125F;
            var10 = 0.6875F;
        }
        else if (!var5 && !var6 && var7 && var8)
        {
            var11 = 0.3125F;
            var12 = 0.6875F;
        }

        *out = aabb_make((double)x + (double)var9, (double)y + 0.0, (double)z + (double)var11,
                         (double)x + (double)var10, (double)y + 1.5, (double)z + (double)var12);
        return 1;
    }

    case 144: /* BlockSkull: setBlockBoundsBasedOnState, then super */
    {
        float b[6];

        skull_bounds(block_meta(w, x, y, z), b);
        *out = from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z);
        return 1;
    }

    case 145: /* the anvil pin's box (see collide_add_boxes) */
        if ((block_meta(w, x, y, z) & 3) != 3 && (block_meta(w, x, y, z) & 3) != 1)
            *out = from_bounds(0.125F, 0.0F, 0.0F, 0.875F, 1.0F, 1.0F, x, y, z);
        else
            *out = from_bounds(0.0F, 0.0F, 0.125F, 1.0F, 1.0F, 0.875F, x, y, z);
        return 1;

    case 171: /* BlockCarpet.getCollisionBoundingBoxFromPool: flat at the block's y */
        *out = aabb_make((double)x, (double)y, (double)z, (double)(x + 1), (double)y, (double)(z + 1));
        return 1;

    case 54: case 146: /* Block's own, over the chest object's shared bounds */
    {
        const float *b = nw_env->collide.chest_bounds[id == 146 ? 1 : 0];
        *out = from_bounds(b[0], b[1], b[2], b[3], b[4], b[5], x, y, z);
        return 1;
    }
    case 120: /* Block's pool box over the shared object's fields: the
               * portal frame's 13/16 its addCollisionBoxesToList left, once
               * one ran */
        if (!nw_env->collide.item_bounds[120]) goto constructor;
        *out = from_bounds(0.0F, 0.0F, 0.0F, 1.0F, 0.8125F, 1.0F, x, y, z);
        return 1;

    default:
    constructor:
        /* Block.getCollisionBoundingBoxFromPool: the bounds the constructor
         * left (the registry carries them). The slab ids stay on this path:
         * their pool box is the constructor bounds, not the state's half. */
        *out = from_bounds(BLOCKS[id].min_x, BLOCKS[id].min_y, BLOCKS[id].min_z,
                           BLOCKS[id].max_x, BLOCKS[id].max_y, BLOCKS[id].max_z, x, y, z);
        return 1;
    }
}

struct collide_list *collide_scratch_begin(void)
{
    if (nw_scratch->collide_depth == COLLIDE_SCRATCH_DEPTH) abort();
    return &nw_scratch->collide[nw_scratch->collide_depth++];
}

void collide_scratch_end(struct collide_list **l)
{
    struct collide_list *c = *l;

    /* a grown list's slab block goes back to the environment that grew it,
     * now: a pool worker's scratch serves every environment it steps, and
     * a block kept for the next query took that query's boxes into this
     * environment's image, past its reset (lane/slabfix) */
    if (c->box != NULL && c->box != c->first) slab_give(c->box, (size_t)c->cap * sizeof *c->box);
    c->box = NULL;
    c->n = c->cap = 0;
    --nw_scratch->collide_depth;
}

static void pane_box(struct collide_list *l, const struct aabb *query, int id, int x, int y, int z,
                     float x0, float z0, float x1, float z1)
{
    collide_pane_bounds_set(id, x0, 0.0F, z0, x1, 1.0F, z1);
    add_query(l, query, from_bounds(x0, 0.0F, z0, x1, 1.0F, z1, x, y, z));
}
