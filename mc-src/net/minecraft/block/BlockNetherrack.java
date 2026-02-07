package net.minecraft.block;

import net.minecraft.block.material.MapColor;
import net.minecraft.block.material.Material;
import net.minecraft.creativetab.CreativeTabs;

public class BlockNetherrack extends Block
{
    private static final String __OBFID = "CL_00000275";

    public BlockNetherrack()
    {
        super(Material.rock);
        this.setCreativeTab(CreativeTabs.tabBlock);
    }

    public MapColor getMapColor(int p_149728_1_)
    {
        return MapColor.netherrackColor;
    }
}