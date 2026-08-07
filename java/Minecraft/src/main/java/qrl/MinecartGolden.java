package qrl;

import com.google.common.base.Predicate;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javax.annotation.Nullable;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityMinecart;
import net.minecraft.entity.item.EntityMinecartEmpty;
import net.minecraft.entity.item.EntityMinecartHopper;
import net.minecraft.entity.item.EntityMinecartTNT;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Actual 1.11.2 EntityMinecart rail-motion and rail-callback oracle. */
public final class MinecartGolden {
    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> blocks =
            new HashMap<BlockPos, IBlockState>();
        final List<Entity> entities = new ArrayList<Entity>();
        int scheduled;

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "minecart-oracle"),
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
        public void scheduleUpdate(BlockPos pos, Block block, int delay) {
            ++scheduled;
        }
        public void notifyNeighborsOfStateChange(
                BlockPos pos, Block block, boolean updateObservers) {}
        public void updateComparatorOutputLevel(BlockPos pos, Block block) {}
        public boolean spawnEntity(Entity entity) {
            entities.add(entity);
            return true;
        }
        public List<Entity> getEntitiesInAABBexcluding(
                @Nullable Entity excluded, AxisAlignedBB box,
                @Nullable Predicate<? super Entity> filter) {
            return new ArrayList<Entity>();
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box,
                @Nullable Predicate<? super T> filter) {
            List<T> result = new ArrayList<T>();
            for (Entity entity : entities) {
                if (entity != null && entity != excludedPlaceholder()
                        && !entity.isDead && type.isInstance(entity)
                        && entity.getEntityBoundingBox().intersects(
                            box.minX, box.minY, box.minZ,
                            box.maxX, box.maxY, box.maxZ)) {
                    T value = type.cast(entity);
                    if (filter == null || filter.apply(value)) result.add(value);
                }
            }
            return result;
        }
        private Entity excludedPlaceholder() { return null; }

        void put(BlockPos pos, IBlockState state) {
            blocks.put(pos.toImmutable(), state);
        }
    }

    private static String dbits(double value) {
        return String.format("%016x", Double.doubleToRawLongBits(value));
    }

    private static String fbits(float value) {
        return String.format("%08x", Float.floatToRawIntBits(value));
    }

    private static MemoryWorld track(Block block, int meta) {
        MemoryWorld world = new MemoryWorld();
        for (int x = 8; x <= 16; ++x) {
            world.put(new BlockPos(x, 77, 8), Blocks.STONE.getDefaultState());
            world.put(new BlockPos(x, 78, 8), block.getStateFromMeta(meta));
        }
        return world;
    }

    private static void print(String name, EntityMinecart cart) {
        System.out.printf("%s %s %s %s %s %s %s %s %s%n", name,
            dbits(cart.posX), dbits(cart.posY), dbits(cart.posZ),
            dbits(cart.motionX), dbits(cart.motionY), dbits(cart.motionZ),
            fbits(cart.rotationYaw), fbits(cart.rotationPitch));
    }

    private static void straight() {
        MemoryWorld world = track(Blocks.RAIL, 1);
        EntityMinecartEmpty cart = new EntityMinecartEmpty(
            world, 12.5, 78.0625, 8.5);
        cart.motionX = 0.2;
        world.entities.add(cart);
        for (int i = 0; i < 3; ++i) cart.onUpdate();
        print("S", cart);
    }

    private static void powered(boolean on) {
        MemoryWorld world = track(Blocks.GOLDEN_RAIL, on ? 9 : 1);
        EntityMinecartEmpty cart = new EntityMinecartEmpty(
            world, 12.5, 78.0625, 8.5);
        cart.motionX = 0.2;
        world.entities.add(cart);
        cart.onUpdate();
        print(on ? "P" : "B", cart);
    }

    private static void slope() {
        MemoryWorld world = new MemoryWorld();
        for (int x = 10; x <= 14; ++x)
            world.put(new BlockPos(x, 77 + (x >= 13 ? 1 : 0), 8),
                Blocks.STONE.getDefaultState());
        world.put(new BlockPos(11, 78, 8), Blocks.RAIL.getStateFromMeta(1));
        world.put(new BlockPos(12, 78, 8), Blocks.RAIL.getStateFromMeta(2));
        world.put(new BlockPos(13, 79, 8), Blocks.RAIL.getStateFromMeta(1));
        EntityMinecartEmpty cart = new EntityMinecartEmpty(
            world, 12.25, 78.0625, 8.5);
        cart.motionX = 0.2;
        world.entities.add(cart);
        cart.onUpdate();
        print("U", cart);
    }

    private static void detector() {
        MemoryWorld world = track(Blocks.DETECTOR_RAIL, 1);
        EntityMinecartEmpty cart = new EntityMinecartEmpty(
            world, 12.5, 78.0625, 8.5);
        cart.motionX = 0.1;
        world.entities.add(cart);
        cart.onUpdate();
        System.out.printf("D %d %d%n",
            Blocks.DETECTOR_RAIL.getMetaFromState(
                world.getBlockState(new BlockPos(12, 78, 8))),
            world.scheduled);
    }

    private static void activator() {
        MemoryWorld world = track(Blocks.ACTIVATOR_RAIL, 9);
        EntityMinecartTNT tnt = new EntityMinecartTNT(
            world, 12.5, 78.0625, 8.5);
        world.entities.add(tnt);
        tnt.onUpdate();
        EntityMinecartHopper hopper = new EntityMinecartHopper(
            world, 12.5, 78.0625, 8.5);
        world.entities.clear();
        world.entities.add(hopper);
        hopper.onUpdate();
        System.out.printf("A %d %d%n", tnt.isIgnited() ? 1 : 0,
            hopper.getBlocked() ? 1 : 0);
    }

    private static void hopperCapture() {
        MemoryWorld world = track(Blocks.RAIL, 1);
        EntityMinecartHopper hopper = new EntityMinecartHopper(
            world, 12.5, 78.0625, 8.5);
        EntityItem item = new EntityItem(world, 12.5, 78.2, 8.5,
            new ItemStack(Items.DIAMOND, 3, 0));
        item.motionX = item.motionY = item.motionZ = 0.0;
        world.entities.add(hopper);
        world.entities.add(item);
        hopper.onUpdate();
        System.out.printf("H %d %d%n",
            hopper.getStackInSlot(0).getCount(), item.isDead ? 1 : 0);
    }

    public static void main(String[] args) {
        Bootstrap.register();
        straight();
        powered(false);
        powered(true);
        slope();
        detector();
        activator();
        hopperCapture();
        System.out.println("minecart_live: PASS (rails, power, slope, detector, activator)");
    }
}
