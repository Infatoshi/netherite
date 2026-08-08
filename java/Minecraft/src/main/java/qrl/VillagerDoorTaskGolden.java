package qrl;

import java.lang.reflect.Field;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.state.IBlockState;
import net.minecraft.block.BlockDoor;
import net.minecraft.entity.ai.EntityAIDoorInteract;
import net.minecraft.entity.ai.EntityAIMoveIndoors;
import net.minecraft.entity.ai.EntityAIMoveTowardsRestriction;
import net.minecraft.entity.ai.EntityAIOpenDoor;
import net.minecraft.entity.ai.EntityAIRestrictOpenDoor;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.pathfinding.PathNavigateGround;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.village.Village;
import net.minecraft.village.VillageCollection;
import net.minecraft.village.VillageDoorInfo;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Direct EntityAIRestrictOpenDoor lifecycle oracle. */
public final class VillagerDoorTaskGolden {
    private static final class MemoryWorld extends World {
        final int doorX;
        boolean daytime;
        IBlockState lower = Blocks.OAK_DOOR.getStateFromMeta(0);
        final IBlockState upper = Blocks.OAK_DOOR.getStateFromMeta(8);
        int lastEvent = -1;

        MemoryWorld() throws Exception { this(0); }

        MemoryWorld(int doorX) throws Exception {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "villager-door-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.doorX = doorX;
            this.provider.setWorld(this);
            this.villageCollectionObj = new VillageCollection(this);
            Village village = new Village(this);
            village.addVillageDoorInfo(new VillageDoorInfo(
                new BlockPos(doorX, 64, 0), 2, 0, 1));
            Field listField = VillageCollection.class
                .getDeclaredField("villageList");
            listField.setAccessible(true);
            @SuppressWarnings("unchecked")
            List<Village> villages = (List<Village>)listField.get(
                this.villageCollectionObj);
            villages.add(village);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public boolean isDaytime() { return daytime; }
        public IBlockState getBlockState(BlockPos pos) {
            if (pos.equals(new BlockPos(doorX, 64, 0))) return lower;
            if (pos.equals(new BlockPos(doorX, 65, 0))) return upper;
            return pos.getY() <= 63
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        public boolean setBlockState(
                BlockPos pos, IBlockState state, int flags) {
            if (pos.equals(new BlockPos(doorX, 64, 0))) {
                lower = state;
                return true;
            }
            return false;
        }
        public void playEvent(
                EntityPlayer player, int type, BlockPos pos, int data) {
            lastEvent = type;
        }
    }

    private static final class ProbeNavigate extends PathNavigateGround {
        boolean moving;
        double x, y, z, speed;

        ProbeNavigate(EntityVillager villager, World world) {
            super(villager, world);
        }
        public boolean tryMoveToXYZ(
                double x, double y, double z, double speed) {
            this.x = x;
            this.y = y;
            this.z = z;
            this.speed = speed;
            this.moving = true;
            return true;
        }
        public boolean noPath() { return !moving; }
        public void clearPathEntity() { moving = false; }
    }

    private static final class ProbeVillager extends EntityVillager {
        final ProbeNavigate probe;

        ProbeVillager(World world) {
            super(world, 0);
            this.probe = new ProbeNavigate(this, world);
            this.navigator = probe;
        }
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.init.Bootstrap.register();
        MemoryWorld world = new MemoryWorld();
        EntityVillager villager = new EntityVillager(world, 0);
        villager.setPosition(1.5D, 64.0D, 0.5D);
        EntityAIRestrictOpenDoor task =
            new EntityAIRestrictOpenDoor(villager);
        boolean started = task.shouldExecute();
        if (started) task.startExecuting();
        if (started) task.updateTask();
        VillageDoorInfo door = world.getVillageCollection()
            .getNearestVillage(new BlockPos(villager), 16)
            .getVillageDoorInfoList().get(0);
        PathNavigateGround navigator =
            (PathNavigateGround)villager.getNavigator();
        System.out.printf("S %d %d %d %d%n",
            started ? 1 : 0, door.getDoorOpeningRestrictionCounter(),
            task.continueExecuting() ? 1 : 0,
            navigator.getEnterDoors() ? 1 : 0);

        villager.setPosition(-0.5D, 64.0D, 0.5D);
        boolean continued = task.continueExecuting();
        if (!continued) task.resetTask();
        System.out.printf("X %d %d%n",
            continued ? 1 : 0, navigator.getEnterDoors() ? 1 : 0);
        world.daytime = true;
        System.out.printf("D %d%n", task.shouldExecute() ? 1 : 0);

        villager.setPosition(-0.5D, 64.0D, 0.5D);
        EntityAIOpenDoor open = new EntityAIOpenDoor(villager, true);
        Field doorPosition = EntityAIDoorInteract.class
            .getDeclaredField("doorPosition");
        doorPosition.setAccessible(true);
        doorPosition.set(open, new BlockPos(0, 65, 0));
        Field doorBlock = EntityAIDoorInteract.class
            .getDeclaredField("doorBlock");
        doorBlock.setAccessible(true);
        doorBlock.set(open, (BlockDoor)Blocks.OAK_DOOR);
        open.startExecuting();
        open.updateTask();
        System.out.printf("O %d %d %d %d%n",
            Blocks.OAK_DOOR.getMetaFromState(world.lower),
            privateInt(open, "closeDoorTemporisation"),
            open.continueExecuting() ? 1 : 0, world.lastEvent);
        for (int tick = 1; tick < 20; ++tick) open.updateTask();
        boolean openContinued = open.continueExecuting();
        if (!openContinued) open.resetTask();
        System.out.printf("C %d %d %d %d%n",
            Blocks.OAK_DOOR.getMetaFromState(world.lower),
            privateInt(open, "closeDoorTemporisation"),
            openContinued ? 1 : 0, world.lastEvent);

        moveIndoors("N", new MemoryWorld(0), 1.5D);
        moveIndoors("F", new MemoryWorld(30), 0.5D);
        moveRestriction(new MemoryWorld(30));
    }

    private static void moveRestriction(MemoryWorld world)
            throws Exception {
        ProbeVillager villager = new ProbeVillager(world);
        villager.setPosition(80.5D, 64.0D, 0.5D);
        villager.setHomePosAndDistance(new BlockPos(30, 64, 0), 32);
        villager.getRNG().setSeed(18L);
        EntityAIMoveTowardsRestriction task =
            new EntityAIMoveTowardsRestriction(villager, 0.6D);
        boolean started = task.shouldExecute();
        if (started) task.startExecuting();
        System.out.printf("H %d %.1f %.1f %.1f %d %d%n",
            started ? 1 : 0,
            villager.probe.x, villager.probe.y, villager.probe.z,
            task.continueExecuting() ? 1 : 0,
            seed48(villager.getRNG()));
    }

    private static void moveIndoors(
            String name, MemoryWorld world, double x) throws Exception {
        ProbeVillager villager = new ProbeVillager(world);
        villager.setPosition(x, 64.0D, 0.5D);
        villager.setHomePosAndDistance(
            new BlockPos(world.doorX, 64, 0), 32);
        villager.getRNG().setSeed(18L);
        EntityAIMoveIndoors task = new EntityAIMoveIndoors(villager);
        boolean started = task.shouldExecute();
        if (started) task.startExecuting();
        System.out.printf("I %s %d %.1f %.1f %.1f %d %d%n",
            name, started ? 1 : 0,
            villager.probe.x, villager.probe.y, villager.probe.z,
            task.continueExecuting() ? 1 : 0,
            seed48(villager.getRNG()));
    }

    private static long seed48(java.util.Random random) throws Exception {
        Field field = java.util.Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get()
            & ((1L << 48) - 1L);
    }

    private static int privateInt(Object object, String name)
            throws Exception {
        Field field = object.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.getInt(object);
    }
}
