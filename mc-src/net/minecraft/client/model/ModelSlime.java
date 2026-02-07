package net.minecraft.client.model;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.Entity;

@SideOnly(Side.CLIENT)
public class ModelSlime extends ModelBase
{
    /** The slime's bodies, both the inside box and the outside box */
    ModelRenderer slimeBodies;
    /** The slime's right eye */
    ModelRenderer slimeRightEye;
    /** The slime's left eye */
    ModelRenderer slimeLeftEye;
    /** The slime's mouth */
    ModelRenderer slimeMouth;
    private static final String __OBFID = "CL_00000858";

    public ModelSlime(int p_i1157_1_)
    {
        this.slimeBodies = new ModelRenderer(this, 0, p_i1157_1_);
        this.slimeBodies.addBox(-4.0F, 16.0F, -4.0F, 8, 8, 8);

        if (p_i1157_1_ > 0)
        {
            this.slimeBodies = new ModelRenderer(this, 0, p_i1157_1_);
            this.slimeBodies.addBox(-3.0F, 17.0F, -3.0F, 6, 6, 6);
            this.slimeRightEye = new ModelRenderer(this, 32, 0);
            this.slimeRightEye.addBox(-3.25F, 18.0F, -3.5F, 2, 2, 2);
            this.slimeLeftEye = new ModelRenderer(this, 32, 4);
            this.slimeLeftEye.addBox(1.25F, 18.0F, -3.5F, 2, 2, 2);
            this.slimeMouth = new ModelRenderer(this, 32, 8);
            this.slimeMouth.addBox(0.0F, 21.0F, -3.5F, 1, 1, 1);
        }
    }

    /**
     * Sets the models various rotation angles then renders the model.
     */
    public void render(Entity p_78088_1_, float p_78088_2_, float p_78088_3_, float p_78088_4_, float p_78088_5_, float p_78088_6_, float p_78088_7_)
    {
        this.setRotationAngles(p_78088_2_, p_78088_3_, p_78088_4_, p_78088_5_, p_78088_6_, p_78088_7_, p_78088_1_);
        this.slimeBodies.render(p_78088_7_);

        if (this.slimeRightEye != null)
        {
            this.slimeRightEye.render(p_78088_7_);
            this.slimeLeftEye.render(p_78088_7_);
            this.slimeMouth.render(p_78088_7_);
        }
    }
}