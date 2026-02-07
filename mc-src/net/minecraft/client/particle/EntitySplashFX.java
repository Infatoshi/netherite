package net.minecraft.client.particle;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.world.World;

@SideOnly(Side.CLIENT)
public class EntitySplashFX extends EntityRainFX
{
    private static final String __OBFID = "CL_00000927";

    public EntitySplashFX(World p_i1230_1_, double p_i1230_2_, double p_i1230_4_, double p_i1230_6_, double p_i1230_8_, double p_i1230_10_, double p_i1230_12_)
    {
        super(p_i1230_1_, p_i1230_2_, p_i1230_4_, p_i1230_6_);
        this.particleGravity = 0.04F;
        this.nextTextureIndexX();

        if (p_i1230_10_ == 0.0D && (p_i1230_8_ != 0.0D || p_i1230_12_ != 0.0D))
        {
            this.motionX = p_i1230_8_;
            this.motionY = p_i1230_10_ + 0.1D;
            this.motionZ = p_i1230_12_;
        }
    }
}