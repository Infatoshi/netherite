package net.minecraft.client.particle;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.entity.Entity;
import net.minecraft.world.World;

@SideOnly(Side.CLIENT)
public class EntityCrit2FX extends EntityFX
{
    /** Entity that had been hit and done the Critical hit on. */
    private Entity theEntity;
    private int currentLife;
    private int maximumLife;
    private String particleName;
    private static final String __OBFID = "CL_00000899";

    public EntityCrit2FX(World p_i1199_1_, Entity p_i1199_2_)
    {
        this(p_i1199_1_, p_i1199_2_, "crit");
    }

    public EntityCrit2FX(World p_i1200_1_, Entity p_i1200_2_, String p_i1200_3_)
    {
        super(p_i1200_1_, p_i1200_2_.posX, p_i1200_2_.boundingBox.minY + (double)(p_i1200_2_.height / 2.0F), p_i1200_2_.posZ, p_i1200_2_.motionX, p_i1200_2_.motionY, p_i1200_2_.motionZ);
        this.theEntity = p_i1200_2_;
        this.maximumLife = 3;
        this.particleName = p_i1200_3_;
        this.onUpdate();
    }

    public void renderParticle(Tessellator p_70539_1_, float p_70539_2_, float p_70539_3_, float p_70539_4_, float p_70539_5_, float p_70539_6_, float p_70539_7_) {}

    /**
     * Called to update the entity's position/logic.
     */
    public void onUpdate()
    {
        for (int i = 0; i < 16; ++i)
        {
            double d0 = (double)(this.rand.nextFloat() * 2.0F - 1.0F);
            double d1 = (double)(this.rand.nextFloat() * 2.0F - 1.0F);
            double d2 = (double)(this.rand.nextFloat() * 2.0F - 1.0F);

            if (d0 * d0 + d1 * d1 + d2 * d2 <= 1.0D)
            {
                double d3 = this.theEntity.posX + d0 * (double)this.theEntity.width / 4.0D;
                double d4 = this.theEntity.boundingBox.minY + (double)(this.theEntity.height / 2.0F) + d1 * (double)this.theEntity.height / 4.0D;
                double d5 = this.theEntity.posZ + d2 * (double)this.theEntity.width / 4.0D;
                this.worldObj.spawnParticle(this.particleName, d3, d4, d5, d0, d1 + 0.2D, d2);
            }
        }

        ++this.currentLife;

        if (this.currentLife >= this.maximumLife)
        {
            this.setDead();
        }
    }

    public int getFXLayer()
    {
        return 3;
    }
}