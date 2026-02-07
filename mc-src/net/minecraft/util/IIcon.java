package net.minecraft.util;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

public interface IIcon
{
    /**
     * Returns the width of the icon, in pixels.
     */
    @SideOnly(Side.CLIENT)
    int getIconWidth();

    /**
     * Returns the height of the icon, in pixels.
     */
    @SideOnly(Side.CLIENT)
    int getIconHeight();

    /**
     * Returns the minimum U coordinate to use when rendering with this icon.
     */
    @SideOnly(Side.CLIENT)
    float getMinU();

    /**
     * Returns the maximum U coordinate to use when rendering with this icon.
     */
    @SideOnly(Side.CLIENT)
    float getMaxU();

    /**
     * Gets a U coordinate on the icon. 0 returns uMin and 16 returns uMax. Other arguments return in-between values.
     */
    @SideOnly(Side.CLIENT)
    float getInterpolatedU(double p_94214_1_);

    /**
     * Returns the minimum V coordinate to use when rendering with this icon.
     */
    @SideOnly(Side.CLIENT)
    float getMinV();

    /**
     * Returns the maximum V coordinate to use when rendering with this icon.
     */
    @SideOnly(Side.CLIENT)
    float getMaxV();

    /**
     * Gets a V coordinate on the icon. 0 returns vMin and 16 returns vMax. Other arguments return in-between values.
     */
    @SideOnly(Side.CLIENT)
    float getInterpolatedV(double p_94207_1_);

    @SideOnly(Side.CLIENT)
    String getIconName();
}