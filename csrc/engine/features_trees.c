/* The large and conifer tree generators of population, mirroring
 * oracle/harness/netherite/oracle/FeatureProbeTrees.java and the Java classes it
 * wraps: WorldGenBigTree, WorldGenTaiga1, WorldGenTaiga2, WorldGenHugeTrees
 * with WorldGenMegaPineTree and WorldGenMegaJungle, WorldGenSavannaTree and
 * WorldGenCanopyTree (oracle/src/world/gen/feature/).
 *
 * The biomes build every one of these with doBlockNotify false, so all their
 * writes go out with flag 2, the path world_set_block ports; dirt, log, log2,
 * leaves, leaves2 and vine all inherit Block's empty onBlockAdded, breakBlock
 * and onBlockPreDestroy, so no block callback is needed for them.
 *
 * Java evaluates operands left to right and C does not, so every expression
 * that draws twice from one generator is a separate statement here, float
 * stays float and double stays double, and the -ffp-contract=off build keeps
 * the two roundings Java makes. StrictMath.pow(a, 2.0D) is a * a: pow with
 * exponent 2 returns the exactly rounded square, checked against 20 million
 * values including every argument these generators build.
 *
 * StrictMath.sin/cos on doubles (WorldGenBigTree's leaf nodes) are libm sin
 * and cos here. They differ from fdlibm by at most one ulp on 3.3% of the
 * arguments this generator uses, and the result only ever reaches
 * MathHelper.floor_double of a value scaled by at most 7, where one ulp of sin
 * is 8e-16: a difference needs the exact value to sit that close to an
 * integer, which the 1200 recorded cases do not see.
 */
#include "features.h"
#include "env.h"
#include "blocks.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "jmath.h"

/* Math.PI, as the double the Java field holds (C's M_PI is not visible in a
 * strict C11 translation unit); a cast makes the float a float expression
 * needs. */
#define PI_D 3.141592653589793

/* Block ids: Block.getIdFromBlock for the blocks these generators place and
 * read. BLK_AIR, BLK_GRASS and BLK_DIRT come from terrain.h. */
enum {
    BLK_SAPLING = 6, BLK_LOG = 17, BLK_LEAVES = 18, BLK_FARMLAND = 60, BLK_VINE = 106,
    BLK_LEAVES2 = 161, BLK_LOG2 = 162,
};

/* WorldGenBigTree.otherCoordPairs: the two axes complementary to a major one. */
static const int OTHER[6] = {2, 0, 0, 1, 2, 1};

/* Direction.offsetX and offsetZ, the step an acacia or a dark oak trunk takes. */
static const int DIR_X[4] = {0, -1, 0, 1};
static const int DIR_Z[4] = {1, 0, -1, 0};

/* WorldGenBigTree's constants: the field values, then what BiomeDecorator's
 * setScale(1.0, 1.0, 1.0) leaves them at (heightLimitLimit is already 12;
 * leafDistanceLimit goes from 4 to 5 because the scale is over 0.5). */
#define BIG_HEIGHT_ATTENUATION 0.618
#define BIG_BRANCH_SLOPE 0.381
#define BIG_TRUNK_SIZE 1
#define BIG_HEIGHT_LIMIT_LIMIT 12
#define BIG_LEAF_DISTANCE 5

/* WorldGenHugeTrees' wood and leaf metadata, per variant. */
#define PINE_WOOD 1
#define PINE_LEAVES 1
#define JUNGLE_WOOD 3
#define JUNGLE_LEAVES 3

/* --------------------------------------------------------------- the helpers */

static int abs_int(int v)
{
    return v < 0 ? -v : v;
}

/* Block.getMaterial() == other.getMaterial(): the generated MATERIALS table
 * holds one entry per distinct Material object, so two ids share one index. */
static int same_material(int id, int ref)
{
    return BLOCKS[id & 4095].material == BLOCKS[ref & 4095].material;
}

static int mat_air(int id)
{
    return same_material(id, BLK_AIR);
}

static int mat_leaves(int id)
{
    return same_material(id, BLK_LEAVES);
}

/* WorldGenAbstractTree.func_150523_a: a block a tree may grow through. */
static int tree_replaceable(int id)
{
    return mat_air(id) || mat_leaves(id) || id == BLK_GRASS || id == BLK_DIRT || id == BLK_LOG ||
           id == BLK_LOG2 || id == BLK_SAPLING || id == BLK_VINE;
}

/* Block.func_149730_j, the opaque field Block's constructor set from
 * isOpaqueCube(). blocks.h's opaque_cube column is that same call. */
static int opaque_j(int id)
{
    return BLOCKS[id & 4095].opaque_cube;
}

/* WorldGenerator.func_150516_a with doBlockNotify false, and func_150515_a on
 * top of it: World.setBlock(..., flag 2). */
static void put(struct world *w, int x, int y, int z, int id, int meta)
{
    features_tree_write(w, x, y, z, id, meta);
}

/* StrictMath.pow(a, 2.0D): the exactly rounded square. */
static double pow2(double a)
{
    return a * a;
}

/* ------------------------------------------------------- WorldGenBigTree */

struct big_node
{
    int x, y, z, base_y;
};

struct bigtree
{
    struct world *w;
    int base[3];
    int height_limit;
    int height;
    int leaf_distance;      /* leafDistanceLimit: 4 fresh (a sapling's new
                             * WorldGenBigTree), 5 after setScale(1, 1, 1) */
    jrand rand;
    struct big_node *nodes;
    int n_nodes;
};

