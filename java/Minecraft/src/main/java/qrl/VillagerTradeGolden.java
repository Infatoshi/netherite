package qrl;

import java.util.Random;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.village.MerchantRecipe;
import net.minecraft.village.MerchantRecipeList;

/** Real 1.11.2 initial ordinary-career recipe oracle. */
public final class VillagerTradeGolden {
    private static final long[][] CASES = {
        {0, 4096}, {0, 6144}, {0, 0}, {0, 256},
        {1, 0}, {2, 0}, {3, 0}, {3, 2},
        {4, 4096}, {4, 0}, {5, 0}
    };

    private static int item(ItemStack stack) {
        return stack.isEmpty() ? 0 : Item.getIdFromItem(stack.getItem());
    }

    private static int count(ItemStack stack) {
        return stack.isEmpty() ? 0 : stack.getCount();
    }

    private static int meta(ItemStack stack) {
        return stack.isEmpty() ? 0 : stack.getMetadata();
    }

    public static void main(String[] args) {
        Bootstrap.register();
        EntityVillager.ITradeList[][][][] all =
            EntityVillager.GET_TRADES_DONT_USE();
        for (long[] fixture : CASES) {
            int profession = (int)fixture[0];
            long seed = fixture[1];
            Random random = new Random(seed);
            int career = random.nextInt(all[profession].length) + 1;
            MerchantRecipeList recipes = new MerchantRecipeList();
            EntityVillager.ITradeList[][] levels = all[profession][career - 1];
            if (levels.length > 0) {
                for (EntityVillager.ITradeList entry : levels[0])
                    entry.addMerchantRecipe(null, recipes, random);
            }
            System.out.printf("T %d %d %d %d%n",
                profession, seed, career, recipes.size());
            for (int i = 0; i < recipes.size(); ++i) {
                MerchantRecipe recipe = recipes.get(i);
                ItemStack a = recipe.getItemToBuy();
                ItemStack b = recipe.getSecondItemToBuy();
                ItemStack out = recipe.getItemToSell();
                System.out.printf("O %d %d %d %d %d %d %d %d %d %d %d%n",
                    profession, seed, i,
                    item(a), count(a), meta(a),
                    item(b), count(b), meta(b),
                    item(out), count(out));
            }
            if (!recipes.isEmpty()) {
                MerchantRecipe recipe = recipes.get(0);
                for (int use = 1; use <= 2; ++use) {
                    recipe.incrementToolUses();
                    float pitch = (random.nextFloat() - random.nextFloat())
                        * 0.2F + 1.0F;
                    int xp = 3 + random.nextInt(4);
                    boolean reset = recipe.getToolUses() == 1
                        || random.nextInt(5) == 0;
                    if (reset) xp += 5;
                    System.out.printf("U %d %d %d %d %d %d%n",
                        profession, seed, use,
                        Float.floatToRawIntBits(pitch), xp, reset ? 1 : 0);
                }
            }
        }
    }
}
