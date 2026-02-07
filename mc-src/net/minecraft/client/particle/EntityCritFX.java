package net.minecraft.client.particle;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.world.World;

@SideOnly(Side.CLIENT)
public class EntityCritFX extends EntityFX
{
    float initialParticleScale;
    private static final String __OBFID = "CL_00000900";

    public EntityCritFX(World p_i1201_1_, double p_i1201_2_, double p_i1201_4_, double p_i1201_6_, double p_i1201_8_, double p_i1201_10_, double p_i1201_12_)
    {
        this(p_i1201_1_, p_i1201_2_, p_i1201_4_, p_i1201_6_, p_i1201_8_, p_i1201_10_, p_i1201_12_, 1.0F);
    }

    public EntityCritFX(World p_i1202_1_, double p_i1202_2_, double p_i1202_4_, double p_i1202_6_, double p_i1202_8_, double p_i1202_10_, double p_i1202_12_, float p_i1202_14_)
    {
        super(p_i1202_1_, p_i1202_2_, p_i1202_4_, p_i1202_6_, 0.0D, 0.0D, 0.0D);
        this.motionX *= 0.10000000149011612D;
        this.motionY *= 0.10000000149011612D;
        this.motionZ *= 0.10000000149011612D;
        this.motionX += p_i1202_8_ * 0.4D;
        this.motionY += p_i1202_10_ * 0.4D;
        this.motionZ += p_i1202_12_ * 0.4D;
        this.particleRed = this.particleGreen = this.particleBlue = (float)(Math.random() * 0.30000001192092896D + 0.6000000238418579D);
        this.particleScale *= 0.75F;
        this.particleScale *= p_i1202_14_;
        this.initialParticleScale = this.particleScale;
        this.particleMaxAge = (int)(6.0D / (Math.random() * 0.8D + 0.6D));
        this.particleMaxAge = (int)((float)this.particleMaxAge * p_i1202_14_);
        this.noClip = false;
        this.setParticleTextureIndex(65);
        this.onUpdate();
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

        this.particleScale = this.initialParticleScale * f6;
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

        this.moveEntity(this.motionX, this.motionY, this.motionZ);
        this.particleGreen = (float)((double)this.particleGreen * 0.96D);
        this.particleBlue = (float)((double)this.particleBlue * 0.9D);
        this.motionX *= 0.699999988079071D;
        this.motionY *= 0.699999988079071D;
        this.motionZ *= 0.699999988079071D;
        this.motionY -= 0.019999999552965164D;

        if (this.onGround)
        {
            this.motionX *= 0.699999988079071D;
            this.motionZ *= 0.699999988079071D;
        }
    }
}