/* WorldGenBigTree keeps heightLimit between calls and a biome holds one
 * generator for the whole world, so this single static carries from case to
 * case exactly as the probe's one instance does. The populate driver keeps one
 * per biome object (BiomeGenBase.worldGeneratorBigTree) with
 * feature_bigtree_state. */
#define bigtree_height_limit (nw_env->features_trees.bigtree_height_limit)

/* WorldGenBigTree's leafDistanceLimit: 4 as the field initializes (the fresh
 * generator a growing sapling builds), 5 after the setScale(1, 1, 1) the
 * population probe applies. */
#define bigtree_scaled (nw_env->features_trees.bigtree_scaled)

void features_bigtree_scaled(int scaled)
{
    bigtree_scaled = scaled;
}

/* The population probe holds one WorldGenBigTree for the whole run, so the
 * heightLimit it drew on the first generate carries from tree to tree. A
 * growing sapling builds a fresh generator, so the draw starts over. */
void features_bigtree_reset(void)
{
    bigtree_height_limit = 0;
}

/* The heightLimit the static carries: a decorator saves and restores it per
 * biome object, since the probe holds one instance and a world holds one per
 * biome. */
void feature_bigtree_state_set(int v) { bigtree_height_limit = v; }
int feature_bigtree_state_get(void) { return bigtree_height_limit; }

/* WorldGenBigTree.layerSize. */
static float big_layer_size(int limit, int i)
{
    if ((double)i < (double)(float)limit * 0.3) return -1.618F;

    float half = (float)limit / 2.0F;
    float d = (float)limit / 2.0F - (float)i;
    float r;

    if (d == 0.0F) r = half;
    else if (fabsf(d) >= half) r = 0.0F;
    else r = (float)sqrt(pow2((double)fabsf(half)) - pow2((double)fabsf(d)));

    r *= 0.5F;
    return r;
}

/* WorldGenBigTree.leafSize. */
static float big_leaf_size(struct bigtree *b, int i)
{
    if (i >= 0 && i < b->leaf_distance) return i != 0 && i != b->leaf_distance - 1 ? 3.0F : 2.0F;
    return -1.0F;
}

/* WorldGenBigTree.checkBlockLine: -1 when the whole line is walkable, else how
 * far in it got. */
static int big_check_block_line(struct world *w, const int *p1, const int *p2)
{
    int d[3] = {0, 0, 0};
    int major = 0;

    for (int i = 0; i < 3; ++i)
    {
        d[i] = p2[i] - p1[i];

        if (abs_int(d[i]) > abs_int(d[major])) major = i;
    }

    if (d[major] == 0) return -1;

    int a = OTHER[major], c = OTHER[major + 3];
    int step = d[major] > 0 ? 1 : -1;
    double s1 = (double)d[a] / (double)d[major];
    double s2 = (double)d[c] / (double)d[major];
    int p[3] = {0, 0, 0};
    int t = 0;
    int end = d[major] + step;

    for (; t != end; t += step)
    {
        p[major] = p1[major] + t;
        p[a] = mh_floor((double)p1[a] + (double)t * s1);
        p[c] = mh_floor((double)p1[c] + (double)t * s2);

        if (!tree_replaceable(world_get_block(w, p[0], p[1], p[2]))) break;
    }

    return t == end ? -1 : abs_int(t);
}

/* WorldGenBigTree.validTreeLocation; raises heightLimit when the trunk has to
 * stop short, exactly as the Java field write does. */
static int big_valid_location(struct bigtree *b)
{
    int p1[3] = {b->base[0], b->base[1], b->base[2]};
    int p2[3] = {b->base[0], b->base[1] + b->height_limit - 1, b->base[2]};
    int below = world_get_block(b->w, b->base[0], b->base[1] - 1, b->base[2]);

    if (below != BLK_DIRT && below != BLK_GRASS && below != BLK_FARMLAND) return 0;

    int r = big_check_block_line(b->w, p1, p2);

    if (r == -1) return 1;
    if (r < 6) return 0;

    b->height_limit = r;
    return 1;
}

/* WorldGenBigTree.func_150529_a: the round leaf blob of one radius, grown along
 * the axis the major one leaves free. */
static void big_leaf_column(struct world *w, int x, int y, int z, float radius, int axis, int id)
{
    int r = (int)((double)radius + 0.618);
    int a = OTHER[axis], c = OTHER[axis + 3];
    int p[3] = {x, y, z};
    int q[3] = {0, 0, 0};

    q[axis] = p[axis];

    for (int i = -r; i <= r; ++i)
    {
        q[a] = p[a] + i;

        for (int j = -r; j <= r;)
        {
            double d = pow2((double)abs_int(i) + 0.5) + pow2((double)abs_int(j) + 0.5);

            if (d > (double)(radius * radius))
            {
                ++j;
            }
            else
            {
                q[c] = p[c] + j;
                int got = world_get_block(w, q[0], q[1], q[2]);

                if (!mat_air(got) && !mat_leaves(got)) ++j;
                else
                {
                    put(w, q[0], q[1], q[2], id, 0);
                    ++j;
                }
            }
        }
    }
}

/* WorldGenBigTree.func_150530_a: the log limb from p1 to p2, stepping along the
 * major axis and picking the facing metadata to match. */
