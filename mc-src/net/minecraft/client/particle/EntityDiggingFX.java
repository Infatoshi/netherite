package net.minecraft.client.particle;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.Block;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;

@SideOnly(Side.CLIENT)
public class EntityDiggingFX extends EntityFX
{
    private Block field_145784_a;
    private static final String __OBFID = "CL_00000932";
    private int side;

    public EntityDiggingFX(World p_i1234_1_, double p_i1234_2_, double p_i1234_4_, double p_i1234_6_, double p_i1234_8_, double p_i1234_10_, double p_i1234_12_, Block p_i1234_14_, int p_i1234_15_)
    {
        this(p_i1234_1_, p_i1234_2_, p_i1234_4_, p_i1234_6_, p_i1234_8_, p_i1234_10_, p_i1234_12_, p_i1234_14_, p_i1234_15_, p_i1234_1_.rand.nextInt(6));
    }

    public EntityDiggingFX(World p_i1234_1_, double p_i1234_2_, double p_i1234_4_, double p_i1234_6_, double p_i1234_8_, double p_i1234_10_, double p_i1234_12_, Block p_i1234_14_, int p_i1234_15_, int side)
    {
        super(p_i1234_1_, p_i1234_2_, p_i1234_4_, p_i1234_6_, p_i1234_8_, p_i1234_10_, p_i1234_12_);
        this.field_145784_a = p_i1234_14_;
        this.setParticleIcon(p_i1234_14_.getIcon(side, p_i1234_15_));
        this.particleGravity = p_i1234_14_.blockParticleGravity;
        this.particleRed = this.particleGreen = this.particleBlue = 0.6F;
        this.particleScale /= 2.0F;
        this.side = side;
    }

    /**
     * If the block has a colour multiplier, copies it to this particle and returns this particle.
     */
    public EntityDiggingFX applyColourMultiplier(int p_70596_1_, int p_70596_2_, int p_70596_3_)
    {
        if (this.field_145784_a == Blocks.grass && this.side != 1)
        {
            return this;
        }
        else
        {
            int l = this.field_145784_a.colorMultiplier(this.worldObj, p_70596_1_, p_70596_2_, p_70596_3_);
            this.particleRed *= (float)(l >> 16 & 255) / 255.0F;
            this.particleGreen *= (float)(l >> 8 & 255) / 255.0F;
            this.particleBlue *= (float)(l & 255) / 255.0F;
            return this;
        }
    }

    /**
     * Creates a new EntityDiggingFX with the block render color applied to the base particle color
     */
    public EntityDiggingFX applyRenderColor(int p_90019_1_)
    {
        if (this.field_145784_a == Blocks.grass)
        {
            return this;
        }
        else
        {
            int j = this.field_145784_a.getRenderColor(p_90019_1_);
            this.particleRed *= (float)(j >> 16 & 255) / 255.0F;
            this.particleGreen *= (float)(j >> 8 & 255) / 255.0F;
            this.particleBlue *= (float)(j & 255) / 255.0F;
            return this;
        }
    }

    public int getFXLayer()
    {
        return 1;
    }

    public void renderParticle(Tessellator p_70539_1_, float p_70539_2_, float p_70539_3_, float p_70539_4_, float p_70539_5_, float p_70539_6_, float p_70539_7_)
    {
        float f6 = ((float)this.particleTextureIndexX + this.particleTextureJitterX / 4.0F) / 16.0F;
        float f7 = f6 + 0.015609375F;
        float f8 = ((float)this.particleTextureIndexY + this.particleTextureJitterY / 4.0F) / 16.0F;
        float f9 = f8 + 0.015609375F;
        float f10 = 0.1F * this.particleScale;

        if (this.particleIcon != null)
        {
            f6 = this.particleIcon.getInterpolatedU((double)(this.particleTextureJitterX / 4.0F * 16.0F));
            f7 = this.particleIcon.getInterpolatedU((double)((this.particleTextureJitterX + 1.0F) / 4.0F * 16.0F));
            f8 = this.particleIcon.getInterpolatedV((double)(this.particleTextureJitterY / 4.0F * 16.0F));
            f9 = this.particleIcon.getInterpolatedV((double)((this.particleTextureJitterY + 1.0F) / 4.0F * 16.0F));
        }

        float f11 = (float)(this.prevPosX + (this.posX - this.prevPosX) * (double)p_70539_2_ - interpPosX);
        float f12 = (float)(this.prevPosY + (this.posY - this.prevPosY) * (double)p_70539_2_ - interpPosY);
        float f13 = (float)(this.prevPosZ + (this.posZ - this.prevPosZ) * (double)p_70539_2_ - interpPosZ);
        p_70539_1_.setColorOpaque_F(this.particleRed, this.particleGreen, this.particleBlue);
        p_70539_1_.addVertexWithUV((double)(f11 - p_70539_3_ * f10 - p_70539_6_ * f10), (double)(f12 - p_70539_4_ * f10), (double)(f13 - p_70539_5_ * f10 - p_70539_7_ * f10), (double)f6, (double)f9);
        p_70539_1_.addVertexWithUV((double)(f11 - p_70539_3_ * f10 + p_70539_6_ * f10), (double)(f12 + p_70539_4_ * f10), (double)(f13 - p_70539_5_ * f10 + p_70539_7_ * f10), (double)f6, (double)f8);
        p_70539_1_.addVertexWithUV((double)(f11 + p_70539_3_ * f10 + p_70539_6_ * f10), (double)(f12 + p_70539_4_ * f10), (double)(f13 + p_70539_5_ * f10 + p_70539_7_ * f10), (double)f7, (double)f8);
        p_70539_1_.addVertexWithUV((double)(f11 + p_70539_3_ * f10 - p_70539_6_ * f10), (double)(f12 - p_70539_4_ * f10), (double)(f13 + p_70539_5_ * f10 - p_70539_7_ * f10), (double)f7, (double)f9);
    }
}