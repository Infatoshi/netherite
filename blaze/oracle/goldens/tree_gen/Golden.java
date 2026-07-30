// Verbatim MC 1.11.2 WorldGenTrees.generate (standard oak) - the vanilla ground truth.
//   net/minecraft/world/gen/feature/WorldGenTrees.java        : generate(World, Random, BlockPos)
//   net/minecraft/world/gen/feature/WorldGenAbstractTree.java : isReplaceable / canGrowInto
//   net/minecraft/world/gen/feature/WorldGenerator.java       : setBlockAndNotifyAdequately (notify=false)
//   net/minecraft/block/Block.java                            : isAir/isLeaves/isWood/canSustainPlant/onPlantGrow
//   net/minecraft/block/BlockBush.java                        : canSustainBush (sapling plantable path)
//
// The MC World/BlockPos/IBlockState/Block object graph is replaced by a minimal synthetic cube
// (same sanctioned substitution as ore_gen/caves): getBlockState/setBlockState read/write the cube,
// and each Block/World predicate reduces to an integer-id check (documented per method below). The
// generate() control flow and EVERY rand draw are verbatim. Constructor = new WorldGenTrees(false):
// notify=false, minTreeHeight=4, metaWood=OAK LOG, metaLeaves=OAK LEAVES, vinesGrow=false. With
// vinesGrow=false the vine/cocoa branches are dead code (never executed, draw no RNG) and are
// omitted; this changes neither control flow nor the LCG sequence. Only rand draws: nextInt(3)
// (height) and nextInt(2) per canopy-corner cell. Prints every cell's packed state as %04x in
// (y,z,x) order, matching cpu/tree_gen.c.
import java.util.Random;

public class Golden {
    static final int DIM = 32;          // worldIn.getHeight()
    static final int FLOOR_Y = 8;
    static final int PLANT_X = 16;
    static final int PLANT_Z = 16;

    // block-state id substitution (mc_state(id,0): (id<<4)|0). meta 0 throughout.
    static final int AIR    = (0  << 4) | 0;
    static final int GRASS  = (2  << 4) | 0;
    static final int DIRT   = (3  << 4) | 0;
    static final int LOG    = (17 << 4) | 0;   // metaWood  (OAK)
    static final int LEAVES = (18 << 4) | 0;   // metaLeaves (OAK)

    static final int[] world = new int[DIM * DIM * DIM];

    static int idx(int x, int y, int z) { return (y * DIM + z) * DIM + x; }

    // net.minecraft.world.World.getBlockState/setBlockState over the cube. OOB read -> a state that
    // matches no predicate (an out-of-world, non-replaceable cell); OOB write dropped.
    static int getBlockState(int x, int y, int z) {
        if (x < 0 || x >= DIM || y < 0 || y >= DIM || z < 0 || z >= DIM) return 0xFFFF;
        return world[idx(x, y, z)];
    }
    static void setBlockState(int x, int y, int z, int s) {
        if (x < 0 || x >= DIM || y < 0 || y >= DIM || z < 0 || z >= DIM) return;
        world[idx(x, y, z)] = s;
    }

    // Block.isAir: state.getMaterial() == Material.AIR.
    static boolean isAir(int s)    { return s == AIR; }
    // Block.isLeaves: state.getMaterial() == Material.LEAVES.
    static boolean isLeaves(int s) { return s == LEAVES; }
    // Block.isWood: default false; BlockLog -> true. Only LOG is wood here.
    static boolean isWood(int s)   { return s == LOG; }
    // WorldGenAbstractTree.canGrowInto: material AIR/LEAVES or block GRASS/DIRT/LOG/LOG2/SAPLING/VINE.
    static boolean canGrowInto(int s) {
        return s == AIR || s == LEAVES || s == GRASS || s == DIRT || s == LOG;
    }
    // WorldGenAbstractTree.isReplaceable: isAir || isLeaves || isWood || canGrowInto.
    static boolean isReplaceable(int x, int y, int z) {
        int s = getBlockState(x, y, z);
        return isAir(s) || isLeaves(s) || isWood(s) || canGrowInto(s);
    }
    // Block.canSustainPlant for the sapling plantable -> BlockBush.canSustainBush(soil):
    // soil == GRASS || DIRT || FARMLAND.
    static boolean canSustainPlant(int soil) { return soil == GRASS || soil == DIRT; }
    // Block.onPlantGrow: GRASS/FARMLAND -> DIRT. (Places dirt under the trunk.)
    static void onPlantGrow(int x, int y, int z) {
        if (getBlockState(x, y, z) == GRASS) setBlockState(x, y, z, DIRT);
    }

    // Verbatim WorldGenTrees.generate (standard oak), World/BlockPos calls -> the cube above.
    public static boolean generate(Random rand, int posX, int posY, int posZ) {
        int i = rand.nextInt(3) + 4;       // + this.minTreeHeight
        boolean flag = true;

        if (posY >= 1 && posY + i + 1 <= DIM) {
            for (int j = posY; j <= posY + 1 + i; ++j) {
                int k = 1;

                if (j == posY) {
                    k = 0;
                }

                if (j >= posY + 1 + i - 2) {
                    k = 2;
                }

                for (int l = posX - k; l <= posX + k && flag; ++l) {
                    for (int i1 = posZ - k; i1 <= posZ + k && flag; ++i1) {
                        if (j >= 0 && j < DIM) {
                            if (!isReplaceable(l, j, i1)) {
                                flag = false;
                            }
                        } else {
                            flag = false;
                        }
                    }
                }
            }

            if (!flag) {
                return false;
            } else {
                int state = getBlockState(posX, posY - 1, posZ);   // worldIn.getBlockState(position.down())

                if (canSustainPlant(state) && posY < DIM - i - 1) {
                    onPlantGrow(posX, posY - 1, posZ);

                    for (int i3 = posY - 3 + i; i3 <= posY + i; ++i3) {
                        int i4 = i3 - (posY + i);
                        int j1 = 1 - i4 / 2;

                        for (int k1 = posX - j1; k1 <= posX + j1; ++k1) {
                            int l1 = k1 - posX;

                            for (int i2 = posZ - j1; i2 <= posZ + j1; ++i2) {
                                int j2 = i2 - posZ;

                                if (Math.abs(l1) != j1 || Math.abs(j2) != j1 || rand.nextInt(2) != 0 && i4 != 0) {
                                    int cs = getBlockState(k1, i3, i2);

                                    if (isAir(cs) || isLeaves(cs)) {   // || getMaterial() == Material.VINE (none)
                                        setBlockState(k1, i3, i2, LEAVES);   // setBlockAndNotifyAdequately(metaLeaves)
                                    }
                                }
                            }
                        }
                    }

                    for (int j3 = 0; j3 < i; ++j3) {
                        int cs = getBlockState(posX, posY + j3, posZ);   // position.up(j3)

                        if (isAir(cs) || isLeaves(cs)) {   // || getMaterial() == Material.VINE (none)
                            setBlockState(posX, posY + j3, posZ, LOG);   // setBlockAndNotifyAdequately(metaWood)
                            // vinesGrow == false: the vine placement block here is dead, draws no RNG.
                        }
                    }

                    return true;
                } else {
                    return false;
                }
            }
        } else {
            return false;
        }
    }

    static void buildWorld() {
        for (int y = 0; y < DIM; ++y) {
            int s = (y < FLOOR_Y) ? DIRT : (y == FLOOR_Y ? GRASS : AIR);
            for (int z = 0; z < DIM; ++z)
                for (int x = 0; x < DIM; ++x)
                    world[idx(x, y, z)] = s;
        }
    }

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        buildWorld();
        Random rand = new Random(seed);
        generate(rand, PLANT_X, FLOOR_Y + 1, PLANT_Z);
        StringBuilder sb = new StringBuilder();
        for (int y = 0; y < DIM; ++y)
            for (int z = 0; z < DIM; ++z)
                for (int x = 0; x < DIM; ++x)
                    sb.append(String.format("%04x%n", world[idx(x, y, z)] & 0xFFFF));
        System.out.print(sb);
    }
}
