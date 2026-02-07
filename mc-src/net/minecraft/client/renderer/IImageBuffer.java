package net.minecraft.client.renderer;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.awt.image.BufferedImage;

@SideOnly(Side.CLIENT)
public interface IImageBuffer
{
    BufferedImage parseUserSkin(BufferedImage p_78432_1_);

    void func_152634_a();
}