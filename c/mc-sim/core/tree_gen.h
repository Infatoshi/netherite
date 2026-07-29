/* tree_gen: exact C port of MC 1.11.2 WorldGenTrees.generate (the standard oak)
 *   net/minecraft/world/gen/feature/WorldGenTrees.java        : generate(World, Random, BlockPos)
 *   net/minecraft/world/gen/feature/WorldGenAbstractTree.java : isReplaceable / canGrowInto
 *   net/minecraft/world/gen/feature/WorldGenerator.java       : setBlockAndNotifyAdequately
 *   net/minecraft/block/Block.java                            : isAir/isLeaves/isWood/
 *                                                               canSustainPlant/onPlantGrow
 *
 * Worldgen is the one vanilla-bit-exact subsystem (SPEC rule 2): checked verbatim-Java == CPU ==
 * CUDA via the Java LCG. Build C with -ffp-contract=off / CUDA with --fmad=false. (This feature
 * draws only ints from the LCG: no float/double math at all - the only floats anywhere are the
 * integer-division leaf-radius math, which is exact.)
 *
 * CONSTRUCTOR FLAGS (default oak = `new WorldGenTrees(false)` -> the 5-arg ctor at WorldGenTrees:32):
 *   notify    = false  (WorldGenerator.doBlockNotify; setBlockAndNotifyAdequately uses flag 2)
 *   minTreeHeight = 4
 *   metaWood  = OAK LOG    (Blocks.LOG default, VARIANT=OAK)            -> id substitution TG_LOG
 *   metaLeaves= OAK LEAVES (Blocks.LEAVES default, VARIANT=OAK, CHECK_DECAY=false) -> TG_LEAVES
 *   vinesGrow = false  -> the vine/cocoa branches are DEAD (never executed) and draw NO RNG; they
 *                        are omitted here and in the golden. This does not change control flow or
 *                        the LCG draw sequence. The ONLY RNG draws are: rand.nextInt(3) (height),
 *                        and rand.nextInt(2) per canopy-corner cell in the leaf loop.
 *
 * SYNTHETIC WORLD (identical here and in oracle/goldens/tree_gen/Golden.java):
 *   A cube of edge TG_DIM (=32), index (y*DIM + z)*DIM + x like ore_gen. worldIn.getHeight() = DIM.
 *   Flat floor: y in [0, TG_FLOOR_Y) = DIRT, y == TG_FLOOR_Y = GRASS, y > TG_FLOOR_Y = AIR.
 *   Tree planted at the fixed BlockPos (TG_PLANT_X, TG_FLOOR_Y+1, TG_PLANT_Z) = (16, 9, 16),
 *   i.e. on top of the grass. position.down() = the grass at (16,8,16).
 *
 * BLOCK-STATE id substitution (sanctioned, same pattern as ore_gen/caves; meta 0 throughout, so a
 * packed state reduces to mc_state(blockId,0)). Each maps to exactly one World/Block predicate:
 *   TG_AIR    = mc_state(BLK_AIR,0)    : Material.AIR  -> isAir, canGrowInto(air material)
 *   TG_GRASS  = mc_state(BLK_GRASS,0)  : Blocks.GRASS  -> canGrowInto, canSustainBush, onPlantGrow->DIRT
 *   TG_DIRT   = mc_state(BLK_DIRT,0)   : Blocks.DIRT   -> canGrowInto, canSustainBush
 *   TG_LOG    = mc_state(BLK_LOG,0)    : Blocks.LOG    -> isWood, canGrowInto; the trunk block (metaWood)
 *   TG_LEAVES = mc_state(BLK_LEAVES,0) : Material.LEAVES -> isLeaves, canGrowInto; canopy (metaLeaves)
 *
 * generate() READS the world mid-loop (leaves placed earlier are seen as leaves on revisit; the
 * trunk loop runs after the leaf loop and overwrites the central leaf column with logs), so it MUST
 * run strictly single-threaded (one env / one thread, also on CUDA) - same as ore_gen.
 *
 * C-vs-Java traps handled: the leaf-corner condition `A || B || C && D` is `A || B || (C && D)` in
 * Java (&& binds tighter than ||) with C = rand.nextInt(2) drawn ONLY when A and B are both false
 * (i.e. abs(l1)==j1 && abs(j2)==j1). Replicated exactly so the LCG draw count matches. All array
 * writes are OOB-guarded. Java int widths (32-bit) and integer-division truncation toward zero
 * (j1 = 1 - i4/2 with i4<=0) match C. */
