package net.minecraft.enchantment;

public class EnchantmentArrowFire extends Enchantment
{
    private static final String __OBFID = "CL_00000099";

    public EnchantmentArrowFire(int p_i1920_1_, int p_i1920_2_)
    {
        super(p_i1920_1_, p_i1920_2_, EnumEnchantmentType.bow);
        this.setName("arrowFire");
    }

    /**
     * Returns the minimal value of enchantability needed on the enchantment level passed.
     */
    public int getMinEnchantability(int p_77321_1_)
    {
        return 20;
    }

    /**
     * Returns the maximum value of enchantability nedded on the enchantment level passed.
     */
    public int getMaxEnchantability(int p_77317_1_)
    {
        return 50;
    }

    /**
     * Returns the maximum level that the enchantment can have.
     */
    public int getMaxLevel()
    {
        return 1;
    }
}