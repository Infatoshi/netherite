package net.minecraft.enchantment;

public class EnchantmentOxygen extends Enchantment
{
    private static final String __OBFID = "CL_00000120";

    public EnchantmentOxygen(int p_i1935_1_, int p_i1935_2_)
    {
        super(p_i1935_1_, p_i1935_2_, EnumEnchantmentType.armor_head);
        this.setName("oxygen");
    }

    /**
     * Returns the minimal value of enchantability needed on the enchantment level passed.
     */
    public int getMinEnchantability(int p_77321_1_)
    {
        return 10 * p_77321_1_;
    }

    /**
     * Returns the maximum value of enchantability nedded on the enchantment level passed.
     */
    public int getMaxEnchantability(int p_77317_1_)
    {
        return this.getMinEnchantability(p_77317_1_) + 30;
    }

    /**
     * Returns the maximum level that the enchantment can have.
     */
    public int getMaxLevel()
    {
        return 3;
    }
}