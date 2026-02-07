package net.minecraft.item;

import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.player.EntityPlayer;

public class ItemNameTag extends Item
{
    private static final String __OBFID = "CL_00000052";

    public ItemNameTag()
    {
        this.setCreativeTab(CreativeTabs.tabTools);
    }

    /**
     * Returns true if the item can be used on the given entity, e.g. shears on sheep.
     */
    public boolean itemInteractionForEntity(ItemStack p_111207_1_, EntityPlayer p_111207_2_, EntityLivingBase p_111207_3_)
    {
        if (!p_111207_1_.hasDisplayName())
        {
            return false;
        }
        else if (p_111207_3_ instanceof EntityLiving)
        {
            EntityLiving entityliving = (EntityLiving)p_111207_3_;
            entityliving.setCustomNameTag(p_111207_1_.getDisplayName());
            entityliving.func_110163_bv();
            --p_111207_1_.stackSize;
            return true;
        }
        else
        {
            return super.itemInteractionForEntity(p_111207_1_, p_111207_2_, p_111207_3_);
        }
    }
}