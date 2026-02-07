package net.minecraft.client.model;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.boss.EntityWither;
import net.minecraft.util.MathHelper;

@SideOnly(Side.CLIENT)
public class ModelWither extends ModelBase
{
    private ModelRenderer[] field_82905_a;
    private ModelRenderer[] field_82904_b;
    private static final String __OBFID = "CL_00000867";

    public ModelWither()
    {
        this.textureWidth = 64;
        this.textureHeight = 64;
        this.field_82905_a = new ModelRenderer[3];
        this.field_82905_a[0] = new ModelRenderer(this, 0, 16);
        this.field_82905_a[0].addBox(-10.0F, 3.9F, -0.5F, 20, 3, 3);
        this.field_82905_a[1] = (new ModelRenderer(this)).setTextureSize(this.textureWidth, this.textureHeight);
        this.field_82905_a[1].setRotationPoint(-2.0F, 6.9F, -0.5F);
        this.field_82905_a[1].setTextureOffset(0, 22).addBox(0.0F, 0.0F, 0.0F, 3, 10, 3);
        this.field_82905_a[1].setTextureOffset(24, 22).addBox(-4.0F, 1.5F, 0.5F, 11, 2, 2);
        this.field_82905_a[1].setTextureOffset(24, 22).addBox(-4.0F, 4.0F, 0.5F, 11, 2, 2);
        this.field_82905_a[1].setTextureOffset(24, 22).addBox(-4.0F, 6.5F, 0.5F, 11, 2, 2);
        this.field_82905_a[2] = new ModelRenderer(this, 12, 22);
        this.field_82905_a[2].addBox(0.0F, 0.0F, 0.0F, 3, 6, 3);
        this.field_82904_b = new ModelRenderer[3];
        this.field_82904_b[0] = new ModelRenderer(this, 0, 0);
        this.field_82904_b[0].addBox(-4.0F, -4.0F, -4.0F, 8, 8, 8);
        this.field_82904_b[1] = new ModelRenderer(this, 32, 0);
        this.field_82904_b[1].addBox(-4.0F, -4.0F, -4.0F, 6, 6, 6);
        this.field_82904_b[1].rotationPointX = -8.0F;
        this.field_82904_b[1].rotationPointY = 4.0F;
        this.field_82904_b[2] = new ModelRenderer(this, 32, 0);
        this.field_82904_b[2].addBox(-4.0F, -4.0F, -4.0F, 6, 6, 6);
        this.field_82904_b[2].rotationPointX = 10.0F;
        this.field_82904_b[2].rotationPointY = 4.0F;
    }

    public int func_82903_a()
    {
        return 32;
    }

    /**
     * Sets the models various rotation angles then renders the model.
     */
    public void render(Entity p_78088_1_, float p_78088_2_, float p_78088_3_, float p_78088_4_, float p_78088_5_, float p_78088_6_, float p_78088_7_)
    {
        this.setRotationAngles(p_78088_2_, p_78088_3_, p_78088_4_, p_78088_5_, p_78088_6_, p_78088_7_, p_78088_1_);
        ModelRenderer[] amodelrenderer = this.field_82904_b;
        int i = amodelrenderer.length;
        int j;
        ModelRenderer modelrenderer;

        for (j = 0; j < i; ++j)
        {
            modelrenderer = amodelrenderer[j];
            modelrenderer.render(p_78088_7_);
        }

        amodelrenderer = this.field_82905_a;
        i = amodelrenderer.length;

        for (j = 0; j < i; ++j)
        {
            modelrenderer = amodelrenderer[j];
            modelrenderer.render(p_78088_7_);
        }
    }

    /**
     * Sets the model's various rotation angles. For bipeds, par1 and par2 are used for animating the movement of arms
     * and legs, where par1 represents the time(so that arms and legs swing back and forth) and par2 represents how
     * "far" arms and legs can swing at most.
     */
    public void setRotationAngles(float p_78087_1_, float p_78087_2_, float p_78087_3_, float p_78087_4_, float p_78087_5_, float p_78087_6_, Entity p_78087_7_)
    {
        float f6 = MathHelper.cos(p_78087_3_ * 0.1F);
        this.field_82905_a[1].rotateAngleX = (0.065F + 0.05F * f6) * (float)Math.PI;
        this.field_82905_a[2].setRotationPoint(-2.0F, 6.9F + MathHelper.cos(this.field_82905_a[1].rotateAngleX) * 10.0F, -0.5F + MathHelper.sin(this.field_82905_a[1].rotateAngleX) * 10.0F);
        this.field_82905_a[2].rotateAngleX = (0.265F + 0.1F * f6) * (float)Math.PI;
        this.field_82904_b[0].rotateAngleY = p_78087_4_ / (180F / (float)Math.PI);
        this.field_82904_b[0].rotateAngleX = p_78087_5_ / (180F / (float)Math.PI);
    }

    /**
     * Used for easily adding entity-dependent animations. The second and third float params here are the same second
     * and third as in the setRotationAngles method.
     */
    public void setLivingAnimations(EntityLivingBase p_78086_1_, float p_78086_2_, float p_78086_3_, float p_78086_4_)
    {
        EntityWither entitywither = (EntityWither)p_78086_1_;

        for (int i = 1; i < 3; ++i)
        {
            this.field_82904_b[i].rotateAngleY = (entitywither.func_82207_a(i - 1) - p_78086_1_.renderYawOffset) / (180F / (float)Math.PI);
            this.field_82904_b[i].rotateAngleX = entitywither.func_82210_r(i - 1) / (180F / (float)Math.PI);
        }
    }
}