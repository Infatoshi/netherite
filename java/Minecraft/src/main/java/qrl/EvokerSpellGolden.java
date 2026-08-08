package qrl;

import com.google.common.base.Predicate;
import com.mojang.authlib.GameProfile;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.UUID;
import javax.annotation.Nullable;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.monster.EntityEvoker;
import net.minecraft.entity.monster.EntityVex;
import net.minecraft.entity.passive.EntitySheep;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.projectile.EntityEvokerFangs;
import net.minecraft.init.Blocks;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.EnumFacing;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Direct 1.11.2 Evoker attack, summon, Wololo, and fang lifecycle oracle. */
public final class EvokerSpellGolden {
    private static final long MULT = 0x5deece66dL;

    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(new UUID(7L, 11L), "evoker-target"));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
    }

    private static final class MemoryWorld extends World {
        final List<Entity> spawned = new ArrayList<Entity>();
        final List<Entity> query = new ArrayList<Entity>();

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "evoker-spell-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
            this.getWorldInfo().setDifficulty(EnumDifficulty.NORMAL);
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
        public boolean isBlockNormalCube(BlockPos pos, boolean defaultValue) {
            return pos.getY() <= 0;
        }
        public boolean isAirBlock(BlockPos pos) { return pos.getY() > 0; }
        public net.minecraft.world.DifficultyInstance getDifficultyForLocation(
                BlockPos pos) {
            return new net.minecraft.world.DifficultyInstance(
                EnumDifficulty.NORMAL, 0L, 0L, 0.0F);
        }
        public boolean spawnEntity(Entity entity) {
            spawned.add(entity);
            return true;
        }
        public List<Entity> getEntitiesInAABBexcluding(
                @Nullable Entity excluded, AxisAlignedBB box,
                @Nullable Predicate<? super Entity> filter) {
            List<Entity> out = new ArrayList<Entity>();
            for (Entity entity : query)
                if (entity != excluded
                        && entity.getEntityBoundingBox().intersectsWith(box)
                        && (filter == null || filter.apply(entity)))
                    out.add(entity);
            return out;
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box,
                @Nullable Predicate<? super T> filter) {
            List<T> out = new ArrayList<T>();
            for (Entity entity : query)
                if (type.isAssignableFrom(entity.getClass())
                        && entity.getEntityBoundingBox().intersectsWith(box)) {
                    T value = type.cast(entity);
                    if (filter == null || filter.apply(value)) out.add(value);
                }
            return out;
        }
    }

    private static Object inner(EntityEvoker evoker, String simpleName)
            throws Exception {
        for (Class<?> type : EntityEvoker.class.getDeclaredClasses())
            if (type.getSimpleName().equals(simpleName)) {
                Constructor<?> constructor =
                    type.getDeclaredConstructor(EntityEvoker.class);
                constructor.setAccessible(true);
                return constructor.newInstance(evoker);
            }
        throw new ClassNotFoundException(simpleName);
    }

    private static void call(Object owner, String name) throws Exception {
        Class<?> type = owner.getClass();
        while (type != null) {
            try {
                Method method = type.getDeclaredMethod(name);
                method.setAccessible(true);
                method.invoke(owner);
                return;
            } catch (NoSuchMethodException missing) {
                type = type.getSuperclass();
            }
        }
        throw new NoSuchMethodException(name);
    }

    private static void setRawRandom(Entity entity, long seed48)
            throws Exception {
        Field field = Entity.class.getDeclaredField("rand");
        field.setAccessible(true);
        ((Random)field.get(entity)).setSeed(seed48 ^ MULT);
    }

    private static int nbtInt(Entity entity, String key) {
        NBTTagCompound tag = new NBTTagCompound();
        entity.writeToNBT(tag);
        return tag.getInteger(key);
    }

    private static void attack() throws Exception {
        MemoryWorld world = new MemoryWorld();
        EntityEvoker evoker = new EntityEvoker(world);
        evoker.setPosition(0.5D, 1.0D, 0.5D);
        TestPlayer player = new TestPlayer(world);
        player.setPosition(20.5D, 1.0D, 0.5D);
        evoker.setAttackTarget(player);
        Object spell = inner(evoker, "AIAttackSpell");
        call(spell, "startExecuting");
        for (int tick = 0; tick < 20; ++tick) call(spell, "updateTask");
        System.out.printf("A %d", world.spawned.size());
        for (Entity entity : world.spawned) {
            EntityEvokerFangs fangs = (EntityEvokerFangs)entity;
            System.out.printf(" %016x %016x %016x %08x %d",
                Double.doubleToRawLongBits(fangs.posX),
                Double.doubleToRawLongBits(fangs.posY),
                Double.doubleToRawLongBits(fangs.posZ),
                Float.floatToRawIntBits(fangs.rotationYaw),
                nbtInt(fangs, "Warmup"));
        }
        System.out.println();

        EntityEvokerFangs first = (EntityEvokerFangs)world.spawned.get(0);
        player.setPosition(first.posX, first.posY, first.posZ);
        player.setHealth(20.0F);
        world.query.add(player);
        System.out.print("F");
        for (int tick = 0; tick < 32 && !first.isDead; ++tick) {
            first.onUpdate();
            System.out.printf(" %d:%08x:%d", tick + 1,
                Float.floatToRawIntBits(player.getHealth()),
                first.isDead ? 1 : 0);
        }
        System.out.println();
    }

    private static void summon() throws Exception {
        MemoryWorld world = new MemoryWorld();
        EntityEvoker evoker = new EntityEvoker(world);
        evoker.setPosition(10.5D, 1.0D, -3.5D);
        TestPlayer player = new TestPlayer(world);
        player.setPosition(20.5D, 1.0D, -3.5D);
        evoker.setAttackTarget(player);
        setRawRandom(evoker, 0x123456789abL);
        Object spell = inner(evoker, "AISummonSpell");
        call(spell, "startExecuting");
        for (int tick = 0; tick < 20; ++tick) call(spell, "updateTask");
        System.out.printf("S %d", world.spawned.size());
        for (Entity entity : world.spawned) {
            EntityVex vex = (EntityVex)entity;
            BlockPos origin = vex.getBoundOrigin();
            System.out.printf(" %016x %016x %016x %d %d %d %d",
                Double.doubleToRawLongBits(vex.posX),
                Double.doubleToRawLongBits(vex.posY),
                Double.doubleToRawLongBits(vex.posZ),
                origin.getX(), origin.getY(), origin.getZ(),
                nbtInt(vex, "LifeTicks"));
        }
        System.out.println();
    }

    private static void wololo() throws Exception {
        MemoryWorld world = new MemoryWorld();
        EntityEvoker evoker = new EntityEvoker(world);
        evoker.setPosition(0.5D, 1.0D, 0.5D);
        EntitySheep sheep = new EntitySheep(world);
        sheep.setPosition(2.5D, 1.0D, 0.5D);
        sheep.setFleeceColor(net.minecraft.item.EnumDyeColor.BLUE);
        world.query.add(sheep);
        Object spell = inner(evoker, "AIWololoSpell");
        call(spell, "shouldExecute");
        call(spell, "startExecuting");
        for (int tick = 0; tick < 40; ++tick) call(spell, "updateTask");
        System.out.printf("W %d%n", sheep.getFleeceColor().getMetadata());
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.init.Bootstrap.register();
        attack();
        summon();
        wololo();
    }
}
