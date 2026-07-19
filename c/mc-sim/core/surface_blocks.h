/* surface_blocks: exact C port of MC 1.11.2 surface layering.
 * PORT TARGETS (net/minecraft):
 *   - world/biome/Biome.java:299 generateBiomeTerrain (walk y=255..0, place topBlock/fillerBlock
 *     over stone, water/ice fill, sand->sandstone, the runlength `i==-1` reset, bedrock-free input).
 *   - world/biome/Biome.java:276 genTerrainBlocks (Plains does not override -> base path).
 *   - world/gen/ChunkProviderOverworld.java:185 replaceBiomeBlocks (depthBuffer =
 *     surfaceNoise.getRegion(...), 16x16 column loop calling genTerrainBlocks).
 *   - world/gen/ChunkProviderOverworld.java:201 provideChunk seeds this.rand with
 *     x*341873128712 + z*132897987541 BEFORE replaceBiomeBlocks (chunk (0,0) -> seed 0).
 *   - world/gen/NoiseGeneratorPerlin.java + NoiseGeneratorSimplex.java (surfaceNoise: the
 *     depth/jitter buffer; getRegion -> per-level Simplex.add()).
 *   - world/chunk/ChunkPrimer.java (char[65536], index = x<<12 | z<<8 | y).
 *
 * Worldgen is the one vanilla-bit-exact subsystem (SPEC rule 2): checked verbatim-Java == CPU ==
 * CUDA via the Java LCG. Build with -ffp-contract=off / --fmad=false.
 *
 * RNG draw order: surfaceNoise is the 4th noise generator built in ChunkProviderOverworld's
 * constructor from new Random(worldSeed), AFTER minLimit(16)+maxLimit(16)+main(8) =
 * 40 NoiseGeneratorImproved. A NoiseGeneratorImproved and a NoiseGeneratorSimplex consume the RNG
 * identically (3 nextDouble + 256 nextInt(256-l)), so we advance past the first 40, then build the
 * 4 Simplex levels of surfaceNoise. The per-chunk `rand` used by genTerrainBlocks is a SEPARATE
 * java.util.Random seeded to x*341873128712+z*132897987541 (=0 for chunk (0,0)); it draws
 * nextDouble() once for k, then nextInt(5) once per y (256x). Plains filler is dirt (never sand),
 * so the sand->sandstone nextInt(4) branch is provably never taken.
 *
 * HARNESS SUBSTITUTIONS (deterministic inputs, NOT numeric changes; same in golden + candidate):
 *  1. Synthetic input ChunkPrimer (replaces setBlocksInChunk + terrain_shape), bedrock-free:
 *       H(px,pz) = 50 + ((px*3 + pz*5) % 24)   (range [50,73]; straddles sea level 63)
 *       stone  for y in [1, H]
 *       water  for y in (H, 62]   (only non-empty when H < 62 -> ocean columns)
 *       air    otherwise (incl. y==0, which generateBiomeTerrain overwrites with bedrock)
 *     This exercises land columns (grass/dirt over stone) and ocean columns (gravel ocean floor).
 *  2. Fixed biome = vanilla Plains: topBlock=GRASS, fillerBlock=DIRT (defaults; BiomePlains does
 *     NOT override genTerrainBlocks/topBlock/fillerBlock - Biome.java:98,100,544), temperature
 *     0.8F (Biome.java:544). getFloatTemperature's noise branch is dead here: it is only reached
 *     via the water/ice fill at y<seaLevel(63)<=64, and getFloatTemperature returns the flat
 *     temperature for y<=64 (Biome.java:258); 0.8 >= 0.15 -> WATER, never ICE.
 *  3. Block-state ids are small integer constants (sanctioned registry substitution, same as
 *     genlayer_biomes), identical in golden + candidate (see SB_* below).
 *
 * Output contract: replaceBiomeBlocks for chunk (0,0) over the synthetic primer + Plains biome,
 * then the FULL resulting ChunkPrimer char[65536] in index order as %04x, one per line.
 *
 * C-vs-Java traps preserved: every (int)/(double) cast and left-to-right operator order replicated
 * to the bit; no a[i]=i++ (write plainly); single-RNG-call expressions need no temporaries but the
 * column loop keeps the LCG in exact lockstep. */
#ifndef MC_SURFACE_BLOCKS_H
#define MC_SURFACE_BLOCKS_H

#include <math.h>
#include "mc.h"
#include "mc_rng.h"

