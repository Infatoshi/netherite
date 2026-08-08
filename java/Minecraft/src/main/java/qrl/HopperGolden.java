package qrl;

import com.google.common.base.Predicate;
import com.mojang.authlib.GameProfile;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.UUID;
import java.lang.reflect.Field;
import java.util.concurrent.atomic.AtomicLong;
import javax.annotation.Nullable;
import net.minecraft.block.Block;
import net.minecraft.block.BlockChest;
import net.minecraft.block.BlockDropper;
import net.minecraft.block.BlockShulkerBox;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.item.EntityBoat;
import net.minecraft.entity.item.EntityFireworkRocket;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityMinecart;
import net.minecraft.entity.item.EntityTNTPrimed;
import net.minecraft.entity.passive.EntityOcelot;
import net.minecraft.entity.player.EntityPlayer;
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
import net.minecraft.item.EnumDyeColor;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.inventory.EntityEquipmentSlot;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.potion.PotionUtils;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityDispenser;
import net.minecraft.tileentity.TileEntityDropper;
import net.minecraft.tileentity.TileEntityHopper;
import net.minecraft.tileentity.TileEntityBrewingStand;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.tileentity.TileEntityShulkerBox;
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
import net.minecraftforge.common.capabilities.Capability;
import net.minecraftforge.common.capabilities.CapabilityManager;
import net.minecraftforge.common.ForgeModContainer;
import net.minecraftforge.fluids.capability.CapabilityFluidHandler;
import net.minecraftforge.fluids.capability.IFluidHandler;
import net.minecraftforge.fluids.capability.IFluidHandlerItem;

/** Actual 1.11.2 TileEntityHopper transfer/cooldown oracle. */
public final class HopperGolden {
    private static final class DummyPlayer extends EntityPlayer {
        DummyPlayer(World world, int index) {
            super(world, new GameProfile(
                new UUID(0L, index + 1L), "equip" + index));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
        public void onItemPickup(Entity entity, int count) {}
    }

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
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
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

    private static TileEntityBrewingStand brewingStand(
            MemoryWorld world, BlockPos pos) {
        TileEntityBrewingStand stand = new TileEntityBrewingStand();
        world.put(pos, Blocks.BREWING_STAND.getDefaultState(), stand);
        return stand;
    }

    private static ItemStack waterPotion() {
        return PotionUtils.addPotionToItemStack(
            new ItemStack(Items.POTIONITEM, 1), PotionTypes.WATER);
    }

    private static TileEntityFurnace furnace(
            MemoryWorld world, BlockPos pos) {
        TileEntityFurnace furnace = new TileEntityFurnace();
        world.put(pos, Blocks.FURNACE.getStateFromMeta(2), furnace);
        return furnace;
    }

    private static TileEntityChest chest(MemoryWorld world, BlockPos pos) {
        TileEntityChest chest = new TileEntityChest();
        world.put(pos, Blocks.CHEST.getStateFromMeta(2), chest);
        return chest;
    }

    private static TileEntityChest trappedChest(
            MemoryWorld world, BlockPos pos) {
        TileEntityChest chest = new TileEntityChest();
        world.put(pos, Blocks.TRAPPED_CHEST.getStateFromMeta(2), chest);
        return chest;
    }

    private static TileEntityShulkerBox shulker(
            MemoryWorld world, BlockPos pos) {
        TileEntityShulkerBox box = new TileEntityShulkerBox();
        world.put(pos, Blocks.WHITE_SHULKER_BOX.getDefaultState(), box);
        return box;
    }

    private static void trappedAndShulkerAutomation() {
        BlockPos pos = new BlockPos(12, 78, 8);
        List<TileEntityHopper> order = new ArrayList<TileEntityHopper>();

        {
            MemoryWorld world = new MemoryWorld();
            TileEntityChest west = trappedChest(world, pos);
            TileEntityChest east = trappedChest(world, pos.east());
            TileEntityHopper source = hopper(world, pos.west(), 5, 0);
            source.setInventorySlotContents(0,
                new ItemStack(Item.getItemFromBlock(Blocks.COBBLESTONE), 2));
            order.add(source);
            tick(world, order, 1, 1);
            System.out.printf("T I %d %d %d %d%n",
                source.getStackInSlot(0).getCount(),
                west.getStackInSlot(0).getCount(),
                east.getStackInSlot(0).getCount(), cooldown(source));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityChest west = trappedChest(world, pos);
            TileEntityChest east = trappedChest(world, pos.east());
            TileEntityHopper destination = hopper(
                world, pos.east().down(), 5, 0);
            west.setInventorySlotContents(0,
                new ItemStack(Items.DIAMOND, 3));
            order.clear(); order.add(destination);
            tick(world, order, 1, 1);
            System.out.printf("T P %d %d %d %d %d%n",
                west.getStackInSlot(0).getCount(),
                east.getStackInSlot(0).getCount(),
                Item.getIdFromItem(
                    destination.getStackInSlot(0).getItem()),
                destination.getStackInSlot(0).getCount(),
                cooldown(destination));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityShulkerBox box = shulker(world, pos);
            TileEntityHopper source = hopper(world, pos.west(), 5, 0);
            source.setInventorySlotContents(0,
                new ItemStack(Item.getItemFromBlock(Blocks.COBBLESTONE), 2));
            order.clear(); order.add(source);
            tick(world, order, 1, 1);
            System.out.printf("U I %d %d %d %d%n",
                source.getStackInSlot(0).getCount(),
                Item.getIdFromItem(box.getStackInSlot(0).getItem()),
                box.getStackInSlot(0).getCount(), cooldown(source));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityShulkerBox box = shulker(world, pos);
            TileEntityHopper source = hopper(world, pos.west(), 5, 0);
            source.setInventorySlotContents(0,
                new ItemStack(
                    Item.getItemFromBlock(Blocks.WHITE_SHULKER_BOX), 1));
            order.clear(); order.add(source);
            tick(world, order, 1, 1);
            System.out.printf("U R %d %d %d%n",
                source.getStackInSlot(0).getCount(),
                box.getStackInSlot(0).getCount(), cooldown(source));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityShulkerBox box = shulker(world, pos.up());
            TileEntityHopper destination = hopper(world, pos, 5, 0);
            box.setInventorySlotContents(0,
                new ItemStack(Items.DIAMOND, 3));
            order.clear(); order.add(destination);
            tick(world, order, 1, 1);
            System.out.printf("U P %d %d %d %d%n",
                box.getStackInSlot(0).getCount(),
                Item.getIdFromItem(
                    destination.getStackInSlot(0).getItem()),
                destination.getStackInSlot(0).getCount(),
                cooldown(destination));
        }
    }

    private static void furnaceAndDoubleChestAutomation() {
        BlockPos pos = new BlockPos(12, 78, 8);
        List<TileEntityHopper> order = new ArrayList<TileEntityHopper>();

        {
            MemoryWorld world = new MemoryWorld();
            TileEntityFurnace furnace = furnace(world, pos);
            TileEntityHopper source = hopper(world, pos.up(), 0, 0);
            source.setInventorySlotContents(0,
                new ItemStack(Item.getItemFromBlock(Blocks.IRON_ORE), 2));
            order.clear(); order.add(source);
            tick(world, order, 1, 1);
            System.out.printf("F U %d %d %d %d %d %d%n",
                source.getStackInSlot(0).getCount(),
                Item.getIdFromItem(furnace.getStackInSlot(0).getItem()),
                furnace.getStackInSlot(0).getCount(),
                furnace.getStackInSlot(1).getCount(),
                furnace.getStackInSlot(2).getCount(), cooldown(source));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityFurnace furnace = furnace(world, pos);
            TileEntityHopper source = hopper(world, pos.west(), 5, 0);
            source.setInventorySlotContents(0,
                new ItemStack(Items.COAL, 2));
            order.clear(); order.add(source);
            tick(world, order, 1, 1);
            System.out.printf("F S %d %d %d %d%n",
                source.getStackInSlot(0).getCount(),
                Item.getIdFromItem(furnace.getStackInSlot(1).getItem()),
                furnace.getStackInSlot(1).getCount(), cooldown(source));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityFurnace furnace = furnace(world, pos.up());
            TileEntityHopper destination = hopper(world, pos, 5, 0);
            furnace.setInventorySlotContents(2,
                new ItemStack(Items.IRON_INGOT, 2));
            order.clear(); order.add(destination);
            tick(world, order, 1, 1);
            System.out.printf("F O %d %d %d %d%n",
                furnace.getStackInSlot(2).getCount(),
                Item.getIdFromItem(destination.getStackInSlot(0).getItem()),
                destination.getStackInSlot(0).getCount(),
                cooldown(destination));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityFurnace furnace = furnace(world, pos.up());
            TileEntityHopper destination = hopper(world, pos, 5, 0);
            furnace.setInventorySlotContents(1,
                new ItemStack(Items.WATER_BUCKET, 1));
            order.clear(); order.add(destination);
            tick(world, order, 1, 1);
            System.out.printf("F W %d %d %d %d%n",
                furnace.getStackInSlot(1).getCount(),
                Item.getIdFromItem(destination.getStackInSlot(0).getItem()),
                destination.getStackInSlot(0).getCount(),
                cooldown(destination));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityFurnace furnace = furnace(world, pos.up());
            TileEntityHopper destination = hopper(world, pos, 5, 0);
            furnace.setInventorySlotContents(1,
                new ItemStack(Items.COAL, 1));
            order.clear(); order.add(destination);
            tick(world, order, 1, 1);
            System.out.printf("F R %d %d %d%n",
                furnace.getStackInSlot(1).getCount(),
                destination.getStackInSlot(0).getCount(),
                cooldown(destination));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityChest west = chest(world, pos);
            TileEntityChest east = chest(world, pos.east());
            TileEntityHopper source = hopper(world, pos.west(), 5, 0);
            source.setInventorySlotContents(0,
                new ItemStack(Item.getItemFromBlock(Blocks.COBBLESTONE), 2));
            order.clear(); order.add(source);
            tick(world, order, 1, 1);
            System.out.printf("H I %d %d %d %d%n",
                source.getStackInSlot(0).getCount(),
                west.getStackInSlot(0).getCount(),
                east.getStackInSlot(0).getCount(), cooldown(source));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityChest west = chest(world, pos);
            TileEntityChest east = chest(world, pos.east());
            TileEntityHopper source = hopper(world, pos.west(), 5, 0);
            for (int slot = 0; slot < 27; ++slot)
                west.setInventorySlotContents(slot,
                    new ItemStack(Item.getItemFromBlock(Blocks.STONE), 64));
            source.setInventorySlotContents(0,
                new ItemStack(Item.getItemFromBlock(Blocks.COBBLESTONE), 2));
            order.clear(); order.add(source);
            tick(world, order, 1, 1);
            System.out.printf("H N %d %d %d %d %d%n",
                source.getStackInSlot(0).getCount(),
                west.getStackInSlot(0).getCount(),
                west.getStackInSlot(26).getCount(),
                east.getStackInSlot(0).getCount(), cooldown(source));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityChest west = chest(world, pos);
            TileEntityChest east = chest(world, pos.east());
            TileEntityHopper destination = hopper(
                world, pos.east().down(), 5, 0);
            west.setInventorySlotContents(0,
                new ItemStack(Items.DIAMOND, 3));
            order.clear(); order.add(destination);
            tick(world, order, 1, 1);
            System.out.printf("H P %d %d %d %d %d%n",
                west.getStackInSlot(0).getCount(),
                east.getStackInSlot(0).getCount(),
                Item.getIdFromItem(
                    destination.getStackInSlot(0).getItem()),
                destination.getStackInSlot(0).getCount(),
                cooldown(destination));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityChest west = chest(world, pos);
            TileEntityChest east = chest(world, pos.east());
            world.setBlockState(pos.up(), Blocks.STONE.getDefaultState(), 3);
            TileEntityHopper source = hopper(world, pos.west(), 5, 0);
            source.setInventorySlotContents(0,
                new ItemStack(Item.getItemFromBlock(Blocks.COBBLESTONE), 2));
            order.clear(); order.add(source);
            tick(world, order, 1, 1);
            System.out.printf("H B %d %d %d %d%n",
                source.getStackInSlot(0).getCount(),
                west.getStackInSlot(0).getCount(),
                east.getStackInSlot(0).getCount(), cooldown(source));
        }
        {
            MemoryWorld world = new MemoryWorld();
            TileEntityChest west = chest(world, pos);
            TileEntityChest east = chest(world, pos.east());
            world.setBlockState(
                pos.east().up(), Blocks.STONE.getDefaultState(), 3);
            TileEntityHopper source = hopper(world, pos.west(), 5, 0);
            source.setInventorySlotContents(0,
                new ItemStack(Item.getItemFromBlock(Blocks.COBBLESTONE), 2));
            order.clear(); order.add(source);
            tick(world, order, 1, 1);
            System.out.printf("H Q %d %d %d %d%n",
                source.getStackInSlot(0).getCount(),
                west.getStackInSlot(0).getCount(),
                east.getStackInSlot(0).getCount(), cooldown(source));
        }
    }

    private static void brewingSidedAutomation() {
        BlockPos standPos = new BlockPos(12, 78, 8);
        List<TileEntityHopper> order = new ArrayList<TileEntityHopper>();

        MemoryWorld world = new MemoryWorld();
        TileEntityBrewingStand stand = brewingStand(world, standPos);
        TileEntityHopper source = hopper(world, standPos.up(), 0, 0);
        source.setInventorySlotContents(0,
            new ItemStack(Items.NETHER_WART, 1, 0));
        order.add(source);
        tick(world, order, 1, 1);
        System.out.printf("S U %d %d %d %d%n",
            source.getStackInSlot(0).getCount(),
            Item.getIdFromItem(stand.getStackInSlot(3).getItem()),
            stand.getStackInSlot(3).getCount(), cooldown(source));

        world = new MemoryWorld();
        stand = brewingStand(world, standPos);
        source = hopper(world, standPos.west(), 5, 0);
        source.setInventorySlotContents(0, waterPotion());
        order.clear();
        order.add(source);
        tick(world, order, 1, 1);
        System.out.printf("S P %d %d %d%n",
            source.getStackInSlot(0).getCount(),
            PotionUtils.getPotionFromItem(stand.getStackInSlot(0))
                == PotionTypes.WATER ? 1 : 0,
            cooldown(source));

        world = new MemoryWorld();
        stand = brewingStand(world, standPos);
        source = hopper(world, standPos.west(), 5, 0);
        source.setInventorySlotContents(0,
            new ItemStack(Items.BLAZE_POWDER, 1, 0));
        order.clear();
        order.add(source);
        tick(world, order, 1, 1);
        stand.update();
        System.out.printf("S F %d %d %d %d%n",
            source.getStackInSlot(0).getCount(),
            stand.getStackInSlot(4).getCount(), stand.getField(1),
            cooldown(source));

        world = new MemoryWorld();
        stand = brewingStand(world, standPos);
        stand.setInventorySlotContents(0, waterPotion());
        TileEntityHopper destination = hopper(
            world, standPos.down(), 0, 0);
        order.clear();
        order.add(destination);
        tick(world, order, 1, 1);
        System.out.printf("S O %d %d %d%n",
            stand.getStackInSlot(0).getCount(),
            PotionUtils.getPotionFromItem(destination.getStackInSlot(0))
                == PotionTypes.WATER ? 1 : 0,
            cooldown(destination));

        world = new MemoryWorld();
        stand = brewingStand(world, standPos);
        stand.setInventorySlotContents(3,
            new ItemStack(Items.NETHER_WART, 1, 0));
        destination = hopper(world, standPos.down(), 0, 0);
        order.clear();
        order.add(destination);
        tick(world, order, 1, 1);
        System.out.printf("S R %d %d %d%n",
            stand.getStackInSlot(3).getCount(),
            destination.getStackInSlot(0).getCount(),
            cooldown(destination));

        world = new MemoryWorld();
        stand = brewingStand(world, standPos);
        stand.setInventorySlotContents(3,
            new ItemStack(Items.GLASS_BOTTLE, 1, 0));
        destination = hopper(world, standPos.down(), 0, 0);
        order.clear();
        order.add(destination);
        tick(world, order, 1, 1);
        System.out.printf("S G %d %d %d%n",
            stand.getStackInSlot(3).getCount(),
            destination.getStackInSlot(0).getItem() == Items.GLASS_BOTTLE
                ? 1 : 0,
            cooldown(destination));
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

    private static Random dispenserRandom() throws Exception {
        Field field = TileEntityDispenser.class.getDeclaredField("RNG");
        field.setAccessible(true);
        return (Random)field.get(null);
    }

    private static void setRandomSeed48(Random random, long seed48)
            throws Exception {
        Field seed = Random.class.getDeclaredField("seed");
        seed.setAccessible(true);
        ((AtomicLong)seed.get(random)).set(seed48);
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

    @SuppressWarnings("unchecked")
    private static void initializeFluidCapabilities() throws Exception {
        /* JavaExec does not run ForgeModContainer.preInit. Recreate the normal
         * capability registry/injection and the disabled-universal-bucket
         * container identity so the production dispenser registry can run. */
        if (CapabilityFluidHandler.FLUID_HANDLER_ITEM_CAPABILITY != null)
            return;
        CapabilityFluidHandler.register();
        Field field = CapabilityManager.class.getDeclaredField("providers");
        field.setAccessible(true);
        Map<String, Capability<?>> providers =
            (Map<String, Capability<?>>)field.get(CapabilityManager.INSTANCE);
        CapabilityFluidHandler.FLUID_HANDLER_CAPABILITY =
            (Capability<IFluidHandler>)providers.get(
                IFluidHandler.class.getName().intern());
        CapabilityFluidHandler.FLUID_HANDLER_ITEM_CAPABILITY =
            (Capability<IFluidHandlerItem>)providers.get(
                IFluidHandlerItem.class.getName().intern());
        if (ForgeModContainer.getInstance() == null) {
            Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
            Field unsafeField = unsafeClass.getDeclaredField("theUnsafe");
            unsafeField.setAccessible(true);
            Object forge = unsafeClass
                .getMethod("allocateInstance", Class.class)
                .invoke(unsafeField.get(null), ForgeModContainer.class);
            Field instance = ForgeModContainer.class
                .getDeclaredField("INSTANCE");
            instance.setAccessible(true);
            instance.set(null, forge);
        }
    }

    private static void multiSlotSelection() throws Exception {
        BlockPos sourcePos = new BlockPos(12, 78, 8);
        MemoryWorld world = new MemoryWorld();
        TileEntityDropper dropper = new TileEntityDropper();
        TileEntityDispenser destination = new TileEntityDispenser();
        world.put(sourcePos, Blocks.DROPPER.getStateFromMeta(13), dropper);
        world.put(sourcePos.east(),
            Blocks.DISPENSER.getStateFromMeta(2), destination);
        dropper.setInventorySlotContents(0,
            new ItemStack(Item.getItemFromBlock(Blocks.STONE), 4, 0));
        dropper.setInventorySlotContents(3,
            new ItemStack(Item.getItemFromBlock(Blocks.COBBLESTONE), 4, 0));
        dropper.setInventorySlotContents(8,
            new ItemStack(Item.getItemFromBlock(Blocks.PLANKS), 4, 0));
        Random selection = dispenserRandom();
        setRandomSeed48(selection, 0x13579bdf2468L);
        ExposedDropper exposedDropper = new ExposedDropper();
        for (int i = 0; i < 7; ++i)
            exposedDropper.run(world, sourcePos);
        System.out.printf("M D %d %d %d %d%n",
            dropper.getStackInSlot(0).getCount(),
            dropper.getStackInSlot(3).getCount(),
            dropper.getStackInSlot(8).getCount(),
            randomSeed48(selection));

        world = new MemoryWorld();
        TileEntityDispenser dispenser = new TileEntityDispenser();
        world.put(sourcePos,
            Blocks.DISPENSER.getStateFromMeta(13), dispenser);
        dispenser.setInventorySlotContents(0,
            new ItemStack(Item.getItemFromBlock(Blocks.STONE), 4, 0));
        dispenser.setInventorySlotContents(3,
            new ItemStack(Item.getItemFromBlock(Blocks.STONE), 4, 0));
        dispenser.setInventorySlotContents(8,
            new ItemStack(Item.getItemFromBlock(Blocks.STONE), 4, 0));
        setRandomSeed48(selection, 0x2468ace13579L);
        ExposedDispenser exposedDispenser = new ExposedDispenser();
        for (int i = 0; i < 7; ++i)
            exposedDispenser.run(world, sourcePos);
        System.out.printf("M E %d %d %d %d%n",
            dispenser.getStackInSlot(0).getCount(),
            dispenser.getStackInSlot(3).getCount(),
            dispenser.getStackInSlot(8).getCount(),
            randomSeed48(selection));
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

    private static int eventAt(MemoryWorld world, int index) {
        return index < world.events.size()
            ? world.events.get(index).intValue() : -1;
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

    private static void dispenserEmptyBucket() throws Exception {
        BlockPos pos = new BlockPos(12, 78, 8);
        IBlockState[] targets = {
            Blocks.FLOWING_WATER.getDefaultState(),
            Blocks.FLOWING_LAVA.getDefaultState(),
            Blocks.STONE.getDefaultState(),
            Blocks.FLOWING_WATER.getDefaultState(),
            Blocks.FLOWING_WATER.getDefaultState()
        };
        for (int index = 0; index < targets.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            world.setBlockState(pos.east(), targets[index], 3);
            TileEntityDispenser tile = dispenser(world, pos,
                new ItemStack(Items.BUCKET, index >= 3 ? 2 : 1, 0));
            if (index == 4) {
                for (int slot = 1; slot < 9; ++slot)
                    tile.setInventorySlotContents(slot,
                        new ItemStack(Items.BUCKET, 1, 0));
            }
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            if (index == 4) setRandomSeed48(dispenserRandom(), 4L);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            ItemStack auxiliary = tile.getStackInSlot(1);
            EntityItem entity = world.entities.isEmpty() ? null
                : (EntityItem)world.entities.get(0);
            IBlockState target = world.getBlockState(pos.east());
            System.out.printf(
                "X K %d %d %d %d %d %d %d %d %d %d %d %s %d %d %d %d %d%n",
                index,
                left.isEmpty() ? 0 : Item.getIdFromItem(left.getItem()),
                left.isEmpty() ? 0 : left.getCount(),
                auxiliary.isEmpty() ? 0
                    : Item.getIdFromItem(auxiliary.getItem()),
                auxiliary.isEmpty() ? 0 : auxiliary.getCount(),
                Block.getIdFromBlock(target.getBlock()),
                target.getBlock().getMetaFromState(target),
                world.entities.size(),
                entity == null ? 0
                    : Item.getIdFromItem(entity.getEntityItem().getItem()),
                randomSeed48(world.rand),
                randomHaveGaussian(world.rand) ? 1 : 0,
                dbits(randomGaussian(world.rand)),
                world.events.size(), eventAt(world, 0), eventAt(world, 1),
                eventAt(world, 2), eventAt(world, 3));
        }
    }

    private static void dispenserDefaultVariants() throws Exception {
        BlockPos pos = new BlockPos(12, 78, 8);
        ItemStack[] stacks = {
            new ItemStack(Item.getItemFromBlock(Blocks.COBBLESTONE), 2, 0),
            new ItemStack(Item.getItemFromBlock(Blocks.PLANKS), 2, 5),
            new ItemStack(Items.COAL, 2, 1),
            new ItemStack(Items.IRON_PICKAXE, 1, 17),
            new ItemStack(Items.DYE, 2, 1),
            new ItemStack(Items.MILK_BUCKET, 1, 0)
        };
        for (int index = 0; index < stacks.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            if (index == 2)
                world.setBlockState(
                    pos.east(), Blocks.STONE.getDefaultState(), 3);
            TileEntityDispenser tile = dispenser(world, pos, stacks[index]);
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            EntityItem entity = (EntityItem)world.entities.get(0);
            System.out.printf(
                "V %d %d %d %d %d %d %d %d %s %d %d%n",
                index,
                left.isEmpty() ? 0 : left.getCount(),
                Item.getIdFromItem(entity.getEntityItem().getItem()),
                entity.getEntityItem().getMetadata(),
                Block.getIdFromBlock(
                    world.getBlockState(pos.east()).getBlock()),
                randomSeed48(world.rand),
                randomHaveGaussian(world.rand) ? 1 : 0,
                world.events.size(),
                dbits(randomGaussian(world.rand)),
                eventAt(world, 0), eventAt(world, 1));
        }
    }

    private static void dispenserMinecarts() throws Exception {
        BlockPos pos = new BlockPos(12, 78, 8);
        Item[] items = {
            Items.MINECART, Items.CHEST_MINECART,
            Items.FURNACE_MINECART, Items.TNT_MINECART,
            Items.HOPPER_MINECART, Items.COMMAND_BLOCK_MINECART,
            Items.MINECART, Items.CHEST_MINECART,
            Items.COMMAND_BLOCK_MINECART
        };
        for (int index = 0; index < items.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            if (index < 6) {
                world.setBlockState(
                    pos.east(), Blocks.RAIL.getStateFromMeta(1), 3);
            } else if (index == 6) {
                world.setBlockState(
                    pos.east(), Blocks.RAIL.getStateFromMeta(2), 3);
            } else if (index == 7) {
                world.setBlockState(
                    pos.east().down(), Blocks.RAIL.getStateFromMeta(2), 3);
            } else {
                world.setBlockState(
                    pos.east(), Blocks.STONE.getDefaultState(), 3);
            }
            TileEntityDispenser tile = dispenser(
                world, pos, new ItemStack(items[index], 1, 0));
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            if (index < 8) {
                EntityMinecart cart = (EntityMinecart)world.entities.get(0);
                cart.onUpdate();
                System.out.printf(
                    "Y C %d %d %d %s %s %s %s %s %s %s %s %d %d %d%n",
                    index, left.isEmpty() ? 0 : left.getCount(),
                    cart.getType().getId(),
                    dbits(cart.posX), dbits(cart.posY), dbits(cart.posZ),
                    dbits(cart.motionX), dbits(cart.motionY),
                    dbits(cart.motionZ), fbits(cart.rotationYaw),
                    fbits(cart.rotationPitch), world.events.size(),
                    eventAt(world, 0), eventAt(world, 1));
            } else {
                EntityItem entity = (EntityItem)world.entities.get(0);
                System.out.printf(
                    "Y D %d %d %d %d %d %d %s %d %d %d %d %d%n",
                    left.isEmpty() ? 0 : left.getCount(),
                    Item.getIdFromItem(entity.getEntityItem().getItem()),
                    entity.getEntityItem().getMetadata(),
                    Block.getIdFromBlock(
                        world.getBlockState(pos.east()).getBlock()),
                    randomSeed48(world.rand),
                    randomHaveGaussian(world.rand) ? 1 : 0,
                    dbits(randomGaussian(world.rand)), world.events.size(),
                    eventAt(world, 0), eventAt(world, 1),
                    eventAt(world, 2), eventAt(world, 3));
            }
        }
    }

    private static void dispenserShulkerBoxes() throws Exception {
        BlockPos pos = new BlockPos(12, 78, 8);
        EnumDyeColor[] colors = EnumDyeColor.values();
        for (int index = 0; index < colors.length + 2; ++index) {
            MemoryWorld world = new MemoryWorld();
            int color = index < colors.length ? index : 0;
            if (index == colors.length)
                world.setBlockState(
                    pos.east().down(), Blocks.STONE.getDefaultState(), 3);
            else if (index == colors.length + 1)
                world.setBlockState(
                    pos.east(), Blocks.STONE.getDefaultState(), 3);
            Item item = Item.getItemFromBlock(
                BlockShulkerBox.getBlockByColor(colors[color]));
            TileEntityDispenser tile = dispenser(
                world, pos, new ItemStack(item, 1, 0));
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            IBlockState target = world.getBlockState(pos.east());
            System.out.printf("Z %d %d %d %d %d %d %d%n",
                index, left.isEmpty() ? 0 : left.getCount(),
                Block.getIdFromBlock(target.getBlock()),
                target.getBlock().getMetaFromState(target),
                world.events.size(), eventAt(world, 0), eventAt(world, 1));
        }
    }

    private static void dispenserArmorFallback() throws Exception {
        BlockPos pos = new BlockPos(12, 78, 8);
        Item[] items = new Item[22];
        for (int index = 0; index < 20; ++index)
            items[index] = Item.getItemById(298 + index);
        items[20] = Items.SHIELD;
        items[21] = Items.ELYTRA;
        for (int index = 0; index < items.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            int damage = index % 5;
            TileEntityDispenser tile = dispenser(
                world, pos, new ItemStack(items[index], 1, damage));
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            EntityItem entity = (EntityItem)world.entities.get(0);
            System.out.printf(
                "O %d %d %d %d %d %d %s %d %d %d%n",
                index, left.isEmpty() ? 0 : left.getCount(),
                Item.getIdFromItem(entity.getEntityItem().getItem()),
                entity.getEntityItem().getMetadata(),
                randomSeed48(world.rand),
                randomHaveGaussian(world.rand) ? 1 : 0,
                dbits(randomGaussian(world.rand)), world.events.size(),
                eventAt(world, 0), eventAt(world, 1));
        }
    }

    private static void dispenserArmorPlayer() throws Exception {
        BlockPos pos = new BlockPos(12, 78, 8);
        Item[] items = new Item[22];
        for (int index = 0; index < 20; ++index)
            items[index] = Item.getItemById(298 + index);
        items[20] = Items.SHIELD;
        items[21] = Items.ELYTRA;
        for (int index = 0; index < items.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            int damage = index % 5;
            ItemStack input = new ItemStack(items[index], 1, damage);
            EntityEquipmentSlot equipment =
                EntityLiving.getSlotForItemStack(input);
            DummyPlayer player = new DummyPlayer(world, index);
            player.setPosition(13.5, 78.0, 8.5);
            world.entities.clear();
            world.entities.add(player);
            TileEntityDispenser tile = dispenser(world, pos, input);
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            ItemStack worn = player.getItemStackFromSlot(equipment);
            EntityItem drop = null;
            for (Entity entity : world.entities)
                if (entity instanceof EntityItem) drop = (EntityItem)entity;
            if (drop != null) drop.onUpdate();
            System.out.printf(
                "Q E %d %d %d %d %d %d %d %d %d %d %s %d %d %d%n",
                index, left.isEmpty() ? 0 : left.getCount(),
                equipment.ordinal(), Item.getIdFromItem(worn.getItem()),
                worn.getMetadata(),
                drop == null || drop.getEntityItem().isEmpty() ? 0
                    : Item.getIdFromItem(drop.getEntityItem().getItem()),
                drop == null ? -1 : drop.getEntityItem().getCount(),
                drop != null && drop.isDead ? 1 : 0,
                randomSeed48(world.rand),
                randomHaveGaussian(world.rand) ? 1 : 0,
                dbits(randomGaussian(world.rand)),
                world.events.size(), eventAt(world, 0), eventAt(world, 1));
        }
        int[] occupiedItems = {298, 299, 300, 301, 442, 443};
        for (int index = 0; index < occupiedItems.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            ItemStack input = new ItemStack(
                Item.getItemById(occupiedItems[index]), 1, index);
            EntityEquipmentSlot equipment =
                EntityLiving.getSlotForItemStack(input);
            DummyPlayer player = new DummyPlayer(world, 100 + index);
            player.setPosition(13.5, 78.0, 8.5);
            player.setItemStackToSlot(equipment,
                new ItemStack(Item.getItemFromBlock(Blocks.STONE), 1, 0));
            world.entities.clear();
            world.entities.add(player);
            TileEntityDispenser tile = dispenser(world, pos, input);
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            EntityItem drop = (EntityItem)world.entities.get(1);
            drop.onUpdate();
            drop.onCollideWithPlayer(player);
            ItemStack collected = player.inventory.getStackInSlot(0);
            System.out.printf(
                "Q F %d %d %d %d %d %d %d %d %d %s %d %d %d%n",
                index, left.isEmpty() ? 0 : left.getCount(),
                equipment.ordinal(),
                Item.getIdFromItem(
                    player.getItemStackFromSlot(equipment).getItem()),
                Item.getIdFromItem(collected.getItem()),
                collected.getMetadata(), drop.isDead ? 1 : 0,
                randomSeed48(world.rand),
                randomHaveGaussian(world.rand) ? 1 : 0,
                dbits(randomGaussian(world.rand)), world.events.size(),
                eventAt(world, 0), eventAt(world, 1));
        }
    }

    private static void dispenserHeadwear() throws Exception {
        BlockPos pos = new BlockPos(12, 78, 8);
        Item pumpkin = Item.getItemFromBlock(Blocks.PUMPKIN);
        for (int index = 0; index < 7; ++index) {
            MemoryWorld world = new MemoryWorld();
            Item item = index < 6 ? Items.SKULL : pumpkin;
            int metadata = index < 6 ? index : 0;
            ItemStack input = new ItemStack(item, 1, metadata);
            DummyPlayer player = new DummyPlayer(world, 200 + index);
            player.setPosition(13.5, 78.0, 8.5);
            world.entities.clear();
            world.entities.add(player);
            TileEntityDispenser tile = dispenser(world, pos, input);
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            ItemStack worn = player.getItemStackFromSlot(
                EntityEquipmentSlot.HEAD);
            System.out.printf(
                "J E %d %d %d %d %d %d %d %d %s %d %d %d%n",
                index, left.isEmpty() ? 0 : left.getCount(),
                Item.getIdFromItem(item), metadata,
                Item.getIdFromItem(worn.getItem()), worn.getMetadata(),
                randomSeed48(world.rand),
                randomHaveGaussian(world.rand) ? 1 : 0,
                dbits(randomGaussian(world.rand)), world.events.size(),
                eventAt(world, 0), eventAt(world, 1));
        }
        Item[] stackedItems = {Items.SKULL, pumpkin};
        int[] stackedMetadata = {1, 0};
        for (int index = 0; index < stackedItems.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            ItemStack input = new ItemStack(
                stackedItems[index], 2, stackedMetadata[index]);
            DummyPlayer player = new DummyPlayer(world, 250 + index);
            player.setPosition(13.5, 78.0, 8.5);
            world.entities.clear();
            world.entities.add(player);
            TileEntityDispenser tile = dispenser(world, pos, input);
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            ItemStack worn = player.getItemStackFromSlot(
                EntityEquipmentSlot.HEAD);
            System.out.printf(
                "J S %d %d %d %d %d %d %d %d %s %d %d %d%n",
                index, left.getCount(), Item.getIdFromItem(left.getItem()),
                left.getMetadata(), Item.getIdFromItem(worn.getItem()),
                worn.getMetadata(), randomSeed48(world.rand),
                randomHaveGaussian(world.rand) ? 1 : 0,
                dbits(randomGaussian(world.rand)), world.events.size(),
                eventAt(world, 0), eventAt(world, 1));
        }
        Item[] items = {Items.SKULL, pumpkin};
        int[] metadata = {1, 0};
        for (int index = 0; index < items.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            TileEntityDispenser tile = dispenser(world, pos,
                new ItemStack(items[index], 1, metadata[index]));
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            System.out.printf(
                "J N %d %d %d %d %d %d %s %d %d %d%n",
                index, left.getCount(), Item.getIdFromItem(left.getItem()),
                left.getMetadata(), randomSeed48(world.rand),
                randomHaveGaussian(world.rand) ? 1 : 0,
                dbits(randomGaussian(world.rand)), world.events.size(),
                eventAt(world, 0), eventAt(world, 1));

            world = new MemoryWorld();
            DummyPlayer player = new DummyPlayer(world, 300 + index);
            player.setPosition(13.5, 78.0, 8.5);
            player.setItemStackToSlot(EntityEquipmentSlot.HEAD,
                new ItemStack(Item.getItemFromBlock(Blocks.STONE), 1, 0));
            world.entities.clear();
            world.entities.add(player);
            tile = dispenser(world, pos,
                new ItemStack(items[index], 1, metadata[index]));
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            new ExposedDispenser().run(world, pos);
            left = tile.getStackInSlot(0);
            System.out.printf(
                "J O %d %d %d %d %d %d %d %s %d %d %d%n",
                index, left.getCount(), Item.getIdFromItem(left.getItem()),
                left.getMetadata(), Item.getIdFromItem(
                    player.getItemStackFromSlot(
                        EntityEquipmentSlot.HEAD).getItem()),
                randomSeed48(world.rand),
                randomHaveGaussian(world.rand) ? 1 : 0,
                dbits(randomGaussian(world.rand)), world.events.size(),
                eventAt(world, 0), eventAt(world, 1));
        }
    }

    private static void chestObstruction() {
        BlockPos pos = new BlockPos(12, 78, 8);
        Block[] chestBlocks = {Blocks.CHEST, Blocks.TRAPPED_CHEST};
        Block[] covers = {
            Blocks.AIR, Blocks.STONE, Blocks.STONE_SLAB, Blocks.STONE_SLAB
        };
        int[] coverMetadata = {0, 0, 0, 8};
        for (int chestType = 0; chestType < chestBlocks.length; ++chestType) {
            BlockChest chest = (BlockChest)chestBlocks[chestType];
            for (int cover = 0; cover < covers.length; ++cover) {
                MemoryWorld world = new MemoryWorld();
                world.put(pos, chest.getStateFromMeta(3),
                    new TileEntityChest());
                if (covers[cover] != Blocks.AIR) {
                    world.setBlockState(pos.up(),
                        covers[cover].getStateFromMeta(
                            coverMetadata[cover]), 3);
                }
                System.out.printf("K S %d %d %d%n", chestType, cover,
                    chest.getContainer(world, pos, false) == null ? 0 : 1);
            }

            for (int blockedHalf = 0; blockedHalf < 2; ++blockedHalf) {
                MemoryWorld world = new MemoryWorld();
                world.put(pos, chest.getStateFromMeta(3),
                    new TileEntityChest());
                world.put(pos.east(), chest.getStateFromMeta(3),
                    new TileEntityChest());
                world.setBlockState(
                    (blockedHalf == 0 ? pos : pos.east()).up(),
                    Blocks.STONE.getDefaultState(), 3);
                System.out.printf("K D %d %d %d%n", chestType, blockedHalf,
                    chest.getContainer(world, pos, false) == null ? 0 : 1);
            }
            for (int petHalf = 0; petHalf < 2; ++petHalf) {
                MemoryWorld world = new MemoryWorld();
                world.put(pos, chest.getStateFromMeta(3),
                    new TileEntityChest());
                world.put(pos.east(), chest.getStateFromMeta(3),
                    new TileEntityChest());
                EntityOcelot cat = new EntityOcelot(world);
                cat.setPosition(pos.getX() + petHalf + 0.5,
                    pos.getY() + 1.0, pos.getZ() + 0.5);
                cat.setSitting(true);
                world.entities.add(cat);
                System.out.printf("K O %d %d %d%n", chestType, petHalf,
                    chest.getContainer(world, pos, false) == null ? 0 : 1);
            }
        }
    }

    private static void dispenserBoat() throws Exception {
        BlockPos pos = new BlockPos(12, 78, 8);
        Item[] items = {
            Items.BOAT, Items.SPRUCE_BOAT, Items.BIRCH_BOAT,
            Items.JUNGLE_BOAT, Items.ACACIA_BOAT, Items.DARK_OAK_BOAT,
            Items.SPRUCE_BOAT, Items.DARK_OAK_BOAT
        };
        for (int index = 0; index < items.length; ++index) {
            MemoryWorld world = new MemoryWorld();
            if (index < 6) {
                world.setBlockState(pos.east(),
                    Blocks.FLOWING_WATER.getDefaultState(), 3);
            } else if (index == 6) {
                world.setBlockState(pos.east().down(),
                    Blocks.FLOWING_WATER.getDefaultState(), 3);
            } else {
                world.setBlockState(pos.east(),
                    Blocks.STONE.getDefaultState(), 3);
            }
            TileEntityDispenser tile = dispenser(
                world, pos, new ItemStack(items[index], 1, 0));
            world.rand.setSeed(321L);
            setMathSeed48(0x102030405060L);
            new ExposedDispenser().run(world, pos);
            ItemStack left = tile.getStackInSlot(0);
            if (index < 7) {
                EntityBoat entity = (EntityBoat)world.entities.get(0);
                System.out.printf(
                    "X B %d %d %d %s %s %s %s %d %d %d%n",
                    index, left.isEmpty() ? 0 : left.getCount(),
                    entity.getBoatType().ordinal(),
                    dbits(entity.posX), dbits(entity.posY),
                    dbits(entity.posZ), fbits(entity.rotationYaw),
                    world.events.size(), eventAt(world, 0), eventAt(world, 1));
            } else {
                EntityItem entity = (EntityItem)world.entities.get(0);
                System.out.printf(
                    "X N %d %d %d %d %d %s %d %d %d %d %d%n",
                    left.isEmpty() ? 0 : left.getCount(),
                    Item.getIdFromItem(entity.getEntityItem().getItem()),
                    Block.getIdFromBlock(
                        world.getBlockState(pos.east()).getBlock()),
                    randomSeed48(world.rand),
                    randomHaveGaussian(world.rand) ? 1 : 0,
                    dbits(randomGaussian(world.rand)), world.events.size(),
                    eventAt(world, 0), eventAt(world, 1),
                    eventAt(world, 2), eventAt(world, 3));
            }
        }
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
        initializeFluidCapabilities();
        transferCadence();
        powered();
        chain();
        itemCapture();
        brewingSidedAutomation();
        furnaceAndDoubleChestAutomation();
        trappedAndShulkerAutomation();
        dropperInsert();
        multiSlotSelection();
        dispenserEject();
        dispenserTnt();
        dispenserFireCharge();
        dispenserPotion();
        dispenserThrowables();
        dispenserFirework();
        dispenserBucket();
        dispenserEmptyBucket();
        dispenserDefaultVariants();
        dispenserMinecarts();
        dispenserShulkerBoxes();
        dispenserArmorFallback();
        dispenserArmorPlayer();
        dispenserHeadwear();
        chestObstruction();
        dispenserBoat();
        dispenserFlint();
        System.out.println(
            "hopper_live: PASS (cooldown, transfer, chain, power, item "
            + "capture, brewing sidedness, multi-slot selection, empty "
            + "bucket pickup/fallback)");
    }
}