static void big_limb(struct world *w, const int *p1, const int *p2, int id)
{
    int d[3] = {0, 0, 0};
    int major = 0;

    for (int i = 0; i < 3; ++i)
    {
        d[i] = p2[i] - p1[i];

        if (abs_int(d[i]) > abs_int(d[major])) major = i;
    }

    if (d[major] == 0) return;

    int a = OTHER[major], c = OTHER[major + 3];
    int step = d[major] > 0 ? 1 : -1;
    double s1 = (double)d[a] / (double)d[major];
    double s2 = (double)d[c] / (double)d[major];
    int q[3] = {0, 0, 0};
    int t = 0;
    int end = d[major] + step;

    for (; t != end; t += step)
    {
        q[major] = mh_floor((double)(p1[major] + t) + 0.5);
        q[a] = mh_floor((double)p1[a] + (double)t * s1 + 0.5);
        q[c] = mh_floor((double)p1[c] + (double)t * s2 + 0.5);

        int meta = 0;
        int dx = abs_int(q[0] - p1[0]), dz = abs_int(q[2] - p1[2]);
        int far = dx > dz ? dx : dz;

        if (far > 0)
        {
            if (dx == far) meta = 4;
            else if (dz == far) meta = 8;
        }

        put(w, q[0], q[1], q[2], id, meta);
    }
}

/* WorldGenBigTree.generateLeafNodeList. */
static void big_leaf_node_list(struct bigtree *b)
{
    b->height = (int)((double)b->height_limit * BIG_HEIGHT_ATTENUATION);

    if (b->height >= b->height_limit) b->height = b->height_limit - 1;

    int cap = (int)(1.382 + pow2(1.0 * (double)b->height_limit / 13.0));

    if (cap < 1) cap = 1;

    int total = cap * b->height_limit;
    struct big_node *all = malloc((size_t)total * sizeof *all);
    int y = b->base[1] + b->height_limit - b->leaf_distance;
    int n = 1;
    int top = b->base[1] + b->height;
    int dy = y - b->base[1];

    all[0].x = b->base[0];
    all[0].y = y;
    all[0].z = b->base[2];
    all[0].base_y = top;
    --y;

    while (dy >= 0)
    {
        int k = 0;
        float size = big_layer_size(b->height_limit, dy);

        if (size < 0.0F)
        {
            --y;
            --dy;
        }
        else
        {
            for (double off = 0.5; k < cap; ++k)
            {
                double rad = 1.0 * (double)size * ((double)jr_float(&b->rand) + 0.328);
                double ang = (double)jr_float(&b->rand) * 2.0 * PI_D;
                int nx = mh_floor(rad * sin(ang) + (double)b->base[0] + off);
                int nz = mh_floor(rad * cos(ang) + (double)b->base[2] + off);
                int p1[3] = {nx, y, nz};
                int p2[3] = {nx, y + b->leaf_distance, nz};

                if (big_check_block_line(b->w, p1, p2) == -1)
                {
                    int from[3] = {b->base[0], b->base[1], b->base[2]};
                    double dist = sqrt(pow2((double)abs_int(b->base[0] - nx)) + pow2((double)abs_int(b->base[2] - nz)));
                    double slope = dist * BIG_BRANCH_SLOPE;

                    if ((double)p1[1] - slope > (double)top) from[1] = top;
                    else from[1] = (int)((double)p1[1] - slope);

                    if (big_check_block_line(b->w, from, p1) == -1)
                    {
                        all[n].x = nx;
                        all[n].y = y;
                        all[n].z = nz;
                        all[n].base_y = from[1];
                        ++n;
                    }
                }
            }

            --y;
            --dy;
        }
    }

    b->nodes = malloc((size_t)n * sizeof *b->nodes);
    memcpy(b->nodes, all, (size_t)n * sizeof *b->nodes);
    b->n_nodes = n;
    free(all);
}

/* WorldGenBigTree.generateLeaves. */
static void big_generate_leaves(struct bigtree *b)
{
    for (int i = 0; i < b->n_nodes; ++i)
    {
        int x = b->nodes[i].x, y = b->nodes[i].y, z = b->nodes[i].z;
        int end = y + b->leaf_distance;

        for (int yy = y; yy < end; ++yy) big_leaf_column(b->w, x, yy, z, big_leaf_size(b, yy - y), 1, BLK_LEAVES);
    }
}

/* WorldGenBigTree.generateTrunk. trunkSize is 1, so the double trunk the Java
 * class can build is not part of this variant. */
static void big_generate_trunk(struct bigtree *b)
{
    int from[3] = {b->base[0], b->base[1], b->base[2]};
    int to[3] = {b->base[0], b->base[1] + b->height, b->base[2]};

    big_limb(b->w, from, to, BLK_LOG);
}

/* WorldGenBigTree.generateLeafNodeBases. */
static void big_leaf_node_bases(struct bigtree *b)
{
    int from[3] = {b->base[0], b->base[1], b->base[2]};

    for (int i = 0; i < b->n_nodes; ++i)
    {
        struct big_node *n = &b->nodes[i];
        int to[3] = {n->x, n->y, n->z};

        from[1] = n->base_y;

        if ((double)(from[1] - b->base[1]) >= (double)b->height_limit * 0.2) big_limb(b->w, from, to, BLK_LOG);
    }
}

