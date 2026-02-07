package net.minecraft.enchantment;

public class EnchantmentFishingSpeed extends Enchantment
{
    private static final String __OBFID = "CL_00000117";

    protected EnchantmentFishingSpeed(int p_i45361_1_, int p_i45361_2_, EnumEnchantmentType p_i45361_3_)
    {
        super(p_i45361_1_, p_i45361_2_, p_i45361_3_);
        this.setName("fishingSpeed");
    }

    /**
     * Returns the minimal value of enchantability needed on the enchantment level passed.
     */
    public int getMinEnchantability(int p_77321_1_)
    {
        return 15 + (p_77321_1_ - 1) * 9;
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
}