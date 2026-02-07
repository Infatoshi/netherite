package net.minecraft.client.resources.data;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public class FontMetadataSection implements IMetadataSection
{
    private final float[] charWidths;
    private final float[] charLefts;
    private final float[] charSpacings;
    private static final String __OBFID = "CL_00001108";

    public FontMetadataSection(float[] p_i1310_1_, float[] p_i1310_2_, float[] p_i1310_3_)
    {
        this.charWidths = p_i1310_1_;
        this.charLefts = p_i1310_2_;
        this.charSpacings = p_i1310_3_;
    }
}