// Verbatim MC 1.11.2 ChunkProviderEnd (net/minecraft/world/gen/ChunkProviderEnd.java) inlined with
// NoiseGeneratorImproved/Octaves/Simplex and a provideChunk-minus-structures driver. Block-state
// substitution (identical to core/chunk_provider_end.h): AIR=0, END_STONE=1, STONE=2 (predicate
// only in buildSurfaces; never placed by setBlocksInChunk). Goldens from real MC only.
import java.util.Random;

public class Golden {

    static final int AIR = 0, END_STONE = 1, STONE = 2;

    static long lfloor(double value) { long i = (long)value; return value < (double)i ? i - 1L : i; }
    static float sqrtf(float value) { return (float)Math.sqrt((double)value); }
    static float absf(float value) { return value >= 0.0F ? value : -value; }
    static double clamp(double num, double min, double max) {
        return num < min ? min : (num > max ? max : num);
    }

    static class NoiseGeneratorImproved {
        private final int[] permutations;
        public double xCoord, yCoord, zCoord;
        private static final double[] GRAD_X = {1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0};
        private static final double[] GRAD_Y = {1,1,-1,-1,0,0,0,0,1,-1,1,-1,1,-1,1,-1};
        private static final double[] GRAD_Z = {0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1};
        private static final double[] GRAD_2X = {1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0};
        private static final double[] GRAD_2Z = {0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1};
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
        public final double lerp(double a, double b, double c) { return b + a * (c - b); }
        public final double grad2(int h, double x, double z) { int i = h & 15; return GRAD_2X[i] * x + GRAD_2Z[i] * z; }
        public final double grad(int h, double x, double y, double z) { int i = h & 15; return GRAD_X[i] * x + GRAD_Y[i] * y + GRAD_Z[i] * z; }
        public void populateNoiseArray(double[] noiseArray, double xOffset, double yOffset, double zOffset, int xSize, int ySize, int zSize, double xScale, double yScale, double zScale, double noiseScale) {
            if (ySize == 1) {
                int l5 = 0; double d16 = 1.0D / noiseScale;
                for (int j2 = 0; j2 < xSize; ++j2) {
                    double d17 = xOffset + (double)j2 * xScale + this.xCoord;
                    int i6 = (int)d17; if (d17 < (double)i6) --i6;
                    int k2 = i6 & 255; d17 = d17 - (double)i6;
                    double d18 = d17 * d17 * d17 * (d17 * (d17 * 6.0D - 15.0D) + 10.0D);
                    for (int j6 = 0; j6 < zSize; ++j6) {
                        double d19 = zOffset + (double)j6 * zScale + this.zCoord;
                        int k6 = (int)d19; if (d19 < (double)k6) --k6;
                        int l6 = k6 & 255; d19 = d19 - (double)k6;
                        double d20 = d19 * d19 * d19 * (d19 * (d19 * 6.0D - 15.0D) + 10.0D);
                        int i5 = this.permutations[k2] + 0;
                        int j5 = this.permutations[i5] + l6;
                        int j  = this.permutations[k2 + 1] + 0;
                        int k5 = this.permutations[j] + l6;
                        double d14 = this.lerp(d18, this.grad2(this.permutations[j5], d17, d19), this.grad(this.permutations[k5], d17 - 1.0D, 0.0D, d19));
                        double d15 = this.lerp(d18, this.grad(this.permutations[j5 + 1], d17, 0.0D, d19 - 1.0D), this.grad(this.permutations[k5 + 1], d17 - 1.0D, 0.0D, d19 - 1.0D));
                        double d21 = this.lerp(d20, d14, d15);
                        noiseArray[l5++] += d21 * d16;
                    }
                }
            } else {
                int i = 0; double d0 = 1.0D / noiseScale; int k = -1;
                int l = 0, i1 = 0, j1 = 0, k1 = 0, l1 = 0, i2 = 0;
                double d1 = 0, d2 = 0, d3 = 0, d4 = 0;
                for (int l2 = 0; l2 < xSize; ++l2) {
                    double d5 = xOffset + (double)l2 * xScale + this.xCoord;
                    int i3 = (int)d5; if (d5 < (double)i3) --i3;
                    int j3 = i3 & 255; d5 = d5 - (double)i3;
                    double d6 = d5 * d5 * d5 * (d5 * (d5 * 6.0D - 15.0D) + 10.0D);
                    for (int k3 = 0; k3 < zSize; ++k3) {
                        double d7 = zOffset + (double)k3 * zScale + this.zCoord;
                        int l3 = (int)d7; if (d7 < (double)l3) --l3;
                        int i4 = l3 & 255; d7 = d7 - (double)l3;
                        double d8 = d7 * d7 * d7 * (d7 * (d7 * 6.0D - 15.0D) + 10.0D);
                        for (int j4 = 0; j4 < ySize; ++j4) {
                            double d9 = yOffset + (double)j4 * yScale + this.yCoord;
                            int k4 = (int)d9; if (d9 < (double)k4) --k4;
                            int l4 = k4 & 255; d9 = d9 - (double)k4;
                            double d10 = d9 * d9 * d9 * (d9 * (d9 * 6.0D - 15.0D) + 10.0D);
                            if (j4 == 0 || l4 != k) {
                                k = l4;
                                l = this.permutations[j3] + l4;
                                i1 = this.permutations[l] + i4;
                                j1 = this.permutations[l + 1] + i4;
                                k1 = this.permutations[j3 + 1] + l4;
                                l1 = this.permutations[k1] + i4;
                                i2 = this.permutations[k1 + 1] + i4;
                                d1 = this.lerp(d6, this.grad(this.permutations[i1], d5, d9, d7), this.grad(this.permutations[l1], d5 - 1.0D, d9, d7));
                                d2 = this.lerp(d6, this.grad(this.permutations[j1], d5, d9 - 1.0D, d7), this.grad(this.permutations[i2], d5 - 1.0D, d9 - 1.0D, d7));
                                d3 = this.lerp(d6, this.grad(this.permutations[i1 + 1], d5, d9, d7 - 1.0D), this.grad(this.permutations[l1 + 1], d5 - 1.0D, d9, d7 - 1.0D));
                                d4 = this.lerp(d6, this.grad(this.permutations[j1 + 1], d5, d9 - 1.0D, d7 - 1.0D), this.grad(this.permutations[i2 + 1], d5 - 1.0D, d9 - 1.0D, d7 - 1.0D));
                            }
                            double d11 = this.lerp(d10, d1, d2);
                            double d12 = this.lerp(d10, d3, d4);
                            double d13 = this.lerp(d8, d11, d12);
                            noiseArray[i++] += d13 * d0;
                        }
                    }
                }
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
        public double[] generateNoiseOctaves(double[] noiseArray, int xOffset, int yOffset, int zOffset, int xSize, int ySize, int zSize, double xScale, double yScale, double zScale) {
            if (noiseArray == null) noiseArray = new double[xSize * ySize * zSize];
            else for (int i = 0; i < noiseArray.length; ++i) noiseArray[i] = 0.0D;
            double d3 = 1.0D;
            for (int j = 0; j < this.octaves; ++j) {
                double d0 = (double)xOffset * d3 * xScale;
                double d1 = (double)yOffset * d3 * yScale;
                double d2 = (double)zOffset * d3 * zScale;
                long k = lfloor(d0);
                long l = lfloor(d2);
                d0 = d0 - (double)k;
                d2 = d2 - (double)l;
                k = k % 16777216L;
                l = l % 16777216L;
                d0 = d0 + (double)k;
                d2 = d2 + (double)l;
                this.generatorCollection[j].populateNoiseArray(noiseArray, d0, d1, d2, xSize, ySize, zSize, xScale * d3, yScale * d3, zScale * d3, d3);
                d3 /= 2.0D;
            }
            return noiseArray;
        }
    }

    static class NoiseGeneratorSimplex {
        private static final int[][] grad3 = new int[][] {{1, 1, 0}, { -1, 1, 0}, {1, -1, 0}, { -1, -1, 0}, {1, 0, 1}, { -1, 0, 1}, {1, 0, -1}, { -1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
        public static final double SQRT_3 = Math.sqrt(3.0D);
        private final int[] p;
        public double xo, yo, zo;
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
        private static int fastFloor(double value) { return value > 0.0D ? (int)value : (int)value - 1; }
        private static double dot(int[] g, double x, double y) { return (double)g[0] * x + (double)g[1] * y; }
        public double getValue(double p_151605_1_, double p_151605_3_) {
            double d3 = 0.5D * (SQRT_3 - 1.0D);
            double d4 = (p_151605_1_ + p_151605_3_) * d3;
            int i = fastFloor(p_151605_1_ + d4);
            int j = fastFloor(p_151605_3_ + d4);
            double d5 = (3.0D - SQRT_3) / 6.0D;
            double d6 = (double)(i + j) * d5;
            double d7 = (double)i - d6;
            double d8 = (double)j - d6;
            double d9 = p_151605_1_ - d7;
            double d10 = p_151605_3_ - d8;
            int k, l;
            if (d9 > d10) { k = 1; l = 0; } else { k = 0; l = 1; }
            double d11 = d9 - (double)k + d5;
            double d12 = d10 - (double)l + d5;
            double d13 = d9 - 1.0D + 2.0D * d5;
            double d14 = d10 - 1.0D + 2.0D * d5;
            int i1 = i & 255;
            int j1 = j & 255;
            int k1 = this.p[i1 + this.p[j1]] % 12;
            int l1 = this.p[i1 + k + this.p[j1 + l]] % 12;
            int i2 = this.p[i1 + 1 + this.p[j1 + 1]] % 12;
            double d15 = 0.5D - d9 * d9 - d10 * d10;
            double d0;
            if (d15 < 0.0D) { d0 = 0.0D; } else { d15 = d15 * d15; d0 = d15 * d15 * dot(grad3[k1], d9, d10); }
            double d16 = 0.5D - d11 * d11 - d12 * d12;
            double d1;
            if (d16 < 0.0D) { d1 = 0.0D; } else { d16 = d16 * d16; d1 = d16 * d16 * dot(grad3[l1], d11, d12); }
            double d17 = 0.5D - d13 * d13 - d14 * d14;
            double d2;
            if (d17 < 0.0D) { d2 = 0.0D; } else { d17 = d17 * d17; d2 = d17 * d17 * dot(grad3[i2], d13, d14); }
            return 70.0D * (d0 + d1 + d2);
        }
    }

    static class ChunkPrimer {
        final char[] data = new char[65536];
        int getBlockState(int x, int y, int z) { return this.data[getBlockIndex(x, y, z)]; }
        void setBlockState(int x, int y, int z, int state) { this.data[getBlockIndex(x, y, z)] = (char)state; }
        static int getBlockIndex(int x, int y, int z) { return x << 12 | z << 8 | y; }
    }

    static NoiseGeneratorOctaves lperlinNoise1, lperlinNoise2, perlinNoise1, noiseGen5, noiseGen6;
    static NoiseGeneratorSimplex islandNoise;
    static double[] buffer, pnr, ar, br;
    static ChunkPrimer primer;

    static float getIslandHeightValue(int p_185960_1_, int p_185960_2_, int p_185960_3_, int p_185960_4_) {
        float f = (float)(p_185960_1_ * 2 + p_185960_3_);
        float f1 = (float)(p_185960_2_ * 2 + p_185960_4_);
        float f2 = 100.0F - sqrtf(f * f + f1 * f1) * 8.0F;
        if (f2 > 80.0F) f2 = 80.0F;
        if (f2 < -100.0F) f2 = -100.0F;
        for (int i = -12; i <= 12; ++i) {
            for (int j = -12; j <= 12; ++j) {
                long k = (long)(p_185960_1_ + i);
                long l = (long)(p_185960_2_ + j);
                if (k * k + l * l > 4096L && islandNoise.getValue((double)k, (double)l) < -0.8999999761581421D) {
                    float f3 = (absf((float)k) * 3439.0F + absf((float)l) * 147.0F) % 13.0F + 9.0F;
                    f = (float)(p_185960_3_ - i * 2);
                    f1 = (float)(p_185960_4_ - j * 2);
                    float f4 = 100.0F - sqrtf(f * f + f1 * f1) * f3;
                    if (f4 > 80.0F) f4 = 80.0F;
                    if (f4 < -100.0F) f4 = -100.0F;
                    if (f4 > f2) f2 = f4;
                }
            }
        }
        return f2;
    }

    static void getHeights(int p_185963_2_, int p_185963_3_, int p_185963_4_, int p_185963_5_, int p_185963_6_, int p_185963_7_) {
        double d0 = 684.412D;
        d0 = d0 * 2.0D;
        pnr = perlinNoise1.generateNoiseOctaves(pnr, p_185963_2_, p_185963_3_, p_185963_4_, p_185963_5_, p_185963_6_, p_185963_7_, d0 / 80.0D, 4.277575000000001D, d0 / 80.0D);
        ar = lperlinNoise1.generateNoiseOctaves(ar, p_185963_2_, p_185963_3_, p_185963_4_, p_185963_5_, p_185963_6_, p_185963_7_, d0, 684.412D, d0);
        br = lperlinNoise2.generateNoiseOctaves(br, p_185963_2_, p_185963_3_, p_185963_4_, p_185963_5_, p_185963_6_, p_185963_7_, d0, 684.412D, d0);
        int i = p_185963_2_ / 2;
        int j = p_185963_4_ / 2;
        int k = 0;
        for (int l = 0; l < p_185963_5_; ++l) {
            for (int i1 = 0; i1 < p_185963_7_; ++i1) {
                float f = getIslandHeightValue(i, j, l, i1);
                for (int j1 = 0; j1 < p_185963_6_; ++j1) {
                    double d2 = ar[k] / 512.0D;
                    double d3 = br[k] / 512.0D;
                    double d5 = (pnr[k] / 10.0D + 1.0D) / 2.0D;
                    double d4;
                    if (d5 < 0.0D) d4 = d2;
                    else if (d5 > 1.0D) d4 = d3;
                    else d4 = d2 + (d3 - d2) * d5;
                    d4 = d4 - 8.0D;
                    d4 = d4 + (double)f;
                    int k1 = 2;
                    if (j1 > p_185963_6_ / 2 - k1) {
                        double d6 = (double)((float)(j1 - (p_185963_6_ / 2 - k1)) / 64.0F);
                        d6 = clamp(d6, 0.0D, 1.0D);
                        d4 = d4 * (1.0D - d6) + -3000.0D * d6;
                    }
                    k1 = 8;
                    if (j1 < k1) {
                        double d7 = (double)((float)(k1 - j1) / ((float)k1 - 1.0F));
                        d4 = d4 * (1.0D - d7) + -30.0D * d7;
                    }
                    buffer[k] = d4;
                    ++k;
                }
            }
        }
    }

    static void setBlocksInChunk(int x, int z, ChunkPrimer primerIn) {
        getHeights(x * 2, 0, z * 2, 3, 33, 3);
        for (int i1 = 0; i1 < 2; ++i1) {
            for (int j1 = 0; j1 < 2; ++j1) {
                for (int k1 = 0; k1 < 32; ++k1) {
                    double d1 = buffer[((i1 + 0) * 3 + j1 + 0) * 33 + k1 + 0];
                    double d2 = buffer[((i1 + 0) * 3 + j1 + 1) * 33 + k1 + 0];
                    double d3 = buffer[((i1 + 1) * 3 + j1 + 0) * 33 + k1 + 0];
                    double d4 = buffer[((i1 + 1) * 3 + j1 + 1) * 33 + k1 + 0];
                    double d5 = (buffer[((i1 + 0) * 3 + j1 + 0) * 33 + k1 + 1] - d1) * 0.25D;
                    double d6 = (buffer[((i1 + 0) * 3 + j1 + 1) * 33 + k1 + 1] - d2) * 0.25D;
                    double d7 = (buffer[((i1 + 1) * 3 + j1 + 0) * 33 + k1 + 1] - d3) * 0.25D;
                    double d8 = (buffer[((i1 + 1) * 3 + j1 + 1) * 33 + k1 + 1] - d4) * 0.25D;
                    for (int l1 = 0; l1 < 4; ++l1) {
                        double d10 = d1;
                        double d11 = d2;
                        double d12 = (d3 - d1) * 0.125D;
                        double d13 = (d4 - d2) * 0.125D;
                        for (int i2 = 0; i2 < 8; ++i2) {
                            double d15 = d10;
                            double d16 = (d11 - d10) * 0.125D;
                            for (int j2 = 0; j2 < 8; ++j2) {
                                int iblockstate = AIR;
                                if (d15 > 0.0D) iblockstate = END_STONE;
                                int k2 = i2 + i1 * 8;
                                int l2 = l1 + k1 * 4;
                                int i3 = j2 + j1 * 8;
                                primerIn.setBlockState(k2, l2, i3, iblockstate);
                                d15 += d16;
                            }
                            d10 += d12;
                            d11 += d13;
                        }
                        d1 += d5; d2 += d6; d3 += d7; d4 += d8;
                    }
                }
            }
        }
    }

    static void buildSurfaces(ChunkPrimer primerIn) {
        for (int i = 0; i < 16; ++i) {
            for (int j = 0; j < 16; ++j) {
                int l = -1;
                int iblockstate = END_STONE;
                int iblockstate1 = END_STONE;
                for (int i1 = 127; i1 >= 0; --i1) {
                    int iblockstate2 = primerIn.getBlockState(i, i1, j);
                    if (iblockstate2 == AIR) {
                        l = -1;
                    } else if (iblockstate2 == STONE) {
                        if (l == -1) {
                            l = 1;
                            if (i1 >= 0) primerIn.setBlockState(i, i1, j, iblockstate);
                            else primerIn.setBlockState(i, i1, j, iblockstate1);
                        } else if (l > 0) {
                            --l;
                            primerIn.setBlockState(i, i1, j, iblockstate1);
                        }
                    }
                }
            }
        }
    }

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        int chunkX = 0, chunkZ = 0;

        buffer = new double[3 * 33 * 3];
        pnr = new double[3 * 33 * 3];
        ar = new double[3 * 33 * 3];
        br = new double[3 * 33 * 3];

        Random rand = new Random(seed);
        lperlinNoise1 = new NoiseGeneratorOctaves(rand, 16);
        lperlinNoise2 = new NoiseGeneratorOctaves(rand, 16);
        perlinNoise1 = new NoiseGeneratorOctaves(rand, 8);
        noiseGen5 = new NoiseGeneratorOctaves(rand, 10);
        noiseGen6 = new NoiseGeneratorOctaves(rand, 16);
        islandNoise = new NoiseGeneratorSimplex(rand);

        primer = new ChunkPrimer();
        setBlocksInChunk(chunkX, chunkZ, primer);
        buildSurfaces(primer);

        StringBuilder sb = new StringBuilder();
        for (int idx = 0; idx < 65536; ++idx) sb.append(String.format("%04x%n", (int)primer.data[idx]));
        System.out.print(sb);
    }
}
