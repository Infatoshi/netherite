/* The population features, mirroring oracle/harness/netherite/oracle/FeatureProbe.java.
 * One function per Java feature, one table row per config in the probe's
 * per-feature table, and the same block ids and size parameters. */
#include "features.h"
#include "env.h"
#include "features_lakes.h"
#include "features_nether.h"
#include "features_springs.h"

#include <math.h>
#include <string.h>

#include "jmath.h"

#define PI_F ((float)3.141592653589793)

/* Block ids, Block.getIdFromBlock on the blocks BiomeDecorator builds its
 * veins from. */
enum {
    BLK_COAL_ORE = 16, BLK_IRON_ORE = 15, BLK_GOLD_ORE = 14, BLK_REDSTONE_ORE = 73,
    BLK_DIAMOND_ORE = 56, BLK_LAPIS_ORE = 21,
};

/* The probe's per-feature table: one row per config, in config order, so a
 * later lane appends its own rows after these and existing probes keep their
 * indices. */
static const struct feature MINABLE[] = {
    {"minable", 0, 4, 124, 17, BLK_DIRT, 32, feature_minable},
    {"minable", 1, 4, 124, 17, BLK_GRAVEL, 32, feature_minable},
    {"minable", 2, 4, 124, 17, BLK_COAL_ORE, 16, feature_minable},
    {"minable", 3, 4, 60, 17, BLK_IRON_ORE, 8, feature_minable},
    {"minable", 4, 4, 28, 17, BLK_GOLD_ORE, 8, feature_minable},
    {"minable", 5, 4, 12, 17, BLK_REDSTONE_ORE, 7, feature_minable},
    {"minable", 6, 4, 12, 17, BLK_DIAMOND_ORE, 7, feature_minable},
    {"minable", 7, 4, 12, 17, BLK_LAPIS_ORE, 6, feature_minable},
};

/* WorldGenDungeons: one row, the deep band a case draws y from and the reach of
 * four blocks out and five up the feature has (FeatureProbeDungeons.row). The
 * block and count fields are unused: the dungeon picks its own blocks. */
static const struct feature DUNGEONS[] = {
    {"dungeons", 0, 8, 64, 5, BLK_AIR, 0, feature_dungeons},
};

const struct feature *features_for(const char *name, int *n)
{
    /* lanes append one delegation each, before the rows that were here first */
    {
        const struct feature *l = features_lakes_for(name, n);

        if (l != NULL) return l;
    }

    {
        const struct feature *s = features_springs_for(name, n);

        if (s != NULL) return s;
    }

    {
        const struct feature *s = features_nether_for(name, n);

        if (s != NULL) return s;
    }

    if (name != NULL && name[0] == 'm' && name[1] == 'i' && name[2] == 'n' && name[3] == 'a' &&
        name[4] == 'b' && name[5] == 'l' && name[6] == 'e' && name[7] == 0)
    {
        *n = (int)(sizeof MINABLE / sizeof MINABLE[0]);
        return MINABLE;
    }

    if (name != NULL && name[0] == 'd' && name[1] == 'u' && name[2] == 'n' && name[3] == 'g' &&
        name[4] == 'e' && name[5] == 'o' && name[6] == 'n' && name[7] == 's' && name[8] == 0)
    {
        *n = (int)(sizeof DUNGEONS / sizeof DUNGEONS[0]);
        return DUNGEONS;
    }

    /* the other feature groups, one function per lane: the large and conifer
     * trees (features_trees.c), the small trees (features_trees_small.c) */
    const struct feature *other = features_trees_for(name, n);
    if (other != NULL) return other;
    other = features_trees_small_for(name, n);
    if (other != NULL) return other;
    other = features_plants_for(name, n);
    if (other != NULL) return other;
    *n = 0;
    return NULL;
}

/* WorldGenerator.func_150516_a: setBlock flag 2, or flag 3 while the flag is
 * on (a growing sapling built the generator). See features.h. */
#define tree_notify (nw_env->features.tree_notify)

void features_tree_notify(int on)
{
    tree_notify = on;
}

int features_tree_write(struct world *w, int x, int y, int z, int id, int meta)
{
    return world_set_block(w, x, y, z, id, meta, tree_notify ? 3 : 2);
}

