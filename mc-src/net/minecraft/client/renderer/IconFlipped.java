package net.minecraft.client.renderer;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.util.IIcon;

@SideOnly(Side.CLIENT)
public class IconFlipped implements IIcon
{
    private final IIcon baseIcon;
    private final boolean flipU;
    private final boolean flipV;
    private static final String __OBFID = "CL_00001511";

    public IconFlipped(IIcon p_i1560_1_, boolean p_i1560_2_, boolean p_i1560_3_)
    {
        this.baseIcon = p_i1560_1_;
        this.flipU = p_i1560_2_;
        this.flipV = p_i1560_3_;
    }

    /**
     * Returns the width of the icon, in pixels.
     */
    public int getIconWidth()
    {
        return this.baseIcon.getIconWidth();
    }

    /**
     * Returns the height of the icon, in pixels.
     */
    public int getIconHeight()
    {
        return this.baseIcon.getIconHeight();
    }

    /**
     * Returns the minimum U coordinate to use when rendering with this icon.
     */
    public float getMinU()
    {
        return this.flipU ? this.baseIcon.getMaxU() : this.baseIcon.getMinU();
    }

    /**
     * Returns the maximum U coordinate to use when rendering with this icon.
     */
    public float getMaxU()
    {
        return this.flipU ? this.baseIcon.getMinU() : this.baseIcon.getMaxU();
    }

    /**
     * Gets a U coordinate on the icon. 0 returns uMin and 16 returns uMax. Other arguments return in-between values.
     */
    public float getInterpolatedU(double p_94214_1_)
    {
        float f = this.getMaxU() - this.getMinU();
        return this.getMinU() + f * ((float)p_94214_1_ / 16.0F);
    }

    /**
     * Returns the minimum V coordinate to use when rendering with this icon.
     */
    public float getMinV()
    {
        return this.flipV ? this.baseIcon.getMinV() : this.baseIcon.getMinV();
    }

    /**
     * Returns the maximum V coordinate to use when rendering with this icon.
     */
    public float getMaxV()
    {
        return this.flipV ? this.baseIcon.getMinV() : this.baseIcon.getMaxV();
    }

    /**
     * Gets a V coordinate on the icon. 0 returns vMin and 16 returns vMax. Other arguments return in-between values.
     */
    public float getInterpolatedV(double p_94207_1_)
    {
        float f = this.getMaxV() - this.getMinV();
        return this.getMinV() + f * ((float)p_94207_1_ / 16.0F);
    }

    public String getIconName()
    {
        return this.baseIcon.getIconName();
    }
}