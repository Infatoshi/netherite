// Verbatim MC 1.11.2 ChunkProviderOverworld.generateHeightmap (net/minecraft/world/gen), with the
// verbatim noise stack it depends on: NoiseGeneratorImproved, NoiseGeneratorOctaves (both overloads),
// NoiseGeneratorPerlin + NoiseGeneratorSimplex (only their RNG-consuming constructors matter here),
// and the MathHelper helpers (lfloor, sqrt, clampedLerp). This is the vanilla ground truth, run
// standalone (eval-pure, render-opt style). Real MC code only - never a hand-port of the candidate.
//
// The only substitution is a deterministic HARNESS INPUT, not a numeric change: generateHeightmap
// reads each neighbor column's biome base-height/height-variation from biomesForGeneration; we bake
// every cell to vanilla Plains (baseHeight 0.125f, heightVariation 0.05f; Biome.java:544). Identical
// values are baked into the C/CUDA candidate. The noise math + blend formula are byte-for-byte vanilla.
//
// Settings are the ChunkProviderSettings.Factory defaults (no JSON override). Output: the 825 doubles
// of heightMap (5x33x5) as raw bits hex, one per line, matching cpu/terrain_shape.c.
import java.util.Random;

public class Golden {
    static long lfloor(double value) { long i = (long)value; return value < (double)i ? i - 1L : i; }
    static float sqrt(float value) { return (float)Math.sqrt((double)value); }
    static double clampedLerp(double lowerBnd, double upperBnd, double slide) {
        return slide < 0.0D ? lowerBnd : (slide > 1.0D ? upperBnd : lowerBnd + (upperBnd - lowerBnd) * slide);
    }

    static abstract class NoiseGenerator {}

    static class NoiseGeneratorImproved extends NoiseGenerator {
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

    static class NoiseGeneratorOctaves extends NoiseGenerator {
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
        public double[] generateNoiseOctaves(double[] noiseArray, int xOffset, int zOffset, int xSize, int zSize, double xScale, double zScale, double p_76305_10_) {
            return this.generateNoiseOctaves(noiseArray, xOffset, 10, zOffset, xSize, 1, zSize, xScale, 1.0D, zScale);
        }
    }

    // Verbatim NoiseGeneratorSimplex: only the RNG-consuming constructor is exercised here, but the
    // full class is included so the draw pattern is provably identical to the real game.
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
    }

    static class NoiseGeneratorPerlin extends NoiseGenerator {
        private final NoiseGeneratorSimplex[] noiseLevels;
        private final int levels;
        public NoiseGeneratorPerlin(Random p_i45470_1_, int p_i45470_2_) {
            this.levels = p_i45470_2_;
            this.noiseLevels = new NoiseGeneratorSimplex[p_i45470_2_];
            for (int i = 0; i < p_i45470_2_; ++i) {
                this.noiseLevels[i] = new NoiseGeneratorSimplex(p_i45470_1_);
            }
        }
    }

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        int chunkX = 0, chunkZ = 0;

        // ChunkProviderOverworld constructor: noise generators drawn in this exact order from rand.
        Random rand = new Random(seed);
        NoiseGeneratorOctaves minLimitPerlinNoise = new NoiseGeneratorOctaves(rand, 16);
        NoiseGeneratorOctaves maxLimitPerlinNoise = new NoiseGeneratorOctaves(rand, 16);
        NoiseGeneratorOctaves mainPerlinNoise = new NoiseGeneratorOctaves(rand, 8);
        NoiseGeneratorPerlin surfaceNoise = new NoiseGeneratorPerlin(rand, 4);
        NoiseGeneratorOctaves scaleNoise = new NoiseGeneratorOctaves(rand, 10);
        NoiseGeneratorOctaves depthNoise = new NoiseGeneratorOctaves(rand, 16);
        // forestNoise = new NoiseGeneratorOctaves(rand, 8); // created after depthNoise; not needed

        double[] heightMap = new double[825];
        float[] biomeWeights = new float[25];
        for (int i = -2; i <= 2; ++i) {
            for (int j = -2; j <= 2; ++j) {
                float f = 10.0F / sqrt((float)(i * i + j * j) + 0.2F);
                biomeWeights[i + 2 + (j + 2) * 5] = f;
            }
        }

        // ChunkProviderSettings.Factory defaults.
        float coordinateScale = 684.412F;
        float heightScale = 684.412F;
        float upperLimitScale = 512.0F;
        float lowerLimitScale = 512.0F;
        float depthNoiseScaleX = 200.0F;
        float depthNoiseScaleZ = 200.0F;
        float depthNoiseScaleExponent = 0.5F;
        float mainNoiseScaleX = 80.0F;
        float mainNoiseScaleY = 160.0F;
        float mainNoiseScaleZ = 80.0F;
        float baseSize = 8.5F;
        float stretchY = 12.0F;
        float biomeDepthWeight = 1.0F;
        float biomeDepthOffSet = 0.0F;
        float biomeScaleWeight = 1.0F;
        float biomeScaleOffset = 0.0F;
        // fixed Plains biome (harness substitution).
        float plainsBaseHeight = 0.125F;
        float plainsHeightVariation = 0.05F;

