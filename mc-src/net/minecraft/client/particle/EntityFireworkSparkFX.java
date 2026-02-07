package net.minecraft.client.particle;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.world.World;

@SideOnly(Side.CLIENT)
public class EntityFireworkSparkFX extends EntityFX
{
    private int baseTextureIndex = 160;
    private boolean field_92054_ax;
    private boolean field_92048_ay;
    private final EffectRenderer field_92047_az;
    private float fadeColourRed;
    private float fadeColourGreen;
    private float fadeColourBlue;
    private boolean hasFadeColour;
    private static final String __OBFID = "CL_00000905";

    public EntityFireworkSparkFX(World p_i1207_1_, double p_i1207_2_, double p_i1207_4_, double p_i1207_6_, double p_i1207_8_, double p_i1207_10_, double p_i1207_12_, EffectRenderer p_i1207_14_)
    {
        super(p_i1207_1_, p_i1207_2_, p_i1207_4_, p_i1207_6_);
        this.motionX = p_i1207_8_;
        this.motionY = p_i1207_10_;
        this.motionZ = p_i1207_12_;
        this.field_92047_az = p_i1207_14_;
        this.particleScale *= 0.75F;
        this.particleMaxAge = 48 + this.rand.nextInt(12);
        this.noClip = false;
    }

    public void setTrail(boolean p_92045_1_)
    {
        this.field_92054_ax = p_92045_1_;
    }

    public void setTwinkle(boolean p_92043_1_)
    {
        this.field_92048_ay = p_92043_1_;
    }

    public void setColour(int p_92044_1_)
    {
        float f = (float)((p_92044_1_ & 16711680) >> 16) / 255.0F;
        float f1 = (float)((p_92044_1_ & 65280) >> 8) / 255.0F;
        float f2 = (float)((p_92044_1_ & 255) >> 0) / 255.0F;
        float f3 = 1.0F;
        this.setRBGColorF(f * f3, f1 * f3, f2 * f3);
    }

    public void setFadeColour(int p_92046_1_)
    {
        this.fadeColourRed = (float)((p_92046_1_ & 16711680) >> 16) / 255.0F;
        this.fadeColourGreen = (float)((p_92046_1_ & 65280) >> 8) / 255.0F;
        this.fadeColourBlue = (float)((p_92046_1_ & 255) >> 0) / 255.0F;
        this.hasFadeColour = true;
    }

    /**
     * returns the bounding box for this entity
     */
    public AxisAlignedBB getBoundingBox()
    {
        return null;
    }

    /**
     * Returns true if this entity should push and be pushed by other entities when colliding.
     */
    public boolean canBePushed()
    {
        return false;
    }

    public void renderParticle(Tessellator p_70539_1_, float p_70539_2_, float p_70539_3_, float p_70539_4_, float p_70539_5_, float p_70539_6_, float p_70539_7_)
    {
        if (!this.field_92048_ay || this.particleAge < this.particleMaxAge / 3 || (this.particleAge + this.particleMaxAge) / 3 % 2 == 0)
        {
            super.renderParticle(p_70539_1_, p_70539_2_, p_70539_3_, p_70539_4_, p_70539_5_, p_70539_6_, p_70539_7_);
        }
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

        if (this.particleAge > this.particleMaxAge / 2)
        {
            this.setAlphaF(1.0F - ((float)this.particleAge - (float)(this.particleMaxAge / 2)) / (float)this.particleMaxAge);

            if (this.hasFadeColour)
            {
                this.particleRed += (this.fadeColourRed - this.particleRed) * 0.2F;
                this.particleGreen += (this.fadeColourGreen - this.particleGreen) * 0.2F;
                this.particleBlue += (this.fadeColourBlue - this.particleBlue) * 0.2F;
            }
        }

        this.setParticleTextureIndex(this.baseTextureIndex + (7 - this.particleAge * 8 / this.particleMaxAge));
        this.motionY -= 0.004D;
        this.moveEntity(this.motionX, this.motionY, this.motionZ);
        this.motionX *= 0.9100000262260437D;
        this.motionY *= 0.9100000262260437D;
        this.motionZ *= 0.9100000262260437D;

        if (this.onGround)
        {
            this.motionX *= 0.699999988079071D;
            this.motionZ *= 0.699999988079071D;
        }

        if (this.field_92054_ax && this.particleAge < this.particleMaxAge / 2 && (this.particleAge + this.particleMaxAge) % 2 == 0)
        {
            EntityFireworkSparkFX entityfireworksparkfx = new EntityFireworkSparkFX(this.worldObj, this.posX, this.posY, this.posZ, 0.0D, 0.0D, 0.0D, this.field_92047_az);
            entityfireworksparkfx.setRBGColorF(this.particleRed, this.particleGreen, this.particleBlue);
            entityfireworksparkfx.particleAge = entityfireworksparkfx.particleMaxAge / 2;

            if (this.hasFadeColour)
            {
                entityfireworksparkfx.hasFadeColour = true;
                entityfireworksparkfx.fadeColourRed = this.fadeColourRed;
                entityfireworksparkfx.fadeColourGreen = this.fadeColourGreen;
                entityfireworksparkfx.fadeColourBlue = this.fadeColourBlue;
            }

            entityfireworksparkfx.field_92048_ay = this.field_92048_ay;
            this.field_92047_az.addEffect(entityfireworksparkfx);
        }
    }

    public int getBrightnessForRender(float p_70070_1_)
    {
        return 15728880;
    }

    /**
     * Gets how bright this entity is.
     */
    public float getBrightness(float p_70013_1_)
    {
        return 1.0F;
    }
}