/* WorldGenBigTree.generate. */
int feature_bigtree(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;
    (void)count;

    struct bigtree b;

    b.w = w;
    b.leaf_distance = bigtree_scaled ? 5 : 4;
    jr_seed(&b.rand, jr_long(r));
    b.base[0] = x;
    b.base[1] = y;
    b.base[2] = z;

    if (bigtree_height_limit == 0) bigtree_height_limit = 5 + jr_int_n(&b.rand, BIG_HEIGHT_LIMIT_LIMIT);

    b.height_limit = bigtree_height_limit;

    if (!big_valid_location(&b))
    {
        bigtree_height_limit = b.height_limit;
        return 0;
    }

    bigtree_height_limit = b.height_limit;
    big_leaf_node_list(&b);
    big_generate_leaves(&b);
    big_generate_trunk(&b);
    big_leaf_node_bases(&b);
    free(b.nodes);
    return 1;
}

/* -------------------------------------------------------- WorldGenTaiga1 */

int feature_taiga1(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;
    (void)count;

    int h = jr_int_n(r, 5) + 7;
    int bare = h - jr_int_n(r, 2) - 3;
    int span = h - bare;
    int width = 1 + jr_int_n(r, span + 1);

    if (!(y >= 1 && y + h + 1 <= 256)) return 0;

    int grow = 1;

    for (int yy = y; yy <= y + 1 + h && grow; ++yy)
    {
        int rad = yy - y < bare ? 0 : width;

        for (int dx = x - rad; dx <= x + rad && grow; ++dx)
        {
            for (int dz = z - rad; dz <= z + rad && grow; ++dz)
            {
                if (yy >= 0 && yy < 256)
                {
                    if (!tree_replaceable(world_get_block(w, dx, yy, dz))) grow = 0;
                }
                else
                {
                    grow = 0;
                }
            }
        }
    }

    if (!grow) return 0;

    int below = world_get_block(w, x, y - 1, z);

    if (!((below == BLK_GRASS || below == BLK_DIRT) && y < 256 - h - 1)) return 0;

    put(w, x, y - 1, z, BLK_DIRT, 0);
    int rad = 0;

    for (int yy = y + h; yy >= y + bare; --yy)
    {
        for (int dx = x - rad; dx <= x + rad; ++dx)
        {
            int ox = dx - x;

            for (int dz = z - rad; dz <= z + rad; ++dz)
            {
                int oz = dz - z;

                if ((abs_int(ox) != rad || abs_int(oz) != rad || rad <= 0) && !opaque_j(world_get_block(w, dx, yy, dz)))
                    put(w, dx, yy, dz, BLK_LEAVES, 1);
            }
        }

        if (rad >= 1 && yy == y + bare + 1) --rad;
        else if (rad < width) ++rad;
    }

    for (int i = 0; i < h - 1; ++i)
    {
        int got = world_get_block(w, x, y + i, z);

        if (mat_air(got) || mat_leaves(got)) put(w, x, y + i, z, BLK_LOG, 1);
    }

    return 1;
}

/* -------------------------------------------------------- WorldGenTaiga2 */

int feature_taiga2(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;
    (void)count;

    int h = jr_int_n(r, 4) + 6;
    int bare = 1 + jr_int_n(r, 2);
    int span = h - bare;
    int width = 2 + jr_int_n(r, 2);

    if (!(y >= 1 && y + h + 1 <= 256)) return 0;

    int grow = 1;

    for (int yy = y; yy <= y + 1 + h && grow; ++yy)
    {
        int rad = yy - y < bare ? 0 : width;

        for (int dx = x - rad; dx <= x + rad && grow; ++dx)
        {
            for (int dz = z - rad; dz <= z + rad && grow; ++dz)
            {
                if (yy >= 0 && yy < 256)
                {
                    int got = world_get_block(w, dx, yy, dz);

                    if (!mat_air(got) && !mat_leaves(got)) grow = 0;
                }
                else
                {
                    grow = 0;
                }
            }
        }
    }

    if (!grow) return 0;

    int below = world_get_block(w, x, y - 1, z);

    if (!((below == BLK_GRASS || below == BLK_DIRT || below == BLK_FARMLAND) && y < 256 - h - 1)) return 0;

    put(w, x, y - 1, z, BLK_DIRT, 0);
    int rad = jr_int_n(r, 2);
    int wide = 1;
    int prev = 0;

    for (int i = 0; i <= span; ++i)
    {
        int yy = y + h - i;

        for (int dx = x - rad; dx <= x + rad; ++dx)
        {
            int ox = dx - x;

            for (int dz = z - rad; dz <= z + rad; ++dz)
            {
                int oz = dz - z;

                if ((abs_int(ox) != rad || abs_int(oz) != rad || rad <= 0) && !opaque_j(world_get_block(w, dx, yy, dz)))
                    put(w, dx, yy, dz, BLK_LEAVES, 1);
            }
        }

        if (rad >= wide)
        {
            rad = prev;
            prev = 1;
            ++wide;

            if (wide > width) wide = width;
        }
        else
        {
            ++rad;
        }
    }

    int trunk = jr_int_n(r, 3);

    for (int i = 0; i < h - trunk; ++i)
    {
        int got = world_get_block(w, x, y + i, z);

        if (mat_air(got) || mat_leaves(got)) put(w, x, y + i, z, BLK_LOG, 1);
    }

    return 1;
}

/* ------------------------------------ WorldGenHugeTrees' two shared checks */

