package net.minecraft.client.model;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public class ModelPig extends ModelQuadruped
{
    private static final String __OBFID = "CL_00000849";

    public ModelPig()
    {
        this(0.0F);
    }

    public ModelPig(float p_i1151_1_)
    {
        super(6, p_i1151_1_);
        this.head.setTextureOffset(16, 16).addBox(-2.0F, 0.0F, -9.0F, 4, 3, 1, p_i1151_1_);
        this.field_78145_g = 4.0F;
    }
}