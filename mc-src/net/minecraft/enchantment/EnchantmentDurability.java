package net.minecraft.enchantment;

import java.util.Random;
import net.minecraft.item.ItemArmor;
import net.minecraft.item.ItemStack;

public class EnchantmentDurability extends Enchantment
{
    private static final String __OBFID = "CL_00000103";

    protected EnchantmentDurability(int p_i1924_1_, int p_i1924_2_)
    {
        super(p_i1924_1_, p_i1924_2_, EnumEnchantmentType.breakable);
        this.setName("durability");
    }

    /**
     * Returns the minimal value of enchantability needed on the enchantment level passed.
     */
    public int getMinEnchantability(int p_77321_1_)
    {
        return 5 + (p_77321_1_ - 1) * 8;
    }

    /**
     * Returns the maximum value of enchantability nedded on the enchantment level passed.
     */
    public int getMaxEnchantability(int p_77317_1_)
    {
        return super.getMinEnchantability(p_77317_1_) + 50;
    }

    /**
     * Returns the maximum level that the enchantment can have.
     */
    public int getMaxLevel()
    {
        return 3;
    }

    public boolean canApply(ItemStack p_92089_1_)
    {
        return p_92089_1_.isItemStackDamageable() ? true : super.canApply(p_92089_1_);
    }

    /**
     * Used by ItemStack.attemptDamageItem. Randomly determines if a point of damage should be negated using the
     * enchantment level (par1). If the ItemStack is Armor then there is a flat 60% chance for damage to be negated no
     * matter the enchantment level, otherwise there is a 1-(par/1) chance for damage to be negated.
     */
    public static boolean negateDamage(ItemStack p_92097_0_, int p_92097_1_, Random p_92097_2_)
    {
        return p_92097_0_.getItem() instanceof ItemArmor && p_92097_2_.nextFloat() < 0.6F ? false : p_92097_2_.nextInt(p_92097_1_ + 1) > 0;
    }
}