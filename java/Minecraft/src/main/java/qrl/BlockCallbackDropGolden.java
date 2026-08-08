package qrl;

import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Executes the four remaining drop callbacks with tile drops disabled. */
public final class BlockCallbackDropGolden {
    private BlockCallbackDropGolden() { }

    private static final class MemoryWorld extends World {
        MemoryWorld() {
            super(new SaveHandlerMP(), new WorldInfo(new WorldSettings(
                0L, GameType.SURVIVAL, true, false, WorldType.DEFAULT),
                "callback-drop-oracle"), new WorldProviderSurface(),
                new Profiler(), false);
            provider.setWorld(this);
            getGameRules().setOrCreateGameRule("doTileDrops", "false");
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public TileEntity getTileEntity(BlockPos pos) { return null; }
    }

    private static void run(String owner, Block block, int meta) {
        MemoryWorld world = new MemoryWorld();
        world.rand.setSeed(1234L);
        IBlockState state = block.getStateFromMeta(meta);
        block.dropBlockAsItemWithChance(
            world, BlockPos.ORIGIN, state, 1.0F, 0);
        System.out.printf("D %s %d %d%n", owner,
            Block.getIdFromBlock(block), world.rand.nextLong());
    }

    public static void main(String[] args) {
        Bootstrap.register();
        run("BlockJukebox", Blocks.JUKEBOX, 0);
        run("BlockMobSpawner", Blocks.MOB_SPAWNER, 0);
        run("BlockPistonMoving", Blocks.PISTON_EXTENSION, 0);
        run("BlockSilverfish", Blocks.MONSTER_EGG, 0);
    }
}
