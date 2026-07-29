// Verbatim MC 1.11.2 MapGenRavine (net/minecraft/world/gen/MapGenRavine.java) driven by
// MapGenBase.generate (net/minecraft/world/gen/MapGenBase.java) + the MathHelper sin/cos/floor
// + SIN_TABLE static init it depends on (net/minecraft/util/math/MathHelper.java). Real MC code
// only - the vanilla ground truth (uses java.util.Random, so left-to-right RNG order is exact).
//
// The Block/Biome/World/ChunkPrimer object graph is replaced by the SAME documented integer model
// as core/ravines.h (sanctioned substitution, identical in golden and candidate):
//  - ChunkPrimer -> char[65536], index x<<12|z<<8|y (verbatim getBlockIndex). Cells hold the
//    substituted block-state id; the ravine logic only ever calls IBlockState.getBlock(), so
//    block id == cell value is exact.
//  - block ids: AIR=0 STONE=1 GRASS=2 DIRT=3 FLOWING_WATER=8 WATER=9 FLOWING_LAVA=10 (vanilla nums).
//  - biome: FIXED Plains (top=GRASS, filler=DIRT, not an exception biome).
//  - synthetic primer: STONE for y in [1,127], AIR elsewhere.
// Prints all 65536 primer cells as %04x in raw index order, matching cpu/ravines.c.
import java.util.Random;

public class Golden {
    static final int AIR = 0, STONE = 1, GRASS = 2, DIRT = 3;
    static final int FLOWING_WATER = 8, WATER = 9, FLOWING_LAVA = 10;
    static final int BIOME_PLAINS = 1, BIOME_DESERT = 2, BIOME_MUSHROOM = 14,
                     BIOME_MUSHROOM_SHORE = 15, BIOME_BEACH = 16;

    // --- verbatim from MathHelper ---
    private static final float[] SIN_TABLE = new float[65536];

    public static float sin(float value)
    {
        return SIN_TABLE[(int)(value * 10430.378F) & 65535];
    }

    public static float cos(float value)
    {
        return SIN_TABLE[(int)(value * 10430.378F + 16384.0F) & 65535];
    }

    public static int floor(double value)
    {
        int i = (int)value;
        return value < (double)i ? i - 1 : i;
    }

    static
    {
        for (int i = 0; i < 65536; ++i)
        {
            SIN_TABLE[i] = (float)Math.sin((double)i * Math.PI * 2.0D / 65536.0D);
        }
    }
    // --- /verbatim ---

    // ChunkPrimer-equivalent (char[65536]; verbatim getBlockIndex; block-id cells)
    static final char[] data = new char[65536];
    static int getBlockIndex(int x, int y, int z) { return x << 12 | z << 8 | y; }
    static int getBlockState(char[] d, int x, int y, int z) { return d[getBlockIndex(x, y, z)]; }
    static void setBlockState(char[] d, int x, int y, int z, int v) { d[getBlockIndex(x, y, z)] = (char)v; }

    // MapGenBase fields
    static int range = 8;
    static Random rand = new Random();
    static long worldSeed;
    // MapGenRavine field
    static final float[] rs = new float[1024];

