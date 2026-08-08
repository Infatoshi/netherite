package qrl;

import com.mojang.authlib.GameProfile;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.util.UUID;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.monster.EntityElderGuardian;
import net.minecraft.entity.monster.EntityGuardian;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.DamageSource;
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

/** Direct 1.11.2 Guardian attributes, beam attack, and thorns oracle. */
public final class GuardianGolden {
    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(new UUID(13L, 17L), "guardian-target"));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
    }

    private static final class MemoryWorld extends World {
        int lastStatus;
        int statusCount;

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "guardian-oracle"),
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
        public void setEntityState(Entity entityIn, byte state) {
            lastStatus = state & 255;
            ++statusCount;
        }
    }

    private static Object attackTask(EntityGuardian guardian) throws Exception {
        for (Class<?> type : EntityGuardian.class.getDeclaredClasses())
            if (type.getSimpleName().equals("AIGuardianAttack")) {
                Constructor<?> constructor =
                    type.getDeclaredConstructor(EntityGuardian.class);
                constructor.setAccessible(true);
                return constructor.newInstance(guardian);
            }
        throw new ClassNotFoundException("AIGuardianAttack");
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

    private static void attributes(String label, EntityGuardian guardian) {
        System.out.printf(
            "%s %08x %08x %08x %016x %016x %016x %016x %d%n",
            label,
            Float.floatToRawIntBits(guardian.width),
            Float.floatToRawIntBits(guardian.height),
            Float.floatToRawIntBits(guardian.getEyeHeight()),
            Double.doubleToRawLongBits(guardian.getMaxHealth()),
            Double.doubleToRawLongBits(guardian.getEntityAttribute(
                SharedMonsterAttributes.MOVEMENT_SPEED).getAttributeValue()),
            Double.doubleToRawLongBits(guardian.getEntityAttribute(
                SharedMonsterAttributes.ATTACK_DAMAGE).getAttributeValue()),
            Double.doubleToRawLongBits(guardian.getEntityAttribute(
                SharedMonsterAttributes.FOLLOW_RANGE).getAttributeValue()),
            guardian.getAttackDuration());
    }

    private static void beam(String label, EntityGuardian guardian,
            MemoryWorld world) throws Exception {
        TestPlayer player = new TestPlayer(world);
        player.setEntityId(77);
        player.setPosition(8.5D, 1.0D, 0.5D);
        player.setHealth(20.0F);
        guardian.setEntityId(33);
        guardian.setPosition(0.5D, 1.0D, 0.5D);
        guardian.setAttackTarget(player);
        Object task = attackTask(guardian);
        call(task, "startExecuting");
        int targetTick = -1;
        int damageTick = -1;
        for (int tick = 1; tick <= 100; ++tick) {
            float before = player.getHealth();
            call(task, "updateTask");
            if (targetTick < 0 && guardian.hasTargetedEntity()) targetTick = tick;
            if (damageTick < 0 && player.getHealth() != before) damageTick = tick;
            if (guardian.getAttackTarget() == null) break;
        }
        System.out.printf("%s %d %d %08x %d %d%n", label,
            targetTick, damageTick, Float.floatToRawIntBits(player.getHealth()),
            world.lastStatus, world.statusCount);
    }

    private static void thorns() {
        MemoryWorld world = new MemoryWorld();
        EntityGuardian guardian = new EntityGuardian(world);
        guardian.setPosition(0.5D, 1.0D, 0.5D);
        guardian.setHealth(30.0F);
        TestPlayer player = new TestPlayer(world);
        player.setPosition(1.5D, 1.0D, 0.5D);
        player.setHealth(20.0F);
        guardian.attackEntityFrom(DamageSource.causeMobDamage(player), 1.0F);
        System.out.printf("T %08x %08x%n",
            Float.floatToRawIntBits(player.getHealth()),
            Float.floatToRawIntBits(guardian.getHealth()));
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.init.Bootstrap.register();
        MemoryWorld normalWorld = new MemoryWorld();
        EntityGuardian normal = new EntityGuardian(normalWorld);
        attributes("G", normal);
        beam("B", normal, normalWorld);
        MemoryWorld elderWorld = new MemoryWorld();
        EntityElderGuardian elder = new EntityElderGuardian(elderWorld);
        attributes("E", elder);
        beam("D", elder, elderWorld);
        thorns();
    }
}
