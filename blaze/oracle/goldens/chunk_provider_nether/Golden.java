// Verbatim MC 1.11.2 ChunkProviderHell.provideChunk (chunk 0,0) minus Chunk/biomes/populate.
// Includes prepareHeights, buildSurfaces, MapGenCavesHell. Fortress hook is no-op at (0,0) for test seeds
// (verified separately via map_gen_fortress; C integration calls ft_generate_* which also no-ops here).
import java.util.Random;

public class Golden {
    /* Vanilla Block.java: 10=flowing_lava, 11=lava still. Sea/oceans use Blocks.LAVA. */
    static final int AIR = 0, GRASS = 2, DIRT = 3, BEDROCK = 7, FLOWING_LAVA = 10, LAVA = 11, GRAVEL = 13;
    static final int NETHERRACK = 87, SOUL_SAND = 88;
    static final int SEA_LEVEL = 63;

    // --- Noise (verbatim net/minecraft/world/gen) ---
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
    static long lfloor(double value) { long i = (long)value; return value < (double)i ? i - 1L : i; }
    static double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

    private static final float[] SIN_TABLE = new float[65536];
    static { for (int i = 0; i < 65536; ++i) SIN_TABLE[i] = (float)Math.sin((double)i * Math.PI * 2.0D / 65536.0D); }
    static float mhsin(float value) { return SIN_TABLE[(int)(value * 10430.378F) & 65535]; }
    static float mhcos(float value) { return SIN_TABLE[(int)(value * 10430.378F + 16384.0F) & 65535]; }
    static int mhfloor(double value) { int i = (int)value; return value < (double)i ? i - 1 : i; }

    static class ChunkPrimer {
        final char[] data = new char[65536];
        int getBlockState(int x, int y, int z) { return this.data[x << 12 | z << 8 | y]; }
        void setBlockState(int x, int y, int z, int state) { this.data[x << 12 | z << 8 | y] = (char)state; }
    }

    static double[] buffer, noiseData4, dr, pnr, ar, br;
    static double[] slowsandNoise = new double[256];
    static double[] gravelNoise = new double[256];
    static double[] depthBuffer = new double[256];
    static NoiseGeneratorOctaves lperlinNoise1, lperlinNoise2, perlinNoise1;
    static NoiseGeneratorOctaves slowsandGravelNoiseGen, netherrackExculsivityNoiseGen, scaleNoise, depthNoise;
    static ChunkPrimer primer;
    static long worldSeed;
    static Random hellCaveRand;

    static double[] getHeights(double[] p_185938_1_, int p2, int p3, int p4, int p5, int p6, int p7) {
        if (p_185938_1_ == null) p_185938_1_ = new double[p5 * p6 * p7];
        noiseData4 = scaleNoise.generateNoiseOctaves(noiseData4, p2, p3, p4, p5, 1, p7, 1.0D, 0.0D, 1.0D);
        dr = depthNoise.generateNoiseOctaves(dr, p2, p3, p4, p5, 1, p7, 100.0D, 0.0D, 100.0D);
        pnr = perlinNoise1.generateNoiseOctaves(pnr, p2, p3, p4, p5, p6, p7, 8.555150000000001D, 34.2206D, 8.555150000000001D);
        ar = lperlinNoise1.generateNoiseOctaves(ar, p2, p3, p4, p5, p6, p7, 684.412D, 2053.236D, 684.412D);
        br = lperlinNoise2.generateNoiseOctaves(br, p2, p3, p4, p5, p6, p7, 684.412D, 2053.236D, 684.412D);
        int i = 0;
        double[] adouble = new double[p6];
        for (int j = 0; j < p6; ++j) {
            adouble[j] = Math.cos((double)j * Math.PI * 6.0D / (double)p6) * 2.0D;
            double d2 = (double)j;
            if (j > p6 / 2) d2 = (double)(p6 - 1 - j);
            if (d2 < 4.0D) { d2 = 4.0D - d2; adouble[j] -= d2 * d2 * d2 * 10.0D; }
        }
        for (int l = 0; l < p5; ++l) {
            for (int i1 = 0; i1 < p7; ++i1) {
                for (int k = 0; k < p6; ++k) {
                    double d4 = adouble[k];
                    double d5 = ar[i] / 512.0D;
                    double d6 = br[i] / 512.0D;
                    double d7 = (pnr[i] / 10.0D + 1.0D) / 2.0D;
                    double d8;
                    if (d7 < 0.0D) d8 = d5;
                    else if (d7 > 1.0D) d8 = d6;
                    else d8 = d5 + (d6 - d5) * d7;
                    d8 = d8 - d4;
                    if (k > p6 - 4) {
                        double d9 = (double)((float)(k - (p6 - 4)) / 3.0F);
                        d8 = d8 * (1.0D - d9) + -10.0D * d9;
                    }
                    if ((double)k < 0.0D) {
                        double d10 = (0.0D - (double)k) / 4.0D;
                        d10 = clamp(d10, 0.0D, 1.0D);
                        d8 = d8 * (1.0D - d10) + -10.0D * d10;
                    }
                    p_185938_1_[i] = d8;
                    ++i;
                }
            }
        }
        return p_185938_1_;
    }