int feature_minable(struct world *w, jrand *r, int x, int y, int z, int block, int count)
{
    return feature_minable_target(w, r, x, y, z, block, count, BLK_STONE);
}

/* the widest box a step of feature_minable_target takes on its fast path
 * (a vein of count 32 spans at most 6) */
#define MINABLE_SPAN 16

/* World.getBlock's band for (x, y, z): 1 with *sec set (NULL for a band with
 * no storage, which reads air) when x and z are in the world's bounds and the
 * chunk is the one in the near cache (world_get_block's inline case); 0 to
 * read through world_get_block. */
static inline int vein_band(struct world *w, int x, int y, int z, const struct chunk_sec **sec)
{
    if (!((unsigned)x + 30000000u < 60000000u && (unsigned)z + 30000000u < 60000000u)) return 0;

    const struct chunk *c = world_chunk_near(w, x >> 4, z >> 4);

    if (c == NULL) return 0;
    *sec = c->mask >> (y >> 4) & 1 ? chunk_sec_at(c, y >> 4) : NULL;
    return 1;
}

/* The bands one step of a vein reads: its box spans at most two chunks along
 * x and z and two bands along y (MINABLE_SPAN), each looked up at its first
 * read. Reading has no effect while the chunks are loaded, so a band stays
 * what it was until a write (which can copy a shared band, and whose
 * callbacks can load and populate a chunk) or a read that went through
 * world_get_block (which can load one): after either, all are looked up
 * again. */
struct vein_bands {
    int cx0, cz0, sy0;
    uint8_t have[8];
    const struct chunk_sec *sec[8];
};

static inline void vein_forget(struct vein_bands *vb)
{
    memset(vb->have, 0, sizeof vb->have);
}

/* World.getBlock(x, y, z) for a cell inside the step's box */
static inline int vein_read(struct world *w, struct vein_bands *vb, int x, int y, int z)
{
    /* outside y 0..255 World.getBlock answers air, touching nothing */
    if ((unsigned)y >= 256u) return world_get_block(w, x, y, z);

    int i = ((x >> 4) - vb->cx0) << 2 | ((z >> 4) - vb->cz0) << 1 | ((y >> 4) - vb->sy0);

    if (!vb->have[i])
    {
        if (!vein_band(w, x, y, z, &vb->sec[i]))
        {
            int id = world_get_block(w, x, y, z);

            vein_forget(vb);
            return id;
        }
        vb->have[i] = 1;
    }

    const struct chunk_sec *s = vb->sec[i];

    return s != NULL ? chunk_sec_id(s, SEC_XYZ(x & 15, y, z & 15)) : 0;
}

/* one step's box of WorldGenMinable.generate as Java loops it */
static void minable_box(struct world *w, double var20, double var22, double var24, double var28, double var30,
                        int var32, int var33, int var34, int var35, int var36, int var37, int block, int target)
{
    for (int var38 = var32; var38 <= var35; ++var38)
    {
        double var39 = ((double)var38 + 0.5 - var20) / (var28 / 2.0);

        if (var39 * var39 < 1.0)
        {
            for (int var41 = var33; var41 <= var36; ++var41)
            {
                double var42 = ((double)var41 + 0.5 - var22) / (var30 / 2.0);

                if (var39 * var39 + var42 * var42 < 1.0)
                {
                    for (int var44 = var34; var44 <= var37; ++var44)
                    {
                        double var45 = ((double)var44 + 0.5 - var24) / (var28 / 2.0);

                        if (var39 * var39 + var42 * var42 + var45 * var45 < 1.0 &&
                            world_get_block(w, var38, var41, var44) == target)
                            world_set_block(w, var38, var41, var44, block, 0, 2);
                    }
                }
            }
        }
    }
}

/* WorldGenMinable.generate, literally. Java evaluates operands left to right
 * and C does not, so the two draws that build the vein's endpoints and the two
 * that build its y line are separate statements, and every float expression
 * keeps Java's float width (the -ffp-contract=off build keeps the two
 * roundings Java makes). */