    // verbatim MapGenRavine.addTunnel (World/Biome/Block -> integer model)
    protected static void addTunnel(long p_180707_1_, int p_180707_3_, int p_180707_4_, char[] p_180707_5_, double p_180707_6_, double p_180707_8_, double p_180707_10_, float p_180707_12_, float p_180707_13_, float p_180707_14_, int p_180707_15_, int p_180707_16_, double p_180707_17_)
    {
        Random random = new Random(p_180707_1_);
        double d0 = (double)(p_180707_3_ * 16 + 8);
        double d1 = (double)(p_180707_4_ * 16 + 8);
        float f = 0.0F;
        float f1 = 0.0F;

        if (p_180707_16_ <= 0)
        {
            int i = range * 16 - 16;
            p_180707_16_ = i - random.nextInt(i / 4);
        }

        boolean flag1 = false;

        if (p_180707_15_ == -1)
        {
            p_180707_15_ = p_180707_16_ / 2;
            flag1 = true;
        }

        float f2 = 1.0F;

        for (int j = 0; j < 256; ++j)
        {
            if (j == 0 || random.nextInt(3) == 0)
            {
                f2 = 1.0F + random.nextFloat() * random.nextFloat();
            }

            rs[j] = f2 * f2;
        }

        for (; p_180707_15_ < p_180707_16_; ++p_180707_15_)
        {
            double d9 = 1.5D + (double)(sin((float)p_180707_15_ * (float)Math.PI / (float)p_180707_16_) * p_180707_12_);
            double d2 = d9 * p_180707_17_;
            d9 = d9 * ((double)random.nextFloat() * 0.25D + 0.75D);
            d2 = d2 * ((double)random.nextFloat() * 0.25D + 0.75D);
            float f3 = cos(p_180707_14_);
            float f4 = sin(p_180707_14_);
            p_180707_6_ += (double)(cos(p_180707_13_) * f3);
            p_180707_8_ += (double)f4;
            p_180707_10_ += (double)(sin(p_180707_13_) * f3);
            p_180707_14_ = p_180707_14_ * 0.7F;
            p_180707_14_ = p_180707_14_ + f1 * 0.05F;
            p_180707_13_ += f * 0.05F;
            f1 = f1 * 0.8F;
            f = f * 0.5F;
            f1 = f1 + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2.0F;
            f = f + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4.0F;

            if (flag1 || random.nextInt(4) != 0)
            {
                double d3 = p_180707_6_ - d0;
                double d4 = p_180707_10_ - d1;
                double d5 = (double)(p_180707_16_ - p_180707_15_);
                double d6 = (double)(p_180707_12_ + 2.0F + 16.0F);

                if (d3 * d3 + d4 * d4 - d5 * d5 > d6 * d6)
                {
                    return;
                }

                if (p_180707_6_ >= d0 - 16.0D - d9 * 2.0D && p_180707_10_ >= d1 - 16.0D - d9 * 2.0D && p_180707_6_ <= d0 + 16.0D + d9 * 2.0D && p_180707_10_ <= d1 + 16.0D + d9 * 2.0D)
                {
                    int k2 = floor(p_180707_6_ - d9) - p_180707_3_ * 16 - 1;
                    int k = floor(p_180707_6_ + d9) - p_180707_3_ * 16 + 1;
                    int l2 = floor(p_180707_8_ - d2) - 1;
                    int l = floor(p_180707_8_ + d2) + 1;
                    int i3 = floor(p_180707_10_ - d9) - p_180707_4_ * 16 - 1;
                    int i1 = floor(p_180707_10_ + d9) - p_180707_4_ * 16 + 1;

                    if (k2 < 0)
                    {
                        k2 = 0;
                    }

                    if (k > 16)
                    {
                        k = 16;
                    }

                    if (l2 < 1)
                    {
                        l2 = 1;
                    }

                    if (l > 248)
                    {
                        l = 248;
                    }

                    if (i3 < 0)
                    {
                        i3 = 0;
                    }

                    if (i1 > 16)
                    {
                        i1 = 16;
                    }

                    boolean flag2 = false;

                    for (int j1 = k2; !flag2 && j1 < k; ++j1)
                    {
                        for (int k1 = i3; !flag2 && k1 < i1; ++k1)
                        {
                            for (int l1 = l + 1; !flag2 && l1 >= l2 - 1; --l1)
                            {
                                if (l1 >= 0 && l1 < 256)
                                {
                                    if (isOceanBlock(p_180707_5_, j1, l1, k1, p_180707_3_, p_180707_4_))
                                    {
                                        flag2 = true;
                                    }

                                    if (l1 != l2 - 1 && j1 != k2 && j1 != k - 1 && k1 != i3 && k1 != i1 - 1)
                                    {
                                        l1 = l2;
                                    }
                                }
                            }
                        }
                    }

                    if (!flag2)
                    {
                        for (int j3 = k2; j3 < k; ++j3)
                        {
                            double d10 = ((double)(j3 + p_180707_3_ * 16) + 0.5D - p_180707_6_) / d9;

                            for (int i2 = i3; i2 < i1; ++i2)
                            {
                                double d7 = ((double)(i2 + p_180707_4_ * 16) + 0.5D - p_180707_10_) / d9;
                                boolean flag = false;

                                if (d10 * d10 + d7 * d7 < 1.0D)
                                {
                                    for (int j2 = l; j2 > l2; --j2)
                                    {
                                        double d8 = ((double)(j2 - 1) + 0.5D - p_180707_8_) / d2;

                                        if ((d10 * d10 + d7 * d7) * (double)rs[j2 - 1] + d8 * d8 / 6.0D < 1.0D)
                                        {
                                            if (isTopBlock(p_180707_5_, j3, j2, i2, p_180707_3_, p_180707_4_))
                                            {
                                                flag = true;
                                            }

                                            digBlock(p_180707_5_, j3, j2, i2, p_180707_3_, p_180707_4_, flag);
                                        }
                                    }
                                }
                            }
                        }

                        if (flag1)
                        {
                            break;
                        }
                    }
                }
            }
        }
    }

