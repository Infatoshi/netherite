package net.minecraft.enchantment;

import net.minecraft.util.WeightedRandom;

public class EnchantmentData extends WeightedRandom.Item
{
    /** Enchantment object associated with this EnchantmentData */
    public final Enchantment enchantmentobj;
    /** Enchantment level associated with this EnchantmentData */
    public final int enchantmentLevel;
    private static final String __OBFID = "CL_00000115";

    public EnchantmentData(Enchantment p_i1930_1_, int p_i1930_2_)
    {
        super(p_i1930_1_.getWeight());
        this.enchantmentobj = p_i1930_1_;
        this.enchantmentLevel = p_i1930_2_;
    }

    public EnchantmentData(int p_i1931_1_, int p_i1931_2_)
    {
        this(Enchantment.enchantmentsList[p_i1931_1_], p_i1931_2_);
    }
}