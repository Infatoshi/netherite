package net.minecraft.enchantment;

public class EnchantmentKnockback extends Enchantment
{
    private static final String __OBFID = "CL_00000118";

    protected EnchantmentKnockback(int p_i1933_1_, int p_i1933_2_)
    {
        super(p_i1933_1_, p_i1933_2_, EnumEnchantmentType.weapon);
        this.setName("knockback");
    }

    /**
     * Returns the minimal value of enchantability needed on the enchantment level passed.
     */
    public int getMinEnchantability(int p_77321_1_)
    {
        return 5 + 20 * (p_77321_1_ - 1);
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
        return 2;
    }
}