    static void prepareHeights(int chunkX, int chunkZ) {
        int j = SEA_LEVEL / 2 + 1;
        buffer = getHeights(buffer, chunkX * 4, 0, chunkZ * 4, 5, 17, 5);
        for (int j1 = 0; j1 < 4; ++j1) {
            for (int k1 = 0; k1 < 4; ++k1) {
                for (int l1 = 0; l1 < 16; ++l1) {
                    double d1 = buffer[((j1 + 0) * 5 + k1 + 0) * 17 + l1 + 0];
                    double d2 = buffer[((j1 + 0) * 5 + k1 + 1) * 17 + l1 + 0];
                    double d3 = buffer[((j1 + 1) * 5 + k1 + 0) * 17 + l1 + 0];
                    double d4 = buffer[((j1 + 1) * 5 + k1 + 1) * 17 + l1 + 0];
                    double d5 = (buffer[((j1 + 0) * 5 + k1 + 0) * 17 + l1 + 1] - d1) * 0.125D;
                    double d6 = (buffer[((j1 + 0) * 5 + k1 + 1) * 17 + l1 + 1] - d2) * 0.125D;
                    double d7 = (buffer[((j1 + 1) * 5 + k1 + 0) * 17 + l1 + 1] - d3) * 0.125D;
                    double d8 = (buffer[((j1 + 1) * 5 + k1 + 1) * 17 + l1 + 1] - d4) * 0.125D;
                    for (int i2 = 0; i2 < 8; ++i2) {
                        double d10 = d1, d11 = d2;
                        double d12 = (d3 - d1) * 0.25D, d13 = (d4 - d2) * 0.25D;
                        for (int j2 = 0; j2 < 4; ++j2) {
                            double d16 = (d11 - d10) * 0.25D;
                            double d15 = d10 - d16;
                            for (int k2 = 0; k2 < 4; ++k2) {
                                int block = AIR;
                                if (l1 * 8 + i2 < j) block = LAVA;
                                if ((d15 += d16) > 0.0D) block = NETHERRACK;
                                primer.setBlockState(j2 + j1 * 4, i2 + l1 * 8, k2 + k1 * 4, block);
                            }
                            d10 += d12; d11 += d13;
                        }
                        d1 += d5; d2 += d6; d3 += d7; d4 += d8;
                    }
                }
            }
        }
    }

    static void buildSurfaces(int chunkX, int chunkZ, Random rand) {
        int i = SEA_LEVEL + 1;
        slowsandNoise = slowsandGravelNoiseGen.generateNoiseOctaves(slowsandNoise, chunkX * 16, chunkZ * 16, 0, 16, 16, 1, 0.03125D, 0.03125D, 1.0D);
        gravelNoise = slowsandGravelNoiseGen.generateNoiseOctaves(gravelNoise, chunkX * 16, 109, chunkZ * 16, 16, 1, 16, 0.03125D, 1.0D, 0.03125D);
        depthBuffer = netherrackExculsivityNoiseGen.generateNoiseOctaves(depthBuffer, chunkX * 16, chunkZ * 16, 0, 16, 16, 1, 0.0625D, 0.0625D, 0.0625D);
        for (int j = 0; j < 16; ++j) {
            for (int k = 0; k < 16; ++k) {
                boolean flag = slowsandNoise[j + k * 16] + rand.nextDouble() * 0.2D > 0.0D;
                boolean flag1 = gravelNoise[j + k * 16] + rand.nextDouble() * 0.2D > 0.0D;
                int l = (int)(depthBuffer[j + k * 16] / 3.0D + 3.0D + rand.nextDouble() * 0.25D);
                int i1 = -1;
                int iblockstate = NETHERRACK, iblockstate1 = NETHERRACK;
                for (int j1 = 127; j1 >= 0; --j1) {
                    if (j1 < 127 - rand.nextInt(5) && j1 > rand.nextInt(5)) {
                        if (primer.getBlockState(k, j1, j) != AIR) {
                            if (primer.getBlockState(k, j1, j) == NETHERRACK) {
                                if (i1 == -1) {
                                    if (l <= 0) { iblockstate = AIR; iblockstate1 = NETHERRACK; }
                                    else if (j1 >= i - 4 && j1 <= i + 1) {
                                        iblockstate = NETHERRACK; iblockstate1 = NETHERRACK;
                                        if (flag1) { iblockstate = GRAVEL; iblockstate1 = NETHERRACK; }
                                        if (flag) { iblockstate = SOUL_SAND; iblockstate1 = SOUL_SAND; }
                                    }
                                    if (j1 < i && iblockstate == AIR) iblockstate = LAVA;
                                    i1 = l;
                                    primer.setBlockState(k, j1, j, j1 >= i - 1 ? iblockstate : iblockstate1);
                                } else if (i1 > 0) {
                                    --i1;
                                    primer.setBlockState(k, j1, j, iblockstate1);
                                }
                            }
                        } else i1 = -1;
                    } else primer.setBlockState(k, j1, j, BEDROCK);
                }
            }
        }
    }

