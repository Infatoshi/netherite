package net.minecraft.block;

import net.minecraft.block.material.MapColor;
import net.minecraft.block.material.Material;
import net.minecraft.creativetab.CreativeTabs;

public class BlockHardenedClay extends Block
{
    private static final String __OBFID = "CL_00000255";

    public BlockHardenedClay()
    {
        super(Material.rock);
        this.setCreativeTab(CreativeTabs.tabBlock);
    }

    public MapColor getMapColor(int p_149728_1_)
    {
        return MapColor.adobeColor;
    }
}