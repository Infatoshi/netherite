package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.init.Blocks;
import net.minecraft.util.IIcon;

public class BlockButtonWood extends BlockButton
{
    private static final String __OBFID = "CL_00000336";

    protected BlockButtonWood()
    {
        super(true);
    }

    /**
     * Gets the block's texture. Args: side, meta
     */
    @SideOnly(Side.CLIENT)
    public IIcon getIcon(int p_149691_1_, int p_149691_2_)
    {
        return Blocks.planks.getBlockTextureFromSide(1);
    }
}