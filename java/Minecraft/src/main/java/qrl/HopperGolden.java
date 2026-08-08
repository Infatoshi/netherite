package qrl;

import com.google.common.base.Predicate;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.lang.reflect.Field;
import java.util.concurrent.atomic.AtomicLong;
import javax.annotation.Nullable;
import net.minecraft.block.Block;
import net.minecraft.block.BlockDropper;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityBoat;
import net.minecraft.entity.item.EntityFireworkRocket;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityTNTPrimed;
import net.minecraft.entity.projectile.EntityFireball;
import net.minecraft.entity.projectile.EntityPotion;
import net.minecraft.entity.projectile.EntitySmallFireball;
import net.minecraft.entity.projectile.EntityEgg;
import net.minecraft.entity.projectile.EntitySnowball;
import net.minecraft.entity.item.EntityExpBottle;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.init.PotionTypes;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.potion.PotionUtils;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityDispenser;
import net.minecraft.tileentity.TileEntityDropper;
import net.minecraft.tileentity.TileEntityHopper;
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

/** Actual 1.11.2 TileEntityHopper transfer/cooldown oracle. */
public final class HopperGolden {
    private static final class ExposedDropper extends BlockDropper {
        void run(MemoryWorld world, BlockPos pos) {
            dispense(world, pos);
        }
    }

