package qrl;

import com.google.common.base.Predicate;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.ai.EntityAIPlay;
import net.minecraft.entity.ai.EntityAIHarvestFarmland;
import net.minecraft.entity.ai.EntityAIMoveToBlock;
import net.minecraft.entity.ai.EntityAIVillagerInteract;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.pathfinding.PathNavigateGround;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.village.VillageCollection;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Direct child-play and villager social-task oracle. */
public final class VillagerSocialGolden {
    private static final class MemoryWorld extends World {
        final List<EntityVillager> villagers =
            new ArrayList<EntityVillager>();
        final List<EntityItem> items = new ArrayList<EntityItem>();
        final List<Entity> others = new ArrayList<Entity>();
        boolean farming;
        IBlockState farmCrop = Blocks.AIR.getDefaultState();

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "villager-social-oracle"),
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
            if (farming && pos.equals(new BlockPos(0, 63, 0)))
                return Blocks.FARMLAND.getDefaultState();
            if (farming && pos.equals(new BlockPos(0, 64, 0)))
                return farmCrop;
            return pos.getY() <= 63
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        public boolean setBlockState(
                BlockPos pos, IBlockState state, int flags) {
            if (farming && pos.equals(new BlockPos(0, 64, 0))) {
                farmCrop = state;
                return true;
            }
            return false;
        }
        public boolean spawnEntity(Entity entity) {
            if (entity instanceof EntityItem) {
                items.add((EntityItem)entity);
                return true;
            }
            return false;
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box) {
            List<T> result = new ArrayList<T>();
            if (type != EntityVillager.class) return result;
            for (EntityVillager villager : villagers)
                if (!villager.isDead
                        && villager.getEntityBoundingBox()
                            .intersectsWith(box))
                    result.add(type.cast(villager));
            return result;
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box,
                Predicate<? super T> predicate) {
            List<T> result = getEntitiesWithinAABB(type, box);
            for (Entity entity : others)
                if (type.isAssignableFrom(entity.getClass())
                        && entity.getEntityBoundingBox().intersectsWith(box)
                        && predicate.apply(type.cast(entity)))
                    result.add(type.cast(entity));
            return result;
        }
    }

    private static final class ProbeNavigate extends PathNavigateGround {
        boolean moving;
        double x, y, z;

        ProbeNavigate(EntityVillager entity, World world) {
            super(entity, world);
        }
        public boolean tryMoveToXYZ(
                double x, double y, double z, double speed) {
            this.x = x;
            this.y = y;
            this.z = z;
            moving = true;
            return true;
        }
        public boolean tryMoveToEntityLiving(
                Entity entity, double speed) {
            return tryMoveToXYZ(entity.posX, entity.posY, entity.posZ, speed);
        }
        public boolean noPath() { return !moving; }
        public void clearPathEntity() { moving = false; }
    }

    private static final class ProbeVillager extends EntityVillager {
        final ProbeNavigate probe;

        ProbeVillager(World world) {
            super(world, 0);
            probe = new ProbeNavigate(this, world);
            navigator = probe;
        }
    }

    private static Field field(String name) throws Exception {
        Field result = EntityAIPlay.class.getDeclaredField(name);
        result.setAccessible(true);
        return result;
    }

    private static long seed48(java.util.Random random) throws Exception {
        Field field = java.util.Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get()
            & ((1L << 48) - 1L);
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.init.Bootstrap.register();
        MemoryWorld world = new MemoryWorld();
        ProbeVillager first = new ProbeVillager(world);
        ProbeVillager second = new ProbeVillager(world);
        first.setPosition(0.5D, 64.0D, 0.5D);
        second.setPosition(5.5D, 64.0D, 0.5D);
        first.setGrowingAge(-100);
        second.setGrowingAge(-100);
        first.getRNG().setSeed(465L);
        world.villagers.add(first);
        world.villagers.add(second);

        EntityAIPlay play = new EntityAIPlay(first, 0.32D);
        boolean started = play.shouldExecute();
        if (started) play.startExecuting();
        if (started) play.updateTask();
        System.out.printf("P %d %d %d %d %.1f %.1f %.1f %d%n",
            started ? 1 : 0, first.isPlaying() ? 1 : 0,
            field("targetVillager").get(play) == second ? 1 : 0,
            field("playTime").getInt(play),
            first.probe.x, first.probe.y, first.probe.z,
            seed48(first.getRNG()));

        farm();
        avoid();
        interact();
        harvest(Blocks.WHEAT.getDefaultState()
            .withProperty(net.minecraft.block.BlockCrops.AGE, 7), "W");
        harvest(Blocks.CARROTS.getDefaultState()
            .withProperty(net.minecraft.block.BlockCrops.AGE, 7), "C");
        harvest(Blocks.POTATOES.getDefaultState()
            .withProperty(net.minecraft.block.BlockCrops.AGE, 7), "O");
        harvest(Blocks.BEETROOTS.getDefaultState()
            .withProperty(net.minecraft.block.BlockBeetroot.BEETROOT_AGE, 3),
            "B");
    }

    private static void interact() throws Exception {
        MemoryWorld world = new MemoryWorld();
        ProbeVillager giver = new ProbeVillager(world);
        ProbeVillager receiver = new ProbeVillager(world);
        giver.setPosition(0.5D, 64.0D, 0.5D);
        receiver.setPosition(2.5D, 64.0D, 0.5D);
        giver.rotationYaw = 0.0F;
        giver.rotationYawHead = 0.0F;
        giver.rotationPitch = 0.0F;
        giver.getVillagerInventory().setInventorySlotContents(
            0, new ItemStack(Items.BREAD, 8));
        giver.getRNG().setSeed(5120L);
        java.util.Random math = mathRandom();
        math.setSeed(65537L);
        world.villagers.add(giver);
        world.villagers.add(receiver);
        EntityAIVillagerInteract interact =
            new EntityAIVillagerInteract(giver);
        boolean started = interact.shouldExecute();
        if (started) interact.startExecuting();
        for (int tick = 0; started && tick < 10; ++tick) {
            interact.updateTask();
            giver.getLookHelper().onUpdateLook();
        }
        ItemStack remaining =
            giver.getVillagerInventory().getStackInSlot(0);
        System.out.printf("I %d %d %d %d %08x %08x %d %d%n",
            started ? 1 : 0,
            remaining.isEmpty() ? 0 : remaining.getCount(),
            world.items.size(),
            world.items.isEmpty() ? 0
                : world.items.get(0).getEntityItem().getCount(),
            Float.floatToRawIntBits(giver.rotationYawHead),
            Float.floatToRawIntBits(giver.rotationPitch),
            seed48(math), seed48(giver.getRNG()));
        for (EntityItem item : world.items) {
            System.out.printf(
                "J %d %016x %016x %016x %016x %016x %016x %08x %08x%n",
                net.minecraft.item.Item.getIdFromItem(
                    item.getEntityItem().getItem()),
                Double.doubleToRawLongBits(item.posX),
                Double.doubleToRawLongBits(item.posY),
                Double.doubleToRawLongBits(item.posZ),
                Double.doubleToRawLongBits(item.motionX),
                Double.doubleToRawLongBits(item.motionY),
                Double.doubleToRawLongBits(item.motionZ),
                Float.floatToRawIntBits(item.rotationYaw),
                Float.floatToRawIntBits(item.hoverStart));
        }
    }

    private static void avoid() throws Exception {
        MemoryWorld world = new MemoryWorld();
        ProbeVillager villager = new ProbeVillager(world);
        EntityZombie zombie = new EntityZombie(world);
        villager.setPosition(0.5D, 64.0D, 0.5D);
        zombie.setPosition(4.5D, 64.0D, 0.5D);
        world.others.add(zombie);
        villager.getRNG().setSeed(1234L);
        Vec3d target = net.minecraft.entity.ai.RandomPositionGenerator
            .findRandomTargetBlockAwayFrom(
                villager, 16, 7,
                new Vec3d(zombie.posX, zombie.posY, zombie.posZ));
        System.out.printf("A %d %016x %016x %016x %d%n",
            target == null ? 0 : 1,
            Double.doubleToRawLongBits(target == null ? 0.0D : target.xCoord),
            Double.doubleToRawLongBits(target == null ? 0.0D : target.yCoord),
            Double.doubleToRawLongBits(target == null ? 0.0D : target.zCoord),
            seed48(villager.getRNG()));
    }

    private static void farm() throws Exception {
        MemoryWorld world = new MemoryWorld();
        world.farming = true;
        ProbeVillager villager = new ProbeVillager(world);
        villager.setPosition(0.5D, 64.0D, 0.5D);
        villager.getVillagerInventory().setInventorySlotContents(
            0, new ItemStack(Items.WHEAT_SEEDS, 2));
        villager.getRNG().setSeed(18L);
        EntityAIHarvestFarmland farm =
            new EntityAIHarvestFarmland(villager, 0.6D);
        boolean started = farm.shouldExecute();
        if (started) farm.startExecuting();
        if (started) farm.updateTask();
        ItemStack seeds = villager.getVillagerInventory().getStackInSlot(0);
        System.out.printf("F %d %d %d %d %d %d %d%n",
            started ? 1 : 0,
            net.minecraft.block.Block.getIdFromBlock(
                world.farmCrop.getBlock()),
            seeds.isEmpty() ? 0 : seeds.getCount(),
            privateInt(farm, EntityAIHarvestFarmland.class, "currentTask"),
            privateInt(farm, EntityAIMoveToBlock.class, "runDelay"),
            privateInt(farm, EntityAIMoveToBlock.class, "timeoutCounter"),
            seed48(villager.getRNG()));
    }

    private static void harvest(IBlockState crop, String label)
            throws Exception {
        MemoryWorld world = new MemoryWorld();
        world.farming = true;
        world.farmCrop = crop;
        ProbeVillager villager = new ProbeVillager(world);
        villager.setPosition(0.5D, 64.0D, 0.5D);
        villager.getRNG().setSeed(18L);
        world.rand.setSeed(1729L);
        randomField(Block.class, "RANDOM").setSeed(8191L);
        java.util.Random math = mathRandom();
        math.setSeed(65537L);
        EntityAIHarvestFarmland farm =
            new EntityAIHarvestFarmland(villager, 0.6D);
        boolean started = farm.shouldExecute();
        if (started) farm.startExecuting();
        if (started) farm.updateTask();
        System.out.printf("D %s %d %d %d %d %d %d %d%n",
            label, started ? 1 : 0,
            Block.getIdFromBlock(world.farmCrop.getBlock()),
            world.items.size(), seed48(world.rand),
            seed48(randomField(Block.class, "RANDOM")),
            seed48(math), seed48(villager.getRNG()));
        for (EntityItem item : world.items) {
            System.out.printf(
                "E %d %016x %016x %016x %016x %016x %016x %08x %08x%n",
                net.minecraft.item.Item.getIdFromItem(
                    item.getEntityItem().getItem()),
                Double.doubleToRawLongBits(item.posX),
                Double.doubleToRawLongBits(item.posY),
                Double.doubleToRawLongBits(item.posZ),
                Double.doubleToRawLongBits(item.motionX),
                Double.doubleToRawLongBits(item.motionY),
                Double.doubleToRawLongBits(item.motionZ),
                Float.floatToRawIntBits(item.rotationYaw),
                Float.floatToRawIntBits(item.hoverStart));
        }
    }

    private static java.util.Random randomField(
            Class<?> owner, String name) throws Exception {
        Field field = owner.getDeclaredField(name);
        field.setAccessible(true);
        return (java.util.Random)field.get(null);
    }

    private static java.util.Random mathRandom() throws Exception {
        Class<?> holder = Class.forName(
            "java.lang.Math$RandomNumberGeneratorHolder");
        return randomField(holder, "randomNumberGenerator");
    }

    private static int privateInt(
            Object object, Class<?> owner, String name) throws Exception {
        Field field = owner.getDeclaredField(name);
        field.setAccessible(true);
        return field.getInt(object);
    }
}
