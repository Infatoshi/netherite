// Verbatim MC 1.11.2 FurnaceRecipes + TileEntityFurnace.getItemBurnTime over a KEEP ores/food
// recipe subset and KEEP fuel battery. Registry -> integer ids (same shim as crafting_recipes golden).
// HashMap iteration -> registration-order array (battery has no ambiguous inputs).
//
// Output: SR_NSMELT * 3 lines (itemId, count, meta) then SR_NFUEL burn-tick lines (%08x each).

import java.util.ArrayList;
import java.util.List;

public class Golden {

    static class Item  { final int id; Item(int id)  { this.id = id; } }
    static class Block { final int id; Block(int id) { this.id = id; } }

    static final int AIR = 0;
    static final int WILDCARD = 32767;

    /* blocks (mc_blocks.h) */
    static final Block GOLD_ORE = new Block(14), IRON_ORE = new Block(15), COAL_ORE = new Block(16),
        LAPIS_ORE = new Block(21), DIAMOND_ORE = new Block(56), REDSTONE_ORE = new Block(73),
        EMERALD_ORE = new Block(129), QUARTZ_ORE = new Block(153);
    static final Block PLANKS = new Block(5), LOG = new Block(17);

    /* items (crafting_recipes id convention) */
    static final Item COAL = new Item(263), DIAMOND = new Item(264), IRON_INGOT = new Item(265),
        GOLD_INGOT = new Item(266), STICK = new Item(280), REDSTONE = new Item(336),
        LAVA_BUCKET = new Item(332), PORKCHOP = new Item(319), COOKED_PORKCHOP = new Item(320),
        FISH = new Item(359), COOKED_FISH = new Item(360), DYE = new Item(361),
        BEEF = new Item(373), COOKED_BEEF = new Item(374), CHICKEN = new Item(375),
        COOKED_CHICKEN = new Item(376), MUTTON = new Item(377), COOKED_MUTTON = new Item(378),
        RABBIT = new Item(379), COOKED_RABBIT = new Item(380), ROTTEN_FLESH = new Item(384),
        BLAZE_ROD = new Item(386), EMERALD = new Item(408), POTATO = new Item(412),
        BAKED_POTATO = new Item(413), QUARTZ = new Item(426), CHORUS_FRUIT = new Item(454),
        CHORUS_FRUIT_POPPED = new Item(455);

    static final int DYE_BLUE = 4;

    static class ItemStack {
        static final ItemStack EMPTY = new ItemStack(AIR, 0, 0);
        int itemId, count, meta;
        ItemStack(int itemId, int count, int meta) { this.itemId = itemId; this.count = count; this.meta = meta; }
        ItemStack(Item item) { this(item.id, 1, 0); }
        ItemStack(Item item, int count) { this(item.id, count, 0); }
        ItemStack(Item item, int count, int meta) { this(item.id, count, meta); }
        ItemStack(Block block) { this(block.id, 1, 0); }
        ItemStack(Block block, int count) { this(block.id, count, 0); }
        ItemStack(Block block, int count, int meta) { this(block.id, count, meta); }
        boolean isEmpty() { return itemId == AIR || count <= 0; }
        int getItem() { return itemId; }
        int getMetadata() { return meta; }
        ItemStack copy() { return new ItemStack(itemId, count, meta); }
    }

    /* Registration-order recipe list (sanctioned HashMap substitution). */
    static class FurnaceRecipes {
        private final List<ItemStack> inputs = new ArrayList<>();
        private final List<ItemStack> outputs = new ArrayList<>();

        FurnaceRecipes() {
            addSmeltingRecipeForBlock(IRON_ORE, new ItemStack(IRON_INGOT), 0.7F);
            addSmeltingRecipeForBlock(GOLD_ORE, new ItemStack(GOLD_INGOT), 1.0F);
            addSmeltingRecipeForBlock(DIAMOND_ORE, new ItemStack(DIAMOND), 1.0F);
            addSmeltingRecipeForBlock(COAL_ORE, new ItemStack(COAL), 0.1F);
            addSmeltingRecipeForBlock(REDSTONE_ORE, new ItemStack(REDSTONE), 0.7F);
            addSmeltingRecipeForBlock(LAPIS_ORE, new ItemStack(DYE, 1, DYE_BLUE), 0.2F);
            addSmeltingRecipeForBlock(QUARTZ_ORE, new ItemStack(QUARTZ), 0.2F);
            addSmeltingRecipeForBlock(EMERALD_ORE, new ItemStack(EMERALD), 1.0F);
            addSmelting(PORKCHOP, new ItemStack(COOKED_PORKCHOP), 0.35F);
            addSmelting(BEEF, new ItemStack(COOKED_BEEF), 0.35F);
            addSmelting(CHICKEN, new ItemStack(COOKED_CHICKEN), 0.35F);
            addSmelting(RABBIT, new ItemStack(COOKED_RABBIT), 0.35F);
            addSmelting(MUTTON, new ItemStack(COOKED_MUTTON), 0.35F);
            addSmelting(POTATO, new ItemStack(BAKED_POTATO), 0.35F);
            addSmelting(CHORUS_FRUIT, new ItemStack(CHORUS_FRUIT_POPPED), 0.1F);
            addSmeltingRecipe(new ItemStack(FISH, 1, 0), new ItemStack(COOKED_FISH, 1, 0), 0.35F);
            addSmeltingRecipe(new ItemStack(FISH, 1, 1), new ItemStack(COOKED_FISH, 1, 1), 0.35F);
        }

        void addSmeltingRecipeForBlock(Block input, ItemStack stack, float xp) {
            addSmelting(new Item(input.id), stack, xp);
        }

        void addSmelting(Item input, ItemStack stack, float xp) {
            addSmeltingRecipe(new ItemStack(input, 1, WILDCARD), stack, xp);
        }

        void addSmeltingRecipe(ItemStack input, ItemStack stack, float xp) {
            inputs.add(input);
            outputs.add(stack.copy());
        }

        ItemStack getSmeltingResult(ItemStack stack) {
            for (int i = 0; i < inputs.size(); ++i) {
                if (compareItemStacks(stack, inputs.get(i)))
                    return outputs.get(i).copy();
            }
            return ItemStack.EMPTY;
        }

        boolean compareItemStacks(ItemStack stack1, ItemStack stack2) {
            return stack2.getItem() == stack1.getItem()
                && (stack2.getMetadata() == WILDCARD || stack2.getMetadata() == stack1.getMetadata());
        }
    }

    /* getItemBurnTime: KEEP fuel branches (verbatim values for listed ids + Material.WOOD). */
    static int getItemBurnTime(ItemStack stack) {
        if (stack.isEmpty()) return 0;
        int id = stack.getItem();
        if (id == COAL.id) return 1600;
        if (id == STICK.id) return 100;
        if (id == LAVA_BUCKET.id) return 20000;
        if (id == BLAZE_ROD.id) return 2400;
        if (id == LOG.id || id == PLANKS.id) return 300;
        return 0;
    }

    static void printResult(ItemStack r) {
        if (r.isEmpty()) {
            System.out.printf("%08x%n", 0xffffffff);
            System.out.printf("%08x%n", 0);
            System.out.printf("%08x%n", 0);
        } else {
            System.out.printf("%08x%n", r.getItem());
            System.out.printf("%08x%n", r.count);
            System.out.printf("%08x%n", r.getMetadata());
        }
    }

    public static void main(String[] args) {
        FurnaceRecipes fr = new FurnaceRecipes();
        ItemStack[] smelt = {
            new ItemStack(IRON_ORE), new ItemStack(GOLD_ORE), new ItemStack(DIAMOND_ORE),
            new ItemStack(COAL_ORE), new ItemStack(REDSTONE_ORE), new ItemStack(LAPIS_ORE),
            new ItemStack(QUARTZ_ORE), new ItemStack(EMERALD_ORE),
            new ItemStack(PORKCHOP), new ItemStack(BEEF), new ItemStack(CHICKEN),
            new ItemStack(RABBIT), new ItemStack(MUTTON), new ItemStack(POTATO),
            new ItemStack(CHORUS_FRUIT),
            new ItemStack(FISH, 1, 0), new ItemStack(FISH, 1, 1),
            new ItemStack(FISH, 1, 2), new ItemStack(FISH, 1, 3),
            new ItemStack(IRON_INGOT), new ItemStack(ROTTEN_FLESH), new ItemStack(COAL),
            new ItemStack(IRON_ORE, 1, 7),
            new ItemStack(FISH, 1, 0),
            ItemStack.EMPTY
        };
        for (ItemStack in : smelt)
            printResult(fr.getSmeltingResult(in));

        ItemStack[] fuel = {
            new ItemStack(COAL), new ItemStack(STICK), new ItemStack(LOG), new ItemStack(PLANKS),
            new ItemStack(LAVA_BUCKET), new ItemStack(BLAZE_ROD), new ItemStack(DIAMOND),
            new ItemStack(IRON_INGOT)
        };
        for (ItemStack f : fuel)
            System.out.printf("%08x%n", getItemBurnTime(f));
    }
}