        // generateHeightmap(chunkX*4, 0, chunkZ*4)  (called from setBlocksInChunk).
        int p_185978_1_ = chunkX * 4;
        int p_185978_2_ = 0;
        int p_185978_3_ = chunkZ * 4;

        double[] depthRegion = depthNoise.generateNoiseOctaves((double[])null, p_185978_1_, p_185978_3_, 5, 5, (double)depthNoiseScaleX, (double)depthNoiseScaleZ, (double)depthNoiseScaleExponent);
        float f = coordinateScale;
        float f1 = heightScale;
        double[] mainNoiseRegion = mainPerlinNoise.generateNoiseOctaves((double[])null, p_185978_1_, p_185978_2_, p_185978_3_, 5, 33, 5, (double)(f / mainNoiseScaleX), (double)(f1 / mainNoiseScaleY), (double)(f / mainNoiseScaleZ));
        double[] minLimitRegion = minLimitPerlinNoise.generateNoiseOctaves((double[])null, p_185978_1_, p_185978_2_, p_185978_3_, 5, 33, 5, (double)f, (double)f1, (double)f);
        double[] maxLimitRegion = maxLimitPerlinNoise.generateNoiseOctaves((double[])null, p_185978_1_, p_185978_2_, p_185978_3_, 5, 33, 5, (double)f, (double)f1, (double)f);
        int i = 0;
        int j = 0;

        for (int k = 0; k < 5; ++k) {
            for (int l = 0; l < 5; ++l) {
                float f2 = 0.0F;
                float f3 = 0.0F;
                float f4 = 0.0F;
                // Biome biome = Plains (center column)

                for (int j1 = -2; j1 <= 2; ++j1) {
                    for (int k1 = -2; k1 <= 2; ++k1) {
                        // Biome biome1 = Plains for every neighbor cell
                        float f5 = biomeDepthOffSet + plainsBaseHeight * biomeDepthWeight;
                        float f6 = biomeScaleOffset + plainsHeightVariation * biomeScaleWeight;
                        // terrainType != AMPLIFIED
                        float f7 = biomeWeights[j1 + 2 + (k1 + 2) * 5] / (f5 + 2.0F);
                        // biome1.getBaseHeight() > biome.getBaseHeight(): 0.125 > 0.125 is false
                        f2 += f6 * f7;
                        f3 += f5 * f7;
                        f4 += f7;
                    }
                }

                f2 = f2 / f4;
                f3 = f3 / f4;
                f2 = f2 * 0.9F + 0.1F;
                f3 = (f3 * 4.0F - 1.0F) / 8.0F;
                double d7 = depthRegion[j] / 8000.0D;

                if (d7 < 0.0D) {
                    d7 = -d7 * 0.3D;
                }

                d7 = d7 * 3.0D - 2.0D;

                if (d7 < 0.0D) {
                    d7 = d7 / 2.0D;

                    if (d7 < -1.0D) {
                        d7 = -1.0D;
                    }

                    d7 = d7 / 1.4D;
                    d7 = d7 / 2.0D;
                } else {
                    if (d7 > 1.0D) {
                        d7 = 1.0D;
                    }

                    d7 = d7 / 8.0D;
                }

                ++j;
                double d8 = (double)f3;
                double d9 = (double)f2;
                d8 = d8 + d7 * 0.2D;
                d8 = d8 * (double)baseSize / 8.0D;
                double d0 = (double)baseSize + d8 * 4.0D;

                for (int l1 = 0; l1 < 33; ++l1) {
                    double d1 = ((double)l1 - d0) * (double)stretchY * 128.0D / 256.0D / d9;

                    if (d1 < 0.0D) {
                        d1 *= 4.0D;
                    }

                    double d2 = minLimitRegion[i] / (double)lowerLimitScale;
                    double d3 = maxLimitRegion[i] / (double)upperLimitScale;
                    double d4 = (mainNoiseRegion[i] / 10.0D + 1.0D) / 2.0D;
                    double d5 = clampedLerp(d2, d3, d4) - d1;

                    if (l1 > 29) {
                        double d6 = (double)((float)(l1 - 29) / 3.0F);
                        d5 = d5 * (1.0D - d6) + -10.0D * d6;
                    }

                    heightMap[i] = d5;
                    ++i;
                }
            }
        }

        StringBuilder sb = new StringBuilder();
        for (int x = 0; x < 825; x++) sb.append(String.format("%016x%n", Double.doubleToRawLongBits(heightMap[x])));
        System.out.print(sb);
    }
}
