package net.minecraft.client.particle;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.world.World;

@SideOnly(Side.CLIENT)
public class EntityExplodeFX extends EntityFX
{
    private static final String __OBFID = "CL_00000903";

    public EntityExplodeFX(World p_i1205_1_, double p_i1205_2_, double p_i1205_4_, double p_i1205_6_, double p_i1205_8_, double p_i1205_10_, double p_i1205_12_)
    {
        super(p_i1205_1_, p_i1205_2_, p_i1205_4_, p_i1205_6_, p_i1205_8_, p_i1205_10_, p_i1205_12_);
        this.motionX = p_i1205_8_ + (double)((float)(Math.random() * 2.0D - 1.0D) * 0.05F);
        this.motionY = p_i1205_10_ + (double)((float)(Math.random() * 2.0D - 1.0D) * 0.05F);
        this.motionZ = p_i1205_12_ + (double)((float)(Math.random() * 2.0D - 1.0D) * 0.05F);
        this.particleRed = this.particleGreen = this.particleBlue = this.rand.nextFloat() * 0.3F + 0.7F;
        this.particleScale = this.rand.nextFloat() * this.rand.nextFloat() * 6.0F + 1.0F;
        this.particleMaxAge = (int)(16.0D / ((double)this.rand.nextFloat() * 0.8D + 0.2D)) + 2;
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
        this.motionY += 0.004D;
        this.moveEntity(this.motionX, this.motionY, this.motionZ);
        this.motionX *= 0.8999999761581421D;
        this.motionY *= 0.8999999761581421D;
        this.motionZ *= 0.8999999761581421D;

        if (this.onGround)
        {
            this.motionX *= 0.699999988079071D;
            this.motionZ *= 0.699999988079071D;
        }
    }
}