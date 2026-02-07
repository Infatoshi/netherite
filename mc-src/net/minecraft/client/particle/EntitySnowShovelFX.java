package net.minecraft.client.particle;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.world.World;

@SideOnly(Side.CLIENT)
public class EntitySnowShovelFX extends EntityFX
{
    float snowDigParticleScale;
    private static final String __OBFID = "CL_00000925";

    public EntitySnowShovelFX(World p_i1227_1_, double p_i1227_2_, double p_i1227_4_, double p_i1227_6_, double p_i1227_8_, double p_i1227_10_, double p_i1227_12_)
    {
        this(p_i1227_1_, p_i1227_2_, p_i1227_4_, p_i1227_6_, p_i1227_8_, p_i1227_10_, p_i1227_12_, 1.0F);
    }

    public EntitySnowShovelFX(World p_i1228_1_, double p_i1228_2_, double p_i1228_4_, double p_i1228_6_, double p_i1228_8_, double p_i1228_10_, double p_i1228_12_, float p_i1228_14_)
    {
        super(p_i1228_1_, p_i1228_2_, p_i1228_4_, p_i1228_6_, p_i1228_8_, p_i1228_10_, p_i1228_12_);
        this.motionX *= 0.10000000149011612D;
        this.motionY *= 0.10000000149011612D;
        this.motionZ *= 0.10000000149011612D;
        this.motionX += p_i1228_8_;
        this.motionY += p_i1228_10_;
        this.motionZ += p_i1228_12_;
        this.particleRed = this.particleGreen = this.particleBlue = 1.0F - (float)(Math.random() * 0.30000001192092896D);
        this.particleScale *= 0.75F;
        this.particleScale *= p_i1228_14_;
        this.snowDigParticleScale = this.particleScale;
        this.particleMaxAge = (int)(8.0D / (Math.random() * 0.8D + 0.2D));
        this.particleMaxAge = (int)((float)this.particleMaxAge * p_i1228_14_);
        this.noClip = false;
    }

    public void renderParticle(Tessellator p_70539_1_, float p_70539_2_, float p_70539_3_, float p_70539_4_, float p_70539_5_, float p_70539_6_, float p_70539_7_)
    {
        float f6 = ((float)this.particleAge + p_70539_2_) / (float)this.particleMaxAge * 32.0F;

        if (f6 < 0.0F)
        {
            f6 = 0.0F;
        }

        if (f6 > 1.0F)
        {
            f6 = 1.0F;
        }

        this.particleScale = this.snowDigParticleScale * f6;
        super.renderParticle(p_70539_1_, p_70539_2_, p_70539_3_, p_70539_4_, p_70539_5_, p_70539_6_, p_70539_7_);
    }

    /**
     * Called to update the entity's position/logic.
     */
    public void onUpdate()
    {
        this.prevPosX = this.posX;
        this.prevPosY = this.posY;
        this.prevPosZ = this.posZ;

        if (this.particleAge++ >= this.particleMaxAge)
        {
            this.setDead();
        }

        this.setParticleTextureIndex(7 - this.particleAge * 8 / this.particleMaxAge);
        this.motionY -= 0.03D;
        this.moveEntity(this.motionX, this.motionY, this.motionZ);
        this.motionX *= 0.9900000095367432D;
        this.motionY *= 0.9900000095367432D;
        this.motionZ *= 0.9900000095367432D;

        if (this.onGround)
        {
            this.motionX *= 0.699999988079071D;
            this.motionZ *= 0.699999988079071D;
        }
    }
}