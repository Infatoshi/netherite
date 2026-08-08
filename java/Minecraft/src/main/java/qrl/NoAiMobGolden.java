package qrl;

import com.google.common.base.Predicate;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import javax.annotation.Nullable;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.monster.EntityBlaze;
import net.minecraft.entity.monster.EntityCaveSpider;
import net.minecraft.entity.monster.EntityCreeper;
import net.minecraft.entity.monster.EntityEnderman;
import net.minecraft.entity.monster.EntityElderGuardian;
import net.minecraft.entity.monster.EntityEvoker;
import net.minecraft.entity.monster.EntityGhast;
import net.minecraft.entity.monster.EntityGuardian;
import net.minecraft.entity.monster.EntityPigZombie;
import net.minecraft.entity.monster.EntitySilverfish;
import net.minecraft.entity.monster.EntitySkeleton;
import net.minecraft.entity.monster.EntitySpider;
import net.minecraft.entity.monster.EntityVex;
import net.minecraft.entity.monster.EntityVindicator;
import net.minecraft.entity.monster.EntityWitherSkeleton;
import net.minecraft.entity.monster.EntityWitch;
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.entity.monster.EntityZombieVillager;
import net.minecraft.entity.passive.EntityCow;
import net.minecraft.init.Bootstrap;
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

/** Common NoAI living-base continuation oracle for real Minecraft 1.11.2. */
public final class NoAiMobGolden {
    private static final long MULT = 0x5deece66dL;
    private static final long MASK = (1L << 48) - 1L;

    private static final class MemoryWorld extends World {
        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "no-ai-mob-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
            this.getWorldInfo().setWorldTime(18000L);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public int getSkylightSubtracted() { return 15; }
        public float getLightBrightness(BlockPos pos) { return 0.0F; }
        public List<Entity> getEntitiesInAABBexcluding(
                @Nullable Entity entity, AxisAlignedBB box,
                @Nullable Predicate<? super Entity> predicate) {
            return new ArrayList<Entity>();
        }
        public IBlockState getBlockState(BlockPos pos) {
            return Blocks.AIR.getDefaultState();
        }
    }

    private static final class Fixture {
        final String name;
        final int type;
        final float health;
        Fixture(String name, int type, float health) {
            this.name = name;
            this.type = type;
            this.health = health;
        }
    }

    private static final Fixture[] FIXTURES = {
        new Fixture("Z", 2, 20.0F),
        new Fixture("K", 3, 20.0F),
        new Fixture("W", 32, 20.0F),
        new Fixture("C", 4, 20.0F),
        new Fixture("S", 5, 16.0F),
        new Fixture("V", 39, 12.0F),
        new Fixture("E", 6, 40.0F),
        new Fixture("B", 7, 20.0F),
        new Fixture("H", 26, 10.0F),
        new Fixture("T", 23, 26.0F),
        new Fixture("I", 51, 24.0F),
        new Fixture("A", 52, 24.0F),
        new Fixture("X", 53, 14.0F),
        new Fixture("G", 55, 30.0F),
        new Fixture("L", 56, 80.0F),
        new Fixture("R", 41, 20.0F),
        new Fixture("P", 15, 20.0F),
        new Fixture("F", 36, 8.0F),
        new Fixture("O", 12, 10.0F),
    };

    private static EntityLiving create(MemoryWorld world, int type) {
        switch (type) {
        case 2: return new EntityZombie(world);
        case 3: return new EntitySkeleton(world);
        case 32: return new EntityWitherSkeleton(world);
        case 4: return new EntityCreeper(world);
        case 5: return new EntitySpider(world);
        case 39: return new EntityCaveSpider(world);
        case 6: return new EntityEnderman(world);
        case 7: return new EntityBlaze(world);
        case 26: return new EntityGhast(world);
        case 23: return new EntityWitch(world);
        case 51: return new EntityVindicator(world);
        case 52: return new EntityEvoker(world);
        case 53: return new EntityVex(world);
        case 55: return new EntityGuardian(world);
        case 56: return new EntityElderGuardian(world);
        case 41: return new EntityZombieVillager(world);
        case 15: return new EntityPigZombie(world);
        case 36: return new EntitySilverfish(world);
        case 12: return new EntityCow(world);
        default: throw new IllegalArgumentException("type " + type);
        }
    }

    private static Random random(EntityLiving entity) throws Exception {
        Field field = net.minecraft.entity.Entity.class
            .getDeclaredField("rand");
        field.setAccessible(true);
        return (Random)field.get(entity);
    }

    private static long rawSeed(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & MASK;
    }

    private static void setRawSeed(Random random, long seed48) {
        random.setSeed(seed48 ^ MULT);
    }

    private static String dbits(double value) {
        return String.format("%016x", Double.doubleToRawLongBits(value));
    }

    private static String fbits(float value) {
        return String.format("%08x", Float.floatToRawIntBits(value));
    }

    private static void run(Fixture fixture, int livingSoundTime)
            throws Exception {
        MemoryWorld world = new MemoryWorld();
        EntityLiving entity = create(world, fixture.type);
        entity.setNoAI(true);
        entity.setPosition(0.5D, 220.0D, 0.5D);
        entity.motionX = 0.125D;
        entity.motionY = fixture.type == 7 ? -0.25D : 0.25D;
        entity.motionZ = -0.0625D;
        entity.rotationYaw = 37.0F;
        entity.rotationPitch = 0.0F;
        entity.setHealth(fixture.health);
        entity.setAir(300);
        entity.onGround = false;
        entity.fallDistance = 1.25F;
        entity.ticksExisted = 19;
        entity.livingSoundTime = livingSoundTime;
        setRawSeed(random(entity), 0x123456789ABCL);
        for (int tick = 0; tick < 3; ++tick) {
            ++entity.ticksExisted;
            entity.onEntityUpdate();
            entity.onLivingUpdate();
        }
        NBTTagCompound nbt = new NBTTagCompound();
        entity.writeToNBT(nbt);
        System.out.printf(
            "N%s%d %d %d %012x %s %s %s %d %d %s %d %s%n",
            fixture.name, livingSoundTime == 1000 ? 1 : 0,
            entity.ticksExisted, entity.livingSoundTime,
            rawSeed(random(entity)), dbits(entity.motionX),
            dbits(entity.motionY), dbits(entity.motionZ), entity.getAir(),
            nbt.getShort("Fire"), fbits(entity.fallDistance),
            entity.onGround ? 1 : 0, fbits(entity.getHealth()));
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        for (Fixture fixture : FIXTURES) {
            run(fixture, 0);
            run(fixture, 1000);
        }
    }
}
