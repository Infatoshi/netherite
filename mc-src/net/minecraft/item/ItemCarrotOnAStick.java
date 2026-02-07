package net.minecraft.item;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.passive.EntityPig;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Items;
import net.minecraft.world.World;

public class ItemCarrotOnAStick extends Item
{
    private static final String __OBFID = "CL_00000001";

    public ItemCarrotOnAStick()
    {
        this.setCreativeTab(CreativeTabs.tabTransport);
        this.setMaxStackSize(1);
        this.setMaxDamage(25);
    }

    /**
     * Returns True is the item is renderer in full 3D when hold.
     */
    @SideOnly(Side.CLIENT)
    public boolean isFull3D()
    {
        return true;
    }

    /**
     * Returns true if this item should be rotated by 180 degrees around the Y axis when being held in an entities
     * hands.
     */
    @SideOnly(Side.CLIENT)
    public boolean shouldRotateAroundWhenRendering()
    {
        return true;
    }

    /**
     * Called whenever this item is equipped and the right mouse button is pressed. Args: itemStack, world, entityPlayer
     */
    public ItemStack onItemRightClick(ItemStack p_77659_1_, World p_77659_2_, EntityPlayer p_77659_3_)
    {
        if (p_77659_3_.isRiding() && p_77659_3_.ridingEntity instanceof EntityPig)
        {
            EntityPig entitypig = (EntityPig)p_77659_3_.ridingEntity;

            if (entitypig.getAIControlledByPlayer().isControlledByPlayer() && p_77659_1_.getMaxDamage() - p_77659_1_.getItemDamage() >= 7)
            {
                entitypig.getAIControlledByPlayer().boostSpeed();
                p_77659_1_.damageItem(7, p_77659_3_);

                if (p_77659_1_.stackSize == 0)
                {
                    ItemStack itemstack1 = new ItemStack(Items.fishing_rod);
                    itemstack1.setTagCompound(p_77659_1_.stackTagCompound);
                    return itemstack1;
                }
            }
        }

        return p_77659_1_;
    }
}