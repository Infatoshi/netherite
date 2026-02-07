package net.minecraft.client.resources;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.List;

@SideOnly(Side.CLIENT)
public interface IReloadableResourceManager extends IResourceManager
{
    void reloadResources(List p_110541_1_);

    void registerReloadListener(IResourceManagerReloadListener p_110542_1_);
}