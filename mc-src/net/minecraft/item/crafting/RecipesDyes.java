package net.minecraft.item.crafting;

import net.minecraft.block.BlockColored;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;

public class RecipesDyes
{
    private static final String __OBFID = "CL_00000082";

    /**
     * Adds the dye recipes to the CraftingManager.
     */
    public void addRecipes(CraftingManager p_77607_1_)
    {
        int i;

        for (i = 0; i < 16; ++i)
        {
            p_77607_1_.addShapelessRecipe(new ItemStack(Blocks.wool, 1, BlockColored.func_150031_c(i)), new Object[] {new ItemStack(Items.dye, 1, i), new ItemStack(Item.getItemFromBlock(Blocks.wool), 1, 0)});
            p_77607_1_.addRecipe(new ItemStack(Blocks.stained_hardened_clay, 8, BlockColored.func_150031_c(i)), new Object[] {"###", "#X#", "###", '#', new ItemStack(Blocks.hardened_clay), 'X', new ItemStack(Items.dye, 1, i)});
            p_77607_1_.addRecipe(new ItemStack(Blocks.stained_glass, 8, BlockColored.func_150031_c(i)), new Object[] {"###", "#X#", "###", '#', new ItemStack(Blocks.glass), 'X', new ItemStack(Items.dye, 1, i)});
            p_77607_1_.addRecipe(new ItemStack(Blocks.stained_glass_pane, 16, i), new Object[] {"###", "###", '#', new ItemStack(Blocks.stained_glass, 1, i)});
        }

        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 1, 11), new Object[] {new ItemStack(Blocks.yellow_flower, 1, 0)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 1, 1), new Object[] {new ItemStack(Blocks.red_flower, 1, 0)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 3, 15), new Object[] {Items.bone});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 9), new Object[] {new ItemStack(Items.dye, 1, 1), new ItemStack(Items.dye, 1, 15)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 14), new Object[] {new ItemStack(Items.dye, 1, 1), new ItemStack(Items.dye, 1, 11)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 10), new Object[] {new ItemStack(Items.dye, 1, 2), new ItemStack(Items.dye, 1, 15)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 8), new Object[] {new ItemStack(Items.dye, 1, 0), new ItemStack(Items.dye, 1, 15)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 7), new Object[] {new ItemStack(Items.dye, 1, 8), new ItemStack(Items.dye, 1, 15)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 3, 7), new Object[] {new ItemStack(Items.dye, 1, 0), new ItemStack(Items.dye, 1, 15), new ItemStack(Items.dye, 1, 15)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 12), new Object[] {new ItemStack(Items.dye, 1, 4), new ItemStack(Items.dye, 1, 15)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 6), new Object[] {new ItemStack(Items.dye, 1, 4), new ItemStack(Items.dye, 1, 2)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 5), new Object[] {new ItemStack(Items.dye, 1, 4), new ItemStack(Items.dye, 1, 1)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 13), new Object[] {new ItemStack(Items.dye, 1, 5), new ItemStack(Items.dye, 1, 9)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 3, 13), new Object[] {new ItemStack(Items.dye, 1, 4), new ItemStack(Items.dye, 1, 1), new ItemStack(Items.dye, 1, 9)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 4, 13), new Object[] {new ItemStack(Items.dye, 1, 4), new ItemStack(Items.dye, 1, 1), new ItemStack(Items.dye, 1, 1), new ItemStack(Items.dye, 1, 15)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 1, 12), new Object[] {new ItemStack(Blocks.red_flower, 1, 1)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 1, 13), new Object[] {new ItemStack(Blocks.red_flower, 1, 2)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 1, 7), new Object[] {new ItemStack(Blocks.red_flower, 1, 3)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 1, 1), new Object[] {new ItemStack(Blocks.red_flower, 1, 4)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 1, 14), new Object[] {new ItemStack(Blocks.red_flower, 1, 5)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 1, 7), new Object[] {new ItemStack(Blocks.red_flower, 1, 6)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 1, 9), new Object[] {new ItemStack(Blocks.red_flower, 1, 7)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 1, 7), new Object[] {new ItemStack(Blocks.red_flower, 1, 8)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 11), new Object[] {new ItemStack(Blocks.double_plant, 1, 0)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 13), new Object[] {new ItemStack(Blocks.double_plant, 1, 1)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 1), new Object[] {new ItemStack(Blocks.double_plant, 1, 4)});
        p_77607_1_.addShapelessRecipe(new ItemStack(Items.dye, 2, 9), new Object[] {new ItemStack(Blocks.double_plant, 1, 5)});

        for (i = 0; i < 16; ++i)
        {
            p_77607_1_.addRecipe(new ItemStack(Blocks.carpet, 3, i), new Object[] {"##", '#', new ItemStack(Blocks.wool, 1, i)});
        }
    }
}