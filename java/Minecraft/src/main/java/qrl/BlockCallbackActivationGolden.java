package qrl;

import com.mojang.authlib.GameProfile;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.EnumFacing;
import net.minecraft.util.EnumHand;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Direct activation oracle for callbacks whose blocks cannot be ray-hit. */
public final class BlockCallbackActivationGolden {
    private BlockCallbackActivationGolden() { }

    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> states =
            new HashMap<BlockPos, IBlockState>();
        MemoryWorld() {
            super(new SaveHandlerMP(), new WorldInfo(new WorldSettings(
                0L, GameType.SURVIVAL, true, false, WorldType.DEFAULT),
                "callback-activation-oracle"), new WorldProviderSurface(),
                new Profiler(), false);
            provider.setWorld(this);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            IBlockState state = states.get(pos);
            return state == null ? Blocks.AIR.getDefaultState() : state;
        }
        public boolean setBlockState(BlockPos pos, IBlockState state, int flags) {
            if (state.getBlock() == Blocks.AIR) states.remove(pos);
            else states.put(pos.toImmutable(), state);
            return true;
        }
        public boolean setBlockToAir(BlockPos pos) {
            states.remove(pos);
            return true;
        }
        public TileEntity getTileEntity(BlockPos pos) { return null; }
    }

    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(
                new UUID(0x424c4f434bL, 0x4143544956415445L), "callback"));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
        public void onItemPickup(Entity entity, int count) { }
    }

    public static void main(String[] args) {
        Bootstrap.register();
        BlockPos source = new BlockPos(8, 200, 6);
        MemoryWorld dragon = new MemoryWorld();
        dragon.states.put(source, Blocks.DRAGON_EGG.getDefaultState());
        dragon.rand.setSeed(1234L);
        boolean dragonResult = Blocks.DRAGON_EGG.onBlockActivated(
            dragon, source, dragon.getBlockState(source),
            new TestPlayer(dragon), EnumHand.MAIN_HAND, EnumFacing.UP,
            0.5F, 0.5F, 0.5F);
        BlockPos destination = null;
        for (Map.Entry<BlockPos, IBlockState> entry : dragon.states.entrySet())
            if (entry.getValue().getBlock() == Blocks.DRAGON_EGG)
                destination = entry.getKey();
        if (destination == null) throw new AssertionError("dragon egg vanished");
        System.out.printf("A BlockDragonEgg %s %d %d %d %d %d%n",
            dragonResult, destination.getX(), destination.getY(),
            destination.getZ(), dragon.getBlockState(source).getBlock() == Blocks.AIR ? 0 : 1,
            dragon.rand.nextLong());

        MemoryWorld moving = new MemoryWorld();
        moving.states.put(source, Blocks.PISTON_EXTENSION.getDefaultState());
        boolean movingResult = Blocks.PISTON_EXTENSION.onBlockActivated(
            moving, source, moving.getBlockState(source),
            new TestPlayer(moving), EnumHand.MAIN_HAND, EnumFacing.UP,
            0.5F, 0.5F, 0.5F);
        System.out.printf("A BlockPistonMoving %s %d%n", movingResult,
            moving.getBlockState(source).getBlock() == Blocks.AIR ? 0 : 1);
    }
}
