package qrl;

import com.mojang.authlib.GameProfile;
import java.lang.reflect.Field;
import java.util.Random;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.passive.EntityOcelot;
import net.minecraft.entity.passive.EntityWolf;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.EnumHand;
import net.minecraft.util.EnumParticleTypes;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Real 1.11.2 wolf/ocelot tame, heal, dye, and sit interaction oracle. */
public final class TameableGolden {
    private static final UUID PLAYER_ID = new UUID(1L, 2L);

    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(PLAYER_ID, "pet-owner"));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() {
            return this.capabilities.isCreativeMode;
        }
    }

    private static final class MemoryWorld extends World {
        TestPlayer player;
        int status = -1;
        int particles;

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "tameable-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            return Blocks.AIR.getDefaultState();
        }
        public EntityPlayer getPlayerEntityByUUID(UUID id) {
            return player != null && PLAYER_ID.equals(id) ? player : null;
        }
        public void setEntityState(Entity entity, byte value) {
            status = value;
        }
        public void spawnParticle(EnumParticleTypes type,
                double x, double y, double z,
                double vx, double vy, double vz, int... parameters) {
            ++particles;
        }
    }

    private static final class TestWolf extends EntityWolf {
        TestWolf(World world) { super(world); }
        void setRawSeed(long seed48) {
            this.rand.setSeed(seed48 ^ 0x5deece66dL);
        }
        Random random() { return this.rand; }
    }

    private static final class TestOcelot extends EntityOcelot {
        TestOcelot(World world) { super(world); }
        void setRawSeed(long seed48) {
            this.rand.setSeed(seed48 ^ 0x5deece66dL);
        }
        Random random() { return this.rand; }
    }

    private static long rawSeed(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & ((1L << 48) - 1L);
    }

    private static void setRawSeed(Random random, long seed48) {
        random.setSeed(seed48 ^ 0x5deece66dL);
    }

    private static String fbits(float value) {
        return String.format("%08x", Float.floatToRawIntBits(value));
    }

    private static void print(String name, boolean handled, ItemStack held,
            boolean tamed, boolean sitting, float health, int variant,
            MemoryWorld world, Random entityRandom) throws Exception {
        System.out.printf(
            "%s %d %d %d %d %d %s %d %d %d %012x %012x%n",
            name, handled ? 1 : 0,
            held.isEmpty() ? 0 : held.getCount(),
            tamed ? 1 : 0, sitting ? 1 : 0, variant, fbits(health),
            world.status, world.particles,
            world.player.capabilities.isCreativeMode ? 1 : 0,
            rawSeed(entityRandom), rawSeed(world.rand));
    }

    private static MemoryWorld world(boolean creative, long worldSeed48) {
        MemoryWorld world = new MemoryWorld();
        setRawSeed(world.rand, worldSeed48);
        world.player = new TestPlayer(world);
        world.player.capabilities.isCreativeMode = creative;
        return world;
    }

    private static void wolfTame(String name, long seed48, boolean creative)
            throws Exception {
        MemoryWorld world = world(creative, 0x123456789ABCL);
        TestWolf wolf = new TestWolf(world);
        wolf.setRawSeed(seed48);
        wolf.setPosition(1.0, 64.0, 1.0);
        ItemStack held = new ItemStack(Items.BONE, 2);
        world.player.setHeldItem(EnumHand.MAIN_HAND, held);
        boolean handled = wolf.processInteract(world.player, EnumHand.MAIN_HAND);
        print(name, handled, held, wolf.isTamed(), wolf.isSitting(),
            wolf.getHealth(), wolf.getCollarColor().getDyeDamage(),
            world, wolf.random());
    }

    private static TestWolf ownedWolf(MemoryWorld world, float health) {
        TestWolf wolf = new TestWolf(world);
        wolf.setOwnerId(PLAYER_ID);
        wolf.setTamed(true);
        wolf.setHealth(health);
        wolf.setRawSeed(0x102030405060L);
        return wolf;
    }

    private static void wolfOwned() throws Exception {
        MemoryWorld world = world(false, 0x123456789ABCL);
        TestWolf wolf = ownedWolf(world, 11.0F);
        if (!wolf.isOwner(world.player)) {
            throw new IllegalStateException("wolf owner fixture mismatch");
        }
        ItemStack meat = new ItemStack(Items.PORKCHOP, 2);
        world.player.setHeldItem(EnumHand.MAIN_HAND, meat);
        boolean handled = wolf.processInteract(world.player, EnumHand.MAIN_HAND);
        print("WH", handled, meat, wolf.isTamed(), wolf.isSitting(),
            wolf.getHealth(), wolf.getCollarColor().getDyeDamage(),
            world, wolf.random());

        world.status = -1;
        world.particles = 0;
        ItemStack dye = new ItemStack(Items.DYE, 2, 4);
        world.player.setHeldItem(EnumHand.MAIN_HAND, dye);
        handled = wolf.processInteract(world.player, EnumHand.MAIN_HAND);
        print("WD", handled, dye, wolf.isTamed(), wolf.isSitting(),
            wolf.getHealth(), wolf.getCollarColor().getDyeDamage(),
            world, wolf.random());

        world.status = -1;
        world.particles = 0;
        ItemStack empty = new ItemStack(Items.STICK, 2);
        world.player.setHeldItem(EnumHand.MAIN_HAND, empty);
        handled = wolf.processInteract(world.player, EnumHand.MAIN_HAND);
        print("WS", handled, empty, wolf.isTamed(), wolf.isSitting(),
            wolf.getHealth(), wolf.getCollarColor().getDyeDamage(),
            world, wolf.random());
    }

    private static void ocelotTame(String name, long seed48, boolean creative)
            throws Exception {
        MemoryWorld world = world(creative, 0x23456789ABCDL);
        TestOcelot cat = new TestOcelot(world);
        cat.setRawSeed(seed48);
        cat.setPosition(1.0, 64.0, 1.0);
        Field tempt = EntityOcelot.class.getDeclaredField("aiTempt");
        tempt.setAccessible(true);
        tempt.set(cat, null);
        world.player.setPosition(1.0, 64.0, 2.0);
        ItemStack held = new ItemStack(Items.FISH, 2);
        world.player.setHeldItem(EnumHand.MAIN_HAND, held);
        boolean handled = cat.processInteract(world.player, EnumHand.MAIN_HAND);
        print(name, handled, held, cat.isTamed(), cat.isSitting(),
            cat.getHealth(), cat.getTameSkin(), world, cat.random());
    }

    private static void printBreed(String name, boolean handled,
            ItemStack held, net.minecraft.entity.passive.EntityAnimal animal,
            MemoryWorld world, Random random) throws Exception {
        System.out.printf("%s %d %d %d %d %d %012x %s %s%n",
            name, handled ? 1 : 0,
            held.isEmpty() ? 0 : held.getCount(),
            animal.getGrowingAge(), animal.isInLove() ? 600 : 0,
            world.status, rawSeed(random), fbits(animal.getHealth()),
            fbits(animal.getMaxHealth()));
    }

    private static void breeding() throws Exception {
        MemoryWorld wolfWorld = world(false, 0x123456789ABCL);
        TestWolf wolf = ownedWolf(wolfWorld, 20.0F);
        ItemStack beef = new ItemStack(Items.PORKCHOP, 2);
        wolfWorld.player.setHeldItem(EnumHand.MAIN_HAND, beef);
        boolean handled = wolf.processInteract(
            wolfWorld.player, EnumHand.MAIN_HAND);
        printBreed("WB", handled, beef, wolf, wolfWorld, wolf.random());

        MemoryWorld catWorld = world(false, 0x23456789ABCDL);
        TestOcelot cat = new TestOcelot(catWorld);
        cat.setOwnerId(PLAYER_ID);
        cat.setTamed(true);
        cat.setTameSkin(2);
        cat.setRawSeed(0x102030405060L);
        ItemStack fish = new ItemStack(Items.FISH, 2);
        catWorld.player.setHeldItem(EnumHand.MAIN_HAND, fish);
        handled = cat.processInteract(catWorld.player, EnumHand.MAIN_HAND);
        printBreed("OB", handled, fish, cat, catWorld, cat.random());
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        wolfTame("W0", 0L, false);
        wolfTame("W1", 1L, false);
        wolfTame("WC", 0L, true);
        wolfOwned();
        ocelotTame("O0", 0L, false);
        ocelotTame("O1", 1L, false);
        ocelotTame("OC", 0L, true);
        breeding();
    }
}
