package qrl;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.inventory.InventoryHelper;
import net.minecraft.item.ItemStack;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Exact InventoryHelper.spawnItemStack oracle over real Minecraft 1.11.2. */
public final class InventoryHelperGolden {
    private static final long MULT = 0x5deece66dL;
    private static final long MASK = (1L << 48) - 1L;

    private static final class MemoryWorld extends World {
        final List<EntityItem> spawned = new ArrayList<EntityItem>();

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "inventory-helper-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            return Blocks.AIR.getDefaultState();
        }
        public boolean spawnEntity(Entity entity) {
            spawned.add((EntityItem)entity);
            return true;
        }
    }

    private static Random inventoryRandom() throws Exception {
        Field field = InventoryHelper.class.getDeclaredField("RANDOM");
        field.setAccessible(true);
        return (Random)field.get(null);
    }

    private static Random mathRandom() throws Exception {
        Class<?> holder =
            Class.forName("java.lang.Math$RandomNumberGeneratorHolder");
        Field field = holder.getDeclaredField("randomNumberGenerator");
        field.setAccessible(true);
        return (Random)field.get(null);
    }

    private static long rawSeed(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & MASK;
    }

    private static void setState(
            Random random, long seed48, boolean have, double next)
            throws Exception {
        random.setSeed(seed48 ^ MULT);
        Field haveField =
            Random.class.getDeclaredField("haveNextNextGaussian");
        Field nextField = Random.class.getDeclaredField("nextNextGaussian");
        haveField.setAccessible(true);
        nextField.setAccessible(true);
        haveField.setBoolean(random, have);
        nextField.setDouble(random, next);
    }

    private static boolean haveGaussian(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("haveNextNextGaussian");
        field.setAccessible(true);
        return field.getBoolean(random);
    }

    private static double nextGaussian(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("nextNextGaussian");
        field.setAccessible(true);
        return field.getDouble(random);
    }

    private static String bits(double value) {
        return String.format("%016x", Double.doubleToRawLongBits(value));
    }

    private static void run(String tag, boolean have, double next)
            throws Exception {
        final long helperSeed = 0x123456789abCL;
        final long mathSeed = 0x0fedcba98765L;
        Random helper = inventoryRandom();
        Random math = mathRandom();
        setState(helper, helperSeed, have, next);
        setState(math, mathSeed, false, 0.0D);
        MemoryWorld world = new MemoryWorld();
        InventoryHelper.spawnItemStack(
            world, 9.0D, 78.0D, 8.0D,
            new ItemStack(Blocks.STONE, 64, 0));
        for (EntityItem entity : world.spawned) {
            System.out.printf(
                "E %s %d %s %s %s %s %s %s%n",
                tag, entity.getEntityItem().getCount(),
                bits(entity.posX), bits(entity.posY), bits(entity.posZ),
                bits(entity.motionX), bits(entity.motionY),
                bits(entity.motionZ));
        }
        System.out.printf(
            "R %s %012x %d %s %012x%n",
            tag, rawSeed(helper), haveGaussian(helper) ? 1 : 0,
            bits(nextGaussian(helper)), rawSeed(math));
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        run("A", false, 0.0D);
        run("B", true, -0.75D);
    }
}
