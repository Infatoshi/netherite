package qrl;

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
import net.minecraft.entity.item.EntityEnderCrystal;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.biome.BiomeEndDecorator;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.gen.feature.WorldGenSpikes;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Real 1.11.2 End-spike descriptor, block, and crystal oracle. */
public final class DragonRespawnGolden {
    private static final long[] SEEDS = {
        0L, 1L, 123456789L, -99887766L
    };

    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> blocks =
            new HashMap<BlockPos, IBlockState>();
        final long seed;
        EntityEnderCrystal crystal;

        MemoryWorld(long seed) {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(seed, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "dragon-respawn-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.seed = seed;
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public long getSeed() { return seed; }
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
        public boolean spawnEntity(Entity entity) {
            if (entity instanceof EntityEnderCrystal)
                crystal = (EntityEnderCrystal)entity;
            return true;
        }
    }

    private static long add(long hash, int value) {
        hash ^= value & 0xffffffffL;
        return hash * 0x100000001b3L;
    }

    private static long hashTargetBlocks(MemoryWorld world) {
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
            IBlockState state = entry.getValue();
            Block block = state.getBlock();
            if (block != Blocks.OBSIDIAN && block != Blocks.IRON_BARS
                    && block != Blocks.BEDROCK)
                continue;
            BlockPos pos = entry.getKey();
            hash = add(hash, pos.getX());
            hash = add(hash, pos.getY());
            hash = add(hash, pos.getZ());
            hash = add(hash, Block.getIdFromBlock(block));
            hash = add(hash, block.getMetaFromState(state));
        }
        return hash;
    }

    private static void descriptors(long seed) {
        MemoryWorld world = new MemoryWorld(seed);
        WorldGenSpikes.EndSpike[] spikes =
            BiomeEndDecorator.getSpikesForWorld(world);
        for (int i = 0; i < spikes.length; ++i) {
            WorldGenSpikes.EndSpike spike = spikes[i];
            System.out.printf("D %d %d %d %d %d %d %d%n",
                seed, i, spike.getCenterX(), spike.getCenterZ(),
                spike.getRadius(), spike.getHeight(), spike.isGuarded() ? 1 : 0);
        }
    }

    private static void generatedSpikes(long seed) {
        MemoryWorld descriptorWorld = new MemoryWorld(seed);
        WorldGenSpikes.EndSpike[] spikes =
            BiomeEndDecorator.getSpikesForWorld(descriptorWorld);
        for (int i = 0; i < spikes.length; ++i) {
            WorldGenSpikes.EndSpike spike = spikes[i];
            MemoryWorld world = new MemoryWorld(seed);
            WorldGenSpikes generator = new WorldGenSpikes();
            generator.setSpike(spike);
            generator.setCrystalInvulnerable(false);
            generator.setBeamTarget(null);
            generator.generate(world, new Random(1234L + i),
                new BlockPos(spike.getCenterX(), 45, spike.getCenterZ()));
            int obsidian = 0, bars = 0, bedrock = 0;
            for (IBlockState state : world.blocks.values()) {
                if (state.getBlock() == Blocks.OBSIDIAN) ++obsidian;
                else if (state.getBlock() == Blocks.IRON_BARS) ++bars;
                else if (state.getBlock() == Blocks.BEDROCK) ++bedrock;
            }
            EntityEnderCrystal crystal = world.crystal;
            BlockPos beam = crystal.getBeamTarget();
            System.out.printf(
                "S %d %d %d %d %016x %016x %016x %016x %d %d %d %d %d%n",
                i, obsidian, bars, bedrock, hashTargetBlocks(world),
                Double.doubleToRawLongBits(crystal.posX),
                Double.doubleToRawLongBits(crystal.posY),
                Double.doubleToRawLongBits(crystal.posZ),
                crystal.getIsInvulnerable() ? 1 : 0,
                beam == null ? 0 : beam.getX(),
                beam == null ? 0 : beam.getY(),
                beam == null ? 0 : beam.getZ(),
                crystal.shouldShowBottom() ? 1 : 0);
        }
    }

    public static void main(String[] args) {
        Bootstrap.register();
        for (long seed : SEEDS) descriptors(seed);
        generatedSpikes(0L);
    }
}
