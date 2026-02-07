package net.minecraft.client.particle;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.renderer.entity.RenderManager;
import net.minecraft.entity.Entity;
import net.minecraft.world.World;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class EntityPickupFX extends EntityFX
{
    private Entity entityToPickUp;
    private Entity entityPickingUp;
    private int age;
    private int maxAge;
    /** renamed from yOffset to fix shadowing Entity.yOffset */
    private float yOffs;
    private static final String __OBFID = "CL_00000930";

    public EntityPickupFX(World p_i1233_1_, Entity p_i1233_2_, Entity p_i1233_3_, float p_i1233_4_)
    {
        super(p_i1233_1_, p_i1233_2_.posX, p_i1233_2_.posY, p_i1233_2_.posZ, p_i1233_2_.motionX, p_i1233_2_.motionY, p_i1233_2_.motionZ);
        this.entityToPickUp = p_i1233_2_;
        this.entityPickingUp = p_i1233_3_;
        this.maxAge = 3;
        this.yOffs = p_i1233_4_;
    }

    public void renderParticle(Tessellator p_70539_1_, float p_70539_2_, float p_70539_3_, float p_70539_4_, float p_70539_5_, float p_70539_6_, float p_70539_7_)
    {
        float f6 = ((float)this.age + p_70539_2_) / (float)this.maxAge;
        f6 *= f6;
        double d0 = this.entityToPickUp.posX;
        double d1 = this.entityToPickUp.posY;
        double d2 = this.entityToPickUp.posZ;
        double d3 = this.entityPickingUp.lastTickPosX + (this.entityPickingUp.posX - this.entityPickingUp.lastTickPosX) * (double)p_70539_2_;
        double d4 = this.entityPickingUp.lastTickPosY + (this.entityPickingUp.posY - this.entityPickingUp.lastTickPosY) * (double)p_70539_2_ + (double)this.yOffs;
        double d5 = this.entityPickingUp.lastTickPosZ + (this.entityPickingUp.posZ - this.entityPickingUp.lastTickPosZ) * (double)p_70539_2_;
        double d6 = d0 + (d3 - d0) * (double)f6;
        double d7 = d1 + (d4 - d1) * (double)f6;
        double d8 = d2 + (d5 - d2) * (double)f6;
        int i = this.getBrightnessForRender(p_70539_2_);
        int j = i % 65536;
        int k = i / 65536;
        OpenGlHelper.setLightmapTextureCoords(OpenGlHelper.lightmapTexUnit, (float)j / 1.0F, (float)k / 1.0F);
        GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        d6 -= interpPosX;
        d7 -= interpPosY;
        d8 -= interpPosZ;
        RenderManager.instance.renderEntityWithPosYaw(this.entityToPickUp, (double)((float)d6), (double)((float)d7), (double)((float)d8), this.entityToPickUp.rotationYaw, p_70539_2_);
    }

    /**
     * Called to update the entity's position/logic.
     */
    public void onUpdate()
    {
        ++this.age;

        if (this.age == this.maxAge)
        {
            this.setDead();
        }
    }

    public int getFXLayer()
    {
        return 3;
    }
}