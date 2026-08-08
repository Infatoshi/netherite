package qrl;

import com.google.common.base.Predicate;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.ai.EntityAIFollowGolem;
import net.minecraft.entity.monster.EntityIronGolem;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.init.Blocks;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.util.math.BlockPos;
import net.minecraft.village.VillageCollection;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Direct EntityAIFollowGolem selection, RNG, and rose-transfer oracle. */
public final class VillagerFollowGolemGolden {
    private static final class MemoryWorld extends World {
        final List<EntityIronGolem> golems =
            new ArrayList<EntityIronGolem>();
        int lastStatus = -1;

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "villager-golem-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
            this.villageCollectionObj = new VillageCollection(this);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public boolean isDaytime() { return true; }
        public IBlockState getBlockState(BlockPos pos) {
            return pos.getY() <= 63
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        public void setEntityState(Entity entity, byte state) {
            lastStatus = state & 255;
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box) {
            List<T> result = new ArrayList<T>();
            if (type != EntityIronGolem.class) return result;
            for (EntityIronGolem golem : golems)
                if (!golem.isDead
                        && golem.getEntityBoundingBox().intersectsWith(box))
                    result.add(type.cast(golem));
            return result;
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box,
                Predicate<? super T> filter) {
            List<T> result = getEntitiesWithinAABB(type, box);
            for (int i = result.size() - 1; i >= 0; --i)
                if (filter != null && !filter.apply(result.get(i)))
                    result.remove(i);
            return result;
        }
    }

    private static Field field(String name) throws Exception {
        Field result = EntityAIFollowGolem.class.getDeclaredField(name);
        result.setAccessible(true);
        return result;
    }

    private static long seed48(java.util.Random random) throws Exception {
        Field field = java.util.Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & ((1L << 48) - 1L);
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.init.Bootstrap.register();
        MemoryWorld world = new MemoryWorld();
        EntityVillager villager = new EntityVillager(world, 0);
        villager.setPosition(0.5D, 64.0D, 0.5D);
        villager.setGrowingAge(-100);
        villager.getRNG().setSeed(3107L);

        EntityIronGolem first = new EntityIronGolem(world);
        EntityIronGolem second = new EntityIronGolem(world);
        first.setPosition(1.5D, 64.0D, 0.5D);
        second.setPosition(1.5D, 64.0D, 0.5D);
        first.setHoldingRose(false);
        second.setHoldingRose(true);
        world.golems.add(first);
        world.golems.add(second);

        EntityAIFollowGolem task = new EntityAIFollowGolem(villager);
        boolean started = task.shouldExecute();
        if (started) task.startExecuting();
        int take = field("takeGolemRoseTick").getInt(task);
        EntityIronGolem selected =
            (EntityIronGolem)field("theGolem").get(task);
        System.out.printf("S %d %d %d %d%n",
            started ? 1 : 0, selected == second ? 1 : 0,
            take, seed48(villager.getRNG()));

        field("tookGolemRose").setBoolean(task, true);
        task.updateTask();
        System.out.printf("R %d %d %d%n",
            second.getHoldRoseTick(), world.lastStatus,
            task.continueExecuting() ? 1 : 0);
        task.resetTask();
        System.out.printf("X %d%n",
            field("theGolem").get(task) == null ? 1 : 0);
    }
}
