package net.minecraft.client.renderer.entity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.model.ModelBase;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityHanging;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public abstract class RenderLiving extends RendererLivingEntity
{
    private static final String __OBFID = "CL_00001015";

    public RenderLiving(ModelBase p_i1262_1_, float p_i1262_2_)
    {
        super(p_i1262_1_, p_i1262_2_);
    }

    protected boolean func_110813_b(EntityLiving p_110813_1_)
    {
        return super.func_110813_b(p_110813_1_) && (p_110813_1_.getAlwaysRenderNameTagForRender() || p_110813_1_.hasCustomNameTag() && p_110813_1_ == this.renderManager.field_147941_i);
    }

    /**
     * Actually renders the given argument. This is a synthetic bridge method, always casting down its argument and then
     * handing it off to a worker function which does the actual work. In all probabilty, the class Render is generic
     * (Render<T extends Entity) and this method has signature public void func_76986_a(T entity, double d, double d1,
     * double d2, float f, float f1). But JAD is pre 1.5 so doesn't do that.
     */
    public void doRender(EntityLiving p_76986_1_, double p_76986_2_, double p_76986_4_, double p_76986_6_, float p_76986_8_, float p_76986_9_)
    {
        super.doRender((EntityLivingBase)p_76986_1_, p_76986_2_, p_76986_4_, p_76986_6_, p_76986_8_, p_76986_9_);
        this.func_110827_b(p_76986_1_, p_76986_2_, p_76986_4_, p_76986_6_, p_76986_8_, p_76986_9_);
    }

    private double func_110828_a(double p_110828_1_, double p_110828_3_, double p_110828_5_)
    {
        return p_110828_1_ + (p_110828_3_ - p_110828_1_) * p_110828_5_;
    }

    protected void func_110827_b(EntityLiving p_110827_1_, double p_110827_2_, double p_110827_4_, double p_110827_6_, float p_110827_8_, float p_110827_9_)
    {
        Entity entity = p_110827_1_.getLeashedToEntity();

        if (entity != null)
        {
            p_110827_4_ -= (1.6D - (double)p_110827_1_.height) * 0.5D;
            Tessellator tessellator = Tessellator.instance;
            double d3 = this.func_110828_a((double)entity.prevRotationYaw, (double)entity.rotationYaw, (double)(p_110827_9_ * 0.5F)) * 0.01745329238474369D;
            double d4 = this.func_110828_a((double)entity.prevRotationPitch, (double)entity.rotationPitch, (double)(p_110827_9_ * 0.5F)) * 0.01745329238474369D;
            double d5 = Math.cos(d3);
            double d6 = Math.sin(d3);
            double d7 = Math.sin(d4);

            if (entity instanceof EntityHanging)
            {
                d5 = 0.0D;
                d6 = 0.0D;
                d7 = -1.0D;
            }

            double d8 = Math.cos(d4);
            double d9 = this.func_110828_a(entity.prevPosX, entity.posX, (double)p_110827_9_) - d5 * 0.7D - d6 * 0.5D * d8;
            double d10 = this.func_110828_a(entity.prevPosY + (double)entity.getEyeHeight() * 0.7D, entity.posY + (double)entity.getEyeHeight() * 0.7D, (double)p_110827_9_) - d7 * 0.5D - 0.25D;
            double d11 = this.func_110828_a(entity.prevPosZ, entity.posZ, (double)p_110827_9_) - d6 * 0.7D + d5 * 0.5D * d8;
            double d12 = this.func_110828_a((double)p_110827_1_.prevRenderYawOffset, (double)p_110827_1_.renderYawOffset, (double)p_110827_9_) * 0.01745329238474369D + (Math.PI / 2D);
            d5 = Math.cos(d12) * (double)p_110827_1_.width * 0.4D;
            d6 = Math.sin(d12) * (double)p_110827_1_.width * 0.4D;
            double d13 = this.func_110828_a(p_110827_1_.prevPosX, p_110827_1_.posX, (double)p_110827_9_) + d5;
            double d14 = this.func_110828_a(p_110827_1_.prevPosY, p_110827_1_.posY, (double)p_110827_9_);
            double d15 = this.func_110828_a(p_110827_1_.prevPosZ, p_110827_1_.posZ, (double)p_110827_9_) + d6;
            p_110827_2_ += d5;
            p_110827_6_ += d6;
            double d16 = (double)((float)(d9 - d13));
            double d17 = (double)((float)(d10 - d14));
            double d18 = (double)((float)(d11 - d15));
            GL11.glDisable(GL11.GL_TEXTURE_2D);
            GL11.glDisable(GL11.GL_LIGHTING);
            GL11.glDisable(GL11.GL_CULL_FACE);
            boolean flag = true;
            double d19 = 0.025D;
            tessellator.startDrawing(5);
            int i;
            float f2;

            for (i = 0; i <= 24; ++i)
            {
                if (i % 2 == 0)
                {
                    tessellator.setColorRGBA_F(0.5F, 0.4F, 0.3F, 1.0F);
                }
                else
                {
                    tessellator.setColorRGBA_F(0.35F, 0.28F, 0.21000001F, 1.0F);
                }

                f2 = (float)i / 24.0F;
                tessellator.addVertex(p_110827_2_ + d16 * (double)f2 + 0.0D, p_110827_4_ + d17 * (double)(f2 * f2 + f2) * 0.5D + (double)((24.0F - (float)i) / 18.0F + 0.125F), p_110827_6_ + d18 * (double)f2);
                tessellator.addVertex(p_110827_2_ + d16 * (double)f2 + 0.025D, p_110827_4_ + d17 * (double)(f2 * f2 + f2) * 0.5D + (double)((24.0F - (float)i) / 18.0F + 0.125F) + 0.025D, p_110827_6_ + d18 * (double)f2);
            }

            tessellator.draw();
            tessellator.startDrawing(5);

            for (i = 0; i <= 24; ++i)
            {
                if (i % 2 == 0)
                {
                    tessellator.setColorRGBA_F(0.5F, 0.4F, 0.3F, 1.0F);
                }
                else
                {
                    tessellator.setColorRGBA_F(0.35F, 0.28F, 0.21000001F, 1.0F);
                }

                f2 = (float)i / 24.0F;
                tessellator.addVertex(p_110827_2_ + d16 * (double)f2 + 0.0D, p_110827_4_ + d17 * (double)(f2 * f2 + f2) * 0.5D + (double)((24.0F - (float)i) / 18.0F + 0.125F) + 0.025D, p_110827_6_ + d18 * (double)f2);
                tessellator.addVertex(p_110827_2_ + d16 * (double)f2 + 0.025D, p_110827_4_ + d17 * (double)(f2 * f2 + f2) * 0.5D + (double)((24.0F - (float)i) / 18.0F + 0.125F), p_110827_6_ + d18 * (double)f2 + 0.025D);
            }

            tessellator.draw();
            GL11.glEnable(GL11.GL_LIGHTING);
            GL11.glEnable(GL11.GL_TEXTURE_2D);
            GL11.glEnable(GL11.GL_CULL_FACE);
        }
    }

    protected boolean func_110813_b(EntityLivingBase p_110813_1_)
    {
        return this.func_110813_b((EntityLiving)p_110813_1_);
    }

    /**
     * Actually renders the given argument. This is a synthetic bridge method, always casting down its argument and then
     * handing it off to a worker function which does the actual work. In all probabilty, the class Render is generic
     * (Render<T extends Entity) and this method has signature public void func_76986_a(T entity, double d, double d1,
     * double d2, float f, float f1). But JAD is pre 1.5 so doesn't do that.
     */
    public void doRender(EntityLivingBase p_76986_1_, double p_76986_2_, double p_76986_4_, double p_76986_6_, float p_76986_8_, float p_76986_9_)
    {
        this.doRender((EntityLiving)p_76986_1_, p_76986_2_, p_76986_4_, p_76986_6_, p_76986_8_, p_76986_9_);
    }

    /**
     * Actually renders the given argument. This is a synthetic bridge method, always casting down its argument and then
     * handing it off to a worker function which does the actual work. In all probabilty, the class Render is generic
     * (Render<T extends Entity) and this method has signature public void func_76986_a(T entity, double d, double d1,
     * double d2, float f, float f1). But JAD is pre 1.5 so doesn't do that.
     */
    public void doRender(Entity p_76986_1_, double p_76986_2_, double p_76986_4_, double p_76986_6_, float p_76986_8_, float p_76986_9_)
    {
        this.doRender((EntityLiving)p_76986_1_, p_76986_2_, p_76986_4_, p_76986_6_, p_76986_8_, p_76986_9_);
    }
}