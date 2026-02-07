package net.minecraft.client.renderer.texture;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.client.resources.IResourceManager;

@SideOnly(Side.CLIENT)
public interface ITextureObject
{
    void loadTexture(IResourceManager p_110551_1_) throws IOException;

    int getGlTextureId();
}