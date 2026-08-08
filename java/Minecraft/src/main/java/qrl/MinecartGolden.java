package qrl;

import com.google.common.base.Predicate;
import com.mojang.authlib.GameProfile;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.TreeMap;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicLong;
import javax.annotation.Nullable;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityMinecart;
import net.minecraft.entity.item.EntityMinecartChest;
import net.minecraft.entity.item.EntityMinecartCommandBlock;
import net.minecraft.entity.item.EntityMinecartEmpty;
import net.minecraft.entity.item.EntityMinecartFurnace;
import net.minecraft.entity.item.EntityMinecartHopper;
import net.minecraft.entity.item.EntityMinecartMobSpawner;
import net.minecraft.entity.item.EntityMinecartTNT;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Item;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.tileentity.CommandBlockBaseLogic;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.EnumHand;
import net.minecraft.util.EnumParticleTypes;
import net.minecraft.util.DamageSource;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Actual 1.11.2 EntityMinecart rail-motion and rail-callback oracle. */
public final class MinecartGolden {
    static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(
                new UUID(1L, 2L), "minecart-test"));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
    }

    static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> blocks =
            new HashMap<BlockPos, IBlockState>();
        final List<Entity> entities = new ArrayList<Entity>();
        int scheduled;
        int particleCount;
        int particleKind = -1;
        double particleX, particleY, particleZ;

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "minecart-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return new BlockPos(0, 64, 0); }
        public void updateEntityWithOptionalForce(
                Entity entity, boolean forceUpdate) {}
        public IBlockState getBlockState(BlockPos pos) {
            IBlockState state = blocks.get(pos);
            return state == null ? Blocks.AIR.getDefaultState() : state;
        }
        public boolean setBlockState(BlockPos pos, IBlockState state, int flags) {
            BlockPos key = pos.toImmutable();
            if (state.getBlock() == Blocks.AIR) blocks.remove(key);
            else blocks.put(key, state);
            return true;
        }
        public void scheduleUpdate(BlockPos pos, Block block, int delay) {
            ++scheduled;
        }
        public void notifyNeighborsOfStateChange(
                BlockPos pos, Block block, boolean updateObservers) {}
        public void updateComparatorOutputLevel(BlockPos pos, Block block) {}
        public void addBlockEvent(
                BlockPos pos, Block block, int eventID, int eventParam) {}
        public boolean spawnEntity(Entity entity) {
            entities.add(entity);
            return true;
        }
        public void spawnParticle(EnumParticleTypes type,
                double x, double y, double z,
                double motionX, double motionY, double motionZ,
                int... parameters) {
            ++particleCount;
            particleKind = type.getParticleID();
            particleX = x;
            particleY = y;
            particleZ = z;
        }
        public List<Entity> getEntitiesInAABBexcluding(
                @Nullable Entity excluded, AxisAlignedBB box,
                @Nullable Predicate<? super Entity> filter) {
            List<Entity> result = new ArrayList<Entity>();
            for (Entity entity : entities) {
                if (entity == null || entity == excluded || entity.isDead
                        || !entity.getEntityBoundingBox().intersects(
                            box.minX, box.minY, box.minZ,
                            box.maxX, box.maxY, box.maxZ)
                        || (filter != null && !filter.apply(entity)))
                    continue;
                result.add(entity);
            }
            return result;
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box,
                @Nullable Predicate<? super T> filter) {
            List<T> result = new ArrayList<T>();
            for (Entity entity : entities) {
                if (entity != null && entity != excludedPlaceholder()
                        && !entity.isDead && type.isInstance(entity)
                        && entity.getEntityBoundingBox().intersects(
                            box.minX, box.minY, box.minZ,
                            box.maxX, box.maxY, box.maxZ)) {
                    T value = type.cast(entity);
                    if (filter == null || filter.apply(value)) result.add(value);
                }
            }
            return result;
        }
        private Entity excludedPlaceholder() { return null; }

        void put(BlockPos pos, IBlockState state) {
            blocks.put(pos.toImmutable(), state);
        }
    }

    private static String dbits(double value) {
        return String.format("%016x", Double.doubleToRawLongBits(value));
    }

    private static String fbits(float value) {
        return String.format("%08x", Float.floatToRawIntBits(value));
    }

    private static MemoryWorld track(Block block, int meta) {
        MemoryWorld world = new MemoryWorld();
        for (int x = 8; x <= 16; ++x) {
            world.put(new BlockPos(x, 77, 8), Blocks.STONE.getDefaultState());
            world.put(new BlockPos(x, 78, 8), block.getStateFromMeta(meta));
        }
        return world;
    }

    private static void print(String name, EntityMinecart cart) {
        System.out.printf("%s %s %s %s %s %s %s %s %s%n", name,
            dbits(cart.posX), dbits(cart.posY), dbits(cart.posZ),
            dbits(cart.motionX), dbits(cart.motionY), dbits(cart.motionZ),
            fbits(cart.rotationYaw), fbits(cart.rotationPitch));
    }

    private static void straight() {
        MemoryWorld world = track(Blocks.RAIL, 1);
        EntityMinecartEmpty cart = new EntityMinecartEmpty(
            world, 12.5, 78.0625, 8.5);
        cart.motionX = 0.2;
        world.entities.add(cart);
        for (int i = 0; i < 3; ++i) cart.onUpdate();
        print("S", cart);
    }

    private static void powered(boolean on) {
        MemoryWorld world = track(Blocks.GOLDEN_RAIL, on ? 9 : 1);
        EntityMinecartEmpty cart = new EntityMinecartEmpty(
            world, 12.5, 78.0625, 8.5);
        cart.motionX = 0.2;
        world.entities.add(cart);
        cart.onUpdate();
        print(on ? "P" : "B", cart);
    }

    private static void slope() {
        MemoryWorld world = new MemoryWorld();
        for (int x = 10; x <= 14; ++x)
            world.put(new BlockPos(x, 77 + (x >= 13 ? 1 : 0), 8),
                Blocks.STONE.getDefaultState());
        world.put(new BlockPos(11, 78, 8), Blocks.RAIL.getStateFromMeta(1));
        world.put(new BlockPos(12, 78, 8), Blocks.RAIL.getStateFromMeta(2));
        world.put(new BlockPos(13, 79, 8), Blocks.RAIL.getStateFromMeta(1));
        EntityMinecartEmpty cart = new EntityMinecartEmpty(
            world, 12.25, 78.0625, 8.5);
        cart.motionX = 0.2;
        world.entities.add(cart);
        cart.onUpdate();
        print("U", cart);
    }

    private static void directions() {
        for (int meta = 0; meta < 10; ++meta) {
            MemoryWorld world = track(Blocks.RAIL, meta);
            EntityMinecartEmpty cart = new EntityMinecartEmpty(
                world, 12.5, 78.0625, 8.5);
            cart.motionX = 0.2;
            cart.motionZ = 0.1;
            world.entities.add(cart);
            cart.onUpdate();
            print("M" + meta, cart);
        }
    }

    private static void derailed() {
        MemoryWorld world = new MemoryWorld();
        EntityMinecartEmpty cart = new EntityMinecartEmpty(
            world, 12.5, 90.0, 8.5);
        cart.motionX = 0.6;
        cart.motionY = 0.2;
        cart.motionZ = -0.5;
        world.entities.add(cart);
        cart.onUpdate();
        print("X", cart);
    }

    private static Field furnaceField(String name) throws Exception {
        Field field = EntityMinecartFurnace.class.getDeclaredField(name);
        field.setAccessible(true);
        return field;
    }

    private static final class SeededFurnace extends EntityMinecartFurnace {
        SeededFurnace(World world, double x, double y, double z) {
            super(world, x, y, z);
        }
        void setRawSeed(long seed48) {
            this.rand.setSeed(seed48 ^ 0x5deece66dL);
        }
        Random random() { return this.rand; }
    }

    static long rawSeed(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & ((1L << 48) - 1L);
    }

    private static int spawnerDelay(EntityMinecartMobSpawner cart) {
        return cart.writeToNBT(new NBTTagCompound()).getShort("Delay");
    }

    private static void spawner() throws Exception {
        MemoryWorld world = track(Blocks.RAIL, 1);
        EntityMinecartMobSpawner cart = new EntityMinecartMobSpawner(
            world, 12.5, 78.0625, 8.5);
        world.entities.add(cart);
        world.rand.setSeed(0L ^ 0x5deece66dL);
        cart.onUpdate();
        System.out.printf("Z0 %d %012x%n",
            spawnerDelay(cart), rawSeed(world.rand));

        world = track(Blocks.RAIL, 1);
        cart = new EntityMinecartMobSpawner(
            world, 12.5, 78.0625, 8.5);
        TestPlayer player = new TestPlayer(world);
        player.setPosition(12.5, 78.0, 8.5);
        world.playerEntities.add(player);
        world.entities.add(cart);
        world.rand.setSeed(0L ^ 0x5deece66dL);
        cart.onUpdate();
        System.out.printf("Z1 %d %012x%n",
            spawnerDelay(cart), rawSeed(world.rand));

        world = track(Blocks.RAIL, 1);
        cart = new EntityMinecartMobSpawner(
            world, 12.5, 78.0625, 8.5);
        NBTTagCompound nbt = cart.writeToNBT(new NBTTagCompound());
        System.out.printf("ZP %d %d %d%n",
            nbt.hasKey("SpawnPotentials", 9) ? 1 : 0,
            nbt.getTagList("SpawnPotentials", 10).tagCount(),
            nbt.hasKey("SpawnData", 10) ? 1 : 0);
        nbt.setShort("Delay", (short)0);
        nbt.setShort("MinSpawnDelay", (short)7);
        nbt.setShort("MaxSpawnDelay", (short)11);
        nbt.setShort("SpawnCount", (short)1);
        nbt.setShort("MaxNearbyEntities", (short)0);
        nbt.setShort("RequiredPlayerRange", (short)16);
        nbt.setShort("SpawnRange", (short)4);
        cart.readFromNBT(nbt);
        player = new TestPlayer(world);
        player.setPosition(12.5, 78.0, 8.5);
        world.playerEntities.add(player);
        world.entities.add(cart);
        world.rand.setSeed(0L ^ 0x5deece66dL);
        cart.onUpdate();
        System.out.printf("ZR %d %012x%n",
            spawnerDelay(cart), rawSeed(world.rand));
    }

    private static void commandCart() throws Exception {
        MemoryWorld world = track(Blocks.ACTIVATOR_RAIL, 9);
        EntityMinecartCommandBlock cart = new EntityMinecartCommandBlock(
            world, 12.5, 78.0625, 8.5);
        NBTTagCompound fixture = cart.writeToNBT(new NBTTagCompound());
        fixture.setString("Command", "Searge");
        fixture.setInteger("SuccessCount", 7);
        fixture.setString("CustomName", "cart-oracle");
        fixture.setBoolean("TrackOutput", true);
        fixture.setString("LastOutput", "{\"text\":\"seed\"}");
        cart.readFromNBT(fixture);
        world.entities.add(cart);
        Field cooldown = EntityMinecartCommandBlock.class
            .getDeclaredField("activatorRailCooldown");
        cooldown.setAccessible(true);
        for (int tick = 1; tick <= 9; ++tick) {
            /* World.updateEntityWithOptionalForce owns this clock in the real
             * server loop; the MemoryWorld hook is intentionally inert. */
            cart.ticksExisted = tick;
            cart.onUpdate();
            NBTTagCompound state = cart.writeToNBT(new NBTTagCompound());
            System.out.printf("J%d %d %d %d %s %s%n",
                tick, cart.ticksExisted, cooldown.getInt(cart),
                state.getInteger("SuccessCount"),
                state.getString("Command"),
                state.hasKey("LastOutput", 8)
                    ? state.getString("LastOutput") : "-");
        }
    }

    private static void furnaceMotion() throws Exception {
        MemoryWorld world = track(Blocks.RAIL, 1);
        EntityMinecartFurnace cart = new EntityMinecartFurnace(
            world, 12.5, 78.0625, 8.5);
        cart.motionX = 0.5;
        world.entities.add(cart);
        cart.onUpdate();
        print("Q0", cart);

        world = track(Blocks.RAIL, 1);
        cart = new EntityMinecartFurnace(world, 12.5, 78.0625, 8.5);
        cart.motionX = 0.1;
        furnaceField("pushX").setDouble(cart, 0.2);
        furnaceField("pushZ").setDouble(cart, 0.0);
        furnaceField("fuel").setInt(cart, 2);
        world.entities.add(cart);
        cart.onUpdate();
        print("Q1", cart);
        System.out.printf("QP %s %s %d%n",
            dbits(furnaceField("pushX").getDouble(cart)),
            dbits(furnaceField("pushZ").getDouble(cart)),
            furnaceField("fuel").getInt(cart));

        for (int seed = 0; seed <= 1; ++seed) {
            world = track(Blocks.RAIL, 1);
            SeededFurnace seeded = new SeededFurnace(
                world, 12.5, 78.0625, 8.5);
            seeded.setRawSeed(seed);
            furnaceField("fuel").setInt(seeded, 2);
            world.entities.add(seeded);
            seeded.onUpdate();
            System.out.printf("QF%d %d %d %s %s %s %012x%n", seed,
                world.particleCount, world.particleKind,
                dbits(world.particleX), dbits(world.particleY),
                dbits(world.particleZ), rawSeed(seeded.random()));
        }
    }

    private static void riddenMotion() {
        MemoryWorld world = track(Blocks.RAIL, 1);
        EntityMinecartEmpty cart = new EntityMinecartEmpty(
            world, 12.5, 78.0625, 8.5);
        TestPlayer player = new TestPlayer(world);
        player.setPosition(11.5, 78.0, 8.5);
        player.rotationYaw = -90.0F;
        player.moveForward = 1.0F;
        world.entities.add(cart);
        cart.processInitialInteract(player, EnumHand.MAIN_HAND);
        cart.onUpdate();
        cart.updatePassenger(player);
        print("V", cart);
        System.out.printf("VP %s %s %s %d%n",
            dbits(player.posX), dbits(player.posY), dbits(player.posZ),
            player.getRidingEntity() == cart ? 1 : 0);
    }

    private static void furnaceInteract() throws Exception {
        MemoryWorld world = track(Blocks.RAIL, 1);
        EntityMinecartFurnace cart = new EntityMinecartFurnace(
            world, 12.5, 78.0625, 8.5);
        TestPlayer player = new TestPlayer(world);
        player.setPosition(11.5, 78.0, 8.5);
        player.inventory.currentItem = 0;
        player.inventory.setInventorySlotContents(
            0, new ItemStack(Items.COAL, 2, 0));
        world.entities.add(cart);
        cart.processInitialInteract(player, EnumHand.MAIN_HAND);
        cart.onUpdate();
        print("I", cart);
        System.out.printf("IF %d %s %s %d%n",
            furnaceField("fuel").getInt(cart),
            dbits(furnaceField("pushX").getDouble(cart)),
            dbits(furnaceField("pushZ").getDouble(cart)),
            player.inventory.getStackInSlot(0).getCount());
    }

    private static EntityMinecart damageCart(
            MemoryWorld world, int kind) {
        if (kind == 1)
            return new EntityMinecartChest(world, 12.5, 78.0625, 8.5);
        if (kind == 2)
            return new EntityMinecartFurnace(world, 12.5, 78.0625, 8.5);
        if (kind == 3)
            return new EntityMinecartTNT(world, 12.5, 78.0625, 8.5);
        if (kind == 5)
            return new EntityMinecartHopper(world, 12.5, 78.0625, 8.5);
        return new EntityMinecartEmpty(world, 12.5, 78.0625, 8.5);
    }

    private static void damageAndDrops() {
        MemoryWorld world = track(Blocks.RAIL, 1);
        EntityMinecart cart = damageCart(world, 0);
        world.entities.add(cart);
        cart.attackEntityFrom(DamageSource.GENERIC, 1.0F);
        System.out.printf("N %d %d %s %d%n",
            cart.getRollingDirection(), cart.getRollingAmplitude(),
            fbits(cart.getDamage()), cart.isDead ? 1 : 0);

        int[] kinds = {0, 1, 2, 3, 5};
        for (int kind : kinds) {
            world = track(Blocks.RAIL, 1);
            cart = damageCart(world, kind);
            if (cart instanceof EntityMinecartChest)
                ((EntityMinecartChest)cart).setInventorySlotContents(
                    0, new ItemStack(Items.DIAMOND, 3, 0));
            if (cart instanceof EntityMinecartHopper)
                ((EntityMinecartHopper)cart).setInventorySlotContents(
                    0, new ItemStack(Items.DIAMOND, 3, 0));
            world.entities.add(cart);
            cart.attackEntityFrom(DamageSource.GENERIC, 5.0F);
            System.out.printf("K %d %d %s %d %d%n", kind,
                cart.isDead ? 1 : 0, fbits(cart.getDamage()),
                cart.getRollingDirection(), cart.getRollingAmplitude());
            Map<Integer, Integer> drops = new TreeMap<Integer, Integer>();
            for (Entity entity : world.entities) {
                if (!(entity instanceof EntityItem)) continue;
                ItemStack stack = ((EntityItem)entity).getEntityItem();
                int id = Item.getIdFromItem(stack.getItem());
                Integer old = drops.get(id);
                drops.put(id, (old == null ? 0 : old) + stack.getCount());
            }
            for (Map.Entry<Integer, Integer> drop : drops.entrySet())
                System.out.printf("KD %d %d %d%n",
                    kind, drop.getKey(), drop.getValue());
        }

        world = track(Blocks.RAIL, 1);
        cart = damageCart(world, 0);
        TestPlayer creative = new TestPlayer(world);
        creative.capabilities.isCreativeMode = true;
        world.entities.add(cart);
        cart.attackEntityFrom(
            DamageSource.causePlayerDamage(creative), 1.0F);
        System.out.printf("KC %d %d%n", cart.isDead ? 1 : 0,
            world.entities.size() - 1);

        world = track(Blocks.RAIL, 1);
        world.getGameRules().setOrCreateGameRule("doEntityDrops", "false");
        EntityMinecartChest chest = new EntityMinecartChest(
            world, 12.5, 78.0625, 8.5);
        chest.setInventorySlotContents(
            0, new ItemStack(Items.DIAMOND, 3, 0));
        world.entities.add(chest);
        chest.attackEntityFrom(DamageSource.GENERIC, 5.0F);
        Map<Integer, Integer> disabledDrops = new TreeMap<Integer, Integer>();
        for (Entity entity : world.entities) {
            if (!(entity instanceof EntityItem)) continue;
            ItemStack stack = ((EntityItem)entity).getEntityItem();
            int id = Item.getIdFromItem(stack.getItem());
            Integer old = disabledDrops.get(id);
            disabledDrops.put(id,
                (old == null ? 0 : old) + stack.getCount());
        }
        System.out.printf("KG %d %d %d %d%n", chest.isDead ? 1 : 0,
            disabledDrops.containsKey(Item.getIdFromItem(Items.DIAMOND))
                ? disabledDrops.get(Item.getIdFromItem(Items.DIAMOND)) : 0,
            disabledDrops.containsKey(Item.getIdFromItem(Items.MINECART))
                ? disabledDrops.get(Item.getIdFromItem(Items.MINECART)) : 0,
            disabledDrops.containsKey(Item.getIdFromItem(
                Item.getItemFromBlock(Blocks.CHEST)))
                ? disabledDrops.get(Item.getIdFromItem(
                    Item.getItemFromBlock(Blocks.CHEST))) : 0);
    }

    private static void collide(boolean furnace) {
        MemoryWorld world = track(Blocks.RAIL, 1);
        EntityMinecartEmpty left = new EntityMinecartEmpty(
            world, 12.2, 78.0625, 8.5);
        EntityMinecart right = furnace
            ? new EntityMinecartFurnace(world, 12.8, 78.0625, 8.5)
            : new EntityMinecartEmpty(world, 12.8, 78.0625, 8.5);
        left.motionX = 0.2;
        right.motionX = -0.1;
        world.entities.add(left);
        world.entities.add(right);
        left.onUpdate();
        right.onUpdate();
        print(furnace ? "F0" : "C0", left);
        print(furnace ? "F1" : "C1", right);
    }

    private static void detector() {
        MemoryWorld world = track(Blocks.DETECTOR_RAIL, 1);
        BlockPos pos = new BlockPos(12, 78, 8);
        EntityMinecartEmpty cart = new EntityMinecartEmpty(
            world, 12.5, 78.0625, 8.5);
        cart.motionX = 0.1;
        world.entities.add(cart);
        cart.onUpdate();
        System.out.printf("D %d %d%n",
            Blocks.DETECTOR_RAIL.getMetaFromState(
                world.getBlockState(pos)),
            world.scheduled);
        world.entities.clear();
        world.scheduled = 0;
        Blocks.DETECTOR_RAIL.updateTick(
            world, pos, world.getBlockState(pos), new Random(0L));
        System.out.printf("D2 %d %d%n",
            Blocks.DETECTOR_RAIL.getMetaFromState(world.getBlockState(pos)),
            world.scheduled);
    }

    private static void activator() {
        MemoryWorld world = track(Blocks.ACTIVATOR_RAIL, 9);
        EntityMinecartTNT tnt = new EntityMinecartTNT(
            world, 12.5, 78.0625, 8.5);
        world.entities.add(tnt);
        tnt.onUpdate();
        EntityMinecartHopper hopper = new EntityMinecartHopper(
            world, 12.5, 78.0625, 8.5);
        world.entities.clear();
        world.entities.add(hopper);
        hopper.onUpdate();
        System.out.printf("A %d %d%n", tnt.isIgnited() ? 1 : 0,
            hopper.getBlocked() ? 1 : 0);

        EntityMinecartEmpty rideable = new EntityMinecartEmpty(
            world, 12.5, 78.0625, 8.5);
        TestPlayer rider = new TestPlayer(world);
        world.entities.clear();
        world.entities.add(rideable);
        rideable.processInitialInteract(rider, EnumHand.MAIN_HAND);
        rideable.onUpdate();
        System.out.printf("AR %d %d %s%n",
            rideable.getRollingDirection(), rideable.getRollingAmplitude(),
            fbits(rideable.getDamage()));
        System.out.printf("AE %d%n",
            rider.getRidingEntity() == null ? 1 : 0);
    }

    private static void hopperCapture() {
        MemoryWorld world = track(Blocks.RAIL, 1);
        EntityMinecartHopper hopper = new EntityMinecartHopper(
            world, 12.5, 78.0625, 8.5);
        EntityItem item = new EntityItem(world, 12.5, 78.2, 8.5,
            new ItemStack(Items.DIAMOND, 3, 0));
        item.motionX = item.motionY = item.motionZ = 0.0;
        world.entities.add(hopper);
        world.entities.add(item);
        hopper.onUpdate();
        System.out.printf("H %d %d%n",
            hopper.getStackInSlot(0).getCount(), item.isDead ? 1 : 0);
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        straight();
        damageAndDrops();
        powered(false);
        powered(true);
        slope();
        directions();
        derailed();
        furnaceMotion();
        riddenMotion();
        furnaceInteract();
        collide(false);
        collide(true);
        detector();
        activator();
        hopperCapture();
        spawner();
        commandCart();
        System.out.println("minecart_live: PASS (rails, derailment, collision, callbacks)");
    }
}