    // verbatim MapGenRavine.recursiveGenerate
    protected static void recursiveGenerate(int chunkX, int chunkZ, int p_180701_4_, int p_180701_5_, char[] chunkPrimerIn)
    {
        if (rand.nextInt(50) == 0)
        {
            double d0 = (double)(chunkX * 16 + rand.nextInt(16));
            double d1 = (double)(rand.nextInt(rand.nextInt(40) + 8) + 20);
            double d2 = (double)(chunkZ * 16 + rand.nextInt(16));
            int i = 1;

            for (int j = 0; j < 1; ++j)
            {
                float f = rand.nextFloat() * ((float)Math.PI * 2F);
                float f1 = (rand.nextFloat() - 0.5F) * 2.0F / 8.0F;
                float f2 = (rand.nextFloat() * 2.0F + rand.nextFloat()) * 2.0F;
                addTunnel(rand.nextLong(), p_180701_4_, p_180701_5_, chunkPrimerIn, d0, d1, d2, f2, f, f1, 0, 0, 3.0D);
            }
        }
    }

    protected static boolean isOceanBlock(char[] data, int x, int y, int z, int chunkX, int chunkZ)
    {
        int block = getBlockState(data, x, y, z);
        return block == FLOWING_WATER || block == WATER;
    }

    // Exception biomes (Plains -> false)
    private static boolean isExceptionBiome(int biome)
    {
        if (biome == BIOME_BEACH) return true;
        if (biome == BIOME_DESERT) return true;
        if (biome == BIOME_MUSHROOM) return true;
        if (biome == BIOME_MUSHROOM_SHORE) return true;
        return false;
    }

    // verbatim isTopBlock (world.getBiome -> fixed Plains; biome.topBlock == GRASS)
    private static boolean isTopBlock(char[] data, int x, int y, int z, int chunkX, int chunkZ)
    {
        int biome = BIOME_PLAINS;
        int state = getBlockState(data, x, y, z);
        return (isExceptionBiome(biome) ? state == GRASS : state == GRASS);
    }

    // verbatim digBlock (Plains top=GRASS, filler=DIRT)
    protected static void digBlock(char[] data, int x, int y, int z, int chunkX, int chunkZ, boolean foundTop)
    {
        int biome = BIOME_PLAINS;
        int state = getBlockState(data, x, y, z);
        int top = isExceptionBiome(biome) ? GRASS : GRASS;
        int filler = isExceptionBiome(biome) ? DIRT : DIRT;

        if (state == STONE || state == top || state == filler)
        {
            if (y - 1 < 10)
            {
                setBlockState(data, x, y, z, FLOWING_LAVA);
            }
            else
            {
                setBlockState(data, x, y, z, AIR);

                if (foundTop && getBlockState(data, x, y - 1, z) == filler)
                {
                    setBlockState(data, x, y - 1, z, top);
                }
            }
        }
    }

    // verbatim MapGenBase.generate (world.getSeed() -> worldSeed)
    public static void generate(int x, int z, char[] primer)
    {
        int i = range;
        rand.setSeed(worldSeed);
        long j = rand.nextLong();
        long k = rand.nextLong();

        for (int l = x - i; l <= x + i; ++l)
        {
            for (int i1 = z - i; i1 <= z + i; ++i1)
            {
                long j1 = (long)l * j;
                long k1 = (long)i1 * k;
                rand.setSeed(j1 ^ k1 ^ worldSeed);
                recursiveGenerate(l, i1, x, z, primer);
            }
        }
    }

    public static void main(String[] args) {
        worldSeed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 256; ++y)
                    data[getBlockIndex(x, y, z)] = (char)((y >= 1 && y <= 127) ? STONE : AIR);

        generate(0, 0, data);

        StringBuilder sb = new StringBuilder();
        for (int idx = 0; idx < 65536; ++idx)
            sb.append(String.format("%04x%n", (int)data[idx]));
        System.out.print(sb);
    }
}