/* ===== block-state id substitution (identical in golden + candidate) ===== */
enum {
    SB_AIR = 0,
    SB_STONE = 1,
    SB_WATER = 2,          /* oceanBlock = Blocks.WATER; Material.WATER (NOT air) */
    SB_GRASS = 3,          /* topBlock */
    SB_DIRT = 4,           /* fillerBlock */
    SB_BEDROCK = 5,
    SB_GRAVEL = 6,
    SB_SAND = 7,
    SB_SANDSTONE = 8,
    SB_RED_SANDSTONE = 9,
    SB_ICE = 10
};

/* Material/Block identity predicates used by generateBiomeTerrain. */
MC_HD static inline int sb_material_is_air(int id) { return id == SB_AIR; }
MC_HD static inline int sb_block_is_stone(int id) { return id == SB_STONE; }
MC_HD static inline int sb_block_is_sand(int id)  { return id == SB_SAND; }

/* Plains temperature (Biome.java:544). */
#define SB_PLAINS_TEMPERATURE 0.8f
#define SB_SEA_LEVEL 63

/* ===== ChunkPrimer (char[65536], index = x<<12 | z<<8 | y) ===== */
typedef struct { u16 data[65536]; } ChunkPrimer;

MC_HD static inline int cp_index(int x, int y, int z) { return x << 12 | z << 8 | y; }
MC_HD MC_NOINLINE static int cp_get(const ChunkPrimer *p, int x, int y, int z) {
    return (int)p->data[cp_index(x, y, z)];
}
MC_HD MC_NOINLINE static void cp_set(ChunkPrimer *p, int x, int y, int z, int state) {
    p->data[cp_index(x, y, z)] = (u16)state;
}

/* ===== NoiseGeneratorSimplex (verbatim numerical port) ===== */
typedef struct {
    int p[512];
    double xo, yo, zo;
} Simplex;

typedef struct {
    Simplex noiseLevels[4];
    int levels;
} Perlin;

/* NoiseGeneratorSimplex(Random): 3 nextDouble + permutation shuffle. */
MC_HD MC_NOINLINE static void sb_simplex_init(Simplex *s, JavaRandom *r) {
    s->xo = jrand_double(r) * 256.0;
    s->yo = jrand_double(r) * 256.0;
    s->zo = jrand_double(r) * 256.0;
    for (int i = 0; i < 256; ++i) s->p[i] = i;   /* Java's p[i]=i++ is identity; a[i]=i++ is UB in C */
    for (int l = 0; l < 256; ++l) {
        int j = jrand_int_bound(r, 256 - l) + l;
        int k = s->p[l];
        s->p[l] = s->p[j];
        s->p[j] = k;
        s->p[l + 256] = s->p[l];
    }
}

/* Advance the Random exactly as one NoiseGeneratorImproved/Simplex constructor would (used for the
 * 40 generators built before surfaceNoise), without storing the result. */
MC_HD MC_NOINLINE static void sb_advance_noise_ctor(JavaRandom *r) {
    jrand_double(r);
    jrand_double(r);
    jrand_double(r);
    for (int l = 0; l < 256; ++l) jrand_int_bound(r, 256 - l);
}

/* NoiseGeneratorSimplex.fastFloor: value > 0 ? (int)value : (int)value - 1. */
MC_HD MC_NOINLINE static int sb_fastfloor(double value) {
    return value > 0.0 ? mc_d2i(value) : mc_d2i(value) - 1;
}

