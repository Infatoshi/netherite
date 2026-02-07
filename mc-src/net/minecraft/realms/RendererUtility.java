package net.minecraft.realms;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public class RendererUtility
{
    private static final String __OBFID = "CL_00001899";

    public static void render(RealmsButton p_render_0_, int p_render_1_, int p_render_2_)
    {
        p_render_0_.render(p_render_1_, p_render_2_);
    }
}