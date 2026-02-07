package net.minecraft.item;

import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Items;
import net.minecraft.world.World;

public class ItemSoup extends ItemFood
{
    private static final String __OBFID = "CL_00001778";

    public ItemSoup(int p_i45330_1_)
    {
        super(p_i45330_1_, false);
        this.setMaxStackSize(1);
    }

    public ItemStack onEaten(ItemStack p_77654_1_, World p_77654_2_, EntityPlayer p_77654_3_)
    {
        super.onEaten(p_77654_1_, p_77654_2_, p_77654_3_);
        return new ItemStack(Items.bowl);
    }
}