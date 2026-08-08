package qrl;

import net.minecraft.block.Block;
import net.minecraft.block.BlockFurnace;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.util.EnumFacing;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Adjacent save/reload boundaries through the real 1.11.2 furnace tick. */
public final class FurnaceBoundaryGolden {
    private static final BlockPos POS = new BlockPos(4, 70, -3);

    private static final class MemoryWorld extends World {
        IBlockState state;
        TileEntity tile;

        MemoryWorld(boolean lit) {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "furnace-boundary-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
            this.state = (lit ? Blocks.LIT_FURNACE : Blocks.FURNACE)
                .getDefaultState().withProperty(
                    BlockFurnace.FACING, EnumFacing.NORTH);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            return POS.equals(pos) ? state : Blocks.AIR.getDefaultState();
        }
        public boolean setBlockState(
                BlockPos pos, IBlockState next, int flags) {
            if (!POS.equals(pos)) return false;
            state = next;
            return true;
        }
        public TileEntity getTileEntity(BlockPos pos) {
            return POS.equals(pos) ? tile : null;
        }
        public void setTileEntity(BlockPos pos, TileEntity value) {
            if (POS.equals(pos)) tile = value;
        }
        public void markChunkDirty(BlockPos pos, TileEntity value) {}
        public void updateComparatorOutputLevel(BlockPos pos, Block block) {}
    }

    private static ItemStack stack(int item, int count, int meta) {
        if (item == 0 || count == 0) return ItemStack.EMPTY;
        return new ItemStack(Item.getItemById(item), count, meta);
    }

    private static String encoded(ItemStack stack) {
        if (stack.isEmpty()) return "0:0:0";
        return Item.getIdFromItem(stack.getItem()) + ":"
            + stack.getCount() + ":" + stack.getMetadata();
    }

    private static void print(
            String tag, String phase, MemoryWorld world,
            TileEntityFurnace furnace) {
        IBlockState state = world.state;
        System.out.printf("%s %s %d %d %d %d %s %s %s %d:%d %s%n",
            tag, phase,
            furnace.getField(0), furnace.getField(1),
            furnace.getField(2), furnace.getField(3),
            encoded(furnace.getStackInSlot(0)),
            encoded(furnace.getStackInSlot(1)),
            encoded(furnace.getStackInSlot(2)),
            Block.getIdFromBlock(state.getBlock()),
            state.getBlock().getMetaFromState(state),
            furnace.hasCustomName() ? furnace.getName() : "-");
    }

    private static void run(
            String tag, int burn, int cook,
            int inputItem, int inputCount, int inputMeta,
            int fuelItem, int fuelCount, int fuelMeta,
            int outputItem, int outputCount, int outputMeta) {
        TileEntityFurnace source = new TileEntityFurnace();
        source.setInventorySlotContents(
            0, stack(inputItem, inputCount, inputMeta));
        source.setInventorySlotContents(
            1, stack(fuelItem, fuelCount, fuelMeta));
        source.setInventorySlotContents(
            2, stack(outputItem, outputCount, outputMeta));
        source.setField(0, burn);
        source.setField(1, 12345);
        source.setField(2, cook);
        source.setField(3, 200);
        source.setCustomInventoryName("boundary");
        NBTTagCompound saved = source.writeToNBT(new NBTTagCompound());

        TileEntityFurnace loaded = new TileEntityFurnace();
        loaded.readFromNBT(saved);
        MemoryWorld world = new MemoryWorld(burn > 0);
        loaded.setWorld(world);
        loaded.setPos(POS);
        world.tile = loaded;
        print(tag, "B", world, loaded);
        loaded.update();
        print(tag, "A", world, loaded);
    }

    public static void main(String[] args) {
        Bootstrap.register();
        run("L0", 0, 0, 15, 1, 0, 263, 1, 0, 0, 0, 0);
        run("L1", 1, 17, 15, 1, 0, 263, 1, 0, 0, 0, 0);
        run("C1", 2, 199, 15, 1, 0, 0, 0, 0, 0, 0, 0);
        run("B1", 2, 199, 15, 1, 0, 0, 0, 0, 265, 64, 0);
        run("D0", 0, 5, 15, 1, 0, 0, 0, 0, 0, 0, 0);
        run("I0", 0, 5, 264, 1, 0, 263, 1, 0, 0, 0, 0);
        run("Z1", 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        run("W1", 2, 199, 19, 1, 1, 325, 1, 0, 0, 0, 0);
    }
}