#ifndef MC_TREE_GEN_H
#define MC_TREE_GEN_H

#include "mc.h"
#include "mc_rng.h"
#include "mc_blocks.h"
#include "mc_world.h"

#define TG_DIM      32
#define TG_FLOOR_Y  8
#define TG_PLANT_X  16
#define TG_PLANT_Z  16

/* block-state id substitution (meta 0; a state reduces to mc_state(id,0)). */
#define TG_AIR    mc_state(BLK_AIR, 0)
#define TG_GRASS  mc_state(BLK_GRASS, 0)
#define TG_DIRT   mc_state(BLK_DIRT, 0)
#define TG_LOG    mc_state(BLK_LOG, 0)
#define TG_LEAVES mc_state(BLK_LEAVES, 0)

/* cube index order matches the drivers' print order: (y*dim + z)*dim + x. */
MC_HD static inline int tg_idx(int dim, int x, int y, int z) { return (y * dim + z) * dim + x; }

/* world.getBlockState / setBlockState over the cube; OOB read returns TG_AIR's complement so no
 * predicate matches (mirrors a non-replaceable, out-of-world cell). OOB writes are dropped. */
MC_HD MC_NOINLINE static u16 tg_get(const u16 *cube, int dim, int x, int y, int z) {
    if (x < 0 || x >= dim || y < 0 || y >= dim || z < 0 || z >= dim) return 0xFFFF;
    return cube[tg_idx(dim, x, y, z)];
}
MC_HD MC_NOINLINE static void tg_set(u16 *cube, int dim, int x, int y, int z, u16 s) {
    if (x < 0 || x >= dim || y < 0 || y >= dim || z < 0 || z >= dim) return;
    cube[tg_idx(dim, x, y, z)] = s;
}

/* ===== Block predicates, reduced to the integer ids actually present (AIR/GRASS/DIRT/LOG/LEAVES) =====
 * Block.isAir: state.getMaterial() == Material.AIR.   Only TG_AIR has the AIR material. */
MC_HD static inline int tg_isAir(u16 s)    { return s == TG_AIR; }
/* Block.isLeaves: state.getMaterial() == Material.LEAVES. */
MC_HD static inline int tg_isLeaves(u16 s) { return s == TG_LEAVES; }
/* Block.isWood: default false; BlockLog overrides -> true. Only TG_LOG is wood here. */
MC_HD static inline int tg_isWood(u16 s)   { return s == TG_LOG; }
/* WorldGenAbstractTree.canGrowInto: material AIR or LEAVES, or block is GRASS/DIRT/LOG/LOG2/
 * SAPLING/VINE. Of the ids present: AIR, LEAVES, GRASS, DIRT, LOG all qualify. */
MC_HD MC_NOINLINE static int tg_canGrowInto(u16 s) {
    return s == TG_AIR || s == TG_LEAVES || s == TG_GRASS || s == TG_DIRT || s == TG_LOG;
}
/* WorldGenAbstractTree.isReplaceable: isAir || isLeaves || isWood || canGrowInto. */
MC_HD MC_NOINLINE static int tg_isReplaceable(const u16 *cube, int dim, int x, int y, int z) {
    u16 s = tg_get(cube, dim, x, y, z);
    return tg_isAir(s) || tg_isLeaves(s) || tg_isWood(s) || tg_canGrowInto(s);
}
/* Block.canSustainPlant for the sapling plantable reduces (via BlockBush.canSustainBush, since
 * BlockSapling extends BlockBush) to: soil is GRASS/DIRT/FARMLAND. FARMLAND is absent here. */
MC_HD static inline int tg_canSustainPlant(u16 soil) { return soil == TG_GRASS || soil == TG_DIRT; }
/* Block.onPlantGrow: if the block is GRASS or FARMLAND, set it to DIRT. (Places dirt under trunk.) */
MC_HD MC_NOINLINE static void tg_onPlantGrow(u16 *cube, int dim, int x, int y, int z) {
    if (tg_get(cube, dim, x, y, z) == TG_GRASS) tg_set(cube, dim, x, y, z, TG_DIRT);
}