/* WorldGenHugeTrees.func_150536_b: the room a huge tree needs. */
static int huge_room(struct world *w, int x, int y, int z, int height)
{
    if (!(y >= 1 && y + height + 1 <= 256)) return 0;

    int ok = 1;

    for (int yy = y; yy <= y + 1 + height; ++yy)
    {
        int rad = 2;

        if (yy == y) rad = 1;
        if (yy >= y + 1 + height - 2) rad = 2;

        for (int dx = x - rad; dx <= x + rad && ok; ++dx)
        {
            for (int dz = z - rad; dz <= z + rad && ok; ++dz)
            {
                if (yy >= 0 && yy < 256)
                {
                    if (!tree_replaceable(world_get_block(w, dx, yy, dz))) ok = 0;
                }
                else
                {
                    ok = 0;
                }
            }
        }
    }

    return ok;
}

/* WorldGenHugeTrees.func_150532_c: the 2x2 patch of dirt a huge tree stands on. */
static int huge_ground(struct world *w, int x, int y, int z)
{
    int below = world_get_block(w, x, y - 1, z);

    if (!((below == BLK_GRASS || below == BLK_DIRT) && y >= 2)) return 0;

    put(w, x, y - 1, z, BLK_DIRT, 0);
    put(w, x + 1, y - 1, z, BLK_DIRT, 0);
    put(w, x, y - 1, z + 1, BLK_DIRT, 0);
    put(w, x + 1, y - 1, z + 1, BLK_DIRT, 0);
    return 1;
}

/* WorldGenHugeTrees.func_150533_a: the trunk's height. */
static int huge_height(jrand *r, int base_height, int jitter)
{
    int h = jr_int_n(r, 3) + base_height;

    if (jitter > 1) h += jr_int_n(r, jitter);
    return h;
}

/* WorldGenHugeTrees.func_150535_a: the four-cornered leaf blob of a huge tree. */
static void huge_leaf_blob(struct world *w, int x, int y, int z, int radius, int leaf_meta)
{
    int r2 = radius * radius;

    for (int dx = x - radius; dx <= x + radius + 1; ++dx)
    {
        int ox = dx - x;

        for (int dz = z - radius; dz <= z + radius + 1; ++dz)
        {
            int oz = dz - z;
            int ox1 = ox - 1, oz1 = oz - 1;

            if (ox * ox + oz * oz <= r2 || ox1 * ox1 + oz1 * oz1 <= r2 || ox * ox + oz1 * oz1 <= r2 ||
                ox1 * ox1 + oz * oz <= r2)
            {
                int got = world_get_block(w, dx, y, dz);

                if (mat_air(got) || mat_leaves(got)) put(w, dx, y, dz, BLK_LEAVES, leaf_meta);
            }
        }
    }
}

/* WorldGenHugeTrees.func_150534_b: the round leaf blob of a branch. */
static void huge_leaf_round(struct world *w, int x, int y, int z, int radius, int leaf_meta)
{
    int r2 = radius * radius;

    for (int dx = x - radius; dx <= x + radius; ++dx)
    {
        int ox = dx - x;

        for (int dz = z - radius; dz <= z + radius; ++dz)
        {
            int oz = dz - z;

            if (ox * ox + oz * oz <= r2)
            {
                int got = world_get_block(w, dx, y, dz);

                if (mat_air(got) || mat_leaves(got)) put(w, dx, y, dz, BLK_LEAVES, leaf_meta);
            }
        }
    }
}

/* -------------------------------------------------- WorldGenMegaPineTree */

/* WorldGenMegaPineTree.func_150541_c: the cone of leaf blobs under the top. */
static void mega_pine_crown(struct world *w, int x, int z, int top, int start_radius, jrand *r, int tall)
{
    int span = jr_int_n(r, 5);

    if (tall) span += 13; /* baseHeight */
    else span += 3;

    int prev = 0;

    for (int yy = top - span; yy <= top; ++yy)
    {
        int down = top - yy;
        int radius = start_radius + mh_floor_float((float)down / (float)span * 3.5F);

        huge_leaf_blob(w, x, yy, z, radius + (down > 0 && radius == prev && (yy & 1) == 0 ? 1 : 0), PINE_LEAVES);
        prev = radius;
    }
}

/* WorldGenMegaPineTree.func_150540_a: one dirt block under the podzol patch. */
static void mega_pine_dirt(struct world *w, int x, int y, int z)
{
    for (int yy = y + 2; yy >= y - 3; --yy)
    {
        int got = world_get_block(w, x, yy, z);

        if (got == BLK_GRASS || got == BLK_DIRT)
        {
            put(w, x, yy, z, BLK_DIRT, 2);
            break;
        }

        if (!mat_air(got) && yy < y) break;
    }
}

/* WorldGenMegaPineTree.func_150539_c: the 5x5 patch around one corner (the
 * Random argument the Java method takes is unused). */
static void mega_pine_patch(struct world *w, int x, int y, int z)
{
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz)
            if (abs_int(dx) != 2 || abs_int(dz) != 2) mega_pine_dirt(w, x + dx, y, z + dz);
}

/* WorldGenMegaPineTree.func_150524_b: what BiomeDecorator calls after a mega
 * taiga grew. */
static void mega_pine_after(struct world *w, jrand *r, int x, int y, int z)
{
    mega_pine_patch(w, x - 1, y, z - 1);
    mega_pine_patch(w, x + 2, y, z - 1);
    mega_pine_patch(w, x - 1, y, z + 2);
    mega_pine_patch(w, x + 2, y, z + 2);

    for (int i = 0; i < 5; ++i)
    {
        int v = jr_int_n(r, 64);
        int dx = v % 8, dz = v / 8;

        if (dx == 0 || dx == 7 || dz == 0 || dz == 7) mega_pine_patch(w, x - 3 + dx, y, z - 3 + dz);
    }
}