    private static final class ExposedDispenser
            extends net.minecraft.block.BlockDispenser {
        void run(MemoryWorld world, BlockPos pos) {
            dispense(world, pos);
        }
    }

    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> blocks =
            new HashMap<BlockPos, IBlockState>();
        final Map<BlockPos, TileEntity> tiles =
            new HashMap<BlockPos, TileEntity>();
        final List<Entity> entities = new ArrayList<Entity>();
        final List<Integer> events = new ArrayList<Integer>();

        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "hopper-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
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
        public TileEntity getTileEntity(BlockPos pos) {
            return tiles.get(pos);
        }
        public void markChunkDirty(BlockPos pos, TileEntity tile) {}
        public void updateComparatorOutputLevel(BlockPos pos, Block block) {}
        public boolean spawnEntity(Entity entity) {
            entities.add(entity);
            return true;
        }
        public void playEvent(int type, BlockPos pos, int data) {
            events.add(Integer.valueOf(type));
        }
        public List<Entity> getEntitiesInAABBexcluding(
                @Nullable Entity excluded, AxisAlignedBB box,
                @Nullable Predicate<? super Entity> filter) {
            return new ArrayList<Entity>();
        }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box,
                @Nullable Predicate<? super T> filter) {
            List<T> result = new ArrayList<T>();
            for (Entity entity : entities) {
                if (!entity.isDead && type.isInstance(entity)
                        && entity.getEntityBoundingBox().intersects(
                            box.minX, box.minY, box.minZ,
                            box.maxX, box.maxY, box.maxZ)) {
                    T value = type.cast(entity);
                    if (filter == null || filter.apply(value)) result.add(value);
                }
            }
            return result;
        }

        void put(BlockPos pos, IBlockState state, TileEntity tile) {
            blocks.put(pos.toImmutable(), state);
            tile.setWorld(this);
            tile.setPos(pos);
            tiles.put(pos.toImmutable(), tile);
        }

        void time(long value) {
            getWorldInfo().setWorldTotalTime(value);
        }
    }

    private static int cooldown(TileEntityHopper hopper) {
        return hopper.writeToNBT(new NBTTagCompound())
            .getInteger("TransferCooldown");
    }

    private static TileEntityHopper hopper(
            MemoryWorld world, BlockPos pos, int meta, int cooldown) {
        TileEntityHopper hopper = new TileEntityHopper();
        world.put(pos, Blocks.HOPPER.getStateFromMeta(meta), hopper);
        hopper.setTransferCooldown(cooldown);
        return hopper;
    }

    private static void tick(
            MemoryWorld world, List<TileEntityHopper> order,
            long firstTime, int count) {
        for (int i = 0; i < count; ++i) {
            world.time(firstTime + i);
            for (TileEntityHopper hopper : order) hopper.update();
        }
    }

    private static void transferCadence() {
        MemoryWorld world = new MemoryWorld();
        BlockPos sourcePos = new BlockPos(12, 78, 8);
        BlockPos destinationPos = sourcePos.east();
        TileEntityHopper source = hopper(world, sourcePos, 5, 0);
        TileEntityDispenser destination = new TileEntityDispenser();
        world.put(destinationPos,
            Blocks.DISPENSER.getStateFromMeta(2), destination);
        source.setInventorySlotContents(0,
            new ItemStack(Item.getItemFromBlock(Blocks.STONE), 3, 0));
        List<TileEntityHopper> order = new ArrayList<TileEntityHopper>();
        order.add(source);
        tick(world, order, 1, 1);
        System.out.printf("A 1 %d %d %d %d%n",
            source.getStackInSlot(0).getCount(),
            destination.getStackInSlot(0).getCount(),
            cooldown(source), source.getLastUpdateTime());
        tick(world, order, 2, 7);
        System.out.printf("A 8 %d %d %d %d%n",
            source.getStackInSlot(0).getCount(),
            destination.getStackInSlot(0).getCount(),
            cooldown(source), source.getLastUpdateTime());
        tick(world, order, 9, 1);
        System.out.printf("A 9 %d %d %d %d%n",
            source.getStackInSlot(0).getCount(),
            destination.getStackInSlot(0).getCount(),
            cooldown(source), source.getLastUpdateTime());
    }

    private static void powered() {
        MemoryWorld world = new MemoryWorld();
        BlockPos sourcePos = new BlockPos(12, 78, 8);
        TileEntityHopper source = hopper(world, sourcePos, 13, 0);
        TileEntityDropper destination = new TileEntityDropper();
        world.put(sourcePos.east(),
            Blocks.DROPPER.getStateFromMeta(2), destination);
        source.setInventorySlotContents(0,
            new ItemStack(Item.getItemFromBlock(Blocks.COBBLESTONE), 1, 0));
        List<TileEntityHopper> order = new ArrayList<TileEntityHopper>();
        order.add(source);
        tick(world, order, 1, 2);
        System.out.printf("P 2 %d %d %d %d%n",
            source.getStackInSlot(0).getCount(),
            destination.getStackInSlot(0).isEmpty()
                ? 0 : destination.getStackInSlot(0).getCount(),
            cooldown(source), source.getLastUpdateTime());
    }

    private static void chain() {
        MemoryWorld world = new MemoryWorld();
        BlockPos lowerPos = new BlockPos(12, 78, 8);
        TileEntityHopper upper = hopper(world, lowerPos.up(), 0, 0);
        TileEntityHopper lower = hopper(world, lowerPos, 5, 0);
        TileEntityDispenser destination = new TileEntityDispenser();
        world.put(lowerPos.east(),
            Blocks.DISPENSER.getStateFromMeta(2), destination);
        upper.setInventorySlotContents(0,
            new ItemStack(Item.getItemFromBlock(Blocks.PLANKS), 2, 0));
        List<TileEntityHopper> order = new ArrayList<TileEntityHopper>();
        order.add(upper);
        order.add(lower);
        tick(world, order, 1, 1);
        System.out.printf("C 1 %d %d %d %d %d%n",
            upper.getStackInSlot(0).getCount(),
            lower.getStackInSlot(0).getCount(),
            cooldown(upper), cooldown(lower), lower.getLastUpdateTime());
    }

    private static void itemCapture() {
        MemoryWorld world = new MemoryWorld();
        BlockPos pos = new BlockPos(12, 78, 8);
        TileEntityHopper hopper = hopper(world, pos, 5, 0);
        EntityItem entity = new EntityItem(world, 12.5, 79.0, 8.5,
            new ItemStack(Items.DIAMOND, 3, 0));
        entity.motionX = entity.motionY = entity.motionZ = 0.0;
        world.entities.add(entity);
        List<TileEntityHopper> order = new ArrayList<TileEntityHopper>();
        order.add(hopper);
        tick(world, order, 1, 1);
        System.out.printf("I 1 %d %d %d%n",
            hopper.getStackInSlot(0).getCount(), entity.isDead ? 1 : 0,
            cooldown(hopper));
    }

    private static void dropperInsert() {
        MemoryWorld world = new MemoryWorld();
        BlockPos sourcePos = new BlockPos(12, 78, 8);
        TileEntityDropper source = new TileEntityDropper();
        TileEntityDispenser destination = new TileEntityDispenser();
        world.put(sourcePos, Blocks.DROPPER.getStateFromMeta(13), source);
        world.put(sourcePos.east(),
            Blocks.DISPENSER.getStateFromMeta(2), destination);
        source.setInventorySlotContents(0,
            new ItemStack(Item.getItemFromBlock(Blocks.STONE), 2, 0));
        new ExposedDropper().run(world, sourcePos);
        System.out.printf("D 1 %d %d%n",
            source.getStackInSlot(0).getCount(),
            destination.getStackInSlot(0).getCount());
    }

    private static long randomSeed48(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get()
            & ((1L << 48) - 1L);
    }

    private static void setMathSeed48(long seed48) throws Exception {
        Class<?> holder = Class.forName(
            "java.lang.Math$RandomNumberGeneratorHolder");
        Field generator = holder.getDeclaredField("randomNumberGenerator");
        generator.setAccessible(true);
        Field seed = Random.class.getDeclaredField("seed");
        seed.setAccessible(true);
        ((AtomicLong)seed.get((Random)generator.get(null))).set(seed48);
    }

    private static boolean randomHaveGaussian(Random random)
            throws Exception {
        Field field = Random.class.getDeclaredField("haveNextNextGaussian");
        field.setAccessible(true);
        return field.getBoolean(random);
    }

    private static double randomGaussian(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("nextNextGaussian");
        field.setAccessible(true);
        return field.getDouble(random);
    }

    private static String dbits(double value) {
        return String.format("%016x", Double.doubleToRawLongBits(value));
    }

    private static String fbits(float value) {
        return String.format("%08x", Float.floatToRawIntBits(value));
    }

    private static void dispenserEject() throws Exception {
        MemoryWorld world = new MemoryWorld();
        BlockPos sourcePos = new BlockPos(12, 78, 8);
        TileEntityDispenser source = new TileEntityDispenser();
        world.put(sourcePos, Blocks.DISPENSER.getStateFromMeta(13), source);
        source.setInventorySlotContents(0,
            new ItemStack(Item.getItemFromBlock(Blocks.STONE), 2, 0));
        world.rand.setSeed(123L);
        setMathSeed48(0x123456789abcl);
        new net.minecraft.block.BlockDispenser() {}.updateTick(
            world, sourcePos, world.getBlockState(sourcePos), world.rand);
        EntityItem entity = (EntityItem)world.entities.get(0);
        entity.onUpdate();
        System.out.printf(
            "E 1 %s %s %s %s %s %s %s %s %d %d %d %s %d%n",
            dbits(entity.posX), dbits(entity.posY), dbits(entity.posZ),
            dbits(entity.motionX), dbits(entity.motionY),
            dbits(entity.motionZ), fbits(entity.rotationYaw),
            fbits(entity.hoverStart), source.getStackInSlot(0).getCount(),
            randomSeed48(world.rand), randomHaveGaussian(world.rand) ? 1 : 0,
            dbits(randomGaussian(world.rand)), 2);
    }

    private static TileEntityDispenser dispenser(
            MemoryWorld world, BlockPos pos, ItemStack stack) {
        TileEntityDispenser tile = new TileEntityDispenser();
        world.put(pos, Blocks.DISPENSER.getStateFromMeta(13), tile);
        tile.setInventorySlotContents(0, stack);
        return tile;
    }

    private static String eventPair(MemoryWorld world) {
        return world.events.size() == 2
            ? world.events.get(0) + " " + world.events.get(1)
            : "-1 -1";
    }

    private static void dispenserTnt() throws Exception {
        MemoryWorld world = new MemoryWorld();
        BlockPos pos = new BlockPos(12, 78, 8);
        TileEntityDispenser tile = dispenser(world, pos,
            new ItemStack(Item.getItemFromBlock(Blocks.TNT), 2, 0));
        setMathSeed48(0x13579bdf2468L);
        new ExposedDispenser().run(world, pos);
        EntityTNTPrimed entity = (EntityTNTPrimed)world.entities.get(0);
        entity.onUpdate();
        System.out.printf(
            "X T %d %s %s %s %s %s %s %d %s%n",
            tile.getStackInSlot(0).getCount(),
            dbits(entity.posX), dbits(entity.posY), dbits(entity.posZ),
            dbits(entity.motionX), dbits(entity.motionY),
            dbits(entity.motionZ), entity.getFuse(), eventPair(world));
    }

    private static void dispenserFireCharge() throws Exception {
        MemoryWorld world = new MemoryWorld();
        BlockPos pos = new BlockPos(12, 78, 8);
        TileEntityDispenser tile = dispenser(world, pos,
            new ItemStack(Items.FIRE_CHARGE, 2, 0));
        world.rand.setSeed(777L);
        new ExposedDispenser().run(world, pos);
        EntitySmallFireball entity =
            (EntitySmallFireball)world.entities.get(0);
        entity.onUpdate();
        System.out.printf(
            "X C %d %s %s %s %d %d %s%n",
            tile.getStackInSlot(0).getCount(),
            dbits(entity.posX), dbits(entity.posY), dbits(entity.posZ),
            randomSeed48(world.rand),
            randomHaveGaussian(world.rand) ? 1 : 0, eventPair(world));
    }

    private static void dispenserPotion() throws Exception {
        MemoryWorld world = new MemoryWorld();
        BlockPos pos = new BlockPos(12, 78, 8);
        ItemStack stack = PotionUtils.addPotionToItemStack(
            new ItemStack(Items.SPLASH_POTION, 1), PotionTypes.SWIFTNESS);
        TileEntityDispenser tile = dispenser(world, pos, stack);
        new ExposedDispenser().run(world, pos);
        EntityPotion entity = (EntityPotion)world.entities.get(0);
        entity.onUpdate();
        System.out.printf(
            "X P %d %d %s%n",
            tile.getStackInSlot(0).getCount(),
            entity.getPotion().getItem() == Items.SPLASH_POTION ? 438 : -1,
            eventPair(world));
    }

    private static void dispenserThrowables() throws Exception {
        Item[] items = { Items.EGG, Items.SNOWBALL, Items.EXPERIENCE_BOTTLE };
        for (int index = 0; index < items.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            BlockPos pos = new BlockPos(12, 78, 8);
            TileEntityDispenser tile = dispenser(
                world, pos, new ItemStack(items[index], 2, 0));
            new ExposedDispenser().run(world, pos);
            Entity entity = world.entities.get(0);
            int kind = entity instanceof EntityEgg ? 7
                : entity instanceof EntitySnowball ? 8
                : entity instanceof EntityExpBottle ? 9 : -1;
            System.out.printf("X Q %d %d %d %s%n",
                Item.getIdFromItem(items[index]),
                tile.getStackInSlot(0).getCount(), kind, eventPair(world));
        }
    }

    private static void dispenserFirework() throws Exception {
        MemoryWorld world = new MemoryWorld();
        BlockPos pos = new BlockPos(12, 78, 8);
        ItemStack stack = new ItemStack(Items.FIREWORKS, 2, 0);
        NBTTagCompound fireworks = new NBTTagCompound();
        fireworks.setByte("Flight", (byte)2);
        NBTTagCompound tag = new NBTTagCompound();
        tag.setTag("Fireworks", fireworks);
        stack.setTagCompound(tag);
        TileEntityDispenser tile = dispenser(world, pos, stack);
        new ExposedDispenser().run(world, pos);
        EntityFireworkRocket entity =
            (EntityFireworkRocket)world.entities.get(0);
        entity.onUpdate();
        NBTTagCompound saved = entity.writeToNBT(new NBTTagCompound());
        System.out.printf(
            "X F %d %d %s%n",
            tile.getStackInSlot(0).getCount(),
            saved.getInteger("Life"), eventPair(world));
    }

    private static void dispenserBucket() throws Exception {
        MemoryWorld world = new MemoryWorld();
        BlockPos pos = new BlockPos(12, 78, 8);
        TileEntityDispenser tile = dispenser(world, pos,
            new ItemStack(Items.WATER_BUCKET, 1, 0));
        new ExposedDispenser().run(world, pos);
        System.out.printf("X W %d %d %d %s%n",
            Item.getIdFromItem(tile.getStackInSlot(0).getItem()),
            tile.getStackInSlot(0).getCount(),
            Block.getIdFromBlock(world.getBlockState(pos.east()).getBlock()),
            eventPair(world));
    }

    private static void dispenserBoat() throws Exception {
        MemoryWorld world = new MemoryWorld();
        BlockPos pos = new BlockPos(12, 78, 8);
        world.setBlockState(pos.east(),
            Blocks.FLOWING_WATER.getDefaultState(), 3);
        TileEntityDispenser tile = dispenser(world, pos,
            new ItemStack(Items.BOAT, 2, 0));
        new ExposedDispenser().run(world, pos);
        EntityBoat entity = (EntityBoat)world.entities.get(0);
        System.out.printf("X B %d %s %s %s %s %s%n",
            tile.getStackInSlot(0).getCount(),
            dbits(entity.posX), dbits(entity.posY), dbits(entity.posZ),
            fbits(entity.rotationYaw), eventPair(world));
    }

    private static void dispenserFlint() throws Exception {
        BlockPos pos = new BlockPos(12, 78, 8);
        IBlockState[] targets = {
            Blocks.AIR.getDefaultState(),
            Blocks.STONE.getDefaultState(),
            Blocks.TNT.getDefaultState()
        };
        for (int index = 0; index < targets.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            ItemStack flint = new ItemStack(Items.FLINT_AND_STEEL, 1, 7);
            TileEntityDispenser tile = dispenser(world, pos, flint);
            world.setBlockState(pos.east().down(),
                Blocks.STONE.getDefaultState(), 3);
            world.setBlockState(pos.east(), targets[index], 3);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            System.out.printf("X L %d %d %d %d %s%n",
                index,
                left.isEmpty() ? 0 : left.getCount(),
                left.isEmpty() ? 0 : left.getItemDamage(),
                Block.getIdFromBlock(
                    world.getBlockState(pos.east()).getBlock()),
                eventPair(world));
        }
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        transferCadence();
        powered();
        chain();
        itemCapture();
        dropperInsert();
        dispenserEject();
        dispenserTnt();
        dispenserFireCharge();
        dispenserPotion();
        dispenserThrowables();
        dispenserFirework();
        dispenserBucket();
        dispenserBoat();
        dispenserFlint();
        System.out.println(
            "hopper_live: PASS (cooldown, transfer, chain, power, item capture)");
    }
}
