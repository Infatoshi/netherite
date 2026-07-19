// Verbatim MC 1.11.2 NoiseGeneratorImproved + NoiseGeneratorOctaves (net/minecraft/world/gen),
// inlined with MathHelper.lfloor and a stdin->stdout driver. This is the vanilla ground truth
// for the noise port: real MC code, run standalone (eval-pure, render-opt style).
import java.util.Random;

public class Golden {
    static long lfloor(double value) { long i = (long)value; return value < (double)i ? i - 1L : i; }

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
    }

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        int xs = 5, ys = 33, zs = 5; double sc = 684.412D;
        Random r = new Random(seed);
        NoiseGeneratorOctaves o = new NoiseGeneratorOctaves(r, 16);
        double[] arr = o.generateNoiseOctaves(null, 0, 0, 0, xs, ys, zs, sc, sc, sc);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < xs * ys * zs; i++) sb.append(String.format("%016x%n", Double.doubleToRawLongBits(arr[i])));
        System.out.print(sb);
    }
}