/* WorldGenMegaPineTree.generate alone: what BlockSapling.func_149878_d
 * calls (a sapling-grown mega pine lays no podzol); tall is
 * field_150542_e. */
int feature_megapine_grow(struct world *w, jrand *r, int x, int y, int z, int tall)
{
    int height = huge_height(r, 13, 15);

    if (!(huge_room(w, x, y, z, height) && huge_ground(w, x, y, z))) return 0;

    mega_pine_crown(w, x, z, y + height, 0, r, tall);

    for (int i = 0; i < height; ++i)
    {
        int got = world_get_block(w, x, y + i, z);

        if (mat_air(got) || mat_leaves(got)) put(w, x, y + i, z, BLK_LOG, PINE_WOOD);

        if (i < height - 1)
        {
            got = world_get_block(w, x + 1, y + i, z);

            if (mat_air(got) || mat_leaves(got)) put(w, x + 1, y + i, z, BLK_LOG, PINE_WOOD);

            got = world_get_block(w, x + 1, y + i, z + 1);

            if (mat_air(got) || mat_leaves(got)) put(w, x + 1, y + i, z + 1, BLK_LOG, PINE_WOOD);

            got = world_get_block(w, x, y + i, z + 1);

            if (mat_air(got) || mat_leaves(got)) put(w, x, y + i, z + 1, BLK_LOG, PINE_WOOD);
        }
    }

    return 1;
}

/* WorldGenMegaPineTree.generate, then the func_150524_b BiomeDecorator runs
 * after a grow; count is field_150542_e, the tall variant BiomeGenTaiga
 * builds as field_150642_aF. */
int feature_megapine(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;

    if (!feature_megapine_grow(w, r, x, y, z, count != 0)) return 0;

    mega_pine_after(w, r, x, y, z);
    return 1;
}

/* -------------------------------------------------- WorldGenMegaJungle */

/* WorldGenMegaJungle.func_150543_c: the leaf blobs down the trunk. */
static void mega_jungle_crown(struct world *w, int x, int z, int top, int radius)
{
    for (int yy = top - radius; yy <= top; ++yy)
    {
        int down = yy - top;

        huge_leaf_blob(w, x, yy, z, radius + 1 - down, JUNGLE_LEAVES);
    }
}

/* WorldGenMegaJungle.generate. */
int feature_megajungle(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;
    (void)count;

    int height = huge_height(r, 10, 20);

    if (!(huge_room(w, x, y, z, height) && huge_ground(w, x, y, z))) return 0;

    mega_jungle_crown(w, x, z, y + height, 2);

    for (int up = y + height - 2 - jr_int_n(r, 4); up > y + height / 2; up -= 2 + jr_int_n(r, 4))
    {
        float ang = jr_float(r) * (float)PI_D * 2.0F;
        int bx = x + (int)(0.5F + mh_cos(ang) * 4.0F);
        int bz = z + (int)(0.5F + mh_sin(ang) * 4.0F);
        int i;

        for (i = 0; i < 5; ++i)
        {
            bx = x + (int)(1.5F + mh_cos(ang) * (float)i);
            bz = z + (int)(1.5F + mh_sin(ang) * (float)i);
            put(w, bx, up - 3 + i / 2, bz, BLK_LOG, JUNGLE_WOOD);
        }

        int len = 1 + jr_int_n(r, 2);
        int end = up;

        for (int yy = up - len; yy <= end; ++yy)
        {
            int down = yy - end;

            huge_leaf_round(w, bx, yy, bz, 1 - down, JUNGLE_LEAVES);
        }
    }

    for (int i = 0; i < height; ++i)
    {
        int yy = y + i;
        int got = world_get_block(w, x, yy, z);

        if (mat_air(got) || mat_leaves(got))
        {
            put(w, x, yy, z, BLK_LOG, JUNGLE_WOOD);

            if (i > 0)
            {
                if (jr_int_n(r, 3) > 0 && mat_air(world_get_block(w, x - 1, yy, z)))
                    put(w, x - 1, yy, z, BLK_VINE, 8);

                if (jr_int_n(r, 3) > 0 && mat_air(world_get_block(w, x, yy, z - 1)))
                    put(w, x, yy, z - 1, BLK_VINE, 1);
            }
        }

        if (i < height - 1)
        {
            got = world_get_block(w, x + 1, yy, z);

            if (mat_air(got) || mat_leaves(got))
            {
                put(w, x + 1, yy, z, BLK_LOG, JUNGLE_WOOD);

                if (i > 0)
                {
                    if (jr_int_n(r, 3) > 0 && mat_air(world_get_block(w, x + 2, yy, z)))
                        put(w, x + 2, yy, z, BLK_VINE, 2);

                    if (jr_int_n(r, 3) > 0 && mat_air(world_get_block(w, x + 1, yy, z - 1)))
                        put(w, x + 1, yy, z - 1, BLK_VINE, 1);
                }
            }

            got = world_get_block(w, x + 1, yy, z + 1);

            if (mat_air(got) || mat_leaves(got))
            {
                put(w, x + 1, yy, z + 1, BLK_LOG, JUNGLE_WOOD);

                if (i > 0)
                {
                    if (jr_int_n(r, 3) > 0 && mat_air(world_get_block(w, x + 2, yy, z + 1)))
                        put(w, x + 2, yy, z + 1, BLK_VINE, 2);

                    if (jr_int_n(r, 3) > 0 && mat_air(world_get_block(w, x + 1, yy, z + 2)))
                        put(w, x + 1, yy, z + 2, BLK_VINE, 4);
                }
            }

            got = world_get_block(w, x, yy, z + 1);

            if (mat_air(got) || mat_leaves(got))
            {
                put(w, x, yy, z + 1, BLK_LOG, JUNGLE_WOOD);

                if (i > 0)
                {
                    if (jr_int_n(r, 3) > 0 && mat_air(world_get_block(w, x - 1, yy, z + 1)))
                        put(w, x - 1, yy, z + 1, BLK_VINE, 8);

                    if (jr_int_n(r, 3) > 0 && mat_air(world_get_block(w, x, yy, z + 2)))
                        put(w, x, yy, z + 2, BLK_VINE, 4);
                }
            }
        }
    }

    return 1;
}

