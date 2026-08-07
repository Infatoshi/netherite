package qrl;

import java.util.HashMap;
import java.util.Map;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityFlowerPot;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.DifficultyInstance;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.gen.structure.ComponentScatteredFeaturePieces;
import net.minecraft.world.gen.structure.StructureBoundingBox;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Actual 1.11.2 SwampHut block volume, metadata, and witch-position oracle. */
public final class ScatteredSwampGolden {
    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> blocks =
            new HashMap<BlockPos, IBlockState>();
        final Map<BlockPos, TileEntity> tiles =
            new HashMap<BlockPos, TileEntity>();
        Entity spawned;

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "swamp-oracle"),
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
            return pos.getY() < 64
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        public boolean setBlockState(BlockPos pos, IBlockState state, int flags) {
            BlockPos key = pos.toImmutable();
            IBlockState old = getBlockState(pos);
            blocks.put(key, state);
            if (old.getBlock() != state.getBlock()) {
                tiles.remove(key);
                if (state.getBlock().hasTileEntity(state)) {
                    TileEntity tile = state.getBlock().createTileEntity(this, state);
                    if (tile != null) {
                        tile.setWorld(this);
                        tile.setPos(key);
                        tiles.put(key, tile);
                    }
                }
            }
            return true;
        }
        public TileEntity getTileEntity(BlockPos pos) { return tiles.get(pos); }
        public BlockPos getTopSolidOrLiquidBlock(BlockPos pos) {
            return new BlockPos(pos.getX(), 64, pos.getZ());
        }
        public DifficultyInstance getDifficultyForLocation(BlockPos pos) {
            return new DifficultyInstance(EnumDifficulty.NORMAL, 0L, 0L, 0.0F);
        }
        public boolean spawnEntity(Entity entity) {
            spawned = entity;
            return true;
        }
        public void notifyNeighborsOfStateChange(
                BlockPos pos, Block block, boolean updateObservers) {}
        public void updateComparatorOutputLevel(BlockPos pos, Block block) {}
    }

    private static void run(long seed) {
        MemoryWorld world = new MemoryWorld();
        Random random = new Random(seed);
        ComponentScatteredFeaturePieces.SwampHut hut =
            new ComponentScatteredFeaturePieces.SwampHut(random, 0, 0);
        StructureBoundingBox bounds =
            new StructureBoundingBox(-64, 0, -64, 64, 255, 64);
        if (!hut.addComponentParts(world, random, bounds))
            throw new AssertionError("swamp hut placement failed");
        System.out.printf("O %d %d%n", seed, hut.getCoordBaseMode().getIndex());
        for (int y = 55; y <= 75; ++y)
            for (int z = 0; z <= 8; ++z)
                for (int x = 0; x <= 8; ++x) {
                    IBlockState state = world.getBlockState(new BlockPos(x, y, z));
                    int id = Block.getIdFromBlock(state.getBlock());
                    int meta = state.getBlock().getMetaFromState(state);
                    System.out.printf("%04x%n", (id << 4) | (meta & 15));
                }
        if (world.spawned == null) throw new AssertionError("witch not spawned");
        System.out.printf("W %.1f %.1f %.1f%n",
            world.spawned.posX, world.spawned.posY, world.spawned.posZ);
        for (TileEntity tile : world.tiles.values())
            if (tile instanceof TileEntityFlowerPot) {
                ItemStack flower = ((TileEntityFlowerPot)tile).getFlowerItemStack();
                System.out.printf("P %d %d%n", flower.isEmpty() ? 0
                    : Item.getIdFromItem(flower.getItem()),
                    flower.isEmpty() ? 0 : flower.getMetadata());
            }
    }

    public static void main(String[] args) {
        Bootstrap.register();
        long[] seeds = {-100000L, -98304L, -94208L, -86016L};
        for (long seed : seeds) run(seed);
    }
}
