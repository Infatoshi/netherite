package net.minecraft.client.model;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.Entity;

@SideOnly(Side.CLIENT)
public class ModelMinecart extends ModelBase
{
    public ModelRenderer[] sideModels = new ModelRenderer[7];
    private static final String __OBFID = "CL_00000844";

    public ModelMinecart()
    {
        this.sideModels[0] = new ModelRenderer(this, 0, 10);
        this.sideModels[1] = new ModelRenderer(this, 0, 0);
        this.sideModels[2] = new ModelRenderer(this, 0, 0);
        this.sideModels[3] = new ModelRenderer(this, 0, 0);
        this.sideModels[4] = new ModelRenderer(this, 0, 0);
        this.sideModels[5] = new ModelRenderer(this, 44, 10);
        byte b0 = 20;
        byte b1 = 8;
        byte b2 = 16;
        byte b3 = 4;
        this.sideModels[0].addBox((float)(-b0 / 2), (float)(-b2 / 2), -1.0F, b0, b2, 2, 0.0F);
        this.sideModels[0].setRotationPoint(0.0F, (float)b3, 0.0F);
        this.sideModels[5].addBox((float)(-b0 / 2 + 1), (float)(-b2 / 2 + 1), -1.0F, b0 - 2, b2 - 2, 1, 0.0F);
        this.sideModels[5].setRotationPoint(0.0F, (float)b3, 0.0F);
        this.sideModels[1].addBox((float)(-b0 / 2 + 2), (float)(-b1 - 1), -1.0F, b0 - 4, b1, 2, 0.0F);
        this.sideModels[1].setRotationPoint((float)(-b0 / 2 + 1), (float)b3, 0.0F);
        this.sideModels[2].addBox((float)(-b0 / 2 + 2), (float)(-b1 - 1), -1.0F, b0 - 4, b1, 2, 0.0F);
        this.sideModels[2].setRotationPoint((float)(b0 / 2 - 1), (float)b3, 0.0F);
        this.sideModels[3].addBox((float)(-b0 / 2 + 2), (float)(-b1 - 1), -1.0F, b0 - 4, b1, 2, 0.0F);
        this.sideModels[3].setRotationPoint(0.0F, (float)b3, (float)(-b2 / 2 + 1));
        this.sideModels[4].addBox((float)(-b0 / 2 + 2), (float)(-b1 - 1), -1.0F, b0 - 4, b1, 2, 0.0F);
        this.sideModels[4].setRotationPoint(0.0F, (float)b3, (float)(b2 / 2 - 1));
        this.sideModels[0].rotateAngleX = ((float)Math.PI / 2F);
        this.sideModels[1].rotateAngleY = ((float)Math.PI * 3F / 2F);
        this.sideModels[2].rotateAngleY = ((float)Math.PI / 2F);
        this.sideModels[3].rotateAngleY = (float)Math.PI;
        this.sideModels[5].rotateAngleX = -((float)Math.PI / 2F);
    }

    /**
     * Sets the models various rotation angles then renders the model.
     */
    public void render(Entity p_78088_1_, float p_78088_2_, float p_78088_3_, float p_78088_4_, float p_78088_5_, float p_78088_6_, float p_78088_7_)
    {
        this.sideModels[5].rotationPointY = 4.0F - p_78088_4_;

        for (int i = 0; i < 6; ++i)
        {
            this.sideModels[i].render(p_78088_7_);
        }
    }
}