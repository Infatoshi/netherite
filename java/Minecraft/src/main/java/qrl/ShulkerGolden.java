package qrl;

import com.google.common.base.Function;
import com.google.common.base.Predicate;
import com.mojang.authlib.GameProfile;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicLong;
import javax.annotation.Nullable;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.IEntityLivingData;
import net.minecraft.entity.monster.EntityShulker;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.projectile.EntityShulkerBullet;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.util.math.BlockPos;
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

/** Real 1.11.2 shulker AI, attachment, teleport, and bullet-motion oracle. */
public final class ShulkerGolden {
    private static final long MASK = (1L << 48) - 1L;

    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(new UUID(3L, 4L), "shulker-target"));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
    }

    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> blocks =
            new HashMap<BlockPos, IBlockState>();
        final List<EntityShulkerBullet> bullets =
            new ArrayList<EntityShulkerBullet>();
        TestPlayer player;

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "shulker-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.getWorldInfo().setDifficulty(EnumDifficulty.NORMAL);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            IBlockState state = blocks.get(pos);
            return state == null ? Blocks.AIR.getDefaultState() : state;
        }
        public boolean isBlockNormalCube(BlockPos pos, boolean defaultValue) {
            IBlockState state = getBlockState(pos);
            return state.getBlock().isNormalCube(state, this, pos);
        }
        public boolean setBlockState(BlockPos pos, IBlockState state, int flags) {
            if (state.getBlock() == Blocks.AIR) blocks.remove(pos);
            else blocks.put(pos.toImmutable(), state);
            return true;
        }
        public EntityPlayer getNearestAttackablePlayer(
                double x, double y, double z, double maxXZ, double maxY,
                @Nullable Function<EntityPlayer, Double> scale,
                @Nullable Predicate<EntityPlayer> filter) {
            if (player == null || player.isDead
                    || filter != null && !filter.apply(player))
                return null;
            double dx = player.posX - x, dz = player.posZ - z;
            return dx * dx + dz * dz < maxXZ * maxXZ
                    && Math.abs(player.posY - y) < maxY * maxY
                ? player : null;
        }
        public boolean spawnEntity(Entity entity) {
            if (entity instanceof EntityShulkerBullet)
                bullets.add((EntityShulkerBullet)entity);
            return true;
        }
        public List<AxisAlignedBB> getCollisionBoxes(
                @Nullable Entity entity, AxisAlignedBB box) {
            List<AxisAlignedBB> out = new ArrayList<AxisAlignedBB>();
            int x0 = (int)Math.floor(box.minX);
            int x1 = (int)Math.ceil(box.maxX);
            int y0 = (int)Math.floor(box.minY);
            int y1 = (int)Math.ceil(box.maxY);
            int z0 = (int)Math.floor(box.minZ);
            int z1 = (int)Math.ceil(box.maxZ);
            for (int x = x0; x < x1; ++x)
                for (int y = y0; y < y1; ++y)
                    for (int z = z0; z < z1; ++z)
                        if (getBlockState(new BlockPos(x, y, z))
                                .isFullCube())
                            out.add(new AxisAlignedBB(
                                x, y, z, x + 1, y + 1, z + 1));
            return out;
        }
        public List<Entity> getEntitiesInAABBexcluding(
                @Nullable Entity excluded, AxisAlignedBB box,
                @Nullable Predicate<? super Entity> filter) {
            List<Entity> out = new ArrayList<Entity>();
            if (player != null && player != excluded
                    && player.getEntityBoundingBox().intersectsWith(box)
                    && (filter == null || filter.apply(player)))
                out.add(player);
            return out;
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box,
                @Nullable Predicate<? super T> filter) {
            List<T> out = new ArrayList<T>();
            if (player != null && type.isAssignableFrom(player.getClass())
                    && player.getEntityBoundingBox().intersectsWith(box)) {
                T value = type.cast(player);
                if (filter == null || filter.apply(value)) out.add(value);
            }
            return out;
        }
    }

    private static final class TestShulker extends EntityShulker {
        TestShulker(World world) { super(world); }
        Random random() { return this.rand; }
    }

    private static void setRawSeed(Random random, long raw) {
        random.setSeed(raw ^ 0x5deece66dL);
    }

    private static long rawSeed(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & MASK;
    }

    private static Object field(Object owner, String name) throws Exception {
        Class<?> type = owner.getClass();
        while (type != null) {
            try {
                Field field = type.getDeclaredField(name);
                field.setAccessible(true);
                return field.get(owner);
            } catch (NoSuchFieldException missing) {
                type = type.getSuperclass();
            }
        }
        throw new NoSuchFieldException(name);
    }

    private static void setField(Object owner, String name, Object value)
            throws Exception {
        Class<?> type = owner.getClass();
        while (type != null) {
            try {
                Field target = type.getDeclaredField(name);
                target.setAccessible(true);
                target.set(owner, value);
                return;
            } catch (NoSuchFieldException missing) {
                type = type.getSuperclass();
            }
        }
        throw new NoSuchFieldException(name);
    }

    private static void invokeDirection(
            EntityShulkerBullet bullet,
            net.minecraft.util.EnumFacing.Axis excludedAxis)
            throws Exception {
        Method method = EntityShulkerBullet.class.getDeclaredMethod(
            "selectNextMoveDirection",
            net.minecraft.util.EnumFacing.Axis.class);
        method.setAccessible(true);
        method.invoke(bullet, excludedAxis);
    }

    private static String dbits(double value) {
        return String.format("%016x", Double.doubleToRawLongBits(value));
    }

    private static String fbits(float value) {
        return String.format("%08x", Float.floatToRawIntBits(value));
    }

    private static void attackTrace() throws Exception {
        MemoryWorld world = new MemoryWorld();
        world.blocks.put(new BlockPos(0, 63, 0), Blocks.PURPUR_BLOCK.getDefaultState());
        world.player = new TestPlayer(world);
        world.player.setPosition(8.5, 64.0, 0.5);
        TestShulker shulker = new TestShulker(world);
        shulker.setPosition(0.5, 64.0, 0.5);
        shulker.setAttachmentPos(new BlockPos(0, 64, 0));
        shulker.onInitialSpawn(new DifficultyInstance(EnumDifficulty.NORMAL,
            0L, 0L, 0.0F), (IEntityLivingData)null);
        setRawSeed(shulker.random(), 0x123456789abL);

        int bulletIndex = 0;
        for (int tick = 1; tick <= 80; ++tick) {
            shulker.onUpdate();
            System.out.printf("A %d %d %s %s %012x%n", tick,
                shulker.getPeekTick(),
                fbits(((Float)field(shulker, "peekAmount")).floatValue()),
                fbits(shulker.getHealth()), rawSeed(shulker.random()));
            while (bulletIndex < world.bullets.size()) {
                ++bulletIndex;
                System.out.printf("F %d%n", tick);
            }
        }
    }

    private static void bulletTrace() throws Exception {
        MemoryWorld world = new MemoryWorld();
        world.player = new TestPlayer(world);
        world.player.setPosition(6.5, 66.0, 4.5);
        TestShulker owner = new TestShulker(world);
        owner.setPosition(0.5, 64.0, 0.5);
        EntityShulkerBullet bullet = new EntityShulkerBullet(world);
        bullet.setPosition(0.5, 64.5, 0.5);
        setField(bullet, "owner", owner);
        setField(bullet, "target", world.player);
        setField(bullet, "direction", net.minecraft.util.EnumFacing.UP);
        setRawSeed((Random)field(bullet, "rand"), 0x102030405060L);
        invokeDirection(bullet, net.minecraft.util.EnumFacing.Axis.Y);
        for (int tick = 1; tick <= 24 && !bullet.isDead; ++tick) {
            bullet.onUpdate();
            System.out.printf("B %d %s %s %s %s %s %s %d %d%n", tick,
                dbits(bullet.posX), dbits(bullet.posY), dbits(bullet.posZ),
                dbits(bullet.motionX), dbits(bullet.motionY),
                dbits(bullet.motionZ),
                ((Integer)field(bullet, "steps")).intValue(),
                field(bullet, "direction") == null ? -1
                    : ((net.minecraft.util.EnumFacing)field(
                        bullet, "direction")).getIndex());
        }
    }

    private static void teleportTrace() throws Exception {
        MemoryWorld world = new MemoryWorld();
        for (int x = -9; x <= 9; ++x)
            for (int z = -9; z <= 9; ++z)
                world.blocks.put(new BlockPos(x, 62, z),
                    Blocks.END_STONE.getDefaultState());
        TestShulker shulker = new TestShulker(world);
        shulker.setPosition(0.5, 64.0, 0.5);
        shulker.setAttachmentPos(new BlockPos(0, 64, 0));
        setRawSeed(shulker.random(), 0x314159265358L);
        shulker.onUpdate();
        BlockPos pos = shulker.getAttachmentPos();
        System.out.printf("T %d %d %d %d %d %012x%n",
            pos.getX(), pos.getY(), pos.getZ(),
            shulker.getAttachmentFacing().getIndex(), shulker.getPeekTick(),
            rawSeed(shulker.random()));
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        attackTrace();
        bulletTrace();
        teleportTrace();
    }
}
