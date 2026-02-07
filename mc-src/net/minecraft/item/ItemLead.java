package net.minecraft.item;

import java.util.Iterator;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.EntityLeashKnot;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.world.World;

public class ItemLead extends Item
{
    private static final String __OBFID = "CL_00000045";

    public ItemLead()
    {
        this.setCreativeTab(CreativeTabs.tabTools);
    }

    /**
     * Callback for item usage. If the item does something special on right clicking, he will have one of those. Return
     * True if something happen and false if it don't. This is for ITEMS, not BLOCKS
     */
    public boolean onItemUse(ItemStack p_77648_1_, EntityPlayer p_77648_2_, World p_77648_3_, int p_77648_4_, int p_77648_5_, int p_77648_6_, int p_77648_7_, float p_77648_8_, float p_77648_9_, float p_77648_10_)
    {
        Block block = p_77648_3_.getBlock(p_77648_4_, p_77648_5_, p_77648_6_);

        if (block.getRenderType() == 11)
        {
            if (p_77648_3_.isRemote)
            {
                return true;
            }
            else
            {
                func_150909_a(p_77648_2_, p_77648_3_, p_77648_4_, p_77648_5_, p_77648_6_);
                return true;
            }
        }
        else
        {
            return false;
        }
    }

    public static boolean func_150909_a(EntityPlayer p_150909_0_, World p_150909_1_, int p_150909_2_, int p_150909_3_, int p_150909_4_)
    {
        EntityLeashKnot entityleashknot = EntityLeashKnot.getKnotForBlock(p_150909_1_, p_150909_2_, p_150909_3_, p_150909_4_);
        boolean flag = false;
        double d0 = 7.0D;
        List list = p_150909_1_.getEntitiesWithinAABB(EntityLiving.class, AxisAlignedBB.getBoundingBox((double)p_150909_2_ - d0, (double)p_150909_3_ - d0, (double)p_150909_4_ - d0, (double)p_150909_2_ + d0, (double)p_150909_3_ + d0, (double)p_150909_4_ + d0));

        if (list != null)
        {
            Iterator iterator = list.iterator();

            while (iterator.hasNext())
            {
                EntityLiving entityliving = (EntityLiving)iterator.next();

                if (entityliving.getLeashed() && entityliving.getLeashedToEntity() == p_150909_0_)
                {
                    if (entityleashknot == null)
                    {
                        entityleashknot = EntityLeashKnot.func_110129_a(p_150909_1_, p_150909_2_, p_150909_3_, p_150909_4_);
                    }

                    entityliving.setLeashedToEntity(entityleashknot, true);
                    flag = true;
                }
            }
        }

        return flag;
    }
}