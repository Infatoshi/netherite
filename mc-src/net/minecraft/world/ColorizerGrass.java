package net.minecraft.world;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public class ColorizerGrass
{
    /** Color buffer for grass */
    private static int[] grassBuffer = new int[65536];
    private static final String __OBFID = "CL_00000138";

    public static void setGrassBiomeColorizer(int[] p_77479_0_)
    {
        grassBuffer = p_77479_0_;
    }

    /**
     * Gets grass color from temperature and humidity. Args: temperature, humidity
     */
    public static int getGrassColor(double p_77480_0_, double p_77480_2_)
    {
        p_77480_2_ *= p_77480_0_;
        int i = (int)((1.0D - p_77480_0_) * 255.0D);
        int j = (int)((1.0D - p_77480_2_) * 255.0D);
        return grassBuffer[j << 8 | i];
    }
}