// Verbatim MC 1.11.2 cave carving (vanilla ground truth, eval-pure):
//   net/minecraft/world/gen/MapGenBase.java  : generate() driver loop
//   net/minecraft/world/gen/MapGenCaves.java : recursiveGenerate/addRoom/addTunnel + helpers
// plus the MathHelper sin/cos/floor + SIN_TABLE static init it depends on
// (net/minecraft/util/math/MathHelper.java). Real MC code only - the vanilla ground truth.
//
// Sanctioned substitutions (identical in core/caves.h), as in ore_gen / genlayer_biomes goldens:
//   * net.minecraft.world.chunk.ChunkPrimer -> the int[65536] `data` below, getBlockState /
//     setBlockState / getBlockIndex verbatim, storing a block-state id (here = block id, meta 0).
//   * The World/Biome registry -> a FIXED Plains biome: world.getBiome(...) == Plains everywhere,
//     so topBlock=GRASS, fillerBlock=DIRT, isExceptionBiome(Plains)==false (BiomePlains inherits
//     Biome.topBlock=Blocks.GRASS / fillerBlock=Blocks.DIRT; the carving exception biomes are only
//     BEACH and DESERT). world.getSeed() -> the worldSeed field.
//   * IBlockState/Block identity comparisons -> integer block-id comparisons.
// The carving ALGORITHM, RNG draw order, and float MathHelper trig are the decompiled MC unchanged.
//
// Synthetic primer: every cell = STONE. Output: the full 65536-cell ChunkPrimer in raw array
// (index = x<<12 | z<<8 | y) order, each cell as %04x, one per line. Matches cpu/caves.c.
public class Golden {

    // ===== block-state id substitution (vanilla numeric block ids; meta always 0 here) =====
    static final int AIR = 0, STONE = 1, GRASS = 2, DIRT = 3,
        FLOWING_WATER = 8, WATER = 9, FLOWING_LAVA = 10, LAVA = 11,
        SAND = 12, GRAVEL = 13, SANDSTONE = 24, SNOW_LAYER = 78,
        MYCELIUM = 110, STAINED_HARDENED_CLAY = 159, HARDENED_CLAY = 172, RED_SANDSTONE = 179;

    // Fixed Plains biome facts.
    static final int PLAINS_TOP = GRASS;
    static final int PLAINS_FILLER = DIRT;

    // MapGenCaves does not override MapGenBase.range (default 8).
    static final int range = 8;

    static long worldSeed;
    static final java.util.Random rand = new java.util.Random();

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

    // ===== ChunkPrimer substitute (verbatim get/set/index, int-id storage) =====
    static final int[] data = new int[65536];

    static int getBlockIndex(int x, int y, int z)
    {
        return x << 12 | z << 8 | y;
    }

    static int getBlockState(int x, int y, int z)
    {
        return data[getBlockIndex(x, y, z)];
    }

    static void setBlockState(int x, int y, int z, int state)
    {
        data[getBlockIndex(x, y, z)] = state;
    }

    // ===== MapGenCaves helpers (verbatim, Plains biome baked) =====
    static boolean canReplaceBlock(int p_175793_1_, int p_175793_2_)
    {
        return p_175793_1_ == STONE ? true : (p_175793_1_ == DIRT ? true : (p_175793_1_ == GRASS ? true : (p_175793_1_ == HARDENED_CLAY ? true : (p_175793_1_ == STAINED_HARDENED_CLAY ? true : (p_175793_1_ == SANDSTONE ? true : (p_175793_1_ == RED_SANDSTONE ? true : (p_175793_1_ == MYCELIUM ? true : (p_175793_1_ == SNOW_LAYER ? true : (p_175793_1_ == SAND || p_175793_1_ == GRAVEL) && p_175793_2_ != WATER && p_175793_2_ != FLOWING_WATER))))))));
    }

    static boolean isOceanBlock(int x, int y, int z, int chunkX, int chunkZ)
    {
        int block = getBlockState(x, y, z);
        return block == FLOWING_WATER || block == WATER;
    }

    // isExceptionBiome(Plains) == false (exceptions are only BEACH and DESERT).
    static boolean isTopBlock(int x, int y, int z, int chunkX, int chunkZ)
    {
        // biome = Plains (non-exception) -> state.getBlock() == biome.topBlock (GRASS)
        int state = getBlockState(x, y, z);
        return state == PLAINS_TOP;
    }

    static void digBlock(int x, int y, int z, int chunkX, int chunkZ, boolean foundTop, int state, int up)
    {
        // biome = Plains -> top = GRASS, filler = DIRT
        int top = PLAINS_TOP;
        int filler = PLAINS_FILLER;

        if (canReplaceBlock(state, up) || state == top || state == filler)
        {
            if (y - 1 < 10)
            {
                setBlockState(x, y, z, LAVA);
            }
            else
            {
                setBlockState(x, y, z, AIR);

                if (foundTop && getBlockState(x, y - 1, z) == filler)
                {
                    setBlockState(x, y - 1, z, top);
                }
            }
        }
    }

    static void addRoom(long p_180703_1_, int p_180703_3_, int p_180703_4_, double p_180703_6_, double p_180703_8_, double p_180703_10_)
    {
        addTunnel(p_180703_1_, p_180703_3_, p_180703_4_, p_180703_6_, p_180703_8_, p_180703_10_, 1.0F + rand.nextFloat() * 6.0F, 0.0F, 0.0F, -1, -1, 0.5D);
    }

    static void addTunnel(long p_180702_1_, int p_180702_3_, int p_180702_4_, double p_180702_6_, double p_180702_8_, double p_180702_10_, float p_180702_12_, float p_180702_13_, float p_180702_14_, int p_180702_15_, int p_180702_16_, double p_180702_17_)
    {
        double d0 = (double)(p_180702_3_ * 16 + 8);
        double d1 = (double)(p_180702_4_ * 16 + 8);
        float f = 0.0F;
        float f1 = 0.0F;
        java.util.Random random = new java.util.Random(p_180702_1_);

        if (p_180702_16_ <= 0)
        {
            int i = range * 16 - 16;
            p_180702_16_ = i - random.nextInt(i / 4);
        }

        boolean flag2 = false;

        if (p_180702_15_ == -1)
        {
            p_180702_15_ = p_180702_16_ / 2;
            flag2 = true;
        }

        int j = random.nextInt(p_180702_16_ / 2) + p_180702_16_ / 4;

        for (boolean flag = random.nextInt(6) == 0; p_180702_15_ < p_180702_16_; ++p_180702_15_)
        {
            double d2 = 1.5D + (double)(MathHelper.sin((float)p_180702_15_ * (float)Math.PI / (float)p_180702_16_) * p_180702_12_);
            double d3 = d2 * p_180702_17_;
            float f2 = MathHelper.cos(p_180702_14_);
            float f3 = MathHelper.sin(p_180702_14_);
            p_180702_6_ += (double)(MathHelper.cos(p_180702_13_) * f2);
            p_180702_8_ += (double)f3;
            p_180702_10_ += (double)(MathHelper.sin(p_180702_13_) * f2);

            if (flag)
            {
                p_180702_14_ = p_180702_14_ * 0.92F;
            }
            else
            {
                p_180702_14_ = p_180702_14_ * 0.7F;
            }

            p_180702_14_ = p_180702_14_ + f1 * 0.1F;
            p_180702_13_ += f * 0.1F;
            f1 = f1 * 0.9F;
            f = f * 0.75F;
            f1 = f1 + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2.0F;
            f = f + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4.0F;

            if (!flag2 && p_180702_15_ == j && p_180702_12_ > 1.0F && p_180702_16_ > 0)
            {
                addTunnel(random.nextLong(), p_180702_3_, p_180702_4_, p_180702_6_, p_180702_8_, p_180702_10_, random.nextFloat() * 0.5F + 0.5F, p_180702_13_ - ((float)Math.PI / 2F), p_180702_14_ / 3.0F, p_180702_15_, p_180702_16_, 1.0D);
                addTunnel(random.nextLong(), p_180702_3_, p_180702_4_, p_180702_6_, p_180702_8_, p_180702_10_, random.nextFloat() * 0.5F + 0.5F, p_180702_13_ + ((float)Math.PI / 2F), p_180702_14_ / 3.0F, p_180702_15_, p_180702_16_, 1.0D);
                return;
            }

            if (flag2 || random.nextInt(4) != 0)
            {
                double d4 = p_180702_6_ - d0;
                double d5 = p_180702_10_ - d1;
                double d6 = (double)(p_180702_16_ - p_180702_15_);
                double d7 = (double)(p_180702_12_ + 2.0F + 16.0F);

                if (d4 * d4 + d5 * d5 - d6 * d6 > d7 * d7)
                {
                    return;
                }

                if (p_180702_6_ >= d0 - 16.0D - d2 * 2.0D && p_180702_10_ >= d1 - 16.0D - d2 * 2.0D && p_180702_6_ <= d0 + 16.0D + d2 * 2.0D && p_180702_10_ <= d1 + 16.0D + d2 * 2.0D)
                {
                    int k2 = MathHelper.floor(p_180702_6_ - d2) - p_180702_3_ * 16 - 1;
                    int k = MathHelper.floor(p_180702_6_ + d2) - p_180702_3_ * 16 + 1;
                    int l2 = MathHelper.floor(p_180702_8_ - d3) - 1;
                    int l = MathHelper.floor(p_180702_8_ + d3) + 1;
                    int i3 = MathHelper.floor(p_180702_10_ - d2) - p_180702_4_ * 16 - 1;
                    int i1 = MathHelper.floor(p_180702_10_ + d2) - p_180702_4_ * 16 + 1;

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

                    boolean flag3 = false;

                    for (int j1 = k2; !flag3 && j1 < k; ++j1)
                    {
                        for (int k1 = i3; !flag3 && k1 < i1; ++k1)
                        {
                            for (int l1 = l + 1; !flag3 && l1 >= l2 - 1; --l1)
                            {
                                if (l1 >= 0 && l1 < 256)
                                {
                                    if (isOceanBlock(j1, l1, k1, p_180702_3_, p_180702_4_))
                                    {
                                        flag3 = true;
                                    }

                                    if (l1 != l2 - 1 && j1 != k2 && j1 != k - 1 && k1 != i3 && k1 != i1 - 1)
                                    {
                                        l1 = l2;
                                    }
                                }
                            }
                        }
                    }

                    if (!flag3)
                    {
                        for (int j3 = k2; j3 < k; ++j3)
                        {
                            double d10 = ((double)(j3 + p_180702_3_ * 16) + 0.5D - p_180702_6_) / d2;

                            for (int i2 = i3; i2 < i1; ++i2)
                            {
                                double d8 = ((double)(i2 + p_180702_4_ * 16) + 0.5D - p_180702_10_) / d2;
                                boolean flag1 = false;

                                if (d10 * d10 + d8 * d8 < 1.0D)
                                {
                                    for (int j2 = l; j2 > l2; --j2)
                                    {
                                        double d9 = ((double)(j2 - 1) + 0.5D - p_180702_8_) / d3;

                                        if (d9 > -0.7D && d10 * d10 + d9 * d9 + d8 * d8 < 1.0D)
                                        {
                                            int iblockstate1 = getBlockState(j3, j2, i2);
                                            int iblockstate2 = getBlockState(j3, j2 + 1, i2);

                                            if (isTopBlock(j3, j2, i2, p_180702_3_, p_180702_4_))
                                            {
                                                flag1 = true;
                                            }

                                            digBlock(j3, j2, i2, p_180702_3_, p_180702_4_, flag1, iblockstate1, iblockstate2);
                                        }
                                    }
                                }
                            }
                        }

                        if (flag2)
                        {
                            break;
                        }
                    }
                }
            }
        }
    }

