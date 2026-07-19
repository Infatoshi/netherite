// Verbatim MC 1.11.2 crafting engine (net/minecraft/item/crafting/ + net/minecraft/inventory/
// InventoryCrafting). The MATCHING ALGORITHM (ShapedRecipes.matches/checkMatch incl. the mirror
// flag and the 32767 metadata wildcard, ShapelessRecipes.matches copy-list removal,
// CraftingManager.addRecipe pattern parsing / addShapelessRecipe / findMatchingRecipe) and the
// RECIPE DATA (the chosen subset, built by the verbatim addRecipe code from the verbatim pattern
// strings, in vanilla registration order) are the decompiled MC code unchanged. The only
// substitution is the SANCTIONED one: the Item/Block object registry -> exact vanilla integer item
// ids (the thin ItemStack/Item/Block id shim below; getItem() reduces to an int id). guava
// Maps/Lists -> java.util HashMap/ArrayList (no logic change). This is the vanilla ground truth for
// cpu/crafting_recipes.c + cuda/crafting_recipes.cu.
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
    static final Block PLANKS = new Block(5), COBBLESTONE = new Block(4), LOG = new Block(17),
        TORCH = new Block(50), CHEST = new Block(54), CRAFTING_TABLE = new Block(58),
        FURNACE = new Block(61);
    static final Item FLINT_AND_STEEL = new Item(259), COAL = new Item(263), IRON_INGOT = new Item(265),
        WOODEN_SWORD = new Item(268), WOODEN_SHOVEL = new Item(269), WOODEN_PICKAXE = new Item(270),
        WOODEN_AXE = new Item(271), STONE_SWORD = new Item(272), STONE_SHOVEL = new Item(273),
        STONE_PICKAXE = new Item(274), STONE_AXE = new Item(275), STICK = new Item(280),
        WOODEN_HOE = new Item(290), STONE_HOE = new Item(291), FLINT = new Item(318);

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

        // Subset registration in EXACT vanilla order (RecipesTools loop wood+stone -> RecipesWeapons
        // loop wood+stone -> RecipesCrafting chest/furnace/table -> inline planks/sticks/torch x2 ->
        // flint_and_steel). Material arrays truncated to {PLANKS, COBBLESTONE}; patterns verbatim.
        void registerSubset() {
            // RecipesTools.addRecipes (verbatim loop; recipeItems truncated to 2 materials)
            String[][] toolPatterns = new String[][] {{"XXX", " # ", " # "}, {"X", "#", "#"}, {"XX", "X#", " #"}, {"XX", " #", " #"}};
            Object[][] toolItems = new Object[][] {{PLANKS, COBBLESTONE}, {WOODEN_PICKAXE, STONE_PICKAXE}, {WOODEN_SHOVEL, STONE_SHOVEL}, {WOODEN_AXE, STONE_AXE}, {WOODEN_HOE, STONE_HOE}};
            for (int i = 0; i < toolItems[0].length; ++i) {
                Object object = toolItems[0][i];
                for (int j = 0; j < toolItems.length - 1; ++j) {
                    Item item = (Item)toolItems[j + 1][i];
                    this.addRecipe(new ItemStack(item), new Object[] {toolPatterns[j], '#', STICK, 'X', object});
                }
            }
            // RecipesWeapons.addRecipes (verbatim loop; recipeItems truncated to 2 materials)
            String[][] weaponPatterns = new String[][] {{"X", "X", "#"}};
            Object[][] weaponItems = new Object[][] {{PLANKS, COBBLESTONE}, {WOODEN_SWORD, STONE_SWORD}};
            for (int i = 0; i < weaponItems[0].length; ++i) {
                Object object = weaponItems[0][i];
                for (int j = 0; j < weaponItems.length - 1; ++j) {
                    Item item = (Item)weaponItems[j + 1][i];
                    this.addRecipe(new ItemStack(item), new Object[] {weaponPatterns[j], '#', STICK, 'X', object});
                }
            }
            // RecipesCrafting subset (verbatim rows)
            this.addRecipe(new ItemStack(CHEST), new Object[] {"###", "# #", "###", '#', PLANKS});
            this.addRecipe(new ItemStack(FURNACE), new Object[] {"###", "# #", "###", '#', COBBLESTONE});
            this.addRecipe(new ItemStack(CRAFTING_TABLE), new Object[] {"##", "##", '#', PLANKS});
            // CraftingManager inline (verbatim rows; oak metadata == 0)
            this.addRecipe(new ItemStack(PLANKS, 4, 0), new Object[] {"#", '#', new ItemStack(LOG, 1, 0)});
            this.addRecipe(new ItemStack(STICK, 4), new Object[] {"#", "#", '#', PLANKS});
            this.addRecipe(new ItemStack(TORCH, 4), new Object[] {"X", "#", 'X', COAL, '#', STICK});
            this.addRecipe(new ItemStack(TORCH, 4), new Object[] {"X", "#", 'X', new ItemStack(COAL, 1, 1), '#', STICK});
            this.addShapelessRecipe(new ItemStack(FLINT_AND_STEEL, 1), new Object[] {new ItemStack(IRON_INGOT, 1), new ItemStack(FLINT, 1)});
        }
    }

    // ===== fixed battery (mirror of core/crafting_recipes.h cr_battery; slot = x + y*3) =====
    public static void main(String[] args) {
        CraftingManager manager = new CraftingManager();
        manager.registerSubset();

        ItemStack E  = ItemStack.EMPTY;
        ItemStack P  = new ItemStack(5, 1, 0);    // planks
        ItemStack C  = new ItemStack(4, 1, 0);    // cobblestone
        ItemStack S  = new ItemStack(280, 1, 0);  // stick
        ItemStack L0 = new ItemStack(17, 1, 0);   // oak log
        ItemStack L1 = new ItemStack(17, 1, 1);   // spruce log
        ItemStack CO = new ItemStack(263, 1, 0);  // coal
        ItemStack CH = new ItemStack(263, 1, 1);  // charcoal
        ItemStack IR = new ItemStack(265, 1, 0);  // iron ingot
        ItemStack FL = new ItemStack(318, 1, 0);  // flint

        ItemStack[][] battery = new ItemStack[][] {
            { P,P,P, E,S,E, E,S,E },     // G0  wooden_pickaxe
            { P,P,P, E,S,E, E,E,E },     // G1  pickaxe NON-match
            { C,C,C, E,S,E, E,S,E },     // G2  stone_pickaxe
            { P,P,E, P,S,E, E,S,E },     // G3  wooden_axe
            { P,P,E, S,P,E, S,E,E },     // G4  wooden_axe MIRRORED
            { E,P,P, E,P,S, E,E,S },     // G5  wooden_axe OFFSET
            { P,P,E, E,S,E, E,S,E },     // G6  wooden_hoe
            { P,E,E, P,E,E, S,E,E },     // G7  wooden_sword
            { E,C,E, E,C,E, E,S,E },     // G8  stone_sword OFFSET
            { P,P,P, P,E,P, P,P,P },     // G9  chest
            { C,C,C, C,E,C, C,C,C },     // G10 furnace
            { C,C,C, C,E,C, C,C,P },     // G11 furnace NON-match
            { P,P,E, P,P,E, E,E,E },     // G12 crafting_table
            { E,E,E, E,P,P, E,P,P },     // G13 crafting_table OFFSET
            { E,E,E, E,L0,E, E,E,E },    // G14 planks
            { E,E,E, E,L1,E, E,E,E },    // G15 planks NON-match (wrong meta)
            { P,E,E, P,E,E, E,E,E },     // G16 sticks
            { CO,E,E, S,E,E, E,E,E },    // G17 torch (coal)
            { CH,E,E, S,E,E, E,E,E },    // G18 torch (charcoal)
            { IR,FL,E, E,E,E, E,E,E },   // G19 flint_and_steel shapeless
            { E,E,E, E,FL,E, E,E,IR },   // G20 flint_and_steel SCRAMBLED
            { IR,FL,S, E,E,E, E,E,E },   // G21 flint_and_steel NON-match (extra)
            { E,E,E, E,E,E, E,E,E },     // G22 empty
            { P,E,E, E,E,E, E,E,E },     // G23 single plank
            { E,C,E, E,C,E, E,P,E },     // G24 sword NON-match
            { CO,E,E, P,E,E, E,E,E },    // G25 torch/sticks NON-match
            { P,E,E, S,E,E, S,E,E },     // G26 wooden_shovel
            { E,E,C, E,E,S, E,E,S },     // G27 stone_shovel OFFSET
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