/* -------------------------------------------------- WorldGenSavannaTree */

/* WorldGenSavannaTree.func_150525_a: acacia leaves, over air or leaves only. */
static void savanna_leaves(struct world *w, int x, int y, int z)
{
    int got = world_get_block(w, x, y, z);

    if (mat_air(got) || mat_leaves(got)) put(w, x, y, z, BLK_LEAVES2, 0);
}

int feature_savanna(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;
    (void)count;

    int height = jr_int_n(r, 3) + jr_int_n(r, 3) + 5;

    if (!(y >= 1 && y + height + 1 <= 256)) return 0;

    int ok = 1;

    for (int yy = y; yy <= y + 1 + height; ++yy)
    {
        int rad = 1;

        if (yy == y) rad = 0;
        if (yy >= y + 1 + height - 2) rad = 2;

        for (int dx = x - rad; dx <= x + rad && ok; ++dx)
        {
            for (int dz = z - rad; dz <= z + rad && ok; ++dz)
            {
                if (yy >= 0 && yy < 256)
                {
                    if (!tree_replaceable(world_get_block(w, dx, yy, dz))) ok = 0;
                }
                else
                {
                    ok = 0;
                }
            }
        }
    }

    if (!ok) return 0;

    int below = world_get_block(w, x, y - 1, z);

    if (!((below == BLK_GRASS || below == BLK_DIRT) && y < 256 - height - 1)) return 0;

    put(w, x, y - 1, z, BLK_DIRT, 0);
    int dir = jr_int_n(r, 4);
    int turn = height - jr_int_n(r, 4) - 1;
    int bend = 3 - jr_int_n(r, 3);
    int tx = x, tz = z, top = 0;

    for (int i = 0; i < height; ++i)
    {
        int yy = y + i;

        if (i >= turn && bend > 0)
        {
            tx += DIR_X[dir];
            tz += DIR_Z[dir];
            --bend;
        }

        int got = world_get_block(w, tx, yy, tz);

        if (mat_air(got) || mat_leaves(got))
        {
            put(w, tx, yy, tz, BLK_LOG2, 0);
            top = yy;
        }
    }

    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) savanna_leaves(w, tx + dx, top + 1, tz + dz);

    savanna_leaves(w, tx + 2, top + 1, tz);
    savanna_leaves(w, tx - 2, top + 1, tz);
    savanna_leaves(w, tx, top + 1, tz + 2);
    savanna_leaves(w, tx, top + 1, tz - 2);

    for (int dx = -3; dx <= 3; ++dx)
        for (int dz = -3; dz <= 3; ++dz)
            if (abs_int(dx) != 3 || abs_int(dz) != 3) savanna_leaves(w, tx + dx, top, tz + dz);

    tx = x;
    tz = z;
    int dir2 = jr_int_n(r, 4);

    if (dir2 != dir)
    {
        int i = turn - jr_int_n(r, 2) - 1;
        int len = 1 + jr_int_n(r, 3);
        top = 0;

        for (; i < height && len > 0; --len)
        {
            if (i >= 1)
            {
                int yy = y + i;

                tx += DIR_X[dir2];
                tz += DIR_Z[dir2];
                int got = world_get_block(w, tx, yy, tz);

                if (mat_air(got) || mat_leaves(got))
                {
                    put(w, tx, yy, tz, BLK_LOG2, 0);
                    top = yy;
                }
            }

            ++i;
        }

        if (top > 0)
        {
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) savanna_leaves(w, tx + dx, top + 1, tz + dz);

            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    if (abs_int(dx) != 2 || abs_int(dz) != 2) savanna_leaves(w, tx + dx, top, tz + dz);
        }
    }

    return 1;
}

/* -------------------------------------------------- WorldGenCanopyTree */

/* WorldGenCanopyTree.func_150526_a: dark oak leaves, over air only. */
static void canopy_leaves(struct world *w, int x, int y, int z)
{
    if (mat_air(world_get_block(w, x, y, z))) put(w, x, y, z, BLK_LEAVES2, 1);
}

