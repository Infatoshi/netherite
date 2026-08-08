package qrl;

import com.google.common.base.Predicate;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.concurrent.atomic.AtomicLong;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.monster.EntityIronGolem;
import net.minecraft.init.Blocks;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;
import net.minecraft.village.VillageCollection;

/** Direct 1.11.2 iron-golem attributes, NBT, timers, and attack RNG oracle. */
public final class IronGolemGolden {
    private static final class MemoryWorld extends World {
        int lastStatus;
        int statusCount;

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "iron-golem-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
            this.villageCollectionObj = new VillageCollection(this);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            return pos.getY() <= 0
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        public void setEntityState(Entity entity, byte state) {
            lastStatus = state & 255;
            ++statusCount;
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box) {
            return new ArrayList<T>();
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box,
                Predicate<? super T> filter) {
            return new ArrayList<T>();
        }
    }

    private static long seed48(java.util.Random random) throws Exception {
        Field field = java.util.Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & ((1L << 48) - 1L);
    }

    private static int privateInt(Object value, String name) throws Exception {
        Field field = value.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.getInt(value);
    }

    private static void invokeUpdateAi(EntityIronGolem golem) throws Exception {
        Method method = EntityIronGolem.class.getDeclaredMethod("updateAITasks");
        method.setAccessible(true);
        method.invoke(golem);
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.init.Bootstrap.register();
        MemoryWorld world = new MemoryWorld();
        EntityIronGolem golem = new EntityIronGolem(world);
        System.out.printf("A %08x %08x %08x %016x %016x %016x%n",
            Float.floatToRawIntBits(golem.width),
            Float.floatToRawIntBits(golem.height),
            Float.floatToRawIntBits(golem.getEyeHeight()),
            Double.doubleToRawLongBits(golem.getMaxHealth()),
            Double.doubleToRawLongBits(golem.getEntityAttribute(
                SharedMonsterAttributes.MOVEMENT_SPEED).getAttributeValue()),
            Double.doubleToRawLongBits(golem.getEntityAttribute(
                SharedMonsterAttributes.KNOCKBACK_RESISTANCE).getAttributeValue()));

        golem.setPlayerCreated(true);
        NBTTagCompound nbt = new NBTTagCompound();
        golem.writeEntityToNBT(nbt);
        EntityIronGolem loaded = new EntityIronGolem(world);
        loaded.readEntityFromNBT(nbt);
        System.out.printf("N %d%n", loaded.isPlayerCreated() ? 1 : 0);

        golem.getRNG().setSeed(3107L);
        invokeUpdateAi(golem);
        System.out.printf("H %d%n", privateInt(golem, "homeCheckTimer"));

        golem.handleStatusUpdate((byte)11);
        System.out.printf("R %d %d%n",
            golem.getAttackTimer(), golem.getHoldRoseTick());

        EntityIronGolem target = new EntityIronGolem(world);
        target.setHealth(100.0F);
        golem.getRNG().setSeed(3107L);
        for (int hit = 0; hit < 6; ++hit) {
            float before = target.getHealth();
            double beforeY = target.motionY;
            boolean accepted = golem.attackEntityAsMob(target);
            System.out.printf("T %d %08x %016x %d %d %d%n",
                hit, Float.floatToRawIntBits(target.getHealth()),
                Double.doubleToRawLongBits(target.motionY),
                accepted ? 1 : 0, golem.getAttackTimer(),
                world.lastStatus);
            if (accepted && target.getHealth() == before
                    && target.motionY == beforeY)
                throw new AssertionError("accepted attack had no effect");
        }
        System.out.printf("C %d %d%n", world.statusCount, seed48(golem.getRNG()));
    }
}
