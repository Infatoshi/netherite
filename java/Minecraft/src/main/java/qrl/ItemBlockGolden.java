package qrl;

import com.mojang.authlib.GameProfile;
import com.google.common.base.Predicate;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemBlock;
import net.minecraft.item.ItemBlockSpecial;
import net.minecraft.item.ItemSlab;
import net.minecraft.item.ItemDoor;
import net.minecraft.item.ItemSeeds;
import net.minecraft.item.ItemSeedFood;
import net.minecraft.item.ItemHoe;
import net.minecraft.item.ItemSpade;
import net.minecraft.item.ItemSnow;
import net.minecraft.item.ItemRedstone;
import net.minecraft.item.ItemStack;
import net.minecraft.profiler.Profiler;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.EnumActionResult;
import net.minecraft.util.EnumFacing;
import net.minecraft.util.EnumHand;
import net.minecraft.util.NonNullList;
import net.minecraft.util.SoundCategory;
import net.minecraft.util.SoundEvent;
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

/** Exhaustive direct ItemBlock.onItemUse boundary for the 1.11.2 registry. */
public final class ItemBlockGolden {
    private static final BlockPos TARGET = BlockPos.ORIGIN;

    private static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> states =
            new HashMap<BlockPos, IBlockState>();
        boolean allowPlace;
        String sound = "-";

        MemoryWorld(Item item, boolean allow) {
            super(new SaveHandlerMP(), new WorldInfo(new WorldSettings(
                0L, GameType.SURVIVAL, true, false, WorldType.DEFAULT),
                "item-block-oracle"), new WorldProviderSurface(),
                new Profiler(), false);
            provider.setWorld(this);
            allowPlace = allow;
            int itemId = Item.getIdFromItem(item);
            states.put(TARGET, item instanceof ItemHoe
                || item instanceof ItemSpade
                ? Blocks.GRASS.getDefaultState()
                : itemId == 372
                ? Blocks.SOUL_SAND.getDefaultState()
                : (item instanceof ItemSeeds || item instanceof ItemSeedFood)
                ? Blocks.FARMLAND.getDefaultState()
                : Blocks.STONE.getDefaultState());
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            IBlockState state = states.get(pos);
            return state == null ? Blocks.AIR.getDefaultState() : state;
        }
        public TileEntity getTileEntity(BlockPos pos) { return null; }
        public <T extends Entity> List<T> getEntitiesWithinAABB(
                Class<? extends T> type, AxisAlignedBB box,
                Predicate<? super T> filter) {
            return Collections.emptyList();
        }
        public boolean setBlockState(
                BlockPos pos, IBlockState state, int flags) {
            states.put(pos.toImmutable(), state);
            return true;
        }
        public boolean mayPlace(Block block, BlockPos pos, boolean skip,
                EnumFacing side, Entity entity) {
            return allowPlace;
        }
        public boolean isSideSolid(
                BlockPos pos, EnumFacing side, boolean defaultValue) {
            IBlockState state = getBlockState(pos);
            return state.getBlock() != Blocks.AIR
                && state.isSideSolid(this, pos, side);
        }
        public void playSound(EntityPlayer except, BlockPos pos,
                SoundEvent event, SoundCategory category,
                float volume, float pitch) {
            sound = SoundEvent.REGISTRY.getNameForObject(event).toString()
                + ":" + Integer.toHexString(Float.floatToRawIntBits(volume))
                + ":" + Integer.toHexString(Float.floatToRawIntBits(pitch));
        }
    }

    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world, BlockPos placed, float yaw, int geometry) {
            super(world, new GameProfile(
                new UUID(0x49424c4f434bL, 0x474f4c44454eL), "item-block"));
            rotationYaw = yaw;
            if (geometry == 1) {
                setPosition(placed.getX() + 0.5D,
                    placed.getY() + 2.0D, placed.getZ() + 0.5D);
            } else if (geometry == 2) {
                setPosition(placed.getX() + 0.5D,
                    placed.getY() - 3.0D, placed.getZ() + 0.5D);
            } else {
                double radians = yaw * Math.PI / 180.0D;
                setPosition(placed.getX() - Math.sin(radians) * 3.0D,
                    placed.getY(), placed.getZ() + Math.cos(radians) * 3.0D);
            }
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
    }

    private static void run(Item item, ItemStack variant, int faceIndex,
            int yawIndex, int highHit, int geometry, boolean allow) {
        MemoryWorld world = new MemoryWorld(item, allow);
        EnumFacing face = EnumFacing.VALUES[faceIndex];
        BlockPos placed = TARGET.offset(face);
        TestPlayer player = new TestPlayer(
            world, placed, yawIndex * 90.0F, geometry);
        ItemStack held = variant.copy();
        held.setCount(2);
        player.setHeldItem(EnumHand.MAIN_HAND, held);
        EnumActionResult result = item.onItemUse(
            player, world, TARGET, EnumHand.MAIN_HAND, face,
            0.25F, highHit != 0 ? 0.75F : 0.25F, 0.25F);
        IBlockState state = world.getBlockState(
            item instanceof ItemHoe || item instanceof ItemSpade
                ? TARGET : placed);
        int placedBlock = item instanceof ItemBlock
            ? Block.getIdFromBlock(((ItemBlock)item).getBlock())
            : item instanceof ItemBlockSpecial
            ? Block.getIdFromBlock(((ItemBlockSpecial)item).getBlock())
            : item == net.minecraft.init.Items.OAK_DOOR ? 64
            : item == net.minecraft.init.Items.IRON_DOOR ? 71
            : item == net.minecraft.init.Items.SPRUCE_DOOR ? 193
            : item == net.minecraft.init.Items.BIRCH_DOOR ? 194
            : item == net.minecraft.init.Items.JUNGLE_DOOR ? 195
            : item == net.minecraft.init.Items.ACACIA_DOOR ? 196 : 197;
        if (item instanceof ItemSeeds || item instanceof ItemSeedFood)
            placedBlock = Item.getIdFromItem(item) == 295 ? 59
                : Item.getIdFromItem(item) == 361 ? 104
                : Item.getIdFromItem(item) == 362 ? 105
                : Item.getIdFromItem(item) == 372 ? 115
                : Item.getIdFromItem(item) == 435 ? 207
                : Item.getIdFromItem(item) == 391 ? 141 : 142;
        if (item instanceof ItemHoe) placedBlock = 60;
        if (item instanceof ItemSpade) placedBlock = 208;
        if (item instanceof ItemRedstone) placedBlock = 55;
        System.out.printf("I %d %d %d %d %d %d %d %d %s %d %d %d %s%n",
            Item.getIdFromItem(item), placedBlock,
            variant.getMetadata(), faceIndex,
            yawIndex, highHit, geometry, allow ? 1 : 0, result.name(),
            Block.getStateId(state), held.getCount(), held.getMetadata(),
            world.sound);
    }

    public static void main(String[] args) {
        Bootstrap.register();
        for (Item item : Item.REGISTRY) {
            if (!(item instanceof ItemBlock)
                    && !(item instanceof ItemBlockSpecial)
                    && !(item instanceof ItemDoor)
                    && !(item instanceof ItemSeeds)
                    && !(item instanceof ItemSeedFood)
                    && !(item instanceof ItemHoe)
                    && !(item instanceof ItemSpade)
                    && !(item instanceof ItemSnow)
                    && !(item instanceof ItemRedstone)) continue;
            try {
                Class<?> owner = item.getClass().getMethod("onItemUse",
                        EntityPlayer.class, World.class, BlockPos.class,
                        EnumHand.class, EnumFacing.class,
                        Float.TYPE, Float.TYPE, Float.TYPE)
                        .getDeclaringClass();
                if (owner != ItemBlock.class && owner != ItemSlab.class
                        && owner != ItemBlockSpecial.class
                        && owner != ItemDoor.class
                        && owner != ItemSeeds.class
                        && owner != ItemSeedFood.class
                        && owner != ItemHoe.class
                        && owner != ItemSpade.class
                        && owner != ItemSnow.class
                        && owner != ItemRedstone.class)
                    continue;
            } catch (NoSuchMethodException exception) {
                throw new RuntimeException(exception);
            }
            NonNullList<ItemStack> variants = NonNullList.create();
            item.getSubItems(item, item.getCreativeTab(), variants);
            if (variants.isEmpty()) variants.add(new ItemStack(item, 1, 0));
            for (ItemStack variant : variants) {
                for (int face = 0; face < 6; ++face)
                    for (int yaw = 0; yaw < 4; ++yaw)
                        for (int high = 0; high < 2; ++high)
                            for (int geometry = 0; geometry < 3; ++geometry)
                                run(item, variant, face, yaw, high,
                                    geometry, true);
                run(item, variant, 1, 0, 0, 0, false);
            }
        }
    }
}
