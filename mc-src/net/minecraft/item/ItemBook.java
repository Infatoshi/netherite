package net.minecraft.item;

public class ItemBook extends Item
{
    private static final String __OBFID = "CL_00001775";

    /**
     * Checks isDamagable and if it cannot be stacked
     */
    public boolean isItemTool(ItemStack p_77616_1_)
    {
        return p_77616_1_.stackSize == 1;
    }

    /**
     * Return the enchantability factor of the item, most of the time is based on material.
     */
    public int getItemEnchantability()
    {
        return 1;
    }
}