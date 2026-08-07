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
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.gen.structure.ComponentScatteredFeaturePieces;
import net.minecraft.world.gen.structure.StructureBoundingBox;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Actual 1.11.2 DesertPyramid block-volume, metadata, and loot-seed oracle. */
public final class ScatteredGolden {
    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> blocks =
            new HashMap<BlockPos, IBlockState>();
        final Map<BlockPos, TileEntity> tiles =
            new HashMap<BlockPos, TileEntity>();

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "scattered-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public IBlockState getBlockState(BlockPos pos) {
            IBlockState state = blocks.get(pos);
            if (state != null) return state;
            return pos.getY() < 64
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        public boolean setBlockState(BlockPos pos, IBlockState state, int flags) {
            BlockPos key = pos.toImmutable();
            blocks.put(key, state);
            if (state.getBlock() == Blocks.CHEST) {
                TileEntityChest chest = new TileEntityChest();
                chest.setWorld(this);
                chest.setPos(key);
                tiles.put(key, chest);
            } else if (state.getBlock() == Blocks.AIR) {
                tiles.remove(key);
            }
            return true;
        }
        public TileEntity getTileEntity(BlockPos pos) {
            return tiles.get(pos);
        }
        public BlockPos getTopSolidOrLiquidBlock(BlockPos pos) {
            return new BlockPos(pos.getX(), 64, pos.getZ());
        }
        public void notifyNeighborsOfStateChange(
                BlockPos pos, Block block, boolean updateObservers) {}
        public void updateComparatorOutputLevel(BlockPos pos, Block block) {}
    }

    private static long chestSeed(TileEntity tile) {
        NBTTagCompound nbt = new NBTTagCompound();
        tile.writeToNBT(nbt);
        return nbt.getLong("LootTableSeed");
    }

    private static void run(long seed) {
        MemoryWorld world = new MemoryWorld();
        Random random = new Random(seed);
        ComponentScatteredFeaturePieces.DesertPyramid pyramid =
            new ComponentScatteredFeaturePieces.DesertPyramid(random, 0, 0);
        StructureBoundingBox bounds =
            new StructureBoundingBox(-64, 0, -64, 64, 255, 64);
        if (!pyramid.addComponentParts(world, random, bounds))
            throw new AssertionError("pyramid placement failed");
        System.out.printf("O %d %d%n", seed,
            pyramid.getCoordBaseMode().getIndex());
        for (int y = 50; y <= 74; ++y)
            for (int z = 0; z <= 20; ++z)
                for (int x = 0; x <= 20; ++x) {
                    IBlockState state = world.getBlockState(
                        new BlockPos(x, y, z));
                    int id = Block.getIdFromBlock(state.getBlock());
                    int meta = state.getBlock().getMetaFromState(state);
                    System.out.printf("%04x%n", (id << 4) | (meta & 15));
                }
        List<Map.Entry<BlockPos, TileEntity>> chests =
            new ArrayList<Map.Entry<BlockPos, TileEntity>>(
                world.tiles.entrySet());
        Collections.sort(chests,
            new Comparator<Map.Entry<BlockPos, TileEntity>>() {
                public int compare(Map.Entry<BlockPos, TileEntity> a,
                                   Map.Entry<BlockPos, TileEntity> b) {
                    BlockPos p = a.getKey(), q = b.getKey();
                    if (p.getZ() != q.getZ()) return p.getZ() - q.getZ();
                    if (p.getX() != q.getX()) return p.getX() - q.getX();
                    return p.getY() - q.getY();
                }
            });
        for (Map.Entry<BlockPos, TileEntity> entry : chests) {
            BlockPos pos = entry.getKey();
            System.out.printf("C %d %d %d %d%n",
                pos.getX(), pos.getY(), pos.getZ(), chestSeed(entry.getValue()));
        }
    }

    public static void main(String[] args) {
        Bootstrap.register();
        long[] seeds = {-100000L, -98304L, -94208L, -86016L};
        for (long seed : seeds) run(seed);
    }
}
