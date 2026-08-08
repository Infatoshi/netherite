package qrl;

import com.google.common.base.Predicate;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.ai.EntityAIVillagerMate;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.inventory.InventoryBasic;
import net.minecraft.item.ItemStack;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.util.math.BlockPos;
import net.minecraft.village.Village;
import net.minecraft.village.VillageCollection;
import net.minecraft.village.VillageDoorInfo;
import net.minecraft.world.GameType;
import net.minecraft.world.DifficultyInstance;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Direct EntityAIVillagerMate task and child-state oracle. */
public final class VillagerMateGolden {
    private static final class MemoryWorld extends World {
        final List<EntityVillager> villagers =
            new ArrayList<EntityVillager>();
        EntityVillager child;
        int status12;
        int status18;

        MemoryWorld() throws Exception {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(1234L, GameType.SURVIVAL,
                    true, false, WorldType.DEFAULT), "villager-mate-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
            this.villageCollectionObj = new VillageCollection(this);
            Village village = new Village(this);
            for (int door = 0; door < 21; ++door)
                village.addVillageDoorInfo(new VillageDoorInfo(
                    new BlockPos(door - 10, 64, 10), 2, 0, 1));
            Field list = VillageCollection.class
                .getDeclaredField("villageList");
            list.setAccessible(true);
            @SuppressWarnings("unchecked")
            List<Village> villages = (List<Village>)list.get(
                this.villageCollectionObj);
            villages.add(village);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public DifficultyInstance getDifficultyForLocation(BlockPos pos) {
            return new DifficultyInstance(
                EnumDifficulty.NORMAL, 0L, 0L, 0.0F);
        }
        public IBlockState getBlockState(BlockPos pos) {
            return pos.getY() <= 63
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box) {
            List<T> result = new ArrayList<T>();
            if (type != EntityVillager.class) return result;
            for (EntityVillager villager : villagers)
                if (!villager.isDead
                        && villager.getEntityBoundingBox().intersectsWith(box))
                    result.add(type.cast(villager));
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
        public boolean spawnEntity(Entity entity) {
            if (!(entity instanceof EntityVillager)) return false;
            child = (EntityVillager)entity;
            villagers.add(child);
            return true;
        }
        public void setEntityState(Entity entity, byte state) {
            if (state == 12) ++status12;
            if (state == 18) ++status18;
        }
    }

    private static InventoryBasic inventory(EntityVillager villager)
            throws Exception {
        Field field = EntityVillager.class
            .getDeclaredField("villagerInventory");
        field.setAccessible(true);
        return (InventoryBasic)field.get(villager);
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.init.Bootstrap.register();
        MemoryWorld world = new MemoryWorld();
        EntityVillager first = new EntityVillager(world, 0);
        EntityVillager second = new EntityVillager(world, 1);
        first.setPosition(0.5D, 64.0D, 0.5D);
        second.setPosition(1.5D, 64.0D, 0.5D);
        first.setGrowingAge(0);
        second.setGrowingAge(0);
        inventory(first).setInventorySlotContents(
            0, new ItemStack(Items.BREAD, 3));
        inventory(second).setInventorySlotContents(
            0, new ItemStack(Items.BREAD, 3));
        first.getRNG().setSeed(61L);
        second.getRNG().setSeed(61L);
        world.rand.setSeed(1234L);
        world.villagers.add(first);
        world.villagers.add(second);

        EntityAIVillagerMate firstTask =
            new EntityAIVillagerMate(first);
        EntityAIVillagerMate secondTask =
            new EntityAIVillagerMate(second);
        boolean firstStarted = firstTask.shouldExecute();
        if (firstStarted) firstTask.startExecuting();
        boolean secondStarted = secondTask.shouldExecute();
        if (secondStarted) secondTask.startExecuting();
        System.out.printf("S %d %d %d %d %d %d %d %d%n",
            firstStarted ? 1 : 0, secondStarted ? 1 : 0,
            first.getIsWillingToMate(false) ? 1 : 0,
            second.getIsWillingToMate(false) ? 1 : 0,
            first.isMating() ? 1 : 0, second.isMating() ? 1 : 0,
            inventory(first).getStackInSlot(0).getCount(),
            inventory(second).getStackInSlot(0).getCount());

        for (int tick = 0; tick < 300; ++tick) {
            if (firstStarted && firstTask.continueExecuting())
                firstTask.updateTask();
            else if (firstStarted) {
                firstTask.resetTask();
                firstStarted = false;
            }
            if (secondStarted && secondTask.continueExecuting())
                secondTask.updateTask();
            else if (secondStarted) {
                secondTask.resetTask();
                secondStarted = false;
            }
        }
        System.out.printf("B %d %d %d %d %d %d %d %d%n",
            first.getGrowingAge(), second.getGrowingAge(),
            first.getIsWillingToMate(false) ? 1 : 0,
            second.getIsWillingToMate(false) ? 1 : 0,
            world.child == null ? 0 : world.child.getGrowingAge(),
            world.child == null ? -1 : world.child.getProfession(),
            world.status12, world.status18);
    }
}
