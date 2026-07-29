// Verbatim MC 1.11.2 surface layering (vanilla ground truth, eval-pure, render-opt style). Real
// decompiled code only - never a hand-port of the candidate. Embeds:
//   - net/minecraft/world/gen/NoiseGeneratorImproved + NoiseGeneratorOctaves (only their RNG-
//     consuming constructors are exercised: minLimit(16)+maxLimit(16)+main(8) advance the Random
//     to the exact point where surfaceNoise is built, mirroring ChunkProviderOverworld's ctor).
//   - net/minecraft/world/gen/NoiseGeneratorSimplex (ctor + add) + NoiseGeneratorPerlin (ctor +
//     both getRegion overloads): surfaceNoise, sampled into depthBuffer.
//   - net/minecraft/world/biome/Biome.generateBiomeTerrain (the surface/filler/water/gravel/
//     bedrock layering) + the base genTerrainBlocks path (Plains does not override).
//   - net/minecraft/world/gen/ChunkProviderOverworld.replaceBiomeBlocks + provideChunk's per-chunk
//     rand seeding (x*341873128712 + z*132897987541; =0 for chunk (0,0)).
//   - net/minecraft/world/chunk/ChunkPrimer (char[65536], index = x<<12 | z<<8 | y).
//
// Sanctioned substitutions (deterministic inputs, NOT numeric changes; identical in the C/CUDA
// candidate core/surface_blocks.h):
//   1. Block-state ids -> small integer constants (same as genlayer_biomes' registry substitution).
//   2. Fixed biome = vanilla Plains: topBlock=GRASS, fillerBlock=DIRT (defaults, Biome.java:98,100;
//      BiomePlains does not override them or genTerrainBlocks), temperature 0.8F (Biome.java:544).
//      getFloatTemperature's TEMPERATURE_NOISE branch is dead here (only reached at y<seaLevel
//      63<=64, where it returns the flat temperature; 0.8>=0.15 -> WATER, never ICE).
//   3. Synthetic input ChunkPrimer (replaces setBlocksInChunk), bedrock-free:
//        H(px,pz) = 50 + ((px*3 + pz*5) % 24); stone y in [1,H]; water y in (H,62]; air otherwise.
//
// Output: the full resulting ChunkPrimer char[65536] in index order as %04x, one per line,
// matching cpu/surface_blocks.c.
import java.util.Random;

public class Golden {
    // ----- block-state id substitution -----
    static final int AIR = 0, STONE = 1, WATER = 2, GRASS = 3, DIRT = 4, BEDROCK = 5,
                     GRAVEL = 6, SAND = 7, SANDSTONE = 8, RED_SANDSTONE = 9, ICE = 10;
    static final int SEA_LEVEL = 63;
    static final float PLAINS_TEMPERATURE = 0.8F;

    // ----- ChunkPrimer (char[65536], index = x<<12 | z<<8 | y) -----
    static class ChunkPrimer {
        final char[] data = new char[65536];
        int getBlockState(int x, int y, int z) { return this.data[getBlockIndex(x, y, z)]; }
        void setBlockState(int x, int y, int z, int state) { this.data[getBlockIndex(x, y, z)] = (char)state; }
        static int getBlockIndex(int x, int y, int z) { return x << 12 | z << 8 | y; }
    }

    // ----- NoiseGeneratorImproved (only the RNG-consuming ctor is exercised) -----
    static class NoiseGeneratorImproved {
        private final int[] permutations;
        public double xCoord, yCoord, zCoord;
        public NoiseGeneratorImproved(Random p) {
            this.permutations = new int[512];
            this.xCoord = p.nextDouble() * 256.0D;
            this.yCoord = p.nextDouble() * 256.0D;
            this.zCoord = p.nextDouble() * 256.0D;
            for (int i = 0; i < 256; this.permutations[i] = i++) { ; }
            for (int l = 0; l < 256; ++l) {
                int j = p.nextInt(256 - l) + l;
                int k = this.permutations[l];
                this.permutations[l] = this.permutations[j];
                this.permutations[j] = k;
                this.permutations[l + 256] = this.permutations[l];
            }
        }
    }

