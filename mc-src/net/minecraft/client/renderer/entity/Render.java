package net.minecraft.client.renderer.entity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.renderer.RenderBlocks;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.renderer.texture.IIconRegister;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.init.Blocks;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.util.ResourceLocation;
import net.minecraft.world.World;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public abstract class Render
{
    private static final ResourceLocation shadowTextures = new ResourceLocation("textures/misc/shadow.png");
    protected RenderManager renderManager;
    protected RenderBlocks field_147909_c = new RenderBlocks();
    protected float shadowSize;
    /** Determines the darkness of the object's shadow. Higher value makes a darker shadow. */
    protected float shadowOpaque = 1.0F;
    private boolean staticEntity = false;
    private static final String __OBFID = "CL_00000992";

    /**
     * Actually renders the given argument. This is a synthetic bridge method, always casting down its argument and then
     * handing it off to a worker function which does the actual work. In all probabilty, the class Render is generic
     * (Render<T extends Entity) and this method has signature public void func_76986_a(T entity, double d, double d1,
     * double d2, float f, float f1). But JAD is pre 1.5 so doesn't do that.
     */
    public abstract void doRender(Entity p_76986_1_, double p_76986_2_, double p_76986_4_, double p_76986_6_, float p_76986_8_, float p_76986_9_);

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected abstract ResourceLocation getEntityTexture(Entity p_110775_1_);

    public boolean isStaticEntity()
    {
        return this.staticEntity;
    }

    protected void bindEntityTexture(Entity p_110777_1_)
    {
        this.bindTexture(this.getEntityTexture(p_110777_1_));
    }

    protected void bindTexture(ResourceLocation p_110776_1_)
    {
        this.renderManager.renderEngine.bindTexture(p_110776_1_);
    }

    /**
     * Renders fire on top of the entity. Args: entity, x, y, z, partialTickTime
     */
    private void renderEntityOnFire(Entity p_76977_1_, double p_76977_2_, double p_76977_4_, double p_76977_6_, float p_76977_8_)
    {
        GL11.glDisable(GL11.GL_LIGHTING);
        IIcon iicon = Blocks.fire.getFireIcon(0);
        IIcon iicon1 = Blocks.fire.getFireIcon(1);
        GL11.glPushMatrix();
        GL11.glTranslatef((float)p_76977_2_, (float)p_76977_4_, (float)p_76977_6_);
        float f1 = p_76977_1_.width * 1.4F;
        GL11.glScalef(f1, f1, f1);
        Tessellator tessellator = Tessellator.instance;
        float f2 = 0.5F;
        float f3 = 0.0F;
        float f4 = p_76977_1_.height / f1;
        float f5 = (float)(p_76977_1_.posY - p_76977_1_.boundingBox.minY);
        GL11.glRotatef(-this.renderManager.playerViewY, 0.0F, 1.0F, 0.0F);
        GL11.glTranslatef(0.0F, 0.0F, -0.3F + (float)((int)f4) * 0.02F);
        GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        float f6 = 0.0F;
        int i = 0;
        tessellator.startDrawingQuads();

        while (f4 > 0.0F)
        {
            IIcon iicon2 = i % 2 == 0 ? iicon : iicon1;
            this.bindTexture(TextureMap.locationBlocksTexture);
            float f7 = iicon2.getMinU();
            float f8 = iicon2.getMinV();
            float f9 = iicon2.getMaxU();
            float f10 = iicon2.getMaxV();

            if (i / 2 % 2 == 0)
            {
                float f11 = f9;
                f9 = f7;
                f7 = f11;
            }

            tessellator.addVertexWithUV((double)(f2 - f3), (double)(0.0F - f5), (double)f6, (double)f9, (double)f10);
            tessellator.addVertexWithUV((double)(-f2 - f3), (double)(0.0F - f5), (double)f6, (double)f7, (double)f10);
            tessellator.addVertexWithUV((double)(-f2 - f3), (double)(1.4F - f5), (double)f6, (double)f7, (double)f8);
            tessellator.addVertexWithUV((double)(f2 - f3), (double)(1.4F - f5), (double)f6, (double)f9, (double)f8);
            f4 -= 0.45F;
            f5 -= 0.45F;
            f2 *= 0.9F;
            f6 += 0.03F;
            ++i;
        }

        tessellator.draw();
        GL11.glPopMatrix();
        GL11.glEnable(GL11.GL_LIGHTING);
    }

    /**
     * Renders the entity shadows at the position, shadow alpha and partialTickTime. Args: entity, x, y, z, shadowAlpha,
     * partialTickTime
     */
    private void renderShadow(Entity p_76975_1_, double p_76975_2_, double p_76975_4_, double p_76975_6_, float p_76975_8_, float p_76975_9_)
    {
        GL11.glEnable(GL11.GL_BLEND);
        GL11.glBlendFunc(GL11.GL_SRC_ALPHA, GL11.GL_ONE_MINUS_SRC_ALPHA);
        this.renderManager.renderEngine.bindTexture(shadowTextures);
        World world = this.getWorldFromRenderManager();
        GL11.glDepthMask(false);
        float f2 = this.shadowSize;

        if (p_76975_1_ instanceof EntityLiving)
        {
            EntityLiving entityliving = (EntityLiving)p_76975_1_;
            f2 *= entityliving.getRenderSizeModifier();

            if (entityliving.isChild())
            {
                f2 *= 0.5F;
            }
        }

        double d8 = p_76975_1_.lastTickPosX + (p_76975_1_.posX - p_76975_1_.lastTickPosX) * (double)p_76975_9_;
        double d3 = p_76975_1_.lastTickPosY + (p_76975_1_.posY - p_76975_1_.lastTickPosY) * (double)p_76975_9_ + (double)p_76975_1_.getShadowSize();
        double d4 = p_76975_1_.lastTickPosZ + (p_76975_1_.posZ - p_76975_1_.lastTickPosZ) * (double)p_76975_9_;
        int i = MathHelper.floor_double(d8 - (double)f2);
        int j = MathHelper.floor_double(d8 + (double)f2);
        int k = MathHelper.floor_double(d3 - (double)f2);
        int l = MathHelper.floor_double(d3);
        int i1 = MathHelper.floor_double(d4 - (double)f2);
        int j1 = MathHelper.floor_double(d4 + (double)f2);
        double d5 = p_76975_2_ - d8;
        double d6 = p_76975_4_ - d3;
        double d7 = p_76975_6_ - d4;
        Tessellator tessellator = Tessellator.instance;
        tessellator.startDrawingQuads();

        for (int k1 = i; k1 <= j; ++k1)
        {
            for (int l1 = k; l1 <= l; ++l1)
            {
                for (int i2 = i1; i2 <= j1; ++i2)
                {
                    Block block = world.getBlock(k1, l1 - 1, i2);

                    if (block.getMaterial() != Material.air && world.getBlockLightValue(k1, l1, i2) > 3)
                    {
                        this.func_147907_a(block, p_76975_2_, p_76975_4_ + (double)p_76975_1_.getShadowSize(), p_76975_6_, k1, l1, i2, p_76975_8_, f2, d5, d6 + (double)p_76975_1_.getShadowSize(), d7);
                    }
                }
            }
        }

        tessellator.draw();
        GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        GL11.glDisable(GL11.GL_BLEND);
        GL11.glDepthMask(true);
    }

    /**
     * Returns the render manager's world object
     */
    private World getWorldFromRenderManager()
    {
        return this.renderManager.worldObj;
    }

    private void func_147907_a(Block p_147907_1_, double p_147907_2_, double p_147907_4_, double p_147907_6_, int p_147907_8_, int p_147907_9_, int p_147907_10_, float p_147907_11_, float p_147907_12_, double p_147907_13_, double p_147907_15_, double p_147907_17_)
    {
        Tessellator tessellator = Tessellator.instance;

        if (p_147907_1_.renderAsNormalBlock())
        {
            double d6 = ((double)p_147907_11_ - (p_147907_4_ - ((double)p_147907_9_ + p_147907_15_)) / 2.0D) * 0.5D * (double)this.getWorldFromRenderManager().getLightBrightness(p_147907_8_, p_147907_9_, p_147907_10_);

            if (d6 >= 0.0D)
            {
                if (d6 > 1.0D)
                {
                    d6 = 1.0D;
                }

                tessellator.setColorRGBA_F(1.0F, 1.0F, 1.0F, (float)d6);
                double d7 = (double)p_147907_8_ + p_147907_1_.getBlockBoundsMinX() + p_147907_13_;
                double d8 = (double)p_147907_8_ + p_147907_1_.getBlockBoundsMaxX() + p_147907_13_;
                double d9 = (double)p_147907_9_ + p_147907_1_.getBlockBoundsMinY() + p_147907_15_ + 0.015625D;
                double d10 = (double)p_147907_10_ + p_147907_1_.getBlockBoundsMinZ() + p_147907_17_;
                double d11 = (double)p_147907_10_ + p_147907_1_.getBlockBoundsMaxZ() + p_147907_17_;
                float f2 = (float)((p_147907_2_ - d7) / 2.0D / (double)p_147907_12_ + 0.5D);
                float f3 = (float)((p_147907_2_ - d8) / 2.0D / (double)p_147907_12_ + 0.5D);
                float f4 = (float)((p_147907_6_ - d10) / 2.0D / (double)p_147907_12_ + 0.5D);
                float f5 = (float)((p_147907_6_ - d11) / 2.0D / (double)p_147907_12_ + 0.5D);
                tessellator.addVertexWithUV(d7, d9, d10, (double)f2, (double)f4);
                tessellator.addVertexWithUV(d7, d9, d11, (double)f2, (double)f5);
                tessellator.addVertexWithUV(d8, d9, d11, (double)f3, (double)f5);
                tessellator.addVertexWithUV(d8, d9, d10, (double)f3, (double)f4);
            }
        }
    }

    /**
     * Renders a white box with the bounds of the AABB translated by the offset. Args: aabb, x, y, z
     */
    public static void renderOffsetAABB(AxisAlignedBB p_76978_0_, double p_76978_1_, double p_76978_3_, double p_76978_5_)
    {
        GL11.glDisable(GL11.GL_TEXTURE_2D);
        Tessellator tessellator = Tessellator.instance;
        GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        tessellator.startDrawingQuads();
        tessellator.setTranslation(p_76978_1_, p_76978_3_, p_76978_5_);
        tessellator.setNormal(0.0F, 0.0F, -1.0F);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.maxY, p_76978_0_.minZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.maxY, p_76978_0_.minZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.minY, p_76978_0_.minZ);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.minY, p_76978_0_.minZ);
        tessellator.setNormal(0.0F, 0.0F, 1.0F);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.minY, p_76978_0_.maxZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.minY, p_76978_0_.maxZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.maxY, p_76978_0_.maxZ);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.maxY, p_76978_0_.maxZ);
        tessellator.setNormal(0.0F, -1.0F, 0.0F);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.minY, p_76978_0_.minZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.minY, p_76978_0_.minZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.minY, p_76978_0_.maxZ);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.minY, p_76978_0_.maxZ);
        tessellator.setNormal(0.0F, 1.0F, 0.0F);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.maxY, p_76978_0_.maxZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.maxY, p_76978_0_.maxZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.maxY, p_76978_0_.minZ);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.maxY, p_76978_0_.minZ);
        tessellator.setNormal(-1.0F, 0.0F, 0.0F);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.minY, p_76978_0_.maxZ);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.maxY, p_76978_0_.maxZ);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.maxY, p_76978_0_.minZ);
        tessellator.addVertex(p_76978_0_.minX, p_76978_0_.minY, p_76978_0_.minZ);
        tessellator.setNormal(1.0F, 0.0F, 0.0F);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.minY, p_76978_0_.minZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.maxY, p_76978_0_.minZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.maxY, p_76978_0_.maxZ);
        tessellator.addVertex(p_76978_0_.maxX, p_76978_0_.minY, p_76978_0_.maxZ);
        tessellator.setTranslation(0.0D, 0.0D, 0.0D);
        tessellator.draw();
        GL11.glEnable(GL11.GL_TEXTURE_2D);
    }

    /**
     * Adds to the tesselator a box using the aabb for the bounds. Args: aabb
     */
    public static void renderAABB(AxisAlignedBB p_76980_0_)
    {
        Tessellator tessellator = Tessellator.instance;
        tessellator.startDrawingQuads();
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.maxY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.maxY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.minY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.minY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.minY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.minY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.maxY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.maxY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.minY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.minY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.minY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.minY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.maxY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.maxY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.maxY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.maxY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.minY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.maxY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.maxY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.minX, p_76980_0_.minY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.minY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.maxY, p_76980_0_.minZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.maxY, p_76980_0_.maxZ);
        tessellator.addVertex(p_76980_0_.maxX, p_76980_0_.minY, p_76980_0_.maxZ);
        tessellator.draw();
    }

    /**
     * Sets the RenderManager.
     */
    public void setRenderManager(RenderManager p_76976_1_)
    {
        this.renderManager = p_76976_1_;
    }

    /**
     * Renders the entity's shadow and fire (if its on fire). Args: entity, x, y, z, yaw, partialTickTime
     */
    public void doRenderShadowAndFire(Entity p_76979_1_, double p_76979_2_, double p_76979_4_, double p_76979_6_, float p_76979_8_, float p_76979_9_)
    {
        if (this.renderManager.options.fancyGraphics && this.shadowSize > 0.0F && !p_76979_1_.isInvisible())
        {
            double d3 = this.renderManager.getDistanceToCamera(p_76979_1_.posX, p_76979_1_.posY, p_76979_1_.posZ);
            float f2 = (float)((1.0D - d3 / 256.0D) * (double)this.shadowOpaque);

            if (f2 > 0.0F)
            {
                this.renderShadow(p_76979_1_, p_76979_2_, p_76979_4_, p_76979_6_, f2, p_76979_9_);
            }
        }

        if (p_76979_1_.canRenderOnFire())
        {
            this.renderEntityOnFire(p_76979_1_, p_76979_2_, p_76979_4_, p_76979_6_, p_76979_9_);
        }
    }

    /**
     * Returns the font renderer from the set render manager
     */
    public FontRenderer getFontRendererFromRenderManager()
    {
        return this.renderManager.getFontRenderer();
    }

    public void updateIcons(IIconRegister p_94143_1_) {}

    protected void func_147906_a(Entity p_147906_1_, String p_147906_2_, double p_147906_3_, double p_147906_5_, double p_147906_7_, int p_147906_9_)
    {
        double d3 = p_147906_1_.getDistanceSqToEntity(this.renderManager.livingPlayer);

        if (d3 <= (double)(p_147906_9_ * p_147906_9_))
        {
            FontRenderer fontrenderer = this.getFontRendererFromRenderManager();
            float f = 1.6F;
            float f1 = 0.016666668F * f;
            GL11.glPushMatrix();
            GL11.glTranslatef((float)p_147906_3_ + 0.0F, (float)p_147906_5_ + p_147906_1_.height + 0.5F, (float)p_147906_7_);
            GL11.glNormal3f(0.0F, 1.0F, 0.0F);
            GL11.glRotatef(-this.renderManager.playerViewY, 0.0F, 1.0F, 0.0F);
            GL11.glRotatef(this.renderManager.playerViewX, 1.0F, 0.0F, 0.0F);
            GL11.glScalef(-f1, -f1, f1);
            GL11.glDisable(GL11.GL_LIGHTING);
            GL11.glDepthMask(false);
            GL11.glDisable(GL11.GL_DEPTH_TEST);
            GL11.glEnable(GL11.GL_BLEND);
            OpenGlHelper.glBlendFunc(770, 771, 1, 0);
            Tessellator tessellator = Tessellator.instance;
            byte b0 = 0;

            if (p_147906_2_.equals("deadmau5"))
            {
                b0 = -10;
            }

            GL11.glDisable(GL11.GL_TEXTURE_2D);
            tessellator.startDrawingQuads();
            int j = fontrenderer.getStringWidth(p_147906_2_) / 2;
            tessellator.setColorRGBA_F(0.0F, 0.0F, 0.0F, 0.25F);
            tessellator.addVertex((double)(-j - 1), (double)(-1 + b0), 0.0D);
            tessellator.addVertex((double)(-j - 1), (double)(8 + b0), 0.0D);
            tessellator.addVertex((double)(j + 1), (double)(8 + b0), 0.0D);
            tessellator.addVertex((double)(j + 1), (double)(-1 + b0), 0.0D);
            tessellator.draw();
            GL11.glEnable(GL11.GL_TEXTURE_2D);
            fontrenderer.drawString(p_147906_2_, -fontrenderer.getStringWidth(p_147906_2_) / 2, b0, 553648127);
            GL11.glEnable(GL11.GL_DEPTH_TEST);
            GL11.glDepthMask(true);
            fontrenderer.drawString(p_147906_2_, -fontrenderer.getStringWidth(p_147906_2_) / 2, b0, -1);
            GL11.glEnable(GL11.GL_LIGHTING);
            GL11.glDisable(GL11.GL_BLEND);
            GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
            GL11.glPopMatrix();
        }
    }
}