package net.minecraft.client.model;

import net.minecraft.util.Vec3;

public class PositionTextureVertex
{
    public Vec3 vector3D;
    public float texturePositionX;
    public float texturePositionY;
    private static final String __OBFID = "CL_00000862";

    public PositionTextureVertex(float p_i1158_1_, float p_i1158_2_, float p_i1158_3_, float p_i1158_4_, float p_i1158_5_)
    {
        this(Vec3.createVectorHelper((double)p_i1158_1_, (double)p_i1158_2_, (double)p_i1158_3_), p_i1158_4_, p_i1158_5_);
    }

    public PositionTextureVertex setTexturePosition(float p_78240_1_, float p_78240_2_)
    {
        return new PositionTextureVertex(this, p_78240_1_, p_78240_2_);
    }

    public PositionTextureVertex(PositionTextureVertex p_i1159_1_, float p_i1159_2_, float p_i1159_3_)
    {
        this.vector3D = p_i1159_1_.vector3D;
        this.texturePositionX = p_i1159_2_;
        this.texturePositionY = p_i1159_3_;
    }

    public PositionTextureVertex(Vec3 p_i1160_1_, float p_i1160_2_, float p_i1160_3_)
    {
        this.vector3D = p_i1160_1_;
        this.texturePositionX = p_i1160_2_;
        this.texturePositionY = p_i1160_3_;
    }
}