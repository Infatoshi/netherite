package qrl;

import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.PrintStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.monster.EntityElderGuardian;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.EnumFacing;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.DifficultyInstance;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.gen.structure.StructureBoundingBox;
import net.minecraft.world.gen.structure.StructureOceanMonumentPieces;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Exact 1.11.2 monument room graph, clipped placement, and Elder sites. */
public final class OceanMonumentGolden {
    private static final PrintStream RAW = new PrintStream(
        new FileOutputStream(FileDescriptor.out));

    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> blocks =
            new HashMap<BlockPos, IBlockState>();
        final List<Entity> entities = new ArrayList<Entity>();

        MemoryWorld(long seed) {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(seed, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "monument-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public IBlockState getBlockState(BlockPos pos) {
            IBlockState state = blocks.get(pos);
            if (state != null) return state;
            if (pos.getY() < 32) return Blocks.STONE.getDefaultState();
            if (pos.getY() < 64) return Blocks.WATER.getDefaultState();
            return Blocks.AIR.getDefaultState();
        }
        public boolean setBlockState(BlockPos pos, IBlockState state, int flags) {
            blocks.put(pos.toImmutable(), state);
            return true;
        }
        public boolean spawnEntity(Entity entity) {
            entities.add(entity);
            return true;
        }
        public DifficultyInstance getDifficultyForLocation(BlockPos pos) {
            return new DifficultyInstance(EnumDifficulty.NORMAL, 0L, 0L, 0.0F);
        }
        public void notifyNeighborsOfStateChange(
                BlockPos pos, Block block, boolean observers) {}
        public void updateComparatorOutputLevel(BlockPos pos, Block block) {}
    }

    private static int floorDiv(int value, int divisor) {
        int quotient = value / divisor;
        return value % divisor < 0 ? quotient - 1 : quotient;
    }

    private static Random populationRandom(long seed, int cx, int cz) {
        Random random = new Random(seed);
        long mulX = random.nextLong() / 2L * 2L + 1L;
        long mulZ = random.nextLong() / 2L * 2L + 1L;
        random.setSeed((long)cx * mulX + (long)cz * mulZ ^ seed);
        return random;
    }

    private static void candidate(long seed, int regionX, int regionZ) {
        Random random = new Random((long)regionX * 341873128712L
            + (long)regionZ * 132897987541L + seed + 10387313L);
        int chunkX = regionX * 32
            + (random.nextInt(27) + random.nextInt(27)) / 2;
        int chunkZ = regionZ * 32
            + (random.nextInt(27) + random.nextInt(27)) / 2;
        RAW.printf("C %d %d %d %d %d%n",
            seed, regionX, regionZ, chunkX, chunkZ);
    }

    private static long add(long hash, int value) {
        hash ^= value & 255; hash *= 0x100000001b3L;
        hash ^= value >>> 8 & 255; hash *= 0x100000001b3L;
        return hash;
    }

    private static void run(long seed, int chunkX, int chunkZ) {
        Random layout = new Random(seed);
        long mulX = layout.nextLong(), mulZ = layout.nextLong();
        layout.setSeed((long)chunkX * mulX ^ (long)chunkZ * mulZ ^ seed);
        int originX = chunkX * 16 + 8 - 29;
        int originZ = chunkZ * 16 + 8 - 29;
        EnumFacing facing = EnumFacing.Plane.HORIZONTAL.random(layout);
        StructureOceanMonumentPieces.MonumentBuilding building =
            new StructureOceanMonumentPieces.MonumentBuilding(
                layout, originX, originZ, facing);
        StructureBoundingBox box = building.getBoundingBox();
        MemoryWorld world = new MemoryWorld(seed);
        for (int cz = floorDiv(box.minZ, 16) - 2;
                cz <= floorDiv(box.maxZ, 16) + 2; ++cz) {
            for (int cx = floorDiv(box.minX, 16) - 2;
                    cx <= floorDiv(box.maxX, 16) + 2; ++cx) {
                int minX = cx * 16 + 8, minZ = cz * 16 + 8;
                if (!box.intersectsWith(minX, minZ, minX + 15, minZ + 15))
                    continue;
                StructureBoundingBox clip = new StructureBoundingBox(
                    minX, 1, minZ, minX + 15, 512, minZ + 15);
                if (!building.addComponentParts(
                        world, populationRandom(seed, cx, cz), clip))
                    throw new AssertionError("placement failed");
            }
        }
        long hash = 0xcbf29ce484222325L;
        int rough = 0, bricks = 0, dark = 0, lantern = 0;
        int gold = 0, sponge = 0, water = 0, stone = 0;
        for (int y = 2; y <= 64; ++y)
            for (int z = originZ - 5; z <= originZ + 62; ++z)
                for (int x = originX - 5; x <= originX + 62; ++x) {
                    IBlockState state = world.getBlockState(new BlockPos(x, y, z));
                    int id = Block.getIdFromBlock(state.getBlock());
                    int meta = state.getBlock().getMetaFromState(state);
                    int raw = id << 4 | meta & 15;
                    if (System.getenv("MONUMENT_VERBOSE") != null
                            && raw != (y < 32 ? 16 : y < 64 ? 144 : 0))
                        RAW.printf("B %d %d %d %04x%n",
                            x - originX, y, z - originZ, raw);
                    hash = add(hash, raw);
                    if (id == 168 && meta == 0) ++rough;
                    else if (id == 168 && meta == 1) ++bricks;
                    else if (id == 168 && meta == 2) ++dark;
                    else if (id == 169) ++lantern;
                    else if (id == 41) ++gold;
                    else if (id == 19 && meta == 1) ++sponge;
                    else if (id == 9) ++water;
                    else if (id == 1) ++stone;
                }
        List<Entity> elders = new ArrayList<Entity>();
        for (Entity entity : world.entities)
            if (entity instanceof EntityElderGuardian) elders.add(entity);
        Collections.sort(elders, new Comparator<Entity>() {
            public int compare(Entity a, Entity b) {
                int value = Double.compare(a.posX, b.posX);
                if (value != 0) return value;
                value = Double.compare(a.posY, b.posY);
                return value != 0 ? value : Double.compare(a.posZ, b.posZ);
            }
        });
        RAW.printf("M %d %d %d %d %016x %d %d %d %d %d %d %d %d %d%n",
            seed, chunkX, chunkZ, facing.getIndex(), hash, rough, bricks,
            dark, lantern, gold, sponge, water, stone, elders.size());
        for (Entity entity : elders)
            RAW.printf("E %016x %016x %016x%n",
                Double.doubleToRawLongBits(entity.posX),
                Double.doubleToRawLongBits(entity.posY),
                Double.doubleToRawLongBits(entity.posZ));
        if (System.getenv("MONUMENT_TICK") != null) {
            for (Entity entity : elders)
                RAW.printf("H %s%n", entity.isEntityInsideOpaqueBlock());
        }
    }

    public static void main(String[] args) {
        Bootstrap.register();
        run(0L, 0, 0);
        if (System.getenv("MONUMENT_ONE") != null) return;
        run(1L, 3, -7);
        run(123456789L, -12, 19);
        run(-99887766L, 33, -41);
        run(0x5eed5eedL, -64, -64);
        candidate(0L, 0, 0);
        candidate(1L, 3, -7);
        candidate(123456789L, -12, 19);
        candidate(-99887766L, 33, -41);
        candidate(0x5eed5eedL, -64, -64);
    }
}