int feature_canopy(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    (void)block;
    (void)count;

    int height = jr_int_n(r, 3) + jr_int_n(r, 2) + 6;

    if (!(y >= 1 && y + height + 1 <= 256)) return 0;

    int ok = 1;

    for (int yy = y; yy <= y + 1 + height; ++yy)
    {
        int rad = 1;

        if (yy == y) rad = 0;
        if (yy >= y + 1 + height - 2) rad = 2;

        for (int dx = x - rad; dx <= x + rad && ok; ++dx)
        {
            for (int dz = z - rad; dz <= z + rad && ok; ++dz)
            {
                if (yy >= 0 && yy < 256)
                {
                    if (!tree_replaceable(world_get_block(w, dx, yy, dz))) ok = 0;
                }
                else
                {
                    ok = 0;
                }
            }
        }
    }

    if (!ok) return 0;

    int below = world_get_block(w, x, y - 1, z);

    if (!((below == BLK_GRASS || below == BLK_DIRT) && y < 256 - height - 1)) return 0;

    put(w, x, y - 1, z, BLK_DIRT, 0);
    put(w, x + 1, y - 1, z, BLK_DIRT, 0);
    put(w, x + 1, y - 1, z + 1, BLK_DIRT, 0);
    put(w, x, y - 1, z + 1, BLK_DIRT, 0);
    int dir = jr_int_n(r, 4);
    int turn = height - jr_int_n(r, 4);
    int bend = 2 - jr_int_n(r, 3);
    int tx = x, tz = z, top = 0;

    for (int i = 0; i < height; ++i)
    {
        int yy = y + i;

        if (i >= turn && bend > 0)
        {
            tx += DIR_X[dir];
            tz += DIR_Z[dir];
            --bend;
        }

        int got = world_get_block(w, tx, yy, tz);

        if (mat_air(got) || mat_leaves(got))
        {
            put(w, tx, yy, tz, BLK_LOG2, 1);
            put(w, tx + 1, yy, tz, BLK_LOG2, 1);
            put(w, tx, yy, tz + 1, BLK_LOG2, 1);
            put(w, tx + 1, yy, tz + 1, BLK_LOG2, 1);
            top = yy;
        }
    }

    for (int dx = -2; dx <= 0; ++dx)
    {
        for (int dz = -2; dz <= 0; ++dz)
        {
            canopy_leaves(w, tx + dx, top - 1, tz + dz);
            canopy_leaves(w, 1 + tx - dx, top - 1, tz + dz);
            canopy_leaves(w, tx + dx, top - 1, 1 + tz - dz);
            canopy_leaves(w, 1 + tx - dx, top - 1, 1 + tz - dz);

            if ((dx > -2 || dz > -1) && (dx != -1 || dz != -2))
            {
                canopy_leaves(w, tx + dx, top + 1, tz + dz);
                canopy_leaves(w, 1 + tx - dx, top + 1, tz + dz);
                canopy_leaves(w, tx + dx, top + 1, 1 + tz - dz);
                canopy_leaves(w, 1 + tx - dx, top + 1, 1 + tz - dz);
            }
        }
    }

    if (jr_bool(r))
    {
        canopy_leaves(w, tx, top + 2, tz);
        canopy_leaves(w, tx + 1, top + 2, tz);
        canopy_leaves(w, tx + 1, top + 2, tz + 1);
        canopy_leaves(w, tx, top + 2, tz + 1);
    }

    for (int dx = -3; dx <= 4; ++dx)
    {
        for (int dz = -3; dz <= 4; ++dz)
        {
            if ((dx != -3 || dz != -3) && (dx != -3 || dz != 4) && (dx != 4 || dz != -3) && (dx != 4 || dz != 4) &&
                (abs_int(dx) < 3 || abs_int(dz) < 3))
                canopy_leaves(w, tx + dx, top, tz + dz);
        }
    }

    for (int dx = -1; dx <= 2; ++dx)
    {
        for (int dz = -1; dz <= 2; ++dz)
        {
            if ((dx < 0 || dx > 1 || dz < 0 || dz > 1) && jr_int_n(r, 3) <= 0)
            {
                int len = jr_int_n(r, 3) + 2;

                for (int i = 0; i < len; ++i) put(w, x + dx, top - i - 1, z + dz, BLK_LOG2, 1);

                for (int i = -1; i <= 1; ++i)
                    for (int j = -1; j <= 1; ++j) canopy_leaves(w, tx + dx + i, top, tz + dz + j);

                for (int i = -2; i <= 2; ++i)
                    for (int j = -2; j <= 2; ++j)
                        if (abs_int(i) != 2 || abs_int(j) != 2) canopy_leaves(w, tx + dx + i, top - 1, tz + dz + j);
            }
        }
    }

    return 1;
}

/* --------------------------------------------------------------- the table */

/* One row per config in the probe's per-feature table, in the same order and
 * with the same config indices: a case's y is already on the surface, so y0
 * and y1 are the probe's business and only the variant count matters here. */
static const struct feature TREES[] = {
    {"bigtree", 0, 0, 0, 24, 0, 0, feature_bigtree},
    {"taiga1", 0, 0, 0, 24, 0, 0, feature_taiga1},
    {"taiga2", 0, 0, 0, 24, 0, 0, feature_taiga2},
    {"megapine", 0, 0, 0, 24, 0, 0, feature_megapine},
    {"megapine", 0, 0, 0, 24, 0, 1, feature_megapine},
    {"megajungle", 0, 0, 0, 24, 0, 0, feature_megajungle},
    {"savanna", 0, 0, 0, 24, 0, 0, feature_savanna},
    {"canopy", 0, 0, 0, 24, 0, 0, feature_canopy},
};

/* The rows of one tree feature name, or NULL; *n gets their number. */
const struct feature *features_trees_for(const char *name, int *n)
{
    int first = -1, count = 0;

    for (int i = 0; i < (int)(sizeof TREES / sizeof TREES[0]); ++i)
    {
        if (strcmp(TREES[i].name, name) != 0) continue;

        if (first < 0) first = i;
        ++count;
    }

    if (count > 0)
    {
        *n = count;
        return TREES + first;
    }

    *n = 0;
    return NULL;
}