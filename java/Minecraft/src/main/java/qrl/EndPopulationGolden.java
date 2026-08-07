package qrl;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.BlockChorusFlower;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.gen.feature.WorldGenEndGateway;
import net.minecraft.world.gen.feature.WorldGenEndIsland;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Actual 1.11.2 chorus, small-island, and gateway generator oracle. */
public final class EndPopulationGolden {
    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> blocks =
            new HashMap<BlockPos, IBlockState>();

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "end-population-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public IBlockState getBlockState(BlockPos pos) {
            IBlockState state = blocks.get(pos);
            return state == null ? Blocks.AIR.getDefaultState() : state;
        }
        public boolean setBlockState(BlockPos pos, IBlockState state, int flags) {
            BlockPos key = pos.toImmutable();
            if (state.getBlock() == Blocks.AIR) blocks.remove(key);
            else blocks.put(key, state);
            return true;
        }
    }

    private static long cursor(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get();
    }

    private static long add(long hash, int value) {
        hash ^= value & 0xffffffffL;
        return hash * 0x100000001b3L;
    }

    private static long hash(MemoryWorld world) {
        List<Map.Entry<BlockPos, IBlockState>> entries =
            new ArrayList<Map.Entry<BlockPos, IBlockState>>(world.blocks.entrySet());
        Collections.sort(entries,
            new Comparator<Map.Entry<BlockPos, IBlockState>>() {
                public int compare(Map.Entry<BlockPos, IBlockState> av,
                                   Map.Entry<BlockPos, IBlockState> bv) {
                    BlockPos a = av.getKey(), b = bv.getKey();
                    if (a.getX() != b.getX()) return Integer.compare(a.getX(), b.getX());
                    if (a.getY() != b.getY()) return Integer.compare(a.getY(), b.getY());
                    return Integer.compare(a.getZ(), b.getZ());
                }
            });
        long hash = 0xcbf29ce484222325L;
        for (Map.Entry<BlockPos, IBlockState> entry : entries) {
            BlockPos pos = entry.getKey();
            IBlockState state = entry.getValue();
            hash = add(hash, pos.getX()); hash = add(hash, pos.getY());
            hash = add(hash, pos.getZ());
            hash = add(hash, net.minecraft.block.Block.getIdFromBlock(state.getBlock()));
            hash = add(hash, state.getBlock().getMetaFromState(state));
        }
        return hash;
    }

    private static void chorus(long seed, int x, int y, int z, int radius)
            throws Exception {
        MemoryWorld world = new MemoryWorld();
        Random random = new Random(seed);
        BlockChorusFlower.generatePlant(world, new BlockPos(x, y, z), random, radius);
        System.out.printf("C %d %d %016x %012x%n", seed, world.blocks.size(),
            hash(world), cursor(random));
    }

    private static void island(long seed, int x, int y, int z) throws Exception {
        MemoryWorld world = new MemoryWorld();
        Random random = new Random(seed);
        new WorldGenEndIsland().generate(world, random, new BlockPos(x, y, z));
        System.out.printf("I %d %d %016x %012x%n", seed, world.blocks.size(),
            hash(world), cursor(random));
    }

    private static void gateway(int x, int y, int z) {
        MemoryWorld world = new MemoryWorld();
        new WorldGenEndGateway().generate(world, new Random(0L),
            new BlockPos(x, y, z));
        System.out.printf("G %d %016x%n", world.blocks.size(), hash(world));
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        chorus(0L, 0, 70, 0, 8);
        chorus(1L, 17, 63, -9, 8);
        chorus(123456789L, -23, 91, 31, 8);
        chorus(-99887766L, 4, 50, 7, 5);
        island(0L, 0, 70, 0);
        island(1L, 17, 63, -9);
        island(123456789L, -23, 91, 31);
        island(-99887766L, 4, 50, 7);
        gateway(32, 75, -48);
    }
}
