// Verbatim MC 1.11.2 WorldGenMinable.generate (net/minecraft/world/gen/feature/WorldGenMinable.java)
// + the MathHelper sin/cos/floor + SIN_TABLE static init it depends on
// (net/minecraft/util/math/MathHelper.java). Real MC code only - the vanilla ground truth.
//
// The MC world is replaced by a minimal flat all-STONE cube: getBlockState returns the cell's
// packed state (or non-stone if out of bounds), and isReplaceableOreGen with the StonePredicate
// reduces to "is the cell natural stone". generate() reads the cube mid-loop, so cells turned to
// ore are seen as ore on revisit and skipped. Prints every cell's packed state as %016x in (y,z,x)
// order, matching cpu/ore_gen.c.
public class Golden {
    static final int DIM = 48;
    static final int STONE = (1 << 4) | 0;    // mc_state(BLK_STONE, 0)
    static final int ORE   = (56 << 4) | 0;   // mc_state(BLK_DIAMOND_ORE, 0)
    static final int[] world = new int[DIM * DIM * DIM];

    static int numberOfBlocks;
    static int oreBlock;

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

    // minimal stone-cube world (replaces net.minecraft.world.World)
    static int getBlockState(int x, int y, int z) {
        if (x < 0 || x >= DIM || y < 0 || y >= DIM || z < 0 || z >= DIM) return -1;
        return world[(y * DIM + z) * DIM + x];
    }
    static void setBlockState(int x, int y, int z, int state) {
        if (x < 0 || x >= DIM || y < 0 || y >= DIM || z < 0 || z >= DIM) return;
        world[(y * DIM + z) * DIM + x] = state;
    }

    // verbatim WorldGenMinable.generate (world/BlockPos calls -> the stone-cube above)
    public static boolean generate(java.util.Random rand, int posX, int posY, int posZ)
    {
        float f = rand.nextFloat() * (float)Math.PI;
        double d0 = (double)((float)(posX + 8) + sin(f) * (float)numberOfBlocks / 8.0F);
        double d1 = (double)((float)(posX + 8) - sin(f) * (float)numberOfBlocks / 8.0F);
        double d2 = (double)((float)(posZ + 8) + cos(f) * (float)numberOfBlocks / 8.0F);
        double d3 = (double)((float)(posZ + 8) - cos(f) * (float)numberOfBlocks / 8.0F);
        double d4 = (double)(posY + rand.nextInt(3) - 2);
        double d5 = (double)(posY + rand.nextInt(3) - 2);

        for (int i = 0; i < numberOfBlocks; ++i)
        {
            float f1 = (float)i / (float)numberOfBlocks;
            double d6 = d0 + (d1 - d0) * (double)f1;
            double d7 = d4 + (d5 - d4) * (double)f1;
            double d8 = d2 + (d3 - d2) * (double)f1;
            double d9 = rand.nextDouble() * (double)numberOfBlocks / 16.0D;
            double d10 = (double)(sin((float)Math.PI * f1) + 1.0F) * d9 + 1.0D;
            double d11 = (double)(sin((float)Math.PI * f1) + 1.0F) * d9 + 1.0D;
            int j = floor(d6 - d10 / 2.0D);
            int k = floor(d7 - d11 / 2.0D);
            int l = floor(d8 - d10 / 2.0D);
            int i1 = floor(d6 + d10 / 2.0D);
            int j1 = floor(d7 + d11 / 2.0D);
            int k1 = floor(d8 + d10 / 2.0D);

            for (int l1 = j; l1 <= i1; ++l1)
            {
                double d12 = ((double)l1 + 0.5D - d6) / (d10 / 2.0D);

                if (d12 * d12 < 1.0D)
                {
                    for (int i2 = k; i2 <= j1; ++i2)
                    {
                        double d13 = ((double)i2 + 0.5D - d7) / (d11 / 2.0D);

                        if (d12 * d12 + d13 * d13 < 1.0D)
                        {
                            for (int j2 = l; j2 <= k1; ++j2)
                            {
                                double d14 = ((double)j2 + 0.5D - d8) / (d10 / 2.0D);

                                if (d12 * d12 + d13 * d13 + d14 * d14 < 1.0D)
                                {
                                    int state = getBlockState(l1, i2, j2);
                                    if (state == STONE)   // isReplaceableOreGen + StonePredicate (natural stone)
                                    {
                                        setBlockState(l1, i2, j2, oreBlock);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        return true;
    }

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        numberOfBlocks = 33;
        oreBlock = ORE;
        for (int i = 0; i < DIM * DIM * DIM; ++i) world[i] = STONE;
        java.util.Random rand = new java.util.Random(seed);
        generate(rand, 16, 24, 16);
        StringBuilder sb = new StringBuilder();
        for (int y = 0; y < DIM; ++y)
            for (int z = 0; z < DIM; ++z)
                for (int x = 0; x < DIM; ++x)
                    sb.append(String.format("%016x%n", (long)world[(y * DIM + z) * DIM + x]));
        System.out.print(sb);
    }
}