int feature_minable_target(struct world *w, jrand *r, int x, int y, int z, int block, int count,
                           int target)
{
    float var6 = jr_float(r) * PI_F;
    float sin6 = mh_sin(var6), cos6 = mh_cos(var6);
    float nc = (float)count;
    double var7 = (double)((float)(x + 8) + sin6 * nc / 8.0F);
    double var9 = (double)((float)(x + 8) - sin6 * nc / 8.0F);
    double var11 = (double)((float)(z + 8) + cos6 * nc / 8.0F);
    double var13 = (double)((float)(z + 8) - cos6 * nc / 8.0F);
    double var15 = (double)(y + jr_int_n(r, 3) - 2);
    double var17 = (double)(y + jr_int_n(r, 3) - 2);

    for (int var19 = 0; var19 <= count; ++var19)
    {
        double var20 = var7 + (var9 - var7) * (double)var19 / (double)count;
        double var22 = var15 + (var17 - var15) * (double)var19 / (double)count;
        double var24 = var11 + (var13 - var11) * (double)var19 / (double)count;
        double var26 = jr_double(r) * (double)count / 16.0;
        double var28 = (double)(mh_sin((float)var19 * PI_F / (float)count) + 1.0f) * var26 + 1.0;
        double var30 = (double)(mh_sin((float)var19 * PI_F / (float)count) + 1.0f) * var26 + 1.0;
        int var32 = mh_floor(var20 - var28 / 2.0);
        int var33 = mh_floor(var22 - var30 / 2.0);
        int var34 = mh_floor(var24 - var28 / 2.0);
        int var35 = mh_floor(var20 + var28 / 2.0);
        int var36 = mh_floor(var22 + var30 / 2.0);
        int var37 = mh_floor(var24 + var28 / 2.0);

        /* each axis's term is a function of its own coordinate: they are
         * computed once a step (the same operations, so the same doubles)
         * and the loops below read them */
        double qx[MINABLE_SPAN], qy[MINABLE_SPAN], qz[MINABLE_SPAN];

        if (var35 - var32 >= MINABLE_SPAN || var36 - var33 >= MINABLE_SPAN || var37 - var34 >= MINABLE_SPAN)
        {
            minable_box(w, var20, var22, var24, var28, var30, var32, var33, var34, var35, var36, var37, block, target);
            continue;
        }
        for (int k = 0; k <= var35 - var32; ++k)
        {
            double v = ((double)(var32 + k) + 0.5 - var20) / (var28 / 2.0);
            qx[k] = v * v;
        }
        for (int k = 0; k <= var36 - var33; ++k)
        {
            double v = ((double)(var33 + k) + 0.5 - var22) / (var30 / 2.0);
            qy[k] = v * v;
        }
        for (int k = 0; k <= var37 - var34; ++k)
        {
            double v = ((double)(var34 + k) + 0.5 - var24) / (var28 / 2.0);
            qz[k] = v * v;
        }

        struct vein_bands vb;

        vb.cx0 = var32 >> 4;
        vb.cz0 = var34 >> 4;
        vb.sy0 = var33 >> 4;
        vein_forget(&vb);

        for (int var38 = var32; var38 <= var35; ++var38)
        {
            double x2 = qx[var38 - var32];

            if (x2 < 1.0)
            {
                for (int var41 = var33; var41 <= var36; ++var41)
                {
                    double a = x2 + qy[var41 - var33];

                    if (a < 1.0)
                    {
                        /* var45 never falls as var44 grows (each step is a
                         * correctly rounded, monotone operation), so the
                         * test, var39^2 + var42^2 + var45^2 < 1, holds on one
                         * run of the row: its ends are found from outside
                         * and the cells between them are the ones Java's
                         * loop reads, in its order */
                        int lo = var34, hi = var37;

                        while (lo <= hi && !(a + qz[lo - var34] < 1.0)) ++lo;
                        while (hi > lo && !(a + qz[hi - var34] < 1.0)) --hi;

                        for (int var44 = lo; var44 <= hi; ++var44)
                        {
                            if (vein_read(w, &vb, var38, var41, var44) == target)
                            {
                                world_set_block(w, var38, var41, var44, block, 0, 2);
                                vein_forget(&vb);
                            }
                        }
                    }
                }
            }
        }
    }

    return 1;
}