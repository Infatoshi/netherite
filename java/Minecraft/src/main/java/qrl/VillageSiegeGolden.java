package qrl;

import com.mojang.authlib.GameProfile;
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
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.util.math.BlockPos;
import net.minecraft.village.VillageCollection;
import net.minecraft.village.VillageSiege;
import net.minecraft.world.DifficultyInstance;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Direct VillageSiege state-machine, spawn position, and RNG oracle. */
public final class VillageSiegeGolden {
    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(new UUID(13L, 17L), "siege-target"));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
    }

    private static final class MemoryWorld extends World {
        final Set<BlockPos> doors = new HashSet<BlockPos>();
        boolean daytime = true;
        float celestial = 0.0F;
        int zombieCount;
        int zombieX, zombieY, zombieZ;
        float zombieYaw;

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "village-siege-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
            this.getWorldInfo().setDifficulty(EnumDifficulty.NORMAL);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public boolean isDaytime() { return daytime; }
        public float getCelestialAngle(float partialTicks) { return celestial; }
        public DifficultyInstance getDifficultyForLocation(BlockPos pos) {
            return new DifficultyInstance(
                EnumDifficulty.NORMAL, 0L, 0L, 0.0F);
        }
        public IBlockState getBlockState(BlockPos pos) {
            if (doors.contains(pos)) return Blocks.OAK_DOOR.getDefaultState();
            return pos.getY() <= 63
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        @SuppressWarnings("unchecked")
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box) {
            if (type == EntityVillager.class)
                return (List<T>)(List<?>)Collections.nCopies(20, (Entity)null);
            if (type == EntityIronGolem.class)
                return new ArrayList<T>();
            return new ArrayList<T>();
        }
        public boolean spawnEntity(Entity entity) {
            if (!(entity instanceof EntityZombie)) return false;
            ++zombieCount;
            zombieX = (int)entity.posX;
            zombieY = (int)entity.posY;
            zombieZ = (int)entity.posZ;
            zombieYaw = entity.rotationYaw;
            return true;
        }
    }

    private static int integer(VillageSiege siege, String name)
            throws Exception {
        Field field = VillageSiege.class.getDeclaredField(name);
        field.setAccessible(true);
        return field.getInt(siege);
    }

    private static boolean bool(VillageSiege siege, String name)
            throws Exception {
        Field field = VillageSiege.class.getDeclaredField(name);
        field.setAccessible(true);
        return field.getBoolean(siege);
    }

    private static long seed48(java.util.Random random) throws Exception {
        Field field = java.util.Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & ((1L << 48) - 1L);
    }

    private static VillageCollection collection(MemoryWorld world) {
        NBTTagCompound village = new NBTTagCompound();
        village.setInteger("PopSize", 20);
        village.setInteger("Radius", 32);
        village.setInteger("Golems", 0);
        village.setInteger("Stable", 80);
        village.setInteger("Tick", 100);
        village.setInteger("MTick", 0);
        village.setInteger("CX", 0);
        village.setInteger("CY", 64);
        village.setInteger("CZ", 0);
        village.setInteger("ACX", 0);
        village.setInteger("ACY", 640);
        village.setInteger("ACZ", 0);
        NBTTagList doors = new NBTTagList();
        for (int door = 0; door < 10; ++door) {
            NBTTagCompound tag = new NBTTagCompound();
            int x = door - 5;
            BlockPos pos = new BlockPos(x, 64, 4);
            world.doors.add(pos);
            tag.setInteger("X", x);
            tag.setInteger("Y", 64);
            tag.setInteger("Z", 4);
            tag.setInteger("IDX", 0);
            tag.setInteger("IDZ", 2);
            tag.setInteger("TS", 80);
            doors.appendTag(tag);
        }
        village.setTag("Doors", doors);
        village.setTag("Players", new NBTTagList());
        NBTTagList villages = new NBTTagList();
        villages.appendTag(village);
        NBTTagCompound root = new NBTTagCompound();
        root.setInteger("Tick", 100);
        root.setTag("Villages", villages);
        VillageCollection result = new VillageCollection(world);
        result.readFromNBT(root);
        result.setWorldsForAll(world);
        return result;
    }

    private static void print(String label, VillageSiege siege,
            MemoryWorld world) throws Exception {
        System.out.printf("%s %d %d %d %d %d %d %d %d %d %d %d %08x %012x%n",
            label,
            integer(siege, "siegeState"),
            bool(siege, "hasSetupSiege") ? 1 : 0,
            integer(siege, "siegeCount"),
            integer(siege, "nextSpawnTime"),
            integer(siege, "spawnX"),
            integer(siege, "spawnY"),
            integer(siege, "spawnZ"),
            world.zombieCount, world.zombieX, world.zombieY, world.zombieZ,
            Float.floatToRawIntBits(world.zombieYaw),
            seed48(world.rand));
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.init.Bootstrap.register();
        MemoryWorld world = new MemoryWorld();
        VillageCollection collection = collection(world);
        world.villageCollectionObj = collection;
        TestPlayer player = new TestPlayer(world);
        player.setPosition(0.5D, 64.0D, 0.5D);
        world.playerEntities.add(player);
        VillageSiege siege = new VillageSiege(world);

        collection.tick();
        siege.tick();
        print("D", siege, world);

        world.rand.setSeed(113668L);
        world.daytime = false;
        world.celestial = 0.5F;
        collection.tick();
        siege.tick();
        print("S", siege, world);

        for (int tick = 0; tick < 57; ++tick) {
            collection.tick();
            siege.tick();
        }
        print("Z", siege, world);
        for (int tick = 0; tick < 3; ++tick) {
            collection.tick();
            siege.tick();
        }
        print("T", siege, world);
        world.daytime = true;
        collection.tick();
        siege.tick();
        print("R", siege, world);
    }
}