/* Verbatim WorldGenTrees.generate (standard oak), world calls -> the cube above. Returns 1/0 like
 * the Java boolean. minTreeHeight is fixed to 4, vinesGrow=false (dead branches omitted). */
MC_HD MC_NOINLINE static int mc_tree_gen(u16 *cube, int dim, JavaRandom *rand,
                                    int posX, int posY, int posZ) {
    int height = dim;                 /* worldIn.getHeight() */
    int minTreeHeight = 4;
    u16 metaWood = TG_LOG;
    u16 metaLeaves = TG_LEAVES;

    int i = jrand_int_bound(rand, 3) + minTreeHeight;
    int flag = 1;

    if (posY >= 1 && posY + i + 1 <= height) {
        for (int j = posY; j <= posY + 1 + i; ++j) {
            int k = 1;

            if (j == posY) {
                k = 0;
            }

            if (j >= posY + 1 + i - 2) {
                k = 2;
            }

            for (int l = posX - k; l <= posX + k && flag; ++l) {
                for (int i1 = posZ - k; i1 <= posZ + k && flag; ++i1) {
                    if (j >= 0 && j < height) {
                        if (!tg_isReplaceable(cube, dim, l, j, i1)) {
                            flag = 0;
                        }
                    } else {
                        flag = 0;
                    }
                }
            }
        }

        if (!flag) {
            return 0;
        } else {
            u16 state = tg_get(cube, dim, posX, posY - 1, posZ);   /* getBlockState(position.down()) */

            if (tg_canSustainPlant(state) && posY < height - i - 1) {
                tg_onPlantGrow(cube, dim, posX, posY - 1, posZ);

                for (int i3 = posY - 3 + i; i3 <= posY + i; ++i3) {
                    int i4 = i3 - (posY + i);
                    int j1 = 1 - i4 / 2;

                    for (int k1 = posX - j1; k1 <= posX + j1; ++k1) {
                        int l1 = k1 - posX;

                        for (int i2 = posZ - j1; i2 <= posZ + j1; ++i2) {
                            int j2 = i2 - posZ;

                            /* Java: abs(l1)!=j1 || abs(j2)!=j1 || (nextInt(2)!=0 && i4!=0).
                             * nextInt(2) is drawn ONLY at the canopy corners (both abs == j1). */
                            int place;
                            int al1 = l1 < 0 ? -l1 : l1;
                            int aj2 = j2 < 0 ? -j2 : j2;
                            if (al1 != j1 || aj2 != j1) {
                                place = 1;
                            } else {
                                int c = (jrand_int_bound(rand, 2) != 0);
                                place = c && (i4 != 0);
                            }

                            if (place) {
                                u16 cs = tg_get(cube, dim, k1, i3, i2);
                                if (tg_isAir(cs) || tg_isLeaves(cs)) {   /* || Material.VINE (none) */
                                    tg_set(cube, dim, k1, i3, i2, metaLeaves);
                                }
                            }
                        }
                    }
                }

                for (int j3 = 0; j3 < i; ++j3) {
                    u16 cs = tg_get(cube, dim, posX, posY + j3, posZ);   /* position.up(j3) */

                    if (tg_isAir(cs) || tg_isLeaves(cs)) {   /* || Material.VINE (none) */
                        tg_set(cube, dim, posX, posY + j3, posZ, metaWood);
                        /* vinesGrow == false: the vine placement block here is dead, draws no RNG. */
                    }
                }

                return 1;
            } else {
                return 0;
            }
        }
    } else {
        return 0;
    }
}

/* Build the synthetic floor cube: DIRT below, GRASS at TG_FLOOR_Y, AIR above. */
MC_HD MC_NOINLINE static void tg_build_world(u16 *cube, int dim) {
    for (int y = 0; y < dim; ++y) {
        u16 s = (y < TG_FLOOR_Y) ? TG_DIRT : (y == TG_FLOOR_Y ? TG_GRASS : TG_AIR);
        for (int z = 0; z < dim; ++z)
            for (int x = 0; x < dim; ++x)
                cube[tg_idx(dim, x, y, z)] = s;
    }
}

#endif /* MC_TREE_GEN_H */
