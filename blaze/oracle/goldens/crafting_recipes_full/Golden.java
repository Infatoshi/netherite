// Verbatim MC 1.11.2 crafting engine (net/minecraft/item/crafting/ + net/minecraft/inventory/
// InventoryCrafting). The MATCHING ALGORITHM (ShapedRecipes.matches/checkMatch incl. the mirror
// flag and the 32767 metadata wildcard, ShapelessRecipes.matches copy-list removal,
// CraftingManager.addRecipe pattern parsing / addShapelessRecipe / findMatchingRecipe) and the
// RECIPE DATA (the chosen subset, built by the verbatim addRecipe code from the verbatim pattern
// strings, in vanilla registration order) are the decompiled MC code unchanged. The only
// substitution is the SANCTIONED one: the Item/Block object registry -> exact vanilla integer item
// ids (the thin ItemStack/Item/Block id shim below; getItem() reduces to an int id). guava
// Maps/Lists -> java.util HashMap/ArrayList (no logic change). This is the vanilla ground truth for
// cpu/crafting_recipes_full.c + cuda/crafting_recipes_full.cu.
//
// Deviation (documented, output-invariant): CraftingManager's constructor ends with
// Collections.sort(recipes, ...). We iterate in registration order instead; the battery is built so
// no grid matches more than one recipe, so first-match is sort-invariant (golden == CPU == CUDA).
//
// Output: for each battery grid, findMatchingRecipe -> three %08x lines (itemId, count, meta);
// no-match prints itemId=0xffffffff, count=0, meta=0.

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class Golden {

    // ===== sanctioned registry shim: Item / Block carry the vanilla legacy integer item id =====
    static class Item  { final int id; Item(int id)  { this.id = id; } }
    static class Block { final int id; Block(int id) { this.id = id; } }   // item form id == block id

    // vanilla legacy ids (blocks: Block.getIdFromBlock; items: Items.java registration order)
    static final int AIR = 0;
    static final Block PLANKS = new Block(5), COBBLESTONE = new Block(4), GLASS = new Block(20),
        LOG = new Block(17), LOG2 = new Block(162),
        BROWN_MUSHROOM = new Block(39), RED_MUSHROOM = new Block(40), GOLD_BLOCK = new Block(41),
        IRON_BLOCK = new Block(42), LAPIS_BLOCK = new Block(22), TORCH = new Block(50),
        CHEST = new Block(54), DIAMOND_BLOCK = new Block(57), CRAFTING_TABLE = new Block(58),
        FURNACE = new Block(61), PUMPKIN = new Block(86), MELON_BLOCK = new Block(103),
        EMERALD_BLOCK = new Block(133), REDSTONE_BLOCK = new Block(152), SLIME_BLOCK = new Block(165),
        HAY_BLOCK = new Block(170), COAL_BLOCK = new Block(173), WOOL = new Block(35);
    static final Item IRON_SHOVEL = new Item(256), IRON_PICKAXE = new Item(257), IRON_AXE = new Item(258),
        FLINT_AND_STEEL = new Item(259), BOW = new Item(261), ARROW = new Item(262), COAL = new Item(263),
        DIAMOND = new Item(264), IRON_INGOT = new Item(265), GOLD_INGOT = new Item(266), IRON_SWORD = new Item(267),
        WOODEN_SWORD = new Item(268), WOODEN_SHOVEL = new Item(269), WOODEN_PICKAXE = new Item(270),
        WOODEN_AXE = new Item(271), STONE_SWORD = new Item(272), STONE_SHOVEL = new Item(273),
        STONE_PICKAXE = new Item(274), STONE_AXE = new Item(275), DIAMOND_SWORD = new Item(276),
        DIAMOND_SHOVEL = new Item(277), DIAMOND_PICKAXE = new Item(278), DIAMOND_AXE = new Item(279),
        STICK = new Item(280), BOWL = new Item(281), MUSHROOM_STEW = new Item(282), GOLDEN_SWORD = new Item(283),
        GOLDEN_SHOVEL = new Item(284), GOLDEN_PICKAXE = new Item(285), GOLDEN_AXE = new Item(286),
        STRING = new Item(287), FEATHER = new Item(288), WOODEN_HOE = new Item(290), STONE_HOE = new Item(291),
        IRON_HOE = new Item(292), DIAMOND_HOE = new Item(293), GOLDEN_HOE = new Item(294), WHEAT = new Item(296),
        LEATHER_HELMET = new Item(298), LEATHER_CHESTPLATE = new Item(299), LEATHER_LEGGINGS = new Item(300),
        LEATHER_BOOTS = new Item(301), IRON_HELMET = new Item(306), IRON_CHESTPLATE = new Item(307),
        IRON_LEGGINGS = new Item(308), IRON_BOOTS = new Item(309), DIAMOND_HELMET = new Item(310),
        DIAMOND_CHESTPLATE = new Item(311), DIAMOND_LEGGINGS = new Item(312), DIAMOND_BOOTS = new Item(313),
        GOLDEN_HELMET = new Item(314), GOLDEN_CHESTPLATE = new Item(315), GOLDEN_LEGGINGS = new Item(316),
        GOLDEN_BOOTS = new Item(317), FLINT = new Item(318), REDSTONE = new Item(331), LEATHER = new Item(334),
        GLOWSTONE_DUST = new Item(348), SUGAR = new Item(353), COOKIE = new Item(357), SHEARS = new Item(359),
        MELON = new Item(360), PUMPKIN_SEEDS = new Item(361), MELON_SEEDS = new Item(362), BLAZE_ROD = new Item(369),
        GOLD_NUGGET = new Item(371), GLASS_BOTTLE = new Item(374), SPIDER_EYE = new Item(375),
        FERMENTED_SPIDER_EYE = new Item(376), BLAZE_POWDER = new Item(377), MAGMA_CREAM = new Item(378),
        BREWING_STAND = new Item(379), SPECKLED_MELON = new Item(382), EMERALD = new Item(388),
        CARROT = new Item(391), BAKED_POTATO = new Item(393), GOLDEN_CARROT = new Item(396),
        EGG = new Item(344), PUMPKIN_PIE = new Item(400), COOKED_RABBIT = new Item(412),
        RABBIT_STEW = new Item(413), BEETROOT = new Item(434), BEETROOT_SOUP = new Item(436),
        SPECTRAL_ARROW = new Item(439), DYE = new Item(351), SLIME_BALL = new Item(341), IRON_NUGGET = new Item(452),
        BUCKET = new Item(325), BED = new Item(355), ENDER_PEARL = new Item(368), ENDER_EYE = new Item(381);

    // ===== thin ItemStack shim (POD triple + the methods the verbatim matcher calls) =====
    static class ItemStack {
        static final ItemStack EMPTY = new ItemStack(AIR, 0, 0);
        int itemId; int count; int meta;
        ItemStack(int itemId, int count, int meta) { this.itemId = itemId; this.count = count; this.meta = meta; }
        ItemStack(Item item) { this(item.id, 1, 0); }
        ItemStack(Item item, int count) { this(item.id, count, 0); }
        ItemStack(Item item, int count, int meta) { this(item.id, count, meta); }
        ItemStack(Block block) { this(block.id, 1, 0); }
        ItemStack(Block block, int count) { this(block.id, count, 0); }
        ItemStack(Block block, int count, int meta) { this(block.id, count, meta); }
        boolean isEmpty() { return this.itemId == AIR || this.count <= 0; }
        int getItem() { return this.itemId; }                 // registry-substituted: Item == int id
        int getMetadata() { return this.meta; }
        void setCount(int c) { this.count = c; }
        ItemStack copy() { return new ItemStack(this.itemId, this.count, this.meta); }
    }

    // ===== InventoryCrafting (verbatim-equivalent; Container/event hooks removed) =====
    static class InventoryCrafting {
        private final ItemStack[] stackList;
        private final int inventoryWidth;
        private final int inventoryHeight;
        InventoryCrafting(int width, int height) {
            this.stackList = new ItemStack[width * height];
            for (int i = 0; i < this.stackList.length; ++i) this.stackList[i] = ItemStack.EMPTY;
            this.inventoryWidth = width;
            this.inventoryHeight = height;
        }
        public int getSizeInventory() { return this.stackList.length; }
        public ItemStack getStackInSlot(int index) {
            return index >= this.getSizeInventory() ? ItemStack.EMPTY : this.stackList[index];
        }
        public ItemStack getStackInRowAndColumn(int row, int column) {
            return row >= 0 && row < this.inventoryWidth && column >= 0 && column <= this.inventoryHeight ? this.getStackInSlot(row + column * this.inventoryWidth) : ItemStack.EMPTY;
        }
        public void setInventorySlotContents(int index, ItemStack stack) { this.stackList[index] = stack; }
        public int getHeight() { return this.inventoryHeight; }
        public int getWidth() { return this.inventoryWidth; }
    }

    interface IRecipe {
        boolean matches(InventoryCrafting inv);
        ItemStack getCraftingResult(InventoryCrafting inv);
        int getRecipeSize();
    }

    // ===== ShapedRecipes (verbatim matches/checkMatch/getCraftingResult) =====
    static class ShapedRecipes implements IRecipe {
        public final int recipeWidth;
        public final int recipeHeight;
        public final ItemStack[] recipeItems;
        private final ItemStack recipeOutput;
        public ShapedRecipes(int width, int height, ItemStack[] ingredientsIn, ItemStack output) {
            this.recipeWidth = width;
            this.recipeHeight = height;
            this.recipeItems = ingredientsIn;
            for (int i = 0; i < this.recipeItems.length; ++i) {
                if (this.recipeItems[i] == null) {
                    this.recipeItems[i] = ItemStack.EMPTY;
                }
            }
            this.recipeOutput = output;
        }
        public ItemStack getRecipeOutput() { return this.recipeOutput; }
        public boolean matches(InventoryCrafting inv) {
            for (int i = 0; i <= 3 - this.recipeWidth; ++i) {
                for (int j = 0; j <= 3 - this.recipeHeight; ++j) {
                    if (this.checkMatch(inv, i, j, true)) {
                        return true;
                    }
                    if (this.checkMatch(inv, i, j, false)) {
                        return true;
                    }
                }
            }
            return false;
        }
        private boolean checkMatch(InventoryCrafting p_77573_1_, int p_77573_2_, int p_77573_3_, boolean p_77573_4_) {
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    int k = i - p_77573_2_;
                    int l = j - p_77573_3_;
                    ItemStack itemstack = ItemStack.EMPTY;
                    if (k >= 0 && l >= 0 && k < this.recipeWidth && l < this.recipeHeight) {
                        if (p_77573_4_) {
                            itemstack = this.recipeItems[this.recipeWidth - k - 1 + l * this.recipeWidth];
                        } else {
                            itemstack = this.recipeItems[k + l * this.recipeWidth];
                        }
                    }
                    ItemStack itemstack1 = p_77573_1_.getStackInRowAndColumn(i, j);
                    if (!itemstack1.isEmpty() || !itemstack.isEmpty()) {
                        if (itemstack1.isEmpty() != itemstack.isEmpty()) {
                            return false;
                        }
                        if (itemstack.getItem() != itemstack1.getItem()) {
                            return false;
                        }
                        if (itemstack.getMetadata() != 32767 && itemstack.getMetadata() != itemstack1.getMetadata()) {
                            return false;
                        }
                    }
                }
            }
            return true;
        }
        public ItemStack getCraftingResult(InventoryCrafting inv) {
            return this.getRecipeOutput().copy();
        }
        public int getRecipeSize() { return this.recipeWidth * this.recipeHeight; }
    }

    // ===== ShapelessRecipes (verbatim matches/getCraftingResult; guava Lists -> ArrayList) =====
    static class ShapelessRecipes implements IRecipe {
        private final ItemStack recipeOutput;
        public final List<ItemStack> recipeItems;
        public ShapelessRecipes(ItemStack output, List<ItemStack> inputList) {
            this.recipeOutput = output;
            this.recipeItems = inputList;
        }
        public boolean matches(InventoryCrafting inv) {
            List<ItemStack> list = new ArrayList<ItemStack>(this.recipeItems);
            for (int i = 0; i < inv.getHeight(); ++i) {
                for (int j = 0; j < inv.getWidth(); ++j) {
                    ItemStack itemstack = inv.getStackInRowAndColumn(j, i);
                    if (!itemstack.isEmpty()) {
                        boolean flag = false;
                        for (ItemStack itemstack1 : list) {
                            if (itemstack.getItem() == itemstack1.getItem() && (itemstack1.getMetadata() == 32767 || itemstack.getMetadata() == itemstack1.getMetadata())) {
                                flag = true;
                                list.remove(itemstack1);
                                break;
                            }
                        }
                        if (!flag) {
                            return false;
                        }
                    }
                }
            }
            return list.isEmpty();
        }
        public ItemStack getCraftingResult(InventoryCrafting inv) {
            return this.recipeOutput.copy();
        }
        public int getRecipeSize() { return this.recipeItems.size(); }
    }

    // ===== CraftingManager (verbatim addRecipe/addShapelessRecipe/findMatchingRecipe; guava -> java.util) =====
    static class CraftingManager {
        private final List<IRecipe> recipes = new ArrayList<IRecipe>();

        public ShapedRecipes addRecipe(ItemStack stack, Object... recipeComponents) {
            String s = "";
            int i = 0;
            int j = 0;
            int k = 0;
            if (recipeComponents[i] instanceof String[]) {
                String[] astring = (String[])((String[])recipeComponents[i++]);
                for (String s2 : astring) {
                    ++k;
                    j = s2.length();
                    s = s + s2;
                }
            } else {
                while (recipeComponents[i] instanceof String) {
                    String s1 = (String)recipeComponents[i++];
                    ++k;
                    j = s1.length();
                    s = s + s1;
                }
            }
            Map<Character, ItemStack> map;
            for (map = new HashMap<Character, ItemStack>(); i < recipeComponents.length; i += 2) {
                Character character = (Character)recipeComponents[i];
                ItemStack itemstack = ItemStack.EMPTY;
                if (recipeComponents[i + 1] instanceof Item) {
                    itemstack = new ItemStack((Item)recipeComponents[i + 1]);
                } else if (recipeComponents[i + 1] instanceof Block) {
                    itemstack = new ItemStack((Block)recipeComponents[i + 1], 1, 32767);
                } else if (recipeComponents[i + 1] instanceof ItemStack) {
                    itemstack = (ItemStack)recipeComponents[i + 1];
                }
                map.put(character, itemstack);
            }
            ItemStack[] aitemstack = new ItemStack[j * k];
            for (int l = 0; l < j * k; ++l) {
                char c0 = s.charAt(l);
                if (map.containsKey(Character.valueOf(c0))) {
                    aitemstack[l] = ((ItemStack)map.get(Character.valueOf(c0))).copy();
                } else {
                    aitemstack[l] = ItemStack.EMPTY;
                }
            }
            ShapedRecipes shapedrecipes = new ShapedRecipes(j, k, aitemstack, stack);
            this.recipes.add(shapedrecipes);
            return shapedrecipes;
        }

        public void addShapelessRecipe(ItemStack stack, Object... recipeComponents) {
            List<ItemStack> list = new ArrayList<ItemStack>();
            for (Object object : recipeComponents) {
                if (object instanceof ItemStack) {
                    list.add(((ItemStack)object).copy());
                } else if (object instanceof Item) {
                    list.add(new ItemStack((Item)object));
                } else {
                    if (!(object instanceof Block)) {
                        throw new IllegalArgumentException("Invalid shapeless recipe: unknown type " + object.getClass().getName() + "!");
                    }
                    list.add(new ItemStack((Block)object));
                }
            }
            this.recipes.add(new ShapelessRecipes(stack, list));
        }

        public ItemStack findMatchingRecipe(InventoryCrafting craftMatrix) {
            for (IRecipe irecipe : this.recipes) {
                if (irecipe.matches(craftMatrix)) {
                    return irecipe.getCraftingResult(craftMatrix);
                }
            }
            return ItemStack.EMPTY;
        }

        // KEEP-scope registration: RecipesTools/Weapons/Ingots/Food, RecipesCrafting(chest,furnace,table),
        // RecipesArmor, inline planks/sticks/torch/flint_and_steel. Vanilla registration order.
        void registerKeepSet() {
            String[][] toolPatterns = new String[][] {{"XXX", " # ", " # "}, {"X", "#", "#"}, {"XX", "X#", " #"}, {"XX", " #", " #"}};
            Object[][] toolItems = new Object[][] {
                {PLANKS, COBBLESTONE, IRON_INGOT, DIAMOND, GOLD_INGOT},
                {WOODEN_PICKAXE, STONE_PICKAXE, IRON_PICKAXE, DIAMOND_PICKAXE, GOLDEN_PICKAXE},
                {WOODEN_SHOVEL, STONE_SHOVEL, IRON_SHOVEL, DIAMOND_SHOVEL, GOLDEN_SHOVEL},
                {WOODEN_AXE, STONE_AXE, IRON_AXE, DIAMOND_AXE, GOLDEN_AXE},
                {WOODEN_HOE, STONE_HOE, IRON_HOE, DIAMOND_HOE, GOLDEN_HOE}};
            for (int i = 0; i < toolItems[0].length; ++i) {
                Object object = toolItems[0][i];
                for (int j = 0; j < toolItems.length - 1; ++j) {
                    Item item = (Item)toolItems[j + 1][i];
                    this.addRecipe(new ItemStack(item), new Object[] {toolPatterns[j], '#', STICK, 'X', object});
                }
            }
            this.addRecipe(new ItemStack(SHEARS), new Object[] {" #", "# ", '#', IRON_INGOT});

            String[][] weaponPatterns = new String[][] {{"X", "X", "#"}};
            Object[][] weaponItems = new Object[][] {
                {PLANKS, COBBLESTONE, IRON_INGOT, DIAMOND, GOLD_INGOT},
                {WOODEN_SWORD, STONE_SWORD, IRON_SWORD, DIAMOND_SWORD, GOLDEN_SWORD}};
            for (int i = 0; i < weaponItems[0].length; ++i) {
                Object object = weaponItems[0][i];
                for (int j = 0; j < weaponItems.length - 1; ++j) {
                    Item item = (Item)weaponItems[j + 1][i];
                    this.addRecipe(new ItemStack(item), new Object[] {weaponPatterns[j], '#', STICK, 'X', object});
                }
            }
            this.addRecipe(new ItemStack(BOW, 1), new Object[] {" #X", "# X", " #X", 'X', STRING, '#', STICK});
            this.addRecipe(new ItemStack(ARROW, 4), new Object[] {"X", "#", "Y", 'Y', FEATHER, 'X', FLINT, '#', STICK});
            this.addRecipe(new ItemStack(SPECTRAL_ARROW, 2), new Object[] {" # ", "#X#", " # ", 'X', ARROW, '#', GLOWSTONE_DUST});

            Object[][] ingotPairs = new Object[][] {
                {GOLD_BLOCK, new ItemStack(GOLD_INGOT, 9)},
                {IRON_BLOCK, new ItemStack(IRON_INGOT, 9)},
                {DIAMOND_BLOCK, new ItemStack(DIAMOND, 9)},
                {EMERALD_BLOCK, new ItemStack(EMERALD, 9)},
                {LAPIS_BLOCK, new ItemStack(DYE, 9, 4)},
                {REDSTONE_BLOCK, new ItemStack(REDSTONE, 9)},
                {COAL_BLOCK, new ItemStack(COAL, 9, 0)},
                {HAY_BLOCK, new ItemStack(WHEAT, 9)},
                {SLIME_BLOCK, new ItemStack(SLIME_BALL, 9)}};
            for (Object[] pair : ingotPairs) {
                Block block = (Block)pair[0];
                ItemStack itemstack = (ItemStack)pair[1];
                ItemStack ing = itemstack.copy();
                ing.setCount(1);
                this.addRecipe(new ItemStack(block), new Object[] {"###", "###", "###", '#', ing});
                this.addRecipe(itemstack, new Object[] {"#", '#', block});
            }
            this.addRecipe(new ItemStack(GOLD_INGOT), new Object[] {"###", "###", "###", '#', GOLD_NUGGET});
            this.addRecipe(new ItemStack(GOLD_NUGGET, 9), new Object[] {"#", '#', GOLD_INGOT});
            this.addRecipe(new ItemStack(IRON_INGOT), new Object[] {"###", "###", "###", '#', IRON_NUGGET});
            this.addRecipe(new ItemStack(IRON_NUGGET, 9), new Object[] {"#", '#', IRON_INGOT});

            this.addShapelessRecipe(new ItemStack(MUSHROOM_STEW), new Object[] {BROWN_MUSHROOM, RED_MUSHROOM, BOWL});
            this.addRecipe(new ItemStack(COOKIE, 8), new Object[] {"#X#", 'X', new ItemStack(DYE, 1, 3), '#', WHEAT});
            this.addRecipe(new ItemStack(RABBIT_STEW), new Object[] {" R ", "CPM", " B ", 'R', new ItemStack(COOKED_RABBIT), 'C', CARROT, 'P', BAKED_POTATO, 'M', BROWN_MUSHROOM, 'B', BOWL});
            this.addRecipe(new ItemStack(RABBIT_STEW), new Object[] {" R ", "CPD", " B ", 'R', new ItemStack(COOKED_RABBIT), 'C', CARROT, 'P', BAKED_POTATO, 'D', RED_MUSHROOM, 'B', BOWL});
            this.addRecipe(new ItemStack(MELON_BLOCK), new Object[] {"MMM", "MMM", "MMM", 'M', MELON});
            this.addRecipe(new ItemStack(BEETROOT_SOUP), new Object[] {"OOO", "OOO", " B ", 'O', BEETROOT, 'B', BOWL});
            this.addRecipe(new ItemStack(MELON_SEEDS), new Object[] {"M", 'M', MELON});
            this.addRecipe(new ItemStack(PUMPKIN_SEEDS, 4), new Object[] {"M", 'M', PUMPKIN});
            this.addShapelessRecipe(new ItemStack(PUMPKIN_PIE), new Object[] {PUMPKIN, SUGAR, EGG});
            this.addShapelessRecipe(new ItemStack(FERMENTED_SPIDER_EYE), new Object[] {SPIDER_EYE, BROWN_MUSHROOM, SUGAR});
            this.addShapelessRecipe(new ItemStack(BLAZE_POWDER, 2), new Object[] {BLAZE_ROD});
            this.addShapelessRecipe(new ItemStack(MAGMA_CREAM), new Object[] {BLAZE_POWDER, SLIME_BALL});

            this.addRecipe(new ItemStack(CHEST), new Object[] {"###", "# #", "###", '#', PLANKS});
            this.addRecipe(new ItemStack(FURNACE), new Object[] {"###", "# #", "###", '#', COBBLESTONE});
            this.addRecipe(new ItemStack(CRAFTING_TABLE), new Object[] {"##", "##", '#', PLANKS});

            String[][] armorPatterns = new String[][] {{"XXX", "X X"}, {"X X", "XXX", "XXX"}, {"XXX", "X X", "X X"}, {"X X", "X X"}};
            Item[][] armorItems = new Item[][] {
                {LEATHER, IRON_INGOT, DIAMOND, GOLD_INGOT},
                {LEATHER_HELMET, IRON_HELMET, DIAMOND_HELMET, GOLDEN_HELMET},
                {LEATHER_CHESTPLATE, IRON_CHESTPLATE, DIAMOND_CHESTPLATE, GOLDEN_CHESTPLATE},
                {LEATHER_LEGGINGS, IRON_LEGGINGS, DIAMOND_LEGGINGS, GOLDEN_LEGGINGS},
                {LEATHER_BOOTS, IRON_BOOTS, DIAMOND_BOOTS, GOLDEN_BOOTS}};
            for (int i = 0; i < armorItems[0].length; ++i) {
                Item mat = armorItems[0][i];
                for (int j = 0; j < armorItems.length - 1; ++j) {
                    Item out = armorItems[j + 1][i];
                    this.addRecipe(new ItemStack(out), new Object[] {armorPatterns[j], 'X', mat});
                }
            }

            // Per-species planks, vanilla registration order (CraftingManager.java:117-122).
            this.addRecipe(new ItemStack(PLANKS, 4, 0), new Object[] {"#", '#', new ItemStack(LOG, 1, 0)});
            this.addRecipe(new ItemStack(PLANKS, 4, 1), new Object[] {"#", '#', new ItemStack(LOG, 1, 1)});
            this.addRecipe(new ItemStack(PLANKS, 4, 2), new Object[] {"#", '#', new ItemStack(LOG, 1, 2)});
            this.addRecipe(new ItemStack(PLANKS, 4, 3), new Object[] {"#", '#', new ItemStack(LOG, 1, 3)});
            this.addRecipe(new ItemStack(PLANKS, 4, 4), new Object[] {"#", '#', new ItemStack(LOG2, 1, 0)});
            this.addRecipe(new ItemStack(PLANKS, 4, 5), new Object[] {"#", '#', new ItemStack(LOG2, 1, 1)});
            this.addRecipe(new ItemStack(STICK, 4), new Object[] {"#", "#", '#', PLANKS});
            this.addRecipe(new ItemStack(TORCH, 4), new Object[] {"X", "#", 'X', COAL, '#', STICK});
            this.addRecipe(new ItemStack(TORCH, 4), new Object[] {"X", "#", 'X', new ItemStack(COAL, 1, 1), '#', STICK});
            this.addShapelessRecipe(new ItemStack(FLINT_AND_STEEL, 1), new Object[] {new ItemStack(IRON_INGOT, 1), new ItemStack(FLINT, 1)});

            // Route-critical End-run recipes (vanilla CraftingManager.java:146,189,193).
            this.addRecipe(new ItemStack(BUCKET, 1), new Object[] {"# #", " # ", '#', IRON_INGOT});
            this.addRecipe(new ItemStack(BED, 1), new Object[] {"###", "XXX", '#', WOOL, 'X', PLANKS});
            this.addShapelessRecipe(new ItemStack(ENDER_EYE, 1), new Object[] {ENDER_PEARL, BLAZE_POWDER});

            // Route-critical brewing recipes (vanilla CraftingManager.java:127,138,177-178).
            this.addRecipe(new ItemStack(GLASS_BOTTLE, 3), new Object[] {"# #", " # ", '#', GLASS});
            this.addRecipe(new ItemStack(BREWING_STAND, 1), new Object[] {" B ", "###", '#', COBBLESTONE, 'B', BLAZE_ROD});
            this.addRecipe(new ItemStack(GOLDEN_CARROT), new Object[] {"###", "#X#", "###", '#', GOLD_NUGGET, 'X', CARROT});
            this.addRecipe(new ItemStack(SPECKLED_MELON, 1), new Object[] {"###", "#X#", "###", '#', GOLD_NUGGET, 'X', MELON});
        }
    }

    // ===== fixed battery (mirror of core/crafting_recipes.h cr_battery; slot = x + y*3) =====
    public static void main(String[] args) {
        CraftingManager manager = new CraftingManager();
        manager.registerKeepSet();

        ItemStack E  = ItemStack.EMPTY;
        ItemStack P  = new ItemStack(5, 1, 0);
        ItemStack C  = new ItemStack(4, 1, 0);
        ItemStack S  = new ItemStack(280, 1, 0);
        ItemStack L0 = new ItemStack(17, 1, 0);
        ItemStack L1 = new ItemStack(17, 1, 1);
        ItemStack LA = new ItemStack(162, 1, 0);
        ItemStack CO = new ItemStack(263, 1, 0);
        ItemStack CH = new ItemStack(263, 1, 1);
        ItemStack IR = new ItemStack(265, 1, 0);
        ItemStack FL = new ItemStack(318, 1, 0);
        ItemStack D  = new ItemStack(264, 1, 0);
        ItemStack G  = new ItemStack(266, 1, 0);
        ItemStack ST = new ItemStack(287, 1, 0);
        ItemStack FE = new ItemStack(288, 1, 0);
        ItemStack GD = new ItemStack(348, 1, 0);
        ItemStack A  = new ItemStack(262, 1, 0);
        ItemStack IB = new ItemStack(42, 1, 0);
        ItemStack BM = new ItemStack(39, 1, 0);
        ItemStack RM = new ItemStack(40, 1, 0);
        ItemStack BW = new ItemStack(281, 1, 0);
        ItemStack W  = new ItemStack(296, 1, 0);
        ItemStack DY3 = new ItemStack(351, 1, 3);
        ItemStack L  = new ItemStack(334, 1, 0);
        ItemStack BR = new ItemStack(369, 1, 0);
        ItemStack PK = new ItemStack(86, 1, 0);
        ItemStack SG = new ItemStack(353, 1, 0);
        ItemStack EG = new ItemStack(344, 1, 0);
        ItemStack WO = new ItemStack(35, 1, 14);
        ItemStack EP = new ItemStack(368, 1, 0);
        ItemStack BP = new ItemStack(377, 1, 0);
        ItemStack GL = new ItemStack(20, 1, 0);
        ItemStack GN = new ItemStack(371, 1, 0);
        ItemStack CA = new ItemStack(391, 1, 0);
        ItemStack ME = new ItemStack(360, 1, 0);

        ItemStack[][] battery = new ItemStack[][] {
            { P,P,P, E,S,E, E,S,E },
            { P,P,P, E,S,E, E,E,E },
            { C,C,C, E,S,E, E,S,E },
            { P,P,E, P,S,E, E,S,E },
            { P,P,E, S,P,E, S,E,E },
            { E,P,P, E,P,S, E,E,S },
            { P,P,E, E,S,E, E,S,E },
            { P,E,E, P,E,E, S,E,E },
            { E,C,E, E,C,E, E,S,E },
            { P,P,P, P,E,P, P,P,P },
            { C,C,C, C,E,C, C,C,C },
            { C,C,C, C,E,C, C,C,P },
            { P,P,E, P,P,E, E,E,E },
            { E,E,E, E,P,P, E,P,P },
            { E,E,E, E,L0,E, E,E,E },
            { E,E,E, E,L1,E, E,E,E },
            { E,E,E, E,LA,E, E,E,E },
            { P,E,E, P,E,E, E,E,E },
            { CO,E,E, S,E,E, E,E,E },
            { CH,E,E, S,E,E, E,E,E },
            { IR,FL,E, E,E,E, E,E,E },
            { E,E,E, E,FL,E, E,E,IR },
            { IR,FL,S, E,E,E, E,E,E },
            { E,E,E, E,E,E, E,E,E },
            { P,E,E, E,E,E, E,E,E },
            { E,C,E, E,C,E, E,P,E },
            { CO,E,E, P,E,E, E,E,E },
            { P,E,E, S,E,E, S,E,E },
            { E,E,C, E,E,S, E,E,S },
            { IR,IR,IR, E,S,E, E,S,E },
            { D,E,E, D,E,E, S,E,E },
            { G,G,E, E,S,E, E,S,E },
            { E,IR,E, IR,E,E, E,E,E },
            { E,S,ST, E,S,ST, E,S,ST },
            { FL,E,E, S,E,E, FE,E,E },
            { E,GD,E, GD,A,GD, E,GD,E },
            { G,G,G, G,G,G, G,G,G },
            { E,E,E, E,IB,E, E,E,E },
            { BM,E,E, E,RM,E, E,BW,E },
            { W,DY3,W, E,E,E, E,E,E },
            { IR,IR,IR, IR,E,IR, E,E,E },
            { D,E,D, D,D,D, D,D,D },
            { L,E,L, L,E,L, E,E,E },
            { BR,E,E, E,E,E, E,E,E },
            { PK,E,E, E,SG,E, E,EG,E },
            { IR,IR,IR, IR,IR,IR, E,S,E },
            { IR,E,IR, E,IR,E, E,E,E },
            { WO,WO,WO, P,P,P, E,E,E },
            { E,E,EP, E,E,E, BP,E,E },
            { GL,E,GL, E,GL,E, E,E,E },
            { GL,E,GL, E,E,E, E,E,E },
            { E,BR,E, C,C,C, E,E,E },
            { GN,GN,GN, GN,CA,GN, GN,GN,GN },
            { GN,GN,GN, GN,ME,GN, GN,GN,GN },
        };

        StringBuilder sb = new StringBuilder();
        for (int t = 0; t < battery.length; ++t) {
            InventoryCrafting inv = new InventoryCrafting(3, 3);
            for (int q = 0; q < 9; ++q) inv.setInventorySlotContents(q, battery[t][q]);
            ItemStack r = manager.findMatchingRecipe(inv);
            int item = r.isEmpty() ? 0xffffffff : r.getItem();
            int count = r.isEmpty() ? 0 : r.count;
            int meta = r.isEmpty() ? 0 : r.getMetadata();
            sb.append(String.format("%08x%n", item));
            sb.append(String.format("%08x%n", count));
            sb.append(String.format("%08x%n", meta));
        }
        System.out.print(sb);
    }
}