    static class NoiseGeneratorOctaves {
        private final NoiseGeneratorImproved[] generatorCollection;
        private final int octaves;
        public NoiseGeneratorOctaves(Random seed, int octavesIn) {
            this.octaves = octavesIn;
            this.generatorCollection = new NoiseGeneratorImproved[octavesIn];
            for (int i = 0; i < octavesIn; ++i) this.generatorCollection[i] = new NoiseGeneratorImproved(seed);
        }
    }

    // ----- NoiseGeneratorSimplex (verbatim) -----
    static class NoiseGeneratorSimplex {
        private static final int[][] grad3 = new int[][] {{1, 1, 0}, { -1, 1, 0}, {1, -1, 0}, { -1, -1, 0}, {1, 0, 1}, { -1, 0, 1}, {1, 0, -1}, { -1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
        public static final double SQRT_3 = Math.sqrt(3.0D);
        private final int[] p;
        public double xo;
        public double yo;
        public double zo;
        private static final double F2 = 0.5D * (SQRT_3 - 1.0D);
        private static final double G2 = (3.0D - SQRT_3) / 6.0D;

        public NoiseGeneratorSimplex(Random p_i45471_1_) {
            this.p = new int[512];
            this.xo = p_i45471_1_.nextDouble() * 256.0D;
            this.yo = p_i45471_1_.nextDouble() * 256.0D;
            this.zo = p_i45471_1_.nextDouble() * 256.0D;
            for (int i = 0; i < 256; this.p[i] = i++) { ; }
            for (int l = 0; l < 256; ++l) {
                int j = p_i45471_1_.nextInt(256 - l) + l;
                int k = this.p[l];
                this.p[l] = this.p[j];
                this.p[j] = k;
                this.p[l + 256] = this.p[l];
            }
        }

        private static int fastFloor(double value) {
            return value > 0.0D ? (int)value : (int)value - 1;
        }

        private static double dot(int[] p_151604_0_, double p_151604_1_, double p_151604_3_) {
            return (double)p_151604_0_[0] * p_151604_1_ + (double)p_151604_0_[1] * p_151604_3_;
        }

        public void add(double[] p_151606_1_, double p_151606_2_, double p_151606_4_, int p_151606_6_, int p_151606_7_, double p_151606_8_, double p_151606_10_, double p_151606_12_) {
            int i = 0;
            for (int j = 0; j < p_151606_7_; ++j) {
                double d0 = (p_151606_4_ + (double)j) * p_151606_10_ + this.yo;
                for (int k = 0; k < p_151606_6_; ++k) {
                    double d1 = (p_151606_2_ + (double)k) * p_151606_8_ + this.xo;
                    double d5 = (d1 + d0) * F2;
                    int l = fastFloor(d1 + d5);
                    int i1 = fastFloor(d0 + d5);
                    double d6 = (double)(l + i1) * G2;
                    double d7 = (double)l - d6;
                    double d8 = (double)i1 - d6;
                    double d9 = d1 - d7;
                    double d10 = d0 - d8;
                    int j1;
                    int k1;
                    if (d9 > d10) { j1 = 1; k1 = 0; } else { j1 = 0; k1 = 1; }
                    double d11 = d9 - (double)j1 + G2;
                    double d12 = d10 - (double)k1 + G2;
                    double d13 = d9 - 1.0D + 2.0D * G2;
                    double d14 = d10 - 1.0D + 2.0D * G2;
                    int l1 = l & 255;
                    int i2 = i1 & 255;
                    int j2 = this.p[l1 + this.p[i2]] % 12;
                    int k2 = this.p[l1 + j1 + this.p[i2 + k1]] % 12;
                    int l2 = this.p[l1 + 1 + this.p[i2 + 1]] % 12;
                    double d15 = 0.5D - d9 * d9 - d10 * d10;
                    double d2;
                    if (d15 < 0.0D) { d2 = 0.0D; } else { d15 = d15 * d15; d2 = d15 * d15 * dot(grad3[j2], d9, d10); }
                    double d16 = 0.5D - d11 * d11 - d12 * d12;
                    double d3;
                    if (d16 < 0.0D) { d3 = 0.0D; } else { d16 = d16 * d16; d3 = d16 * d16 * dot(grad3[k2], d11, d12); }
                    double d17 = 0.5D - d13 * d13 - d14 * d14;
                    double d4;
                    if (d17 < 0.0D) { d4 = 0.0D; } else { d17 = d17 * d17; d4 = d17 * d17 * dot(grad3[l2], d13, d14); }
                    int i3 = i++;
                    p_151606_1_[i3] += 70.0D * (d2 + d3 + d4) * p_151606_12_;
                }
            }
        }
    }

    // ----- NoiseGeneratorPerlin (verbatim) -----
    static class NoiseGeneratorPerlin {
        private final NoiseGeneratorSimplex[] noiseLevels;
        private final int levels;
        public NoiseGeneratorPerlin(Random p_i45470_1_, int p_i45470_2_) {
            this.levels = p_i45470_2_;
            this.noiseLevels = new NoiseGeneratorSimplex[p_i45470_2_];
            for (int i = 0; i < p_i45470_2_; ++i) this.noiseLevels[i] = new NoiseGeneratorSimplex(p_i45470_1_);
        }
        public double[] getRegion(double[] p_151599_1_, double p_151599_2_, double p_151599_4_, int p_151599_6_, int p_151599_7_, double p_151599_8_, double p_151599_10_, double p_151599_12_) {
            return this.getRegion(p_151599_1_, p_151599_2_, p_151599_4_, p_151599_6_, p_151599_7_, p_151599_8_, p_151599_10_, p_151599_12_, 0.5D);
        }
        public double[] getRegion(double[] p_151600_1_, double p_151600_2_, double p_151600_4_, int p_151600_6_, int p_151600_7_, double p_151600_8_, double p_151600_10_, double p_151600_12_, double p_151600_14_) {
            if (p_151600_1_ != null && p_151600_1_.length >= p_151600_6_ * p_151600_7_) {
                for (int i = 0; i < p_151600_1_.length; ++i) p_151600_1_[i] = 0.0D;
            } else {
                p_151600_1_ = new double[p_151600_6_ * p_151600_7_];
            }
            double d1 = 1.0D;
            double d0 = 1.0D;
            for (int j = 0; j < this.levels; ++j) {
                this.noiseLevels[j].add(p_151600_1_, p_151600_2_, p_151600_4_, p_151600_6_, p_151600_7_, p_151600_8_ * d0 * d1, p_151600_10_ * d0 * d1, 0.55D / d1);
                d0 *= p_151600_12_;
                d1 *= p_151600_14_;
            }
            return p_151600_1_;
        }
    }

    // ----- Biome.getFloatTemperature (the only reachable case: y<=64 -> flat temperature) -----
    static float getFloatTemperature(int y) {
        if (y > 64) {
            // vanilla samples TEMPERATURE_NOISE here; unreachable in this kernel (callers have y<63).
            return PLAINS_TEMPERATURE;
        } else {
            return PLAINS_TEMPERATURE;
        }
    }

    // ----- Biome.generateBiomeTerrain (verbatim; Plains topBlock=GRASS, fillerBlock=DIRT) -----
    static void generateBiomeTerrain(Random rand, ChunkPrimer chunkPrimerIn, int x, int z, double noiseVal) {
        int i = SEA_LEVEL;
        int iblockstate = GRASS;        // this.topBlock
        int iblockstate1 = DIRT;        // this.fillerBlock
        int j = -1;
        int k = (int)(noiseVal / 3.0D + 3.0D + rand.nextDouble() * 0.25D);
        int l = x & 15;
        int i1 = z & 15;

        for (int j1 = 255; j1 >= 0; --j1) {
            if (j1 <= rand.nextInt(5)) {
                chunkPrimerIn.setBlockState(i1, j1, l, BEDROCK);
            } else {
                int iblockstate2 = chunkPrimerIn.getBlockState(i1, j1, l);
                if (iblockstate2 == AIR) {              // iblockstate2.getMaterial() == Material.AIR
                    j = -1;
                } else if (iblockstate2 == STONE) {     // iblockstate2.getBlock() == Blocks.STONE
                    if (j == -1) {
                        if (k <= 0) {
                            iblockstate = AIR;
                            iblockstate1 = STONE;
                        } else if (j1 >= i - 4 && j1 <= i + 1) {
                            iblockstate = GRASS;        // this.topBlock
                            iblockstate1 = DIRT;        // this.fillerBlock
                        }
                        if (j1 < i && iblockstate == AIR) {   // (iblockstate == null || material == AIR)
                            if (getFloatTemperature(j1) < 0.15F) {
                                iblockstate = ICE;
                            } else {
                                iblockstate = WATER;
                            }
                        }
                        j = k;
                        if (j1 >= i - 1) {
                            chunkPrimerIn.setBlockState(i1, j1, l, iblockstate);
                        } else if (j1 < i - 7 - k) {
                            iblockstate = AIR;
                            iblockstate1 = STONE;
                            chunkPrimerIn.setBlockState(i1, j1, l, GRAVEL);
                        } else {
                            chunkPrimerIn.setBlockState(i1, j1, l, iblockstate1);
                        }
                    } else if (j > 0) {
                        --j;
                        chunkPrimerIn.setBlockState(i1, j1, l, iblockstate1);
                        if (j == 0 && iblockstate1 == SAND && k > 1) {
                            j = rand.nextInt(4) + Math.max(0, j1 - 63);
                            iblockstate1 = SANDSTONE;   // fillerBlock is dirt, never RED_SAND
                        }
                    }
                }
            }
        }
    }

    // ----- synthetic input primer (harness substitution; same rule as the candidate) -----
    static int height(int px, int pz) {
        return 50 + ((px * 3 + pz * 5) % 24);
    }
    static void fillSyntheticPrimer(ChunkPrimer p) {
        for (int px = 0; px < 16; ++px) {
            for (int pz = 0; pz < 16; ++pz) {
                int H = height(px, pz);
                for (int py = 0; py < 256; ++py) {
                    int id;
                    if (py >= 1 && py <= H) id = STONE;
                    else if (py > H && py <= 62) id = WATER;
                    else id = AIR;
                    p.setBlockState(px, py, pz, id);
                }
            }
        }
    }

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        int chunkX = 0, chunkZ = 0;

        // ChunkProviderOverworld constructor: surfaceNoise is built 4th from new Random(seed).
        Random rand = new Random(seed);
        NoiseGeneratorOctaves minLimitPerlinNoise = new NoiseGeneratorOctaves(rand, 16);
        NoiseGeneratorOctaves maxLimitPerlinNoise = new NoiseGeneratorOctaves(rand, 16);
        NoiseGeneratorOctaves mainPerlinNoise = new NoiseGeneratorOctaves(rand, 8);
        NoiseGeneratorPerlin surfaceNoise = new NoiseGeneratorPerlin(rand, 4);
        // scaleNoise/depthNoise/forestNoise are built after; not needed for surface layering.

        ChunkPrimer primer = new ChunkPrimer();
        fillSyntheticPrimer(primer);

        // replaceBiomeBlocks(chunkX, chunkZ, primer, biomes=Plains).
        Random chunkRand = new Random();
        chunkRand.setSeed((long)chunkX * 341873128712L + (long)chunkZ * 132897987541L);  // provideChunk
        double[] depthBuffer = new double[256];
        depthBuffer = surfaceNoise.getRegion(depthBuffer, (double)(chunkX * 16), (double)(chunkZ * 16), 16, 16, 0.0625D, 0.0625D, 1.0D);
        for (int i = 0; i < 16; ++i) {
            for (int j = 0; j < 16; ++j) {
                // biomesIn[j + i*16] = Plains for every column.
                generateBiomeTerrain(chunkRand, primer, chunkX * 16 + i, chunkZ * 16 + j, depthBuffer[j + i * 16]);
            }
        }

        StringBuilder sb = new StringBuilder();
        for (int idx = 0; idx < 65536; idx++) sb.append(String.format("%04x%n", (int)primer.data[idx]));
        System.out.print(sb);
    }
}