    // MapGenCavesHell (verbatim)
    static void hellAddTunnel(long p1, int p3, int p4, double p6, double p8, double p10, float p12, float p13, float p14, int p15, int p16, double p17) {
        double d0 = (double)(p3 * 16 + 8);
        double d1 = (double)(p4 * 16 + 8);
        float f = 0.0F, f1 = 0.0F;
        Random random = new Random(p1);
        if (p16 <= 0) { int i = 8 * 16 - 16; p16 = i - random.nextInt(i / 4); }
        boolean flag1 = false;
        if (p15 == -1) { p15 = p16 / 2; flag1 = true; }
        int jj = random.nextInt(p16 / 2) + p16 / 4;
        boolean flag = random.nextInt(6) == 0;
        for (; p15 < p16; ++p15) {
            double d2 = 1.5D + (double)(mhsin((float)p15 * (float)Math.PI / (float)p16) * p12);
            double d3 = d2 * p17;
            float f2 = mhcos(p14), f3 = mhsin(p14);
            p6 += (double)(mhcos(p13) * f2);
            p8 += (double)f3;
            p10 += (double)(mhsin(p13) * f2);
            if (flag) p14 = p14 * 0.92F; else p14 = p14 * 0.7F;
            p14 = p14 + f1 * 0.1F;
            p13 += f * 0.1F;
            f1 = f1 * 0.9F;
            f = f * 0.75F;
            f1 = f1 + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2.0F;
            f = f + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4.0F;
            if (!flag1 && p15 == jj && p12 > 1.0F) {
                hellAddTunnel(random.nextLong(), p3, p4, p6, p8, p10, random.nextFloat() * 0.5F + 0.5F, p13 - ((float)Math.PI / 2F), p14 / 3.0F, p15, p16, 1.0D);
                hellAddTunnel(random.nextLong(), p3, p4, p6, p8, p10, random.nextFloat() * 0.5F + 0.5F, p13 + ((float)Math.PI / 2F), p14 / 3.0F, p15, p16, 1.0D);
                return;
            }
            if (flag1 || random.nextInt(4) != 0) {
                double d4 = p6 - d0, d5 = p10 - d1;
                double d6 = (double)(p16 - p15);
                double d7 = (double)(p12 + 2.0F + 16.0F);
                if (d4 * d4 + d5 * d5 - d6 * d6 > d7 * d7) return;
                if (p6 >= d0 - 16.0D - d2 * 2.0D && p10 >= d1 - 16.0D - d2 * 2.0D && p6 <= d0 + 16.0D + d2 * 2.0D && p10 <= d1 + 16.0D + d2 * 2.0D) {
                    int k2 = mhfloor(p6 - d2) - p3 * 16 - 1;
                    int k = mhfloor(p6 + d2) - p3 * 16 + 1;
                    int l2 = mhfloor(p8 - d3) - 1;
                    int l = mhfloor(p8 + d3) + 1;
                    int i3 = mhfloor(p10 - d2) - p4 * 16 - 1;
                    int i1 = mhfloor(p10 + d2) - p4 * 16 + 1;
                    if (k2 < 0) k2 = 0; if (k > 16) k = 16;
                    if (l2 < 1) l2 = 1; if (l > 120) l = 120;
                    if (i3 < 0) i3 = 0; if (i1 > 16) i1 = 16;
                    boolean flag2 = false;
                    for (int j1 = k2; !flag2 && j1 < k; ++j1)
                        for (int k1 = i3; !flag2 && k1 < i1; ++k1)
                            for (int l1 = l + 1; !flag2 && l1 >= l2 - 1; --l1)
                                if (l1 >= 0 && l1 < 128) {
                                    int b = primer.getBlockState(j1, l1, k1);
                                    if (b == FLOWING_LAVA || b == LAVA) flag2 = true;
                                    if (l1 != l2 - 1 && j1 != k2 && j1 != k - 1 && k1 != i3 && k1 != i1 - 1) l1 = l2;
                                }
                    if (!flag2) {
                        for (int i3a = k2; i3a < k; ++i3a) {
                            double d10 = ((double)(i3a + p3 * 16) + 0.5D - p6) / d2;
                            for (int j3 = i3; j3 < i1; ++j3) {
                                double d8 = ((double)(j3 + p4 * 16) + 0.5D - p10) / d2;
                                for (int i2 = l; i2 > l2; --i2) {
                                    double d9 = ((double)(i2 - 1) + 0.5D - p8) / d3;
                                    if (d9 > -0.7D && d10 * d10 + d9 * d9 + d8 * d8 < 1.0D) {
                                        int s = primer.getBlockState(i3a, i2, j3);
                                        if (s == NETHERRACK || s == DIRT || s == GRASS) primer.setBlockState(i3a, i2, j3, AIR);
                                    }
                                }
                            }
                        }
                        if (flag1) break;
                    }
                }
            }
        }
    }