    static void recursiveGenerate(int chunkX, int chunkZ, int p_180701_4_, int p_180701_5_)
    {
        int i = rand.nextInt(rand.nextInt(rand.nextInt(15) + 1) + 1);

        if (rand.nextInt(7) != 0)
        {
            i = 0;
        }

        for (int j = 0; j < i; ++j)
        {
            double d0 = (double)(chunkX * 16 + rand.nextInt(16));
            double d1 = (double)rand.nextInt(rand.nextInt(120) + 8);
            double d2 = (double)(chunkZ * 16 + rand.nextInt(16));
            int k = 1;

            if (rand.nextInt(4) == 0)
            {
                addRoom(rand.nextLong(), p_180701_4_, p_180701_5_, d0, d1, d2);
                k += rand.nextInt(4);
            }

            for (int l = 0; l < k; ++l)
            {
                float f = rand.nextFloat() * ((float)Math.PI * 2F);
                float f1 = (rand.nextFloat() - 0.5F) * 2.0F / 8.0F;
                float f2 = rand.nextFloat() * 2.0F + rand.nextFloat();

                if (rand.nextInt(10) == 0)
                {
                    f2 *= rand.nextFloat() * rand.nextFloat() * 3.0F + 1.0F;
                }

                addTunnel(rand.nextLong(), p_180701_4_, p_180701_5_, d0, d1, d2, f2, f, f1, 0, 0, 1.0D);
            }
        }
    }

    // ===== MapGenBase.generate (verbatim; world.getSeed() -> worldSeed) =====
    static void generate(int x, int z)
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
                recursiveGenerate(l, i1, x, z);
            }
        }
    }

    // MathHelper proxy so the verbatim bodies above read like the decompiled source.
    static class MathHelper {
        static float sin(float v) { return Golden.sin(v); }
        static float cos(float v) { return Golden.cos(v); }
        static int floor(double v) { return Golden.floor(v); }
    }

    public static void main(String[] args) {
        worldSeed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        for (int i = 0; i < 65536; ++i) data[i] = STONE;
        generate(0, 0);
        StringBuilder sb = new StringBuilder();
        for (int idx = 0; idx < 65536; ++idx)
            sb.append(String.format("%04x%n", data[idx]));
        System.out.print(sb);
    }
}
