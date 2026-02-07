package net.minecraft.client.model;

import net.minecraft.client.renderer.Tessellator;
import net.minecraft.util.Vec3;

public class TexturedQuad
{
    public PositionTextureVertex[] vertexPositions;
    public int nVertices;
    private boolean invertNormal;
    private static final String __OBFID = "CL_00000850";

    public TexturedQuad(PositionTextureVertex[] p_i1152_1_)
    {
        this.vertexPositions = p_i1152_1_;
        this.nVertices = p_i1152_1_.length;
    }

    public TexturedQuad(PositionTextureVertex[] p_i1153_1_, int p_i1153_2_, int p_i1153_3_, int p_i1153_4_, int p_i1153_5_, float p_i1153_6_, float p_i1153_7_)
    {
        this(p_i1153_1_);
        float f2 = 0.0F / p_i1153_6_;
        float f3 = 0.0F / p_i1153_7_;
        p_i1153_1_[0] = p_i1153_1_[0].setTexturePosition((float)p_i1153_4_ / p_i1153_6_ - f2, (float)p_i1153_3_ / p_i1153_7_ + f3);
        p_i1153_1_[1] = p_i1153_1_[1].setTexturePosition((float)p_i1153_2_ / p_i1153_6_ + f2, (float)p_i1153_3_ / p_i1153_7_ + f3);
        p_i1153_1_[2] = p_i1153_1_[2].setTexturePosition((float)p_i1153_2_ / p_i1153_6_ + f2, (float)p_i1153_5_ / p_i1153_7_ - f3);
        p_i1153_1_[3] = p_i1153_1_[3].setTexturePosition((float)p_i1153_4_ / p_i1153_6_ - f2, (float)p_i1153_5_ / p_i1153_7_ - f3);
    }

    public void flipFace()
    {
        PositionTextureVertex[] apositiontexturevertex = new PositionTextureVertex[this.vertexPositions.length];

        for (int i = 0; i < this.vertexPositions.length; ++i)
        {
            apositiontexturevertex[i] = this.vertexPositions[this.vertexPositions.length - i - 1];
        }

        this.vertexPositions = apositiontexturevertex;
    }

    public void draw(Tessellator p_78236_1_, float p_78236_2_)
    {
        Vec3 vec3 = this.vertexPositions[1].vector3D.subtract(this.vertexPositions[0].vector3D);
        Vec3 vec31 = this.vertexPositions[1].vector3D.subtract(this.vertexPositions[2].vector3D);
        Vec3 vec32 = vec31.crossProduct(vec3).normalize();
        p_78236_1_.startDrawingQuads();

        if (this.invertNormal)
        {
            p_78236_1_.setNormal(-((float)vec32.xCoord), -((float)vec32.yCoord), -((float)vec32.zCoord));
        }
        else
        {
            p_78236_1_.setNormal((float)vec32.xCoord, (float)vec32.yCoord, (float)vec32.zCoord);
        }

        for (int i = 0; i < 4; ++i)
        {
            PositionTextureVertex positiontexturevertex = this.vertexPositions[i];
            p_78236_1_.addVertexWithUV((double)((float)positiontexturevertex.vector3D.xCoord * p_78236_2_), (double)((float)positiontexturevertex.vector3D.yCoord * p_78236_2_), (double)((float)positiontexturevertex.vector3D.zCoord * p_78236_2_), (double)positiontexturevertex.texturePositionX, (double)positiontexturevertex.texturePositionY);
        }

        p_78236_1_.draw();
    }
}