package net.minecraft.inventory;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

public class AnimalChest extends InventoryBasic
{
    private static final String __OBFID = "CL_00001731";

    public AnimalChest(String p_i1796_1_, int p_i1796_2_)
    {
        super(p_i1796_1_, false, p_i1796_2_);
    }

    @SideOnly(Side.CLIENT)
    public AnimalChest(String p_i1797_1_, boolean p_i1797_2_, int p_i1797_3_)
    {
        super(p_i1797_1_, p_i1797_2_, p_i1797_3_);
    }
}