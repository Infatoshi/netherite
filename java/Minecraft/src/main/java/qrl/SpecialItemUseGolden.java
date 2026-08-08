package qrl;

import com.mojang.authlib.GameProfile;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityBanner;
import net.minecraft.tileentity.TileEntitySign;
import net.minecraft.tileentity.TileEntitySkull;
import net.minecraft.util.EnumActionResult;
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

/** Direct 1.11.2 oracle for non-ItemBlock decorative placement callbacks. */
public final class SpecialItemUseGolden {
    private static final BlockPos TARGET = BlockPos.ORIGIN;

    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> states =
            new HashMap<BlockPos, IBlockState>();
        final Map<BlockPos, TileEntity> tiles =
            new HashMap<BlockPos, TileEntity>();

        MemoryWorld() {
            super(new SaveHandlerMP(), new WorldInfo(new WorldSettings(
                0L, GameType.SURVIVAL, true, false, WorldType.DEFAULT),
                "special-item-oracle"), new WorldProviderSurface(),
                new Profiler(), false);
            provider.setWorld(this);
            for (int x = -2; x <= 2; ++x)
                for (int z = -2; z <= 2; ++z)
                    for (int y = -1; y <= 0; ++y)
                        states.put(new BlockPos(x, y, z),
                            Blocks.STONE.getDefaultState());
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
        public TileEntity getTileEntity(BlockPos pos) { return tiles.get(pos); }
        public void markChunkDirty(BlockPos pos, TileEntity tile) {}
        public boolean setBlockState(
                BlockPos pos, IBlockState state, int flags) {
            BlockPos key = pos.toImmutable();
            states.put(key, state);
            TileEntity tile = state.getBlock().createTileEntity(this, state);
            if (tile != null) {
                tile.setWorld(this);
                tile.setPos(key);
                tiles.put(key, tile);
            } else {
                tiles.remove(key);
            }
            return true;
        }
        public void notifyNeighborsRespectDebug(
                BlockPos pos, Block block, boolean updateObservers) {}
    }

    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world, float yaw) {
            super(world, new GameProfile(
                new UUID(0x5350454349414cL, 0x4954454d555345L),
                "special-item"));
            rotationYaw = yaw;
            setPosition(0.5D, 1.0D, 2.5D);
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
    }

    private static void run(int itemId, int itemMeta,
            int faceIndex, int yawIndex) {
        MemoryWorld world = new MemoryWorld();
        BlockPos target = faceIndex >= 2 ? new BlockPos(0, 1, 0) : TARGET;
        if (faceIndex >= 2)
            world.states.put(target, Blocks.STONE.getDefaultState());
        TestPlayer player = new TestPlayer(world, yawIndex * 90.0F);
        Item item = Item.getItemById(itemId);
        ItemStack held = new ItemStack(item, 2, itemMeta);
        player.setHeldItem(EnumHand.MAIN_HAND, held);
        EnumFacing face = EnumFacing.VALUES[faceIndex];
        EnumActionResult result = item.onItemUse(
            player, world, target, EnumHand.MAIN_HAND, face,
            0.25F, 0.75F, 0.25F);
        int placedBlock = 0, placedMeta = 0, tileKind = 0, aux0 = 0;
        BlockPos placed = target.offset(face);
        IBlockState placedState = world.getBlockState(placed);
        int id = Block.getIdFromBlock(placedState.getBlock());
        if (id == 26 || id == 63 || id == 68 || id == 144
                || id == 176 || id == 177) {
            placedBlock = id;
            placedMeta = placedState.getBlock().getMetaFromState(placedState);
            TileEntity tile = world.tiles.get(placed);
            if (tile instanceof TileEntitySign) tileKind = 1;
            else if (tile instanceof TileEntityBanner) {
                tileKind = 2;
                aux0 = ((TileEntityBanner)tile).getColorList()
                    .get(0).getDyeDamage();
            } else if (tile instanceof TileEntitySkull) {
                tileKind = 3;
                aux0 = ((TileEntitySkull)tile).getSkullType() * 16
                    + ((TileEntitySkull)tile).getSkullRotation();
            }
        }
        System.out.printf("S %d %d %d %d %s %d %d %d %d %d%n",
            itemId, itemMeta, faceIndex, yawIndex, result.name(),
            held.getCount(), placedBlock, placedMeta, tileKind, aux0);
    }

    public static void main(String[] args) {
        Bootstrap.register();
        int[] items = {323, 355, 397, 425};
        for (int item : items) {
            int variants = item == 397 ? 6 : item == 425 ? 16 : 1;
            for (int meta = 0; meta < variants; ++meta)
                for (int face = 0; face < 6; ++face)
                    for (int yaw = 0; yaw < 4; ++yaw)
                        run(item, meta, face, yaw);
        }
    }
}
