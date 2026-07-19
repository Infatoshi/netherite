// Verbatim MC 1.11.2 WorldGenLakes.generate (net/minecraft/world/gen/feature/WorldGenLakes.java)
// + WorldGenerator base. Real MC code only - the vanilla ground truth.
//
// The MC World/Block/Material/Biome/BlockPos objects are replaced by a minimal synthetic cube and
// integer block-state ids (sanctioned substitution, identical to core/lake_gen.h):
//  - world = a mutable DIM^3 cube of packed states (id<<4, meta 0). Deterministic fill: STONE for
//    y<=12, DIRT 13<=y<=16, AIR y>=17 (flat solid surface at y=16, air above). getBlockState reads
//    AIR out of bounds (vanilla unloaded/out-of-height). generate() reads its own mid-pass writes.
//  - Material facts (block/material/Material.java + MaterialLiquid + MaterialTransparent): AIR
//    isSolid=0 isLiquid=0; STONE/DIRT/GRASS/ICE isSolid=1 isLiquid=0; WATER/LAVA isSolid=0
//    isLiquid=1. isSolid here = "not air and not liquid"; isLiquid = water/lava.
//  - Liquid fixed to Blocks.WATER. Biome fixed PLAINS: topBlock=GRASS (not MYCELIUM); skylight=15;
//    canBlockFreezeWater=false (Plains temp 0.8 > 0.15). The blob math, RNG draw order, boolean[2048]
//    index packing and the multi-pass fill are verbatim. Prints every cell as %04x in (y,z,x) order,
//    matching cpu/lake_gen.c and cuda/lake_gen.cu.
public class Golden {
    static final int DIM = 32;

    // packed block-states (mc_state(id,0) == id<<4); vanilla numeric block ids
    static final int AIR_ID = 0, STONE_ID = 1, GRASS_ID = 2, DIRT_ID = 3,
                     FLOWING_WATER_ID = 8, WATER_ID = 9, FLOWING_LAVA_ID = 10, LAVA_ID = 11,
                     ICE_ID = 79, MYCELIUM_ID = 110;
    static int state(int id) { return id << 4; }
    static int blockId(int s) { return s >> 4; }

    static final int AIR = state(AIR_ID), STONE = state(STONE_ID), GRASS = state(GRASS_ID),
                     DIRT = state(DIRT_ID), WATER = state(WATER_ID), ICE = state(ICE_ID),
                     MYCELIUM = state(MYCELIUM_ID);

    static final int[] world = new int[DIM * DIM * DIM];

    // the lake's liquid block (new WorldGenLakes(Blocks.WATER))
    static final int block = WATER_ID;
    static int blockDefaultState() { return state(block); }

    // --- minimal BlockPos (net/minecraft/util/math/BlockPos), immutable ---
    static final class BlockPos {
        final int x, y, z;
        BlockPos(int x, int y, int z) { this.x = x; this.y = y; this.z = z; }
        BlockPos add(int dx, int dy, int dz) { return new BlockPos(x + dx, y + dy, z + dz); }
        BlockPos down() { return new BlockPos(x, y - 1, z); }
        BlockPos down(int n) { return new BlockPos(x, y - n, z); }
        int getY() { return y; }
    }

    // --- synthetic world (replaces net.minecraft.world.World) ---
    static boolean inBounds(int x, int y, int z) {
        return x >= 0 && x < DIM && y >= 0 && y < DIM && z >= 0 && z < DIM;
    }
    static int getBlockState(BlockPos p) {
        return inBounds(p.x, p.y, p.z) ? world[(p.y * DIM + p.z) * DIM + p.x] : AIR;
    }
    static void setBlockState(BlockPos p, int s, int flags) {
        if (inBounds(p.x, p.y, p.z)) world[(p.y * DIM + p.z) * DIM + p.x] = s;
    }
    static boolean isAirBlock(BlockPos p) { return blockId(getBlockState(p)) == AIR_ID; }

