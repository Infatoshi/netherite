package net.minecraft.client.renderer.entity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.Block;
import net.minecraft.client.model.ModelBase;
import net.minecraft.client.model.ModelMinecart;
import net.minecraft.client.renderer.RenderBlocks;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityMinecart;
import net.minecraft.util.MathHelper;
import net.minecraft.util.ResourceLocation;
import net.minecraft.util.Vec3;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class RenderMinecart extends Render
{
    private static final ResourceLocation minecartTextures = new ResourceLocation("textures/entity/minecart.png");
    /** instance of ModelMinecart for rendering */
    protected ModelBase modelMinecart = new ModelMinecart();
    protected final RenderBlocks field_94145_f;
    private static final String __OBFID = "CL_00001013";

    public RenderMinecart()
    {
        this.shadowSize = 0.5F;
        this.field_94145_f = new RenderBlocks();
    }

    /**
     * Actually renders the given argument. This is a synthetic bridge method, always casting down its argument and then
     * handing it off to a worker function which does the actual work. In all probabilty, the class Render is generic
     * (Render<T extends Entity) and this method has signature public void func_76986_a(T entity, double d, double d1,
     * double d2, float f, float f1). But JAD is pre 1.5 so doesn't do that.
     */
    public void doRender(EntityMinecart p_76986_1_, double p_76986_2_, double p_76986_4_, double p_76986_6_, float p_76986_8_, float p_76986_9_)
    {
        GL11.glPushMatrix();
        this.bindEntityTexture(p_76986_1_);
        long i = (long)p_76986_1_.getEntityId() * 493286711L;
        i = i * i * 4392167121L + i * 98761L;
        float f2 = (((float)(i >> 16 & 7L) + 0.5F) / 8.0F - 0.5F) * 0.004F;
        float f3 = (((float)(i >> 20 & 7L) + 0.5F) / 8.0F - 0.5F) * 0.004F;
        float f4 = (((float)(i >> 24 & 7L) + 0.5F) / 8.0F - 0.5F) * 0.004F;
        GL11.glTranslatef(f2, f3, f4);
        double d3 = p_76986_1_.lastTickPosX + (p_76986_1_.posX - p_76986_1_.lastTickPosX) * (double)p_76986_9_;
        double d4 = p_76986_1_.lastTickPosY + (p_76986_1_.posY - p_76986_1_.lastTickPosY) * (double)p_76986_9_;
        double d5 = p_76986_1_.lastTickPosZ + (p_76986_1_.posZ - p_76986_1_.lastTickPosZ) * (double)p_76986_9_;
        double d6 = 0.30000001192092896D;
        Vec3 vec3 = p_76986_1_.func_70489_a(d3, d4, d5);
        float f5 = p_76986_1_.prevRotationPitch + (p_76986_1_.rotationPitch - p_76986_1_.prevRotationPitch) * p_76986_9_;

        if (vec3 != null)
        {
            Vec3 vec31 = p_76986_1_.func_70495_a(d3, d4, d5, d6);
            Vec3 vec32 = p_76986_1_.func_70495_a(d3, d4, d5, -d6);

            if (vec31 == null)
            {
                vec31 = vec3;
            }

            if (vec32 == null)
            {
                vec32 = vec3;
            }

            p_76986_2_ += vec3.xCoord - d3;
            p_76986_4_ += (vec31.yCoord + vec32.yCoord) / 2.0D - d4;
            p_76986_6_ += vec3.zCoord - d5;
            Vec3 vec33 = vec32.addVector(-vec31.xCoord, -vec31.yCoord, -vec31.zCoord);

            if (vec33.lengthVector() != 0.0D)
            {
                vec33 = vec33.normalize();
                p_76986_8_ = (float)(Math.atan2(vec33.zCoord, vec33.xCoord) * 180.0D / Math.PI);
                f5 = (float)(Math.atan(vec33.yCoord) * 73.0D);
            }
        }

        GL11.glTranslatef((float)p_76986_2_, (float)p_76986_4_, (float)p_76986_6_);
        GL11.glRotatef(180.0F - p_76986_8_, 0.0F, 1.0F, 0.0F);
        GL11.glRotatef(-f5, 0.0F, 0.0F, 1.0F);
        float f7 = (float)p_76986_1_.getRollingAmplitude() - p_76986_9_;
        float f8 = p_76986_1_.getDamage() - p_76986_9_;

        if (f8 < 0.0F)
        {
            f8 = 0.0F;
        }

        if (f7 > 0.0F)
        {
            GL11.glRotatef(MathHelper.sin(f7) * f7 * f8 / 10.0F * (float)p_76986_1_.getRollingDirection(), 1.0F, 0.0F, 0.0F);
        }

        int k = p_76986_1_.getDisplayTileOffset();
        Block block = p_76986_1_.func_145820_n();
        int j = p_76986_1_.getDisplayTileData();

        if (block.getRenderType() != -1)
        {
            GL11.glPushMatrix();
            this.bindTexture(TextureMap.locationBlocksTexture);
            float f6 = 0.75F;
            GL11.glScalef(f6, f6, f6);
            GL11.glTranslatef(0.0F, (float)k / 16.0F, 0.0F);
            this.func_147910_a(p_76986_1_, p_76986_9_, block, j);
            GL11.glPopMatrix();
            GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
            this.bindEntityTexture(p_76986_1_);
        }

        GL11.glScalef(-1.0F, -1.0F, 1.0F);
        this.modelMinecart.render(p_76986_1_, 0.0F, 0.0F, -0.1F, 0.0F, 0.0F, 0.0625F);
        GL11.glPopMatrix();
    }

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected ResourceLocation getEntityTexture(EntityMinecart p_110775_1_)
    {
        return minecartTextures;
    }

    protected void func_147910_a(EntityMinecart p_147910_1_, float p_147910_2_, Block p_147910_3_, int p_147910_4_)
    {
        float f1 = p_147910_1_.getBrightness(p_147910_2_);
        GL11.glPushMatrix();
        this.field_94145_f.renderBlockAsItem(p_147910_3_, p_147910_4_, f1);
        GL11.glPopMatrix();
    }

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected ResourceLocation getEntityTexture(Entity p_110775_1_)
    {
        return this.getEntityTexture((EntityMinecart)p_110775_1_);
    }

    /**
     * Actually renders the given argument. This is a synthetic bridge method, always casting down its argument and then
     * handing it off to a worker function which does the actual work. In all probabilty, the class Render is generic
     * (Render<T extends Entity) and this method has signature public void func_76986_a(T entity, double d, double d1,
     * double d2, float f, float f1). But JAD is pre 1.5 so doesn't do that.
     */
    public void doRender(Entity p_76986_1_, double p_76986_2_, double p_76986_4_, double p_76986_6_, float p_76986_8_, float p_76986_9_)
    {
        this.doRender((EntityMinecart)p_76986_1_, p_76986_2_, p_76986_4_, p_76986_6_, p_76986_8_, p_76986_9_);
    }
}