/* ore_gen: exact C port of MC 1.11.2 WorldGenMinable.generate
 * (net/minecraft/world/gen/feature/WorldGenMinable.java). Places an ore "blob": a line of
 * overlapping ellipsoids whose endpoints/radii come from rand + the MathHelper table trig.
 *
 * Worldgen is the one vanilla-bit-exact subsystem (SPEC rule 2): checked verbatim-Java == CPU ==
 * CUDA via the Java LCG. Build with -ffp-contract=off / --fmad=false.
 *
 * Faithful test model (see oracle/goldens/ore_gen/Golden.java): the world is a flat all-STONE cube.
 * StonePredicate replaces a cell iff it is (natural) stone; generate() READS the world mid-loop, so
 * once a cell is turned to ore a later revisit sees ore and is skipped. We mutate the cube in place
 * and read prior writes => MUST run strictly single-threaded (one env / one thread, also on CUDA).
 *
 * C-vs-Java traps preserved: rand draw order is nextFloat, nextInt(3), nextInt(3), then per-iter
 * nextDouble (nextFloat is stored in f before the sin/cos lines, which draw no RNG). Float vs
 * double precision matches the Java at every step ((float)Math.PI, (float)i/(float)numberOfBlocks,
 * MathHelper.sin returns float). MathHelper.floor is floor-toward-neg-inf returning int. */
#ifndef MC_ORE_GEN_H
#define MC_ORE_GEN_H

#include "mc.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "mc_blocks.h"
#include "mc_world.h"

/* cube index order matches the drivers' print order: (y*dim + z)*dim + x. */
MC_HD static inline int mc_ore_idx(int dim, int x, int y, int z) { return (y * dim + z) * dim + x; }

/* WorldGenMinable.generate(world, rand, position) over a flat all-stone cube of edge `dim`.
 * position = (posX, posY, posZ); replaces natural-stone cells in the blob with oreBlock. */
MC_HD MC_NOINLINE static void mc_ore_gen(u16 *cube, int dim, const McSinTable *st, JavaRandom *rand,
                                    int posX, int posY, int posZ, int numberOfBlocks,
                                    u16 oreBlock, u16 naturalStone) {
    float f = jrand_float(rand) * (float)MC_PI;
    double d0 = (double)((float)(posX + 8) + mc_sin(st, f) * (float)numberOfBlocks / 8.0F);
    double d1 = (double)((float)(posX + 8) - mc_sin(st, f) * (float)numberOfBlocks / 8.0F);
    double d2 = (double)((float)(posZ + 8) + mc_cos(st, f) * (float)numberOfBlocks / 8.0F);
    double d3 = (double)((float)(posZ + 8) - mc_cos(st, f) * (float)numberOfBlocks / 8.0F);
    double d4 = (double)(posY + jrand_int_bound(rand, 3) - 2);
    double d5 = (double)(posY + jrand_int_bound(rand, 3) - 2);

    for (int i = 0; i < numberOfBlocks; ++i) {
        float f1 = (float)i / (float)numberOfBlocks;
        double d6 = d0 + (d1 - d0) * (double)f1;
        double d7 = d4 + (d5 - d4) * (double)f1;
        double d8 = d2 + (d3 - d2) * (double)f1;
        double d9 = jrand_double(rand) * (double)numberOfBlocks / 16.0;
        double d10 = (double)(mc_sin(st, (float)MC_PI * f1) + 1.0F) * d9 + 1.0;
        double d11 = (double)(mc_sin(st, (float)MC_PI * f1) + 1.0F) * d9 + 1.0;
        int j = mc_floor(d6 - d10 / 2.0);
        int k = mc_floor(d7 - d11 / 2.0);
        int l = mc_floor(d8 - d10 / 2.0);
        int i1 = mc_floor(d6 + d10 / 2.0);
        int j1 = mc_floor(d7 + d11 / 2.0);
        int k1 = mc_floor(d8 + d10 / 2.0);

        for (int l1 = j; l1 <= i1; ++l1) {
            double d12 = ((double)l1 + 0.5 - d6) / (d10 / 2.0);

            if (d12 * d12 < 1.0) {
                for (int i2 = k; i2 <= j1; ++i2) {
                    double d13 = ((double)i2 + 0.5 - d7) / (d11 / 2.0);

                    if (d12 * d12 + d13 * d13 < 1.0) {
                        for (int j2 = l; j2 <= k1; ++j2) {
                            double d14 = ((double)j2 + 0.5 - d8) / (d10 / 2.0);

                            if (d12 * d12 + d13 * d13 + d14 * d14 < 1.0) {
                                /* worldIn.getBlockState(pos); isReplaceableOreGen w/ StonePredicate
                                 * == "is the cell natural stone". OOB cells hold nothing -> skip. */
                                if (l1 >= 0 && l1 < dim && i2 >= 0 && i2 < dim && j2 >= 0 && j2 < dim) {
                                    int idx = mc_ore_idx(dim, l1, i2, j2);
                                    if (cube[idx] == naturalStone) cube[idx] = oreBlock;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

#endif /* MC_ORE_GEN_H */
