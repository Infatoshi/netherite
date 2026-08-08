package qrl;

import com.mojang.authlib.GameProfile;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.IMerchant;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.init.PotionTypes;
import net.minecraft.inventory.ClickType;
import net.minecraft.inventory.ContainerBrewingStand;
import net.minecraft.inventory.ContainerEnchantment;
import net.minecraft.inventory.ContainerFurnace;
import net.minecraft.inventory.ContainerMerchant;
import net.minecraft.inventory.ContainerPlayer;
import net.minecraft.inventory.ContainerRepair;
import net.minecraft.inventory.ContainerWorkbench;
import net.minecraft.inventory.InventoryMerchant;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.profiler.Profiler;
import net.minecraft.potion.PotionUtils;
import net.minecraft.stats.AchievementList;
import net.minecraft.stats.StatBase;
import net.minecraft.tileentity.TileEntityBrewingStand;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.ActionResult;
import net.minecraft.util.EnumHand;
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
import net.minecraft.world.storage.MapData;
import net.minecraft.world.storage.MapStorage;

/** Direct transcript of the remaining Container.slotClick modes in 1.11.2. */
public final class ContainerClickGolden {
    private static final class MemoryWorld extends World {
        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "container-click-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
            this.mapStorage = new MapStorage(new SaveHandlerMP());
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            return Blocks.AIR.getDefaultState();
        }
        public boolean spawnEntity(Entity entity) { return true; }
    }

    private static final class TestPlayer extends EntityPlayer {
        final List<ItemStack> dropped = new ArrayList<ItemStack>();
        int potionStats;

        TestPlayer(boolean creative) {
            super(new MemoryWorld(), new GameProfile(
                new UUID(41L, 43L), "container-click-test"));
            capabilities.isCreativeMode = creative;
            setPosition(0.5D, 70.0D, 0.5D);
        }

        public boolean isSpectator() { return false; }
        public boolean isCreative() { return capabilities.isCreativeMode; }
        public void onItemPickup(Entity entity, int count) {}
        public void addStat(StatBase stat, int amount) {
            if (stat == AchievementList.POTION) potionStats += amount;
        }
        public EntityItem dropItem(ItemStack stack, boolean unused) {
            if (!stack.isEmpty()) dropped.add(stack.copy());
            return null;
        }
    }

    private static final class Merchant implements IMerchant {
        EntityPlayer customer;
        final MerchantRecipeList recipes = new MerchantRecipeList();
        int uses;

        public void setCustomer(EntityPlayer player) { customer = player; }
        public EntityPlayer getCustomer() { return customer; }
        public MerchantRecipeList getRecipes(EntityPlayer player) {
            return recipes;
        }
        public void setRecipes(MerchantRecipeList value) {
            recipes.clear();
            if (value != null) recipes.addAll(value);
        }
        public void useRecipe(MerchantRecipe recipe) {
            recipe.incrementToolUses();
            ++uses;
        }
        public void verifySellingItem(ItemStack value) {}
        public ITextComponent getDisplayName() {
            return new TextComponentString("Villager");
        }
        public World getWorld() { return customer.world; }
        public BlockPos getPos() { return BlockPos.ORIGIN; }
    }

    private static ItemStack stack(int item, int count) {
        return item == 0 || count == 0 ? ItemStack.EMPTY
            : new ItemStack(Item.getItemById(item), count, 0);
    }

    private static ItemStack stackMeta(int item, int count, int meta) {
        return item == 0 || count == 0 ? ItemStack.EMPTY
            : new ItemStack(Item.getItemById(item), count, meta);
    }

    private static String enc(ItemStack value) {
        return value.isEmpty() ? "0:0:0"
            : Item.getIdFromItem(value.getItem()) + ":"
                + value.getCount() + ":" + value.getMetadata();
    }

    private static String inv(TestPlayer player, int slot) {
        return enc(player.inventory.getStackInSlot(slot));
    }

    private static ItemStack potion(boolean water) {
        return PotionUtils.addPotionToItemStack(
            new ItemStack(Items.POTIONITEM),
            water ? PotionTypes.WATER : PotionTypes.AWKWARD);
    }

    private static String brewEnc(ItemStack value) {
        if (value.isEmpty()) return "0:0:0";
        int kind = PotionUtils.getPotionFromItem(value) == PotionTypes.WATER
            ? 1 : PotionUtils.getPotionFromItem(value) == PotionTypes.AWKWARD
                ? 2 : 3;
        return Item.getIdFromItem(value.getItem()) + ":"
            + value.getCount() + ":" + kind;
    }

    private static int dropped(TestPlayer player, int item) {
        int count = 0;
        for (ItemStack stack : player.dropped)
            if (Item.getIdFromItem(stack.getItem()) == item)
                count += stack.getCount();
        return count;
    }

    private static void droppedSequence(String tag, TestPlayer player) {
        System.out.print(tag);
        for (ItemStack stack : player.dropped)
            System.out.print(" " + enc(stack));
        System.out.println();
    }

    private static void swaps() {
        TestPlayer p = new TestPlayer(false);
        ContainerPlayer c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(9, stack(1, 5));
        p.inventory.setInventorySlotContents(0, stack(3, 3));
        c.slotClick(9, 0, ClickType.SWAP, p);
        System.out.printf("S0 %s %s %s%n", inv(p, 9), inv(p, 0),
            enc(p.inventory.getItemStack()));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(0, stack(310, 1));
        c.slotClick(5, 0, ClickType.SWAP, p);
        System.out.printf("S1 %s %s%n", inv(p, 39), inv(p, 0));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(0, stack(1, 2));
        c.slotClick(5, 0, ClickType.SWAP, p);
        System.out.printf("S2 %s %s%n", inv(p, 39), inv(p, 0));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(0, stack(442, 1));
        p.inventory.setInventorySlotContents(40, stack(50, 2));
        c.slotClick(45, 0, ClickType.SWAP, p);
        System.out.printf("S3 %s %s%n", inv(p, 40), inv(p, 0));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(0, stack(310, 3));
        c.slotClick(5, 0, ClickType.SWAP, p);
        System.out.printf("S4 %s %s%n", inv(p, 39), inv(p, 0));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(0, stack(310, 3));
        p.inventory.setInventorySlotContents(39, stack(298, 1));
        c.slotClick(5, 0, ClickType.SWAP, p);
        System.out.printf("S5 %s %s %s%n", inv(p, 39), inv(p, 0),
            inv(p, 1));
    }

    private static void clones() {
        TestPlayer p = new TestPlayer(true);
        ContainerPlayer c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(9, stack(368, 7));
        c.slotClick(9, 2, ClickType.CLONE, p);
        System.out.printf("C0 %s %s%n", inv(p, 9),
            enc(p.inventory.getItemStack()));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(9, stack(368, 7));
        c.slotClick(9, 2, ClickType.CLONE, p);
        System.out.printf("C1 %s %s%n", inv(p, 9),
            enc(p.inventory.getItemStack()));
    }

    private static void quickMoves() {
        TestPlayer p = new TestPlayer(false);
        ContainerPlayer c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(9, stack(442, 2));
        c.slotClick(9, 0, ClickType.QUICK_MOVE, p);
        System.out.printf("Q0 %s %s%n", inv(p, 9), inv(p, 40));
        c.slotClick(45, 0, ClickType.QUICK_MOVE, p);
        System.out.printf("Q1 %s %s%n", inv(p, 9), inv(p, 40));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(9, stack(86, 2));
        c.slotClick(9, 0, ClickType.QUICK_MOVE, p);
        System.out.printf("Q2 %s %s%n", inv(p, 9), inv(p, 39));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(9, stack(397, 2));
        c.slotClick(9, 0, ClickType.QUICK_MOVE, p);
        System.out.printf("Q3 %s %s%n", inv(p, 9), inv(p, 39));
    }

    private static void drag(
            String tag, boolean creative, int start, int add, int end) {
        TestPlayer p = new TestPlayer(creative);
        ContainerPlayer c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setItemStack(stack(1, 10));
        c.slotClick(-999, start, ClickType.QUICK_CRAFT, p);
        c.slotClick(9, add, ClickType.QUICK_CRAFT, p);
        c.slotClick(10, add, ClickType.QUICK_CRAFT, p);
        c.slotClick(11, add, ClickType.QUICK_CRAFT, p);
        c.slotClick(-999, end, ClickType.QUICK_CRAFT, p);
        System.out.printf("%s %s %s %s %s%n", tag,
            inv(p, 9), inv(p, 10), inv(p, 11),
            enc(p.inventory.getItemStack()));
    }

    private static void interruptedDrag() {
        TestPlayer p = new TestPlayer(false);
        ContainerPlayer c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setItemStack(stack(1, 10));
        c.slotClick(-999, 0, ClickType.QUICK_CRAFT, p);
        c.slotClick(9, 1, ClickType.QUICK_CRAFT, p);
        c.slotClick(10, 0, ClickType.PICKUP, p);
        c.slotClick(-999, 2, ClickType.QUICK_CRAFT, p);
        System.out.printf("D3 %s %s %s%n", inv(p, 9), inv(p, 10),
            enc(p.inventory.getItemStack()));
    }

    private static void dragEdges() {
        TestPlayer p = new TestPlayer(false);
        ContainerPlayer c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setItemStack(stack(1, 10));
        p.inventory.setInventorySlotContents(9, stack(1, 60));
        p.inventory.setInventorySlotContents(11, stack(3, 7));
        c.slotClick(-999, 0, ClickType.QUICK_CRAFT, p);
        c.slotClick(9, 1, ClickType.QUICK_CRAFT, p);
        c.slotClick(10, 1, ClickType.QUICK_CRAFT, p);
        c.slotClick(11, 1, ClickType.QUICK_CRAFT, p);
        c.slotClick(-999, 2, ClickType.QUICK_CRAFT, p);
        System.out.printf("D4 %s %s %s %s%n", inv(p, 9), inv(p, 10),
            inv(p, 11), enc(p.inventory.getItemStack()));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setItemStack(stack(368, 10));
        p.inventory.setInventorySlotContents(9, stack(368, 15));
        c.slotClick(-999, 0, ClickType.QUICK_CRAFT, p);
        c.slotClick(9, 1, ClickType.QUICK_CRAFT, p);
        c.slotClick(10, 1, ClickType.QUICK_CRAFT, p);
        c.slotClick(-999, 2, ClickType.QUICK_CRAFT, p);
        System.out.printf("D5 %s %s %s%n", inv(p, 9), inv(p, 10),
            enc(p.inventory.getItemStack()));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setItemStack(stack(310, 3));
        c.slotClick(-999, 0, ClickType.QUICK_CRAFT, p);
        c.slotClick(5, 1, ClickType.QUICK_CRAFT, p);
        c.slotClick(9, 1, ClickType.QUICK_CRAFT, p);
        c.slotClick(-999, 2, ClickType.QUICK_CRAFT, p);
        System.out.printf("D6 %s %s %s%n", inv(p, 39), inv(p, 9),
            enc(p.inventory.getItemStack()));
    }

    private static void pickupAll(String tag, int button) {
        TestPlayer p = new TestPlayer(false);
        ContainerPlayer c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setItemStack(stack(1, 40));
        p.inventory.setInventorySlotContents(9, stack(1, 64));
        p.inventory.setInventorySlotContents(10, stack(1, 5));
        p.inventory.setInventorySlotContents(11, stack(1, 20));
        p.inventory.setInventorySlotContents(0, stack(1, 7));
        c.slotClick(12, button, ClickType.PICKUP_ALL, p);
        System.out.printf("%s %s %s %s %s %s%n", tag,
            inv(p, 9), inv(p, 10), inv(p, 11), inv(p, 0),
            enc(p.inventory.getItemStack()));
    }

    private static ItemStack tagged(int item, int count, String value) {
        ItemStack result = stack(item, count);
        NBTTagCompound tag = new NBTTagCompound();
        tag.setString("oracle", value);
        result.setTagCompound(tag);
        return result;
    }

    private static ItemStack matrixStack(int spec) {
        switch (spec) {
        case 0: return ItemStack.EMPTY;
        case 1: return stack(1, 1);
        case 2: return stack(1, 5);
        case 3: return stack(1, 64);
        case 4: return stack(368, 15);
        case 5: return tagged(1, 5, "a");
        case 6: return stack(310, 1);
        case 7: return stack(1, 63);
        case 8: return stack(3, 3);
        case 9: return tagged(1, 1, "a");
        case 10: return tagged(1, 1, "b");
        case 11: return stack(1, 65);
        case 12: return stack(368, 16);
        case 13: return stack(368, 17);
        case 14: return stackMeta(35, 5, 1);
        case 15: return stackMeta(35, 5, 2);
        default: return stackMeta(276, 1, 4);
        }
    }

    private static String matrixEnc(ItemStack value) {
        int tag = 0;
        if (!value.isEmpty() && value.hasTagCompound())
            tag = "b".equals(value.getTagCompound().getString("oracle"))
                ? 2 : 1;
        return enc(value) + ":" + tag;
    }

    private static void pickupMatrix() {
        int row = 0;
        for (int slotSpec = 0; slotSpec <= 6; ++slotSpec)
            for (int cursorSpec : new int[] {0, 1, 2, 7, 8, 9, 10})
                for (int button = 0; button <= 1; ++button) {
                    TestPlayer p = new TestPlayer(false);
                    ContainerPlayer c = new ContainerPlayer(
                        p.inventory, false, p);
                    p.inventory.setInventorySlotContents(
                        9, matrixStack(slotSpec));
                    p.inventory.setItemStack(matrixStack(cursorSpec));
                    c.slotClick(9, button, ClickType.PICKUP, p);
                    System.out.printf("M%03d %s %s%n", row++,
                        matrixEnc(p.inventory.getStackInSlot(9)),
                        matrixEnc(p.inventory.getItemStack()));
                }
    }

    private static void printOrdinaryRow(
            int row, TestPlayer player) {
        System.out.printf("O%05d", row);
        for (int slot = 0; slot <= 40; ++slot)
            System.out.print(" " + matrixEnc(
                player.inventory.getStackInSlot(slot)));
        System.out.print(" " + matrixEnc(player.inventory.getItemStack()));
        for (ItemStack value : player.dropped)
            System.out.print(" " + matrixEnc(value));
        System.out.println();
    }

    private static void resetOrdinary(TestPlayer player) {
        for (int slot = 0; slot < player.inventory.getSizeInventory(); ++slot)
            player.inventory.setInventorySlotContents(slot, ItemStack.EMPTY);
        player.inventory.setItemStack(ItemStack.EMPTY);
        player.dropped.clear();
    }

    /** Generated semantic partitions for Container's ordinary Slot path. */
    private static void ordinaryCorpus() {
        final int specs = 17;
        int row = 0;
        TestPlayer survival = new TestPlayer(false);
        TestPlayer creativePlayer = new TestPlayer(true);
        ContainerPlayer survivalContainer = new ContainerPlayer(
            survival.inventory, false, survival);
        ContainerPlayer creativeContainer = new ContainerPlayer(
            creativePlayer.inventory, false, creativePlayer);
        for (int slotSpec = 0; slotSpec < specs; ++slotSpec)
            for (int cursorSpec = 0; cursorSpec < specs; ++cursorSpec)
                for (int button = 0; button <= 1; ++button) {
                    TestPlayer p = survival;
                    ContainerPlayer c = survivalContainer;
                    resetOrdinary(p);
                    p.inventory.setInventorySlotContents(
                        9, matrixStack(slotSpec));
                    p.inventory.setItemStack(matrixStack(cursorSpec));
                    c.slotClick(9, button, ClickType.PICKUP, p);
                    printOrdinaryRow(row++, p);
                }

        for (int slotSpec = 0; slotSpec < specs; ++slotSpec)
            for (int button = 0; button <= 1; ++button) {
                TestPlayer p = survival;
                ContainerPlayer c = survivalContainer;
                resetOrdinary(p);
                p.inventory.setInventorySlotContents(
                    9, matrixStack(slotSpec));
                c.slotClick(9, button, ClickType.QUICK_MOVE, p);
                printOrdinaryRow(row++, p);
            }

        for (int slotSpec = 0; slotSpec < specs; ++slotSpec)
            for (int targetSpec = 0; targetSpec < specs; ++targetSpec) {
                TestPlayer p = survival;
                ContainerPlayer c = survivalContainer;
                int button = (slotSpec * specs + targetSpec) % 9;
                resetOrdinary(p);
                p.inventory.setInventorySlotContents(
                    9, matrixStack(slotSpec));
                p.inventory.setInventorySlotContents(
                    button, matrixStack(targetSpec));
                c.slotClick(9, button, ClickType.SWAP, p);
                printOrdinaryRow(row++, p);
            }

        for (int slotSpec = 0; slotSpec < specs; ++slotSpec)
            for (int cursorSpec = 0; cursorSpec < specs; ++cursorSpec)
                for (int creative = 0; creative <= 1; ++creative) {
                    TestPlayer p = creative != 0
                        ? creativePlayer : survival;
                    ContainerPlayer c = creative != 0
                        ? creativeContainer : survivalContainer;
                    int button = (slotSpec + cursorSpec + creative) % 3;
                    resetOrdinary(p);
                    p.inventory.setInventorySlotContents(
                        9, matrixStack(slotSpec));
                    p.inventory.setItemStack(matrixStack(cursorSpec));
                    c.slotClick(9, button, ClickType.CLONE, p);
                    printOrdinaryRow(row++, p);
                }

        for (int slotSpec = 0; slotSpec < specs; ++slotSpec)
            for (int cursorSpec = 0; cursorSpec < specs; ++cursorSpec)
                for (int button = 0; button <= 1; ++button) {
                    TestPlayer p = survival;
                    ContainerPlayer c = survivalContainer;
                    resetOrdinary(p);
                    p.inventory.setInventorySlotContents(
                        9, matrixStack(slotSpec));
                    p.inventory.setItemStack(matrixStack(cursorSpec));
                    c.slotClick(9, button, ClickType.THROW, p);
                    printOrdinaryRow(row++, p);
                }

        for (int cursorSpec = 0; cursorSpec < specs; ++cursorSpec)
            for (int mode = 0; mode <= 2; ++mode) {
                TestPlayer p = mode == 2 ? creativePlayer : survival;
                ContainerPlayer c = mode == 2
                    ? creativeContainer : survivalContainer;
                resetOrdinary(p);
                p.inventory.setItemStack(matrixStack(cursorSpec));
                c.slotClick(-999, mode << 2, ClickType.QUICK_CRAFT, p);
                c.slotClick(9, 1 | mode << 2, ClickType.QUICK_CRAFT, p);
                c.slotClick(10, 1 | mode << 2, ClickType.QUICK_CRAFT, p);
                c.slotClick(-999, 2 | mode << 2, ClickType.QUICK_CRAFT, p);
                printOrdinaryRow(row++, p);
            }

        for (int cursorSpec = 0; cursorSpec < specs; ++cursorSpec)
            for (int slotSpec = 0; slotSpec < specs; ++slotSpec)
                for (int button = 0; button <= 1; ++button) {
                    TestPlayer p = survival;
                    ContainerPlayer c = survivalContainer;
                    resetOrdinary(p);
                    p.inventory.setItemStack(matrixStack(cursorSpec));
                    p.inventory.setInventorySlotContents(
                        9, matrixStack(slotSpec));
                    p.inventory.setInventorySlotContents(
                        10, matrixStack(cursorSpec));
                    c.slotClick(11, button, ClickType.PICKUP_ALL, p);
                    printOrdinaryRow(row++, p);
                }
        if (row != 2686)
            throw new AssertionError("ordinary corpus rows: " + row);
    }

    private static void pickupAllEdges() {
        TestPlayer p = new TestPlayer(false);
        ContainerPlayer c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setItemStack(tagged(1, 60, "a"));
        p.inventory.setInventorySlotContents(9, tagged(1, 2, "a"));
        p.inventory.setInventorySlotContents(10, tagged(1, 2, "b"));
        c.slotClick(11, 0, ClickType.PICKUP_ALL, p);
        System.out.printf("A2 %s %s %s%n", inv(p, 9), inv(p, 10),
            enc(p.inventory.getItemStack()));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setItemStack(stack(368, 10));
        p.inventory.setInventorySlotContents(9, stack(368, 10));
        c.slotClick(10, 0, ClickType.PICKUP_ALL, p);
        System.out.printf("A3 %s %s%n", inv(p, 9),
            enc(p.inventory.getItemStack()));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setItemStack(stack(368, 1));
        p.inventory.setInventorySlotContents(9, stack(368, 17));
        p.inventory.setInventorySlotContents(10, stack(368, 2));
        c.slotClick(11, 0, ClickType.PICKUP_ALL, p);
        System.out.printf("A4 %s %s %s%n", inv(p, 9), inv(p, 10),
            enc(p.inventory.getItemStack()));
    }

    private static void throwsFromSlot() {
        TestPlayer p = new TestPlayer(false);
        ContainerPlayer c = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setInventorySlotContents(9, stack(1, 3));
        c.slotClick(9, 0, ClickType.THROW, p);
        System.out.printf("T0 %s %d%n", inv(p, 9), dropped(p, 1));
        c.slotClick(9, 1, ClickType.THROW, p);
        System.out.printf("T1 %s %d%n", inv(p, 9), dropped(p, 1));
        p.inventory.setInventorySlotContents(9, stack(1, 3));
        p.inventory.setItemStack(stack(3, 1));
        c.slotClick(9, 1, ClickType.THROW, p);
        System.out.printf("T2 %s %s %d%n", inv(p, 9),
            enc(p.inventory.getItemStack()), dropped(p, 1));
    }

    private static void furnaceBucket() {
        TestPlayer p = new TestPlayer(false);
        TileEntityFurnace furnace = new TileEntityFurnace();
        ContainerFurnace c = new ContainerFurnace(p.inventory, furnace);
        p.inventory.setInventorySlotContents(0, stack(325, 4));
        c.slotClick(1, 0, ClickType.SWAP, p);
        System.out.printf("F0 %s %s%n", enc(furnace.getStackInSlot(1)),
            inv(p, 0));

        p = new TestPlayer(false);
        furnace = new TileEntityFurnace();
        c = new ContainerFurnace(p.inventory, furnace);
        p.inventory.setInventorySlotContents(0, stack(325, 4));
        furnace.setInventorySlotContents(1, stack(263, 2));
        c.slotClick(1, 0, ClickType.SWAP, p);
        System.out.printf("F1 %s %s %s%n", enc(furnace.getStackInSlot(1)),
            inv(p, 0), inv(p, 1));

        p = new TestPlayer(false);
        furnace = new TileEntityFurnace();
        c = new ContainerFurnace(p.inventory, furnace);
        furnace.setInventorySlotContents(2, stack(265, 1));
        p.inventory.setItemStack(stack(265, 63));
        c.slotClick(2, 0, ClickType.PICKUP, p);
        System.out.printf("F2 %s %s%n", enc(furnace.getStackInSlot(2)),
            enc(p.inventory.getItemStack()));

        p = new TestPlayer(false);
        furnace = new TileEntityFurnace();
        c = new ContainerFurnace(p.inventory, furnace);
        furnace.setInventorySlotContents(2, stack(265, 4));
        c.slotClick(2, 0, ClickType.SWAP, p);
        System.out.printf("F3 %s %s%n", enc(furnace.getStackInSlot(2)),
            inv(p, 0));

        p = new TestPlayer(false);
        furnace = new TileEntityFurnace();
        c = new ContainerFurnace(p.inventory, furnace);
        furnace.setInventorySlotContents(2, stackMeta(265, 1, 1));
        p.inventory.setItemStack(stackMeta(265, 63, 2));
        c.slotClick(2, 0, ClickType.PICKUP, p);
        System.out.printf("F4 %s %s%n", enc(furnace.getStackInSlot(2)),
            enc(p.inventory.getItemStack()));

        p = new TestPlayer(false);
        furnace = new TileEntityFurnace();
        c = new ContainerFurnace(p.inventory, furnace);
        furnace.setInventorySlotContents(2, stackMeta(1, 1, 1));
        p.inventory.setItemStack(stackMeta(1, 63, 2));
        c.slotClick(2, 0, ClickType.PICKUP, p);
        System.out.printf("F5 %s %s%n", enc(furnace.getStackInSlot(2)),
            enc(p.inventory.getItemStack()));
    }

    private static void craftingOutputMetadata() {
        TestPlayer p = new TestPlayer(false);
        ContainerWorkbench c = new ContainerWorkbench(
            p.inventory, p.world, BlockPos.ORIGIN);
        c.craftMatrix.setInventorySlotContents(0, stack(5, 1));
        c.craftMatrix.setInventorySlotContents(3, stack(5, 1));
        p.inventory.setItemStack(stackMeta(280, 60, 2));
        c.slotClick(0, 0, ClickType.PICKUP, p);
        System.out.printf("R4 %s %s %s %s%n",
            enc(c.craftResult.getStackInSlot(0)),
            enc(p.inventory.getItemStack()),
            enc(c.craftMatrix.getStackInSlot(0)),
            enc(c.craftMatrix.getStackInSlot(3)));
    }

    /** Exercise SlotCrafting around an NBT-producing special recipe. */
    private static void specialCraftingTakes() {
        for (int mode = 0; mode < 3; ++mode) {
            TestPlayer p = new TestPlayer(false);
            ContainerWorkbench c = new ContainerWorkbench(
                p.inventory, p.world, BlockPos.ORIGIN);
            c.craftMatrix.setInventorySlotContents(0, stack(299, 1));
            c.craftMatrix.setInventorySlotContents(1,
                stackMeta(351, 1, 11));
            if (mode == 0)
                c.slotClick(0, 0, ClickType.PICKUP, p);
            else if (mode == 1)
                c.slotClick(0, 0, ClickType.QUICK_MOVE, p);
            else
                c.slotClick(0, 0, ClickType.THROW, p);
            System.out.printf("T%d %s %s %s %s %d%n", mode,
                enc(c.craftResult.getStackInSlot(0)),
                enc(c.craftMatrix.getStackInSlot(0)),
                enc(c.craftMatrix.getStackInSlot(1)),
                enc(p.inventory.getItemStack()), dropped(p, 299));
        }
    }

    private static void emptyMapUse() {
        for (int count = 1; count <= 2; ++count) {
            TestPlayer p = new TestPlayer(false);
            p.inventory.currentItem = 0;
            p.inventory.setInventorySlotContents(0, stack(395, count));
            ActionResult<ItemStack> result = Items.MAP.onItemRightClick(
                p.world, p, EnumHand.MAIN_HAND);
            p.setHeldItem(EnumHand.MAIN_HAND, result.getResult());
            ItemStack filled = count == 1
                ? p.getHeldItemMainhand()
                : p.inventory.getStackInSlot(1);
            MapData data = Items.FILLED_MAP.getMapData(filled, p.world);
            System.out.printf("Q%d %s %s %d %d %d %d %s %s%n",
                count - 1, enc(p.getHeldItemMainhand()),
                enc(p.inventory.getStackInSlot(1)), filled.getMetadata(),
                data.xCenter, data.zCenter, data.dimension,
                data.trackingPosition, data.unlimitedTracking);
        }
    }

    private static void brewingTakes() {
        for (int mode = 0; mode < 5; ++mode) {
            TestPlayer p = new TestPlayer(false);
            TileEntityBrewingStand stand = new TileEntityBrewingStand();
            ContainerBrewingStand c = new ContainerBrewingStand(
                p.inventory, stand);
            stand.setInventorySlotContents(0, potion(mode == 1));
            if (mode == 0 || mode == 1) {
                c.slotClick(0, 0, ClickType.PICKUP, p);
            } else if (mode == 2) {
                c.slotClick(0, 0, ClickType.THROW, p);
            } else if (mode == 3) {
                c.slotClick(0, 0, ClickType.SWAP, p);
            } else {
                c.slotClick(0, 0, ClickType.QUICK_MOVE, p);
            }
            System.out.printf("B%d %s %s %d%n", mode,
                brewEnc(stand.getStackInSlot(0)),
                brewEnc(p.inventory.getItemStack()), p.potionStats);
        }

        TestPlayer p = new TestPlayer(false);
        TileEntityBrewingStand stand = new TileEntityBrewingStand();
        ContainerBrewingStand c = new ContainerBrewingStand(
            p.inventory, stand);
        stand.setInventorySlotContents(0, potion(false));
        p.inventory.setItemStack(potion(true));
        c.slotClick(0, 0, ClickType.PICKUP, p);
        System.out.printf("B5 %s %s %d%n",
            brewEnc(stand.getStackInSlot(0)),
            brewEnc(p.inventory.getItemStack()), p.potionStats);
    }

    private static void enchantingTransferTags() {
        TestPlayer p = new TestPlayer(false);
        ContainerEnchantment c = new ContainerEnchantment(
            p.inventory, p.world, BlockPos.ORIGIN);
        p.inventory.setInventorySlotContents(9, tagged(1, 2, "a"));
        c.slotClick(2, 0, ClickType.QUICK_MOVE, p);
        System.out.printf("E0 %s %s %s %s%n",
            enc(c.getSlot(0).getStack()),
            c.getSlot(0).getStack().hasTagCompound(), inv(p, 9),
            p.inventory.getStackInSlot(9).hasTagCompound());

        p = new TestPlayer(false);
        c = new ContainerEnchantment(p.inventory, p.world, BlockPos.ORIGIN);
        p.inventory.setInventorySlotContents(9, tagged(1, 1, "a"));
        c.slotClick(2, 0, ClickType.QUICK_MOVE, p);
        System.out.printf("E1 %s %s %s%n",
            enc(c.getSlot(0).getStack()),
            c.getSlot(0).getStack().hasTagCompound(), inv(p, 9));
    }

    private static void resultEdges() {
        TestPlayer p = new TestPlayer(false);
        ContainerPlayer c = new ContainerPlayer(p.inventory, false, p);
        c.craftMatrix.setInventorySlotContents(0, stack(17, 1));
        c.slotClick(0, 0, ClickType.THROW, p);
        System.out.printf("R0 %s %s %d%n",
            enc(c.craftMatrix.getStackInSlot(0)),
            enc(c.craftResult.getStackInSlot(0)), dropped(p, 5));

        p = new TestPlayer(false);
        c = new ContainerPlayer(p.inventory, false, p);
        c.craftMatrix.setInventorySlotContents(0, stack(17, 1));
        c.slotClick(0, 1, ClickType.THROW, p);
        System.out.printf("R1 %s %s %d%n",
            enc(c.craftMatrix.getStackInSlot(0)),
            enc(c.craftResult.getStackInSlot(0)), dropped(p, 5));

        p = new TestPlayer(false);
        p.experienceLevel = 30;
        ContainerRepair repair = new ContainerRepair(
            p.inventory, p.world, BlockPos.ORIGIN, p);
        repair.getSlot(0).putStack(stack(421, 2));
        repair.updateItemName("oracle");
        ItemStack namedCursor = repair.getSlot(2).getStack().copy();
        namedCursor.setCount(63);
        p.inventory.setItemStack(namedCursor);
        repair.slotClick(3, 1, ClickType.PICKUP_ALL, p);
        System.out.printf("R2 %s %s %s %d%n",
            enc(repair.getSlot(0).getStack()),
            enc(repair.getSlot(2).getStack()),
            enc(p.inventory.getItemStack()), p.experienceLevel);

        p = new TestPlayer(false);
        Merchant merchant = new Merchant();
        merchant.setCustomer(p);
        merchant.recipes.add(new MerchantRecipe(
            new ItemStack(Items.EMERALD, 3),
            new ItemStack(Items.BREAD, 2)));
        ContainerMerchant trade = new ContainerMerchant(
            p.inventory, merchant, p.world);
        InventoryMerchant inventory = trade.getMerchantInventory();
        inventory.setInventorySlotContents(0,
            new ItemStack(Items.EMERALD, 3));
        p.inventory.setItemStack(new ItemStack(Items.BREAD, 63));
        trade.slotClick(3, 0, ClickType.PICKUP_ALL, p);
        System.out.printf("R3 %s %s %s %d%n",
            enc(inventory.getStackInSlot(0)),
            enc(inventory.getStackInSlot(2)),
            enc(p.inventory.getItemStack()), merchant.uses);

        p = new TestPlayer(false);
        merchant = new Merchant();
        merchant.setCustomer(p);
        merchant.recipes.add(new MerchantRecipe(
            new ItemStack(Items.EMERALD, 3),
            new ItemStack(Items.BREAD, 2)));
        trade = new ContainerMerchant(p.inventory, merchant, p.world);
        inventory = trade.getMerchantInventory();
        inventory.setInventorySlotContents(0,
            new ItemStack(Items.EMERALD, 3));
        p.inventory.setItemStack(stackMeta(297, 62, 2));
        trade.slotClick(2, 0, ClickType.PICKUP, p);
        System.out.printf("R5 %s %s %s %d%n",
            enc(inventory.getStackInSlot(0)),
            enc(inventory.getStackInSlot(2)),
            enc(p.inventory.getItemStack()), merchant.uses);
    }

    private static void closeSequences() {
        TestPlayer p = new TestPlayer(false);
        ContainerPlayer player = new ContainerPlayer(p.inventory, false, p);
        p.inventory.setItemStack(stack(3, 2));
        player.craftMatrix.setInventorySlotContents(0, stack(17, 1));
        player.craftMatrix.setInventorySlotContents(1, stack(1, 3));
        player.onContainerClosed(p);
        droppedSequence("L0", p);

        p = new TestPlayer(false);
        ContainerEnchantment enchanting = new ContainerEnchantment(
            p.inventory, p.world, BlockPos.ORIGIN);
        p.inventory.setItemStack(stack(3, 2));
        enchanting.getSlot(0).putStack(stack(276, 1));
        enchanting.getSlot(1).putStack(stack(351, 4));
        enchanting.onContainerClosed(p);
        droppedSequence("L1", p);

        p = new TestPlayer(false);
        ContainerRepair repair = new ContainerRepair(
            p.inventory, p.world, BlockPos.ORIGIN, p);
        p.inventory.setItemStack(stack(3, 2));
        repair.getSlot(0).putStack(stack(267, 1));
        repair.getSlot(1).putStack(stack(265, 3));
        repair.onContainerClosed(p);
        droppedSequence("L2", p);

        p = new TestPlayer(false);
        Merchant merchant = new Merchant();
        merchant.setCustomer(p);
        ContainerMerchant trade = new ContainerMerchant(
            p.inventory, merchant, p.world);
        p.inventory.setItemStack(stack(3, 2));
        trade.getMerchantInventory().setInventorySlotContents(0, stack(388, 3));
        trade.getMerchantInventory().setInventorySlotContents(1, stack(4, 1));
        trade.onContainerClosed(p);
        droppedSequence("L3", p);
    }

    public static void main(String[] args) {
        Bootstrap.register();
        swaps();
        clones();
        quickMoves();
        drag("D0", false, 0, 1, 2);
        drag("D1", false, 4, 5, 6);
        drag("D2", true, 8, 9, 10);
        interruptedDrag();
        dragEdges();
        pickupAll("A0", 0);
        pickupAll("A1", 1);
        pickupAllEdges();
        pickupMatrix();
        ordinaryCorpus();
        throwsFromSlot();
        furnaceBucket();
        brewingTakes();
        enchantingTransferTags();
        resultEdges();
        craftingOutputMetadata();
        specialCraftingTakes();
        emptyMapUse();
        closeSequences();
    }
}