/* NoiseGeneratorSimplex.add (the getRegion path; getValue is unused here). */
MC_HD MC_NOINLINE static void sb_simplex_add(const Simplex *s, double *out,
        double p2, double p4, int p6, int p7, double p8, double p10, double p12) {
    const int grad3[12][3] = {
        {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
        {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
    const double SQRT_3 = sqrt(3.0);
    const double F2 = 0.5 * (SQRT_3 - 1.0);
    const double G2 = (3.0 - SQRT_3) / 6.0;
    int i = 0;
    for (int j = 0; j < p7; ++j) {
        double d0 = (p4 + (double)j) * p10 + s->yo;
        for (int k = 0; k < p6; ++k) {
            double d1 = (p2 + (double)k) * p8 + s->xo;
            double d5 = (d1 + d0) * F2;
            int l = sb_fastfloor(d1 + d5);
            int i1 = sb_fastfloor(d0 + d5);
            double d6 = (double)(l + i1) * G2;
            double d7 = (double)l - d6;
            double d8 = (double)i1 - d6;
            double d9 = d1 - d7;
            double d10 = d0 - d8;
            int j1, k1;
            if (d9 > d10) { j1 = 1; k1 = 0; } else { j1 = 0; k1 = 1; }
            double d11 = d9 - (double)j1 + G2;
            double d12 = d10 - (double)k1 + G2;
            double d13 = d9 - 1.0 + 2.0 * G2;
            double d14 = d10 - 1.0 + 2.0 * G2;
            int l1 = l & 255;
            int i2 = i1 & 255;
            int j2 = s->p[l1 + s->p[i2]] % 12;
            int k2 = s->p[l1 + j1 + s->p[i2 + k1]] % 12;
            int l2 = s->p[l1 + 1 + s->p[i2 + 1]] % 12;
            double d15 = 0.5 - d9 * d9 - d10 * d10;
            double d2;
            if (d15 < 0.0) {
                d2 = 0.0;
            } else {
                d15 = d15 * d15;
                d2 = d15 * d15 * ((double)grad3[j2][0] * d9 + (double)grad3[j2][1] * d10);
            }
            double d16 = 0.5 - d11 * d11 - d12 * d12;
            double d3;
            if (d16 < 0.0) {
                d3 = 0.0;
            } else {
                d16 = d16 * d16;
                d3 = d16 * d16 * ((double)grad3[k2][0] * d11 + (double)grad3[k2][1] * d12);
            }
            double d17 = 0.5 - d13 * d13 - d14 * d14;
            double d4;
            if (d17 < 0.0) {
                d4 = 0.0;
            } else {
                d17 = d17 * d17;
                d4 = d17 * d17 * ((double)grad3[l2][0] * d13 + (double)grad3[l2][1] * d14);
            }
            int i3 = i;
            ++i;
            out[i3] += 70.0 * (d2 + d3 + d4) * p12;
        }
    }
}

/* NoiseGeneratorPerlin.getRegion (9-arg). The 8-arg overload passes p14 = 0.5. out must be length
 * p6*p7; it is zeroed then accumulated, matching the !=null && length>= branch in vanilla. */
MC_HD MC_NOINLINE static void sb_perlin_getRegion(const Perlin *pn, double *out,
        double p2, double p4, int p6, int p7, double p8, double p10, double p12, double p14) {
    int total = p6 * p7;
    for (int i = 0; i < total; ++i) out[i] = 0.0;
    double d1 = 1.0;
    double d0 = 1.0;
    for (int j = 0; j < pn->levels; ++j) {
        sb_simplex_add(&pn->noiseLevels[j], out, p2, p4, p6, p7, p8 * d0 * d1, p10 * d0 * d1, 0.55 / d1);
        d0 *= p12;
        d1 *= p14;
    }
}

/* ChunkProviderOverworld constructor: build surfaceNoise (4 Simplex), advancing the RNG past the
 * 40 NoiseGeneratorImproved (minLimit 16 + maxLimit 16 + main 8) created before it. */
MC_HD MC_NOINLINE static void sb_surface_noise_init(Perlin *sn, i64 seed) {
    JavaRandom r; jrand_set(&r, seed);
    for (int i = 0; i < 16 + 16 + 8; ++i) sb_advance_noise_ctor(&r);  /* minLimit + maxLimit + main */
    sn->levels = 4;
    for (int i = 0; i < 4; ++i) sb_simplex_init(&sn->noiseLevels[i], &r);  /* surfaceNoise = Perlin(rand,4) */
}

/* ===== synthetic input primer (harness substitution; documented above) ===== */
MC_HD MC_NOINLINE static int sb_height(int px, int pz) {
    return 50 + ((px * 3 + pz * 5) % 24);
}
MC_HD MC_NOINLINE static void sb_fill_synthetic_primer(ChunkPrimer *p) {
    for (int i = 0; i < 65536; ++i) p->data[i] = (u16)SB_AIR;
    for (int px = 0; px < 16; ++px) {
        for (int pz = 0; pz < 16; ++pz) {
            int H = sb_height(px, pz);
            for (int py = 0; py < 256; ++py) {
                int id;
                if (py >= 1 && py <= H) id = SB_STONE;
                else if (py > H && py <= 62) id = SB_WATER;
                else id = SB_AIR;
                cp_set(p, px, py, pz, id);
            }
        }
    }
}

/* getFloatTemperature(pos) (Biome.java:258) restricted to the only reachable case here: the
 * water/ice fill calls it at y < seaLevel(63) <= 64, so the y>64 TEMPERATURE_NOISE branch is dead
 * and it returns the flat biome temperature. */
MC_HD MC_NOINLINE static float sb_getFloatTemperature(int y) {
    if (y > 64) {
        /* unreachable in this kernel (callers have y < 63); the vanilla path samples
         * TEMPERATURE_NOISE. Kept to mirror Biome.getFloatTemperature's structure. */
        return SB_PLAINS_TEMPERATURE;
    }
    return SB_PLAINS_TEMPERATURE;
}

/* Biome.generateBiomeTerrain (verbatim port; Plains topBlock=grass, fillerBlock=dirt). */
MC_HD MC_NOINLINE static void sb_generateBiomeTerrain(JavaRandom *rand, ChunkPrimer *primer,
        int x, int z, double noiseVal) {
    int i = SB_SEA_LEVEL;                  /* worldIn.getSeaLevel() = 63 */
    int iblockstate = SB_GRASS;            /* this.topBlock */
    int iblockstate1 = SB_DIRT;            /* this.fillerBlock */
    int j = -1;
    int k = mc_d2i(noiseVal / 3.0 + 3.0 + jrand_double(rand) * 0.25);
    int l = x & 15;
    int i1 = z & 15;

    for (int j1 = 255; j1 >= 0; --j1) {
        if (j1 <= jrand_int_bound(rand, 5)) {
            cp_set(primer, i1, j1, l, SB_BEDROCK);
        } else {
            int iblockstate2 = cp_get(primer, i1, j1, l);

            if (sb_material_is_air(iblockstate2)) {
                j = -1;
            } else if (sb_block_is_stone(iblockstate2)) {
                if (j == -1) {
                    if (k <= 0) {
                        iblockstate = SB_AIR;
                        iblockstate1 = SB_STONE;
                    } else if (j1 >= i - 4 && j1 <= i + 1) {
                        iblockstate = SB_GRASS;     /* this.topBlock */
                        iblockstate1 = SB_DIRT;     /* this.fillerBlock */
                    }

                    if (j1 < i && sb_material_is_air(iblockstate)) {
                        if (sb_getFloatTemperature(j1) < 0.15f) {
                            iblockstate = SB_ICE;
                        } else {
                            iblockstate = SB_WATER;
                        }
                    }

                    j = k;

                    if (j1 >= i - 1) {
                        cp_set(primer, i1, j1, l, iblockstate);
                    } else if (j1 < i - 7 - k) {
                        iblockstate = SB_AIR;
                        iblockstate1 = SB_STONE;
                        cp_set(primer, i1, j1, l, SB_GRAVEL);
                    } else {
                        cp_set(primer, i1, j1, l, iblockstate1);
                    }
                } else if (j > 0) {
                    --j;
                    cp_set(primer, i1, j1, l, iblockstate1);

                    if (j == 0 && sb_block_is_sand(iblockstate1) && k > 1) {
                        int mx = j1 - 63; if (mx < 0) mx = 0;       /* Math.max(0, j1 - 63) */
                        j = jrand_int_bound(rand, 4) + mx;
                        iblockstate1 = SB_SANDSTONE;                /* dirt is never red sand -> SANDSTONE */
                    }
                }
            }
        }
    }
}

/* ChunkProviderOverworld.replaceBiomeBlocks for chunk (0,0), every column = Plains. */
MC_HD MC_NOINLINE static void sb_replaceBiomeBlocks(const Perlin *surfaceNoise, ChunkPrimer *primer,
        int x, int z) {
    double depthBuffer[256];
    JavaRandom rand;
    jrand_set(&rand, (i64)x * 341873128712LL + (i64)z * 132897987541LL);   /* provideChunk seeding */

    sb_perlin_getRegion(surfaceNoise, depthBuffer, (double)(x * 16), (double)(z * 16),
                        16, 16, 0.0625, 0.0625, 1.0, 0.5);

    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            /* biomesIn[j + i*16] = Plains for every column (harness fixed biome) */
            sb_generateBiomeTerrain(&rand, primer, x * 16 + i, z * 16 + j, depthBuffer[j + i * 16]);
        }
    }
}

/* Full driver: seed -> synthetic primer -> replaceBiomeBlocks chunk (0,0) -> primer. */
MC_HD MC_NOINLINE static void sb_run(ChunkPrimer *primer, i64 seed) {
    Perlin surfaceNoise;
    sb_surface_noise_init(&surfaceNoise, seed);
    sb_fill_synthetic_primer(primer);
    sb_replaceBiomeBlocks(&surfaceNoise, primer, 0, 0);
}

#endif /* MC_SURFACE_BLOCKS_H */
