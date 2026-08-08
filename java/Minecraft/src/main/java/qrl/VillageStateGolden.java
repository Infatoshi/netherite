package qrl;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.monster.EntityIronGolem;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.init.Blocks;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
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
import net.minecraft.village.Village;
import net.minecraft.village.VillageDoorInfo;

/** Direct Village.java saved-state, door, reputation, and golem oracle. */
public final class VillageStateGolden {
    private static final UUID FIRST = new UUID(
        0x0123456789ABCDEFL, 0x0FEDCBA987654321L);
    private static final UUID SECOND = new UUID(
        0x1111222233334444L, 0x5555666677778888L);

    private static final class MemoryWorld extends World {
        final Set<BlockPos> doors = new HashSet<BlockPos>();
        int villagerCount = 20;
        int golemCount = 0;
        boolean keepDoors = true;
        int spawned;
        int spawnX, spawnY, spawnZ;

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "village-state-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            if (keepDoors && doors.contains(pos))
                return Blocks.OAK_DOOR.getDefaultState();
            return pos.getY() <= 63
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        @SuppressWarnings("unchecked")
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box) {
            if (type == EntityVillager.class)
                return (List<T>)(List<?>)Collections.nCopies(
                    villagerCount, (Entity)null);
            if (type == EntityIronGolem.class)
                return (List<T>)(List<?>)Collections.nCopies(
                    golemCount, (Entity)null);
            return new ArrayList<T>();
        }
        public boolean spawnEntity(Entity entity) {
            if (!(entity instanceof EntityIronGolem)) return false;
            ++spawned;
            spawnX = (int)entity.posX;
            spawnY = (int)entity.posY;
            spawnZ = (int)entity.posZ;
            return true;
        }
    }

    private static long seed48(java.util.Random random) throws Exception {
        Field field = java.util.Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & ((1L << 48) - 1L);
    }

    private static Village persisted(Village source, MemoryWorld world) {
        NBTTagCompound nbt = new NBTTagCompound();
        source.writeVillageDataToNBT(nbt);
        Village loaded = new Village();
        loaded.setWorld(world);
        loaded.readVillageDataFromNBT(nbt);
        return loaded;
    }

    private static NBTTagCompound saved(Village village) {
        NBTTagCompound nbt = new NBTTagCompound();
        village.writeVillageDataToNBT(nbt);
        return nbt;
    }

    private static int aggressorCount(Village village) throws Exception {
        Field field = Village.class.getDeclaredField("villageAgressors");
        field.setAccessible(true);
        return ((List<?>)field.get(village)).size();
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.init.Bootstrap.register();
        MemoryWorld world = new MemoryWorld();
        Village village = new Village(world);
        for (int i = 0; i < 21; ++i) {
            BlockPos pos = new BlockPos(i % 7 - 3, 64, i / 7 - 1);
            world.doors.add(pos);
            village.addVillageDoorInfo(new VillageDoorInfo(
                pos, (i & 1) == 0 ? 2 : -2, 0, 100));
        }
        village.getVillageDoorInfoList().get(0)
            .incrementDoorOpeningRestrictionCounter();
        village.modifyPlayerReputation(FIRST, -40);
        village.modifyPlayerReputation(FIRST, 7);
        village.modifyPlayerReputation(SECOND, 12);
        village.setDefaultPlayerReputation(5);
        NBTTagCompound initial = saved(village);
        initial.setInteger("Tick", 100);
        village = new Village();
        village.setWorld(world);
        village.readVillageDataFromNBT(initial);
        village.endMatingSeason();
        village = persisted(village, world);
        BlockPos center = village.getCenter();
        System.out.printf("S %d %d %d %d %d %d %d %d %d %d %d%n",
            center.getX(), center.getY(), center.getZ(),
            village.getVillageRadius(), village.getNumVillageDoors(),
            village.getTicksSinceLastDoorAdding(),
            village.getPlayerReputation(FIRST),
            village.getPlayerReputation(SECOND),
            village.isPlayerReputationTooLow(FIRST) ? 1 : 0,
            village.isMatingSeason() ? 1 : 0,
            village.getVillageDoorInfoList().get(0)
                .getDoorOpeningRestrictionCounter());

        world.rand.setSeed(3107L);
        village.tick(160);
        NBTTagCompound afterTick = saved(village);
        System.out.printf("T %d %d %d %d %d %d %d%n",
            village.getNumVillagers(), afterTick.getInteger("Golems"),
            world.spawnX, world.spawnY, world.spawnZ,
            seed48(world.rand), village.isMatingSeason() ? 1 : 0);

        world.keepDoors = false;
        village.tick(1301);
        world.villagerCount = 0;
        village.tick(1320);
        NBTTagCompound removed = saved(village);
        center = village.getCenter();
        NBTTagList players = removed.getTagList("Players", 10);
        System.out.printf("R %d %d %d %d %d %d %d%n",
            village.getNumVillageDoors(), center.getX(), center.getY(),
            center.getZ(), village.getVillageRadius(),
            village.getPlayerReputation(FIRST), players.tagCount());
        village.tick(3700);
        System.out.printf("M %d %d%n",
            village.isMatingSeason() ? 1 : 0, seed48(world.rand));

        Village live = new Village(world);
        EntityVillager first = new EntityVillager(world);
        EntityVillager second = new EntityVillager(world);
        live.tick(100);
        live.addOrRenewAgressor(first);
        live.addOrRenewAgressor(second);
        int before = aggressorCount(live);
        live.tick(350);
        live.addOrRenewAgressor(first);
        live.tick(401);
        int afterExpiry = aggressorCount(live);
        int afterReload = aggressorCount(persisted(live, world));
        System.out.printf("A %d %d %d%n", before, afterExpiry, afterReload);
    }
}