    static void hellRecursive(int chunkX, int chunkZ, int p4, int p5) {
        int i = hellCaveRand.nextInt(hellCaveRand.nextInt(hellCaveRand.nextInt(10) + 1) + 1);
        if (hellCaveRand.nextInt(5) != 0) i = 0;
        for (int j = 0; j < i; ++j) {
            double d0 = (double)(chunkX * 16 + hellCaveRand.nextInt(16));
            double d1 = (double)hellCaveRand.nextInt(128);
            double d2 = (double)(chunkZ * 16 + hellCaveRand.nextInt(16));
            int k = 1;
            if (hellCaveRand.nextInt(4) == 0) {
                hellAddTunnel(hellCaveRand.nextLong(), p4, p5, d0, d1, d2, 1.0F + hellCaveRand.nextFloat() * 6.0F, 0.0F, 0.0F, -1, -1, 0.5D);
                k += hellCaveRand.nextInt(4);
            }
            for (int l = 0; l < k; ++l) {
                float ff = hellCaveRand.nextFloat() * ((float)Math.PI * 2F);
                float f1 = (hellCaveRand.nextFloat() - 0.5F) * 2.0F / 8.0F;
                float f2 = hellCaveRand.nextFloat() * 2.0F + hellCaveRand.nextFloat();
                hellAddTunnel(hellCaveRand.nextLong(), p4, p5, d0, d1, d2, f2 * 2.0F, ff, f1, 0, 0, 0.5D);
            }
        }
    }

    static void hellCaveGenerate(int x, int z) {
        int range = 8;
        hellCaveRand.setSeed(worldSeed);
        long j = hellCaveRand.nextLong(), k = hellCaveRand.nextLong();
        for (int l = x - range; l <= x + range; ++l) {
            for (int i1 = z - range; i1 <= z + range; ++i1) {
                hellCaveRand.setSeed((long)l * j ^ (long)i1 * k ^ worldSeed);
                hellRecursive(l, i1, x, z);
            }
        }
    }

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        worldSeed = seed;
        int chunkX = 0, chunkZ = 0;
        Random ctorRand = new Random(seed);
        lperlinNoise1 = new NoiseGeneratorOctaves(ctorRand, 16);
        lperlinNoise2 = new NoiseGeneratorOctaves(ctorRand, 16);
        perlinNoise1 = new NoiseGeneratorOctaves(ctorRand, 8);
        slowsandGravelNoiseGen = new NoiseGeneratorOctaves(ctorRand, 4);
        netherrackExculsivityNoiseGen = new NoiseGeneratorOctaves(ctorRand, 4);
        scaleNoise = new NoiseGeneratorOctaves(ctorRand, 10);
        depthNoise = new NoiseGeneratorOctaves(ctorRand, 16);
        hellCaveRand = new Random();
        primer = new ChunkPrimer();
        Random thisRand = new Random();
        thisRand.setSeed((long)chunkX * 341873128712L + (long)chunkZ * 132897987541L);
        prepareHeights(chunkX, chunkZ);
        buildSurfaces(chunkX, chunkZ, thisRand);
        hellCaveGenerate(chunkX, chunkZ);
        StringBuilder sb = new StringBuilder();
        for (int idx = 0; idx < 65536; ++idx) sb.append(String.format("%04x%n", (int)primer.data[idx]));
        System.out.print(sb);
    }
}