    // Material.isLiquid / isSolid on a packed state (sanctioned substitution)
    static boolean isLiquid(int s) {
        int id = blockId(s);
        return id == WATER_ID || id == FLOWING_WATER_ID || id == LAVA_ID || id == FLOWING_LAVA_ID;
    }
    static boolean isSolid(int s) {
        int id = blockId(s);
        if (id == AIR_ID) return false;
        if (isLiquid(s)) return false;
        return true;
    }
    static boolean materialIsLava(int id) { return id == LAVA_ID || id == FLOWING_LAVA_ID; }
    static boolean materialIsWater(int id) { return id == WATER_ID || id == FLOWING_WATER_ID; }

    // fixed-PLAINS environment substitutions
    static int getLightForSky(BlockPos p) { return 15; }            // synthetic full skylight
    static int biomeTopBlock(BlockPos p) { return GRASS_ID; }       // Plains topBlock == GRASS
    static boolean canBlockFreezeWater(BlockPos p) { return false; } // Plains temp 0.8 -> no ice

    // --- verbatim WorldGenLakes.generate (World/Block/Material/Biome calls -> synthetic above) ---
    public static boolean generate(java.util.Random rand, BlockPos position)
    {
        for (position = position.add(-8, 0, -8); position.getY() > 5 && isAirBlock(position); position = position.down())
        {
            ;
        }

        if (position.getY() <= 4)
        {
            return false;
        }
        else
        {
            position = position.down(4);
            boolean[] aboolean = new boolean[2048];
            int i = rand.nextInt(4) + 4;

            for (int j = 0; j < i; ++j)
            {
                double d0 = rand.nextDouble() * 6.0D + 3.0D;
                double d1 = rand.nextDouble() * 4.0D + 2.0D;
                double d2 = rand.nextDouble() * 6.0D + 3.0D;
                double d3 = rand.nextDouble() * (16.0D - d0 - 2.0D) + 1.0D + d0 / 2.0D;
                double d4 = rand.nextDouble() * (8.0D - d1 - 4.0D) + 2.0D + d1 / 2.0D;
                double d5 = rand.nextDouble() * (16.0D - d2 - 2.0D) + 1.0D + d2 / 2.0D;

                for (int l = 1; l < 15; ++l)
                {
                    for (int i1 = 1; i1 < 15; ++i1)
                    {
                        for (int j1 = 1; j1 < 7; ++j1)
                        {
                            double d6 = ((double)l - d3) / (d0 / 2.0D);
                            double d7 = ((double)j1 - d4) / (d1 / 2.0D);
                            double d8 = ((double)i1 - d5) / (d2 / 2.0D);
                            double d9 = d6 * d6 + d7 * d7 + d8 * d8;

                            if (d9 < 1.0D)
                            {
                                aboolean[(l * 16 + i1) * 8 + j1] = true;
                            }
                        }
                    }
                }
            }

            for (int k1 = 0; k1 < 16; ++k1)
            {
                for (int l2 = 0; l2 < 16; ++l2)
                {
                    for (int k = 0; k < 8; ++k)
                    {
                        boolean flag = !aboolean[(k1 * 16 + l2) * 8 + k] && (k1 < 15 && aboolean[((k1 + 1) * 16 + l2) * 8 + k] || k1 > 0 && aboolean[((k1 - 1) * 16 + l2) * 8 + k] || l2 < 15 && aboolean[(k1 * 16 + l2 + 1) * 8 + k] || l2 > 0 && aboolean[(k1 * 16 + (l2 - 1)) * 8 + k] || k < 7 && aboolean[(k1 * 16 + l2) * 8 + k + 1] || k > 0 && aboolean[(k1 * 16 + l2) * 8 + (k - 1)]);

                        if (flag)
                        {
                            int material = getBlockState(position.add(k1, k, l2));

                            if (k >= 4 && isLiquid(material))
                            {
                                return false;
                            }

                            if (k < 4 && !isSolid(material) && blockId(getBlockState(position.add(k1, k, l2))) != this_block())
                            {
                                return false;
                            }
                        }
                    }
                }
            }

            for (int l1 = 0; l1 < 16; ++l1)
            {
                for (int i3 = 0; i3 < 16; ++i3)
                {
                    for (int i4 = 0; i4 < 8; ++i4)
                    {
                        if (aboolean[(l1 * 16 + i3) * 8 + i4])
                        {
                            setBlockState(position.add(l1, i4, i3), i4 >= 4 ? AIR : blockDefaultState(), 2);
                        }
                    }
                }
            }

            for (int i2 = 0; i2 < 16; ++i2)
            {
                for (int j3 = 0; j3 < 16; ++j3)
                {
                    for (int j4 = 4; j4 < 8; ++j4)
                    {
                        if (aboolean[(i2 * 16 + j3) * 8 + j4])
                        {
                            BlockPos blockpos = position.add(i2, j4 - 1, j3);

                            if (blockId(getBlockState(blockpos)) == DIRT_ID && getLightForSky(position.add(i2, j4, j3)) > 0)
                            {
                                int biomeTop = biomeTopBlock(blockpos);

                                if (biomeTop == MYCELIUM_ID)
                                {
                                    setBlockState(blockpos, MYCELIUM, 2);
                                }
                                else
                                {
                                    setBlockState(blockpos, GRASS, 2);
                                }
                            }
                        }
                    }
                }
            }

            if (materialIsLava(this_block()))
            {
                for (int j2 = 0; j2 < 16; ++j2)
                {
                    for (int k3 = 0; k3 < 16; ++k3)
                    {
                        for (int k4 = 0; k4 < 8; ++k4)
                        {
                            boolean flag1 = !aboolean[(j2 * 16 + k3) * 8 + k4] && (j2 < 15 && aboolean[((j2 + 1) * 16 + k3) * 8 + k4] || j2 > 0 && aboolean[((j2 - 1) * 16 + k3) * 8 + k4] || k3 < 15 && aboolean[(j2 * 16 + k3 + 1) * 8 + k4] || k3 > 0 && aboolean[(j2 * 16 + (k3 - 1)) * 8 + k4] || k4 < 7 && aboolean[(j2 * 16 + k3) * 8 + k4 + 1] || k4 > 0 && aboolean[(j2 * 16 + k3) * 8 + (k4 - 1)]);

                            if (flag1 && (k4 < 4 || rand.nextInt(2) != 0) && isSolid(getBlockState(position.add(j2, k4, k3))))
                            {
                                setBlockState(position.add(j2, k4, k3), STONE, 2);
                            }
                        }
                    }
                }
            }

            if (materialIsWater(this_block()))
            {
                for (int k2 = 0; k2 < 16; ++k2)
                {
                    for (int l3 = 0; l3 < 16; ++l3)
                    {
                        int l4 = 4;

                        if (canBlockFreezeWater(position.add(k2, 4, l3)))
                        {
                            setBlockState(position.add(k2, 4, l3), ICE, 2);
                        }
                    }
                }
            }

            return true;
        }
    }

    static int this_block() { return block; }

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        for (int y = 0; y < DIM; ++y) {
            int fill = (y <= 12) ? STONE : (y <= 16 ? DIRT : AIR);
            for (int z = 0; z < DIM; ++z)
                for (int x = 0; x < DIM; ++x)
                    world[(y * DIM + z) * DIM + x] = fill;
        }
        java.util.Random rand = new java.util.Random(seed);
        generate(rand, new BlockPos(16, 24, 16));
        StringBuilder sb = new StringBuilder();
        for (int y = 0; y < DIM; ++y)
            for (int z = 0; z < DIM; ++z)
                for (int x = 0; x < DIM; ++x)
                    sb.append(String.format("%04x%n", world[(y * DIM + z) * DIM + x] & 0xffff));
        System.out.print(sb);
    }
}
