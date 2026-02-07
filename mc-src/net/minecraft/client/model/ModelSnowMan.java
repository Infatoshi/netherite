package net.minecraft.client.model;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.Entity;
import net.minecraft.util.MathHelper;

@SideOnly(Side.CLIENT)
public class ModelSnowMan extends ModelBase
{
    public ModelRenderer body;
    public ModelRenderer bottomBody;
    public ModelRenderer head;
    public ModelRenderer rightHand;
    public ModelRenderer leftHand;
    private static final String __OBFID = "CL_00000859";

    public ModelSnowMan()
    {
        float f = 4.0F;
        float f1 = 0.0F;
        this.head = (new ModelRenderer(this, 0, 0)).setTextureSize(64, 64);
        this.head.addBox(-4.0F, -8.0F, -4.0F, 8, 8, 8, f1 - 0.5F);
        this.head.setRotationPoint(0.0F, 0.0F + f, 0.0F);
        this.rightHand = (new ModelRenderer(this, 32, 0)).setTextureSize(64, 64);
        this.rightHand.addBox(-1.0F, 0.0F, -1.0F, 12, 2, 2, f1 - 0.5F);
        this.rightHand.setRotationPoint(0.0F, 0.0F + f + 9.0F - 7.0F, 0.0F);
        this.leftHand = (new ModelRenderer(this, 32, 0)).setTextureSize(64, 64);
        this.leftHand.addBox(-1.0F, 0.0F, -1.0F, 12, 2, 2, f1 - 0.5F);
        this.leftHand.setRotationPoint(0.0F, 0.0F + f + 9.0F - 7.0F, 0.0F);
        this.body = (new ModelRenderer(this, 0, 16)).setTextureSize(64, 64);
        this.body.addBox(-5.0F, -10.0F, -5.0F, 10, 10, 10, f1 - 0.5F);
        this.body.setRotationPoint(0.0F, 0.0F + f + 9.0F, 0.0F);
        this.bottomBody = (new ModelRenderer(this, 0, 36)).setTextureSize(64, 64);
        this.bottomBody.addBox(-6.0F, -12.0F, -6.0F, 12, 12, 12, f1 - 0.5F);
        this.bottomBody.setRotationPoint(0.0F, 0.0F + f + 20.0F, 0.0F);
    }

    /**
     * Sets the model's various rotation angles. For bipeds, par1 and par2 are used for animating the movement of arms
     * and legs, where par1 represents the time(so that arms and legs swing back and forth) and par2 represents how
     * "far" arms and legs can swing at most.
     */
    public void setRotationAngles(float p_78087_1_, float p_78087_2_, float p_78087_3_, float p_78087_4_, float p_78087_5_, float p_78087_6_, Entity p_78087_7_)
    {
        super.setRotationAngles(p_78087_1_, p_78087_2_, p_78087_3_, p_78087_4_, p_78087_5_, p_78087_6_, p_78087_7_);
        this.head.rotateAngleY = p_78087_4_ / (180F / (float)Math.PI);
        this.head.rotateAngleX = p_78087_5_ / (180F / (float)Math.PI);
        this.body.rotateAngleY = p_78087_4_ / (180F / (float)Math.PI) * 0.25F;
        float f6 = MathHelper.sin(this.body.rotateAngleY);
        float f7 = MathHelper.cos(this.body.rotateAngleY);
        this.rightHand.rotateAngleZ = 1.0F;
        this.leftHand.rotateAngleZ = -1.0F;
        this.rightHand.rotateAngleY = 0.0F + this.body.rotateAngleY;
        this.leftHand.rotateAngleY = (float)Math.PI + this.body.rotateAngleY;
        this.rightHand.rotationPointX = f7 * 5.0F;
        this.rightHand.rotationPointZ = -f6 * 5.0F;
        this.leftHand.rotationPointX = -f7 * 5.0F;
        this.leftHand.rotationPointZ = f6 * 5.0F;
    }

    /**
     * Sets the models various rotation angles then renders the model.
     */
    public void render(Entity p_78088_1_, float p_78088_2_, float p_78088_3_, float p_78088_4_, float p_78088_5_, float p_78088_6_, float p_78088_7_)
    {
        this.setRotationAngles(p_78088_2_, p_78088_3_, p_78088_4_, p_78088_5_, p_78088_6_, p_78088_7_, p_78088_1_);
        this.body.render(p_78088_7_);
        this.bottomBody.render(p_78088_7_);
        this.head.render(p_78088_7_);
        this.rightHand.render(p_78088_7_);
        this.leftHand.render(p_78088_7_);
    }
}