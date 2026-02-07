package net.minecraft.client.model;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.monster.EntityIronGolem;

@SideOnly(Side.CLIENT)
public class ModelIronGolem extends ModelBase
{
    /** The head model for the iron golem. */
    public ModelRenderer ironGolemHead;
    /** The body model for the iron golem. */
    public ModelRenderer ironGolemBody;
    /** The right arm model for the iron golem. */
    public ModelRenderer ironGolemRightArm;
    /** The left arm model for the iron golem. */
    public ModelRenderer ironGolemLeftArm;
    /** The left leg model for the Iron Golem. */
    public ModelRenderer ironGolemLeftLeg;
    /** The right leg model for the Iron Golem. */
    public ModelRenderer ironGolemRightLeg;
    private static final String __OBFID = "CL_00000863";

    public ModelIronGolem()
    {
        this(0.0F);
    }

    public ModelIronGolem(float p_i1161_1_)
    {
        this(p_i1161_1_, -7.0F);
    }

    public ModelIronGolem(float p_i1162_1_, float p_i1162_2_)
    {
        short short1 = 128;
        short short2 = 128;
        this.ironGolemHead = (new ModelRenderer(this)).setTextureSize(short1, short2);
        this.ironGolemHead.setRotationPoint(0.0F, 0.0F + p_i1162_2_, -2.0F);
        this.ironGolemHead.setTextureOffset(0, 0).addBox(-4.0F, -12.0F, -5.5F, 8, 10, 8, p_i1162_1_);
        this.ironGolemHead.setTextureOffset(24, 0).addBox(-1.0F, -5.0F, -7.5F, 2, 4, 2, p_i1162_1_);
        this.ironGolemBody = (new ModelRenderer(this)).setTextureSize(short1, short2);
        this.ironGolemBody.setRotationPoint(0.0F, 0.0F + p_i1162_2_, 0.0F);
        this.ironGolemBody.setTextureOffset(0, 40).addBox(-9.0F, -2.0F, -6.0F, 18, 12, 11, p_i1162_1_);
        this.ironGolemBody.setTextureOffset(0, 70).addBox(-4.5F, 10.0F, -3.0F, 9, 5, 6, p_i1162_1_ + 0.5F);
        this.ironGolemRightArm = (new ModelRenderer(this)).setTextureSize(short1, short2);
        this.ironGolemRightArm.setRotationPoint(0.0F, -7.0F, 0.0F);
        this.ironGolemRightArm.setTextureOffset(60, 21).addBox(-13.0F, -2.5F, -3.0F, 4, 30, 6, p_i1162_1_);
        this.ironGolemLeftArm = (new ModelRenderer(this)).setTextureSize(short1, short2);
        this.ironGolemLeftArm.setRotationPoint(0.0F, -7.0F, 0.0F);
        this.ironGolemLeftArm.setTextureOffset(60, 58).addBox(9.0F, -2.5F, -3.0F, 4, 30, 6, p_i1162_1_);
        this.ironGolemLeftLeg = (new ModelRenderer(this, 0, 22)).setTextureSize(short1, short2);
        this.ironGolemLeftLeg.setRotationPoint(-4.0F, 18.0F + p_i1162_2_, 0.0F);
        this.ironGolemLeftLeg.setTextureOffset(37, 0).addBox(-3.5F, -3.0F, -3.0F, 6, 16, 5, p_i1162_1_);
        this.ironGolemRightLeg = (new ModelRenderer(this, 0, 22)).setTextureSize(short1, short2);
        this.ironGolemRightLeg.mirror = true;
        this.ironGolemRightLeg.setTextureOffset(60, 0).setRotationPoint(5.0F, 18.0F + p_i1162_2_, 0.0F);
        this.ironGolemRightLeg.addBox(-3.5F, -3.0F, -3.0F, 6, 16, 5, p_i1162_1_);
    }

    /**
     * Sets the models various rotation angles then renders the model.
     */
    public void render(Entity p_78088_1_, float p_78088_2_, float p_78088_3_, float p_78088_4_, float p_78088_5_, float p_78088_6_, float p_78088_7_)
    {
        this.setRotationAngles(p_78088_2_, p_78088_3_, p_78088_4_, p_78088_5_, p_78088_6_, p_78088_7_, p_78088_1_);
        this.ironGolemHead.render(p_78088_7_);
        this.ironGolemBody.render(p_78088_7_);
        this.ironGolemLeftLeg.render(p_78088_7_);
        this.ironGolemRightLeg.render(p_78088_7_);
        this.ironGolemRightArm.render(p_78088_7_);
        this.ironGolemLeftArm.render(p_78088_7_);
    }

    /**
     * Sets the model's various rotation angles. For bipeds, par1 and par2 are used for animating the movement of arms
     * and legs, where par1 represents the time(so that arms and legs swing back and forth) and par2 represents how
     * "far" arms and legs can swing at most.
     */
    public void setRotationAngles(float p_78087_1_, float p_78087_2_, float p_78087_3_, float p_78087_4_, float p_78087_5_, float p_78087_6_, Entity p_78087_7_)
    {
        this.ironGolemHead.rotateAngleY = p_78087_4_ / (180F / (float)Math.PI);
        this.ironGolemHead.rotateAngleX = p_78087_5_ / (180F / (float)Math.PI);
        this.ironGolemLeftLeg.rotateAngleX = -1.5F * this.func_78172_a(p_78087_1_, 13.0F) * p_78087_2_;
        this.ironGolemRightLeg.rotateAngleX = 1.5F * this.func_78172_a(p_78087_1_, 13.0F) * p_78087_2_;
        this.ironGolemLeftLeg.rotateAngleY = 0.0F;
        this.ironGolemRightLeg.rotateAngleY = 0.0F;
    }

    /**
     * Used for easily adding entity-dependent animations. The second and third float params here are the same second
     * and third as in the setRotationAngles method.
     */
    public void setLivingAnimations(EntityLivingBase p_78086_1_, float p_78086_2_, float p_78086_3_, float p_78086_4_)
    {
        EntityIronGolem entityirongolem = (EntityIronGolem)p_78086_1_;
        int i = entityirongolem.getAttackTimer();

        if (i > 0)
        {
            this.ironGolemRightArm.rotateAngleX = -2.0F + 1.5F * this.func_78172_a((float)i - p_78086_4_, 10.0F);
            this.ironGolemLeftArm.rotateAngleX = -2.0F + 1.5F * this.func_78172_a((float)i - p_78086_4_, 10.0F);
        }
        else
        {
            int j = entityirongolem.getHoldRoseTick();

            if (j > 0)
            {
                this.ironGolemRightArm.rotateAngleX = -0.8F + 0.025F * this.func_78172_a((float)j, 70.0F);
                this.ironGolemLeftArm.rotateAngleX = 0.0F;
            }
            else
            {
                this.ironGolemRightArm.rotateAngleX = (-0.2F + 1.5F * this.func_78172_a(p_78086_2_, 13.0F)) * p_78086_3_;
                this.ironGolemLeftArm.rotateAngleX = (-0.2F - 1.5F * this.func_78172_a(p_78086_2_, 13.0F)) * p_78086_3_;
            }
        }
    }

    private float func_78172_a(float p_78172_1_, float p_78172_2_)
    {
        return (Math.abs(p_78172_1_ % p_78172_2_ - p_78172_2_ * 0.5F) - p_78172_2_ * 0.25F) / (p_78172_2_ * 0.25F);
    }
}