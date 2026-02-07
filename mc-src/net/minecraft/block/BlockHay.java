package net.minecraft.block;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.material.Material;
import net.minecraft.client.renderer.texture.IIconRegister;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.util.IIcon;

public class BlockHay extends BlockRotatedPillar
{
    private static final String __OBFID = "CL_00000256";

    public BlockHay()
    {
        super(Material.grass);
        this.setCreativeTab(CreativeTabs.tabBlock);
    }

    @SideOnly(Side.CLIENT)
    protected IIcon getSideIcon(int p_150163_1_)
    {
        return this.blockIcon;
    }

    @SideOnly(Side.CLIENT)
    public void registerBlockIcons(IIconRegister p_149651_1_)
    {
        this.field_150164_N = p_149651_1_.registerIcon(this.getTextureName() + "_top");
        this.blockIcon = p_149651_1_.registerIcon(this.getTextureName() + "_side");
    }
}