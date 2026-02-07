package net.minecraft.util;

import java.util.Random;
import net.minecraft.inventory.IInventory;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.tileentity.TileEntityDispenser;
import net.minecraftforge.common.ChestGenHooks;

public class WeightedRandomChestContent extends WeightedRandom.Item
{
    /** The Item/Block ID to generate in the Chest. */
    public ItemStack theItemId;
    /** The minimum chance of item generating. */
    public int theMinimumChanceToGenerateItem;
    /** The maximum chance of item generating. */
    public int theMaximumChanceToGenerateItem;
    private static final String __OBFID = "CL_00001505";

    public WeightedRandomChestContent(Item p_i45311_1_, int p_i45311_2_, int p_i45311_3_, int p_i45311_4_, int p_i45311_5_)
    {
        super(p_i45311_5_);
        this.theItemId = new ItemStack(p_i45311_1_, 1, p_i45311_2_);
        this.theMinimumChanceToGenerateItem = p_i45311_3_;
        this.theMaximumChanceToGenerateItem = p_i45311_4_;
    }

    public WeightedRandomChestContent(ItemStack p_i1558_1_, int p_i1558_2_, int p_i1558_3_, int p_i1558_4_)
    {
        super(p_i1558_4_);
        this.theItemId = p_i1558_1_;
        this.theMinimumChanceToGenerateItem = p_i1558_2_;
        this.theMaximumChanceToGenerateItem = p_i1558_3_;
    }

    /**
     * Generates the Chest contents.
     */
    public static void generateChestContents(Random p_76293_0_, WeightedRandomChestContent[] p_76293_1_, IInventory p_76293_2_, int p_76293_3_)
    {
        for (int j = 0; j < p_76293_3_; ++j)
        {
            WeightedRandomChestContent weightedrandomchestcontent = (WeightedRandomChestContent)WeightedRandom.getRandomItem(p_76293_0_, p_76293_1_);
            ItemStack[] stacks = weightedrandomchestcontent.generateChestContent(p_76293_0_, p_76293_2_);

            for (ItemStack item : stacks)
            {
                p_76293_2_.setInventorySlotContents(p_76293_0_.nextInt(p_76293_2_.getSizeInventory()), item);
            }
        }
    }

    public static void generateDispenserContents(Random p_150706_0_, WeightedRandomChestContent[] p_150706_1_, TileEntityDispenser p_150706_2_, int p_150706_3_)
    {
        for (int j = 0; j < p_150706_3_; ++j)
        {
            WeightedRandomChestContent weightedrandomchestcontent = (WeightedRandomChestContent)WeightedRandom.getRandomItem(p_150706_0_, p_150706_1_);
            int k = weightedrandomchestcontent.theMinimumChanceToGenerateItem + p_150706_0_.nextInt(weightedrandomchestcontent.theMaximumChanceToGenerateItem - weightedrandomchestcontent.theMinimumChanceToGenerateItem + 1);
            ItemStack[] stacks = weightedrandomchestcontent.generateChestContent(p_150706_0_, p_150706_2_);
            for (ItemStack item : stacks)
            {
                p_150706_2_.setInventorySlotContents(p_150706_0_.nextInt(p_150706_2_.getSizeInventory()), item);
            }
        }
    }

    public static WeightedRandomChestContent[] func_92080_a(WeightedRandomChestContent[] p_92080_0_, WeightedRandomChestContent ... p_92080_1_)
    {
        WeightedRandomChestContent[] aweightedrandomchestcontent1 = new WeightedRandomChestContent[p_92080_0_.length + p_92080_1_.length];
        int i = 0;

        for (int j = 0; j < p_92080_0_.length; ++j)
        {
            aweightedrandomchestcontent1[i++] = p_92080_0_[j];
        }

        WeightedRandomChestContent[] aweightedrandomchestcontent2 = p_92080_1_;
        int k = p_92080_1_.length;

        for (int l = 0; l < k; ++l)
        {
            WeightedRandomChestContent weightedrandomchestcontent1 = aweightedrandomchestcontent2[l];
            aweightedrandomchestcontent1[i++] = weightedrandomchestcontent1;
        }

        return aweightedrandomchestcontent1;
    }

    // -- Forge hooks
    /**
     * Allow a mod to submit a custom implementation that can delegate item stack generation beyond simple stack lookup
     *
     * @param random The current random for generation
     * @param newInventory The inventory being generated (do not populate it, but you can refer to it)
     * @return An array of {@link ItemStack} to put into the chest
     */
    protected ItemStack[] generateChestContent(Random random, IInventory newInventory)
    {
        return ChestGenHooks.generateStacks(random, theItemId, theMinimumChanceToGenerateItem, theMaximumChanceToGenerateItem);
    }
}