package net.minecraft.client.renderer.culling;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public class ClippingHelper
{
    public float[][] frustum = new float[16][16];
    public float[] projectionMatrix = new float[16];
    public float[] modelviewMatrix = new float[16];
    public float[] clippingMatrix = new float[16];
    private static final String __OBFID = "CL_00000977";

    /**
     * Returns true if the box is inside all 6 clipping planes, otherwise returns false.
     */
    public boolean isBoxInFrustum(double p_78553_1_, double p_78553_3_, double p_78553_5_, double p_78553_7_, double p_78553_9_, double p_78553_11_)
    {
        for (int i = 0; i < 6; ++i)
        {
            if ((double)this.frustum[i][0] * p_78553_1_ + (double)this.frustum[i][1] * p_78553_3_ + (double)this.frustum[i][2] * p_78553_5_ + (double)this.frustum[i][3] <= 0.0D && (double)this.frustum[i][0] * p_78553_7_ + (double)this.frustum[i][1] * p_78553_3_ + (double)this.frustum[i][2] * p_78553_5_ + (double)this.frustum[i][3] <= 0.0D && (double)this.frustum[i][0] * p_78553_1_ + (double)this.frustum[i][1] * p_78553_9_ + (double)this.frustum[i][2] * p_78553_5_ + (double)this.frustum[i][3] <= 0.0D && (double)this.frustum[i][0] * p_78553_7_ + (double)this.frustum[i][1] * p_78553_9_ + (double)this.frustum[i][2] * p_78553_5_ + (double)this.frustum[i][3] <= 0.0D && (double)this.frustum[i][0] * p_78553_1_ + (double)this.frustum[i][1] * p_78553_3_ + (double)this.frustum[i][2] * p_78553_11_ + (double)this.frustum[i][3] <= 0.0D && (double)this.frustum[i][0] * p_78553_7_ + (double)this.frustum[i][1] * p_78553_3_ + (double)this.frustum[i][2] * p_78553_11_ + (double)this.frustum[i][3] <= 0.0D && (double)this.frustum[i][0] * p_78553_1_ + (double)this.frustum[i][1] * p_78553_9_ + (double)this.frustum[i][2] * p_78553_11_ + (double)this.frustum[i][3] <= 0.0D && (double)this.frustum[i][0] * p_78553_7_ + (double)this.frustum[i][1] * p_78553_9_ + (double)this.frustum[i][2] * p_78553_11_ + (double)this.frustum[i][3] <= 0.0D)
            {
                return false;
            }
        }

        return true;
    }
}