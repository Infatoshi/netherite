package net.minecraft.client.resources.data;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.util.IChatComponent;

@SideOnly(Side.CLIENT)
public class PackMetadataSection implements IMetadataSection
{
    private final IChatComponent packDescription;
    private final int packFormat;
    private static final String __OBFID = "CL_00001112";

    public PackMetadataSection(IChatComponent p_i1034_1_, int p_i1034_2_)
    {
        this.packDescription = p_i1034_1_;
        this.packFormat = p_i1034_2_;
    }

    public IChatComponent func_152805_a()
    {
        return this.packDescription;
    }

    public int getPackFormat()
    {
        return this.packFormat;
    }
}