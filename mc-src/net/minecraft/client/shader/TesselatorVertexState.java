package net.minecraft.client.shader;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public class TesselatorVertexState
{
    private int[] rawBuffer;
    private int rawBufferIndex;
    private int vertexCount;
    private boolean hasTexture;
    private boolean hasBrightness;
    private boolean hasNormals;
    private boolean hasColor;
    private static final String __OBFID = "CL_00000961";

    public TesselatorVertexState(int[] p_i45079_1_, int p_i45079_2_, int p_i45079_3_, boolean p_i45079_4_, boolean p_i45079_5_, boolean p_i45079_6_, boolean p_i45079_7_)
    {
        this.rawBuffer = p_i45079_1_;
        this.rawBufferIndex = p_i45079_2_;
        this.vertexCount = p_i45079_3_;
        this.hasTexture = p_i45079_4_;
        this.hasBrightness = p_i45079_5_;
        this.hasNormals = p_i45079_6_;
        this.hasColor = p_i45079_7_;
    }

    public int[] getRawBuffer()
    {
        return this.rawBuffer;
    }

    public int getRawBufferIndex()
    {
        return this.rawBufferIndex;
    }

    public int getVertexCount()
    {
        return this.vertexCount;
    }

    public boolean getHasTexture()
    {
        return this.hasTexture;
    }

    public boolean getHasBrightness()
    {
        return this.hasBrightness;
    }

    public boolean getHasNormals()
    {
        return this.hasNormals;
    }

    public boolean getHasColor()
    {
        return this.hasColor;
    }
}