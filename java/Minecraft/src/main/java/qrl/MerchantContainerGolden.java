package qrl;

import com.mojang.authlib.GameProfile;
import java.util.UUID;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.IMerchant;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.inventory.ClickType;
import net.minecraft.inventory.ContainerMerchant;
import net.minecraft.inventory.InventoryMerchant;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.text.ITextComponent;
import net.minecraft.util.text.TextComponentString;
import net.minecraft.village.MerchantRecipe;
import net.minecraft.village.MerchantRecipeList;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Actual 1.11.2 ContainerMerchant/InventoryMerchant click oracle. */
public final class MerchantContainerGolden {
    private static final class MemoryWorld extends World {
        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "merchant-container-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return new BlockPos(0, 64, 0); }
        public IBlockState getBlockState(BlockPos pos) {
            return Blocks.AIR.getDefaultState();
        }
    }

    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(new UUID(7L, 11L), "merchant-test"));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
        public void onItemPickup(Entity entity, int count) {}
    }

    private static final class Merchant implements IMerchant {
        private EntityPlayer customer;
        private MerchantRecipeList recipes = new MerchantRecipeList();
        int uses;
        public void setCustomer(EntityPlayer player) { customer = player; }
        public EntityPlayer getCustomer() { return customer; }
        public MerchantRecipeList getRecipes(EntityPlayer player) {
            return recipes;
        }
        public void setRecipes(MerchantRecipeList value) { recipes = value; }
        public void useRecipe(MerchantRecipe recipe) {
            recipe.incrementToolUses();
            ++uses;
        }
        public void verifySellingItem(ItemStack stack) {}
        public ITextComponent getDisplayName() {
            return new TextComponentString("Villager");
        }
        public World getWorld() { return customer.world; }
        public BlockPos getPos() { return BlockPos.ORIGIN; }
    }

    private static String stack(ItemStack value) {
        return value.isEmpty() ? "0 0 0"
            : Item.getIdFromItem(value.getItem()) + " "
                + value.getCount() + " " + value.getMetadata();
    }

    private static void row(String tag, TestPlayer player,
                            InventoryMerchant inv, Merchant merchant) {
        System.out.printf("%s %s %s %s %s %d %d%n", tag,
            stack(inv.getStackInSlot(0)), stack(inv.getStackInSlot(1)),
            stack(inv.getStackInSlot(2)),
            stack(player.inventory.getItemStack()),
            merchant.recipes.get(0).getToolUses(), merchant.uses);
    }

    public static void main(String[] args) {
        Bootstrap.register();
        MemoryWorld world = new MemoryWorld();
        TestPlayer player = new TestPlayer(world);
        Merchant merchant = new Merchant();
        merchant.setCustomer(player);
        merchant.recipes.add(new MerchantRecipe(
            new ItemStack(Items.EMERALD, 3),
            new ItemStack(Items.BREAD, 2)));
        merchant.recipes.add(new MerchantRecipe(
            new ItemStack(Items.BOOK), new ItemStack(Items.EMERALD, 5),
            new ItemStack(Items.DIAMOND)));
        ContainerMerchant container = new ContainerMerchant(
            player.inventory, merchant, world);
        InventoryMerchant inv = container.getMerchantInventory();

        inv.setInventorySlotContents(0, new ItemStack(Items.EMERALD, 5));
        row("A", player, inv, merchant);
        container.slotClick(2, 0, ClickType.PICKUP, player);
        row("B", player, inv, merchant);
        player.inventory.setItemStack(ItemStack.EMPTY);

        inv.setCurrentRecipeIndex(1);
        inv.setInventorySlotContents(0, new ItemStack(Items.EMERALD, 5));
        inv.setInventorySlotContents(1, new ItemStack(Items.BOOK));
        row("C", player, inv, merchant);
        container.slotClick(2, 0, ClickType.PICKUP, player);
        row("D", player, inv, merchant);
        player.inventory.setItemStack(ItemStack.EMPTY);

        inv.setCurrentRecipeIndex(0);
        inv.setInventorySlotContents(0, new ItemStack(Items.EMERALD, 6));
        inv.setInventorySlotContents(1, ItemStack.EMPTY);
        container.slotClick(2, 0, ClickType.QUICK_MOVE, player);
        System.out.printf("E %s %s %s %s %d %d%n",
            stack(inv.getStackInSlot(0)), stack(inv.getStackInSlot(1)),
            stack(inv.getStackInSlot(2)), stack(player.inventory.getStackInSlot(8)),
            merchant.recipes.get(0).getToolUses(), merchant.uses);
    }
}
