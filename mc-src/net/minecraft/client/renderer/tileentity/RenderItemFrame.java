package net.minecraft.client.renderer.tileentity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.Block;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.renderer.RenderBlocks;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.renderer.entity.Render;
import net.minecraft.client.renderer.entity.RenderItem;
import net.minecraft.client.renderer.entity.RenderManager;
import net.minecraft.client.renderer.texture.IIconRegister;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.renderer.texture.TextureCompass;
import net.minecraft.client.renderer.texture.TextureManager;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityItemFrame;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.util.Direction;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.util.ResourceLocation;
import net.minecraft.world.storage.MapData;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class RenderItemFrame extends Render
{
    private static final ResourceLocation mapBackgroundTextures = new ResourceLocation("textures/map/map_background.png");
    private final RenderBlocks field_147916_f = new RenderBlocks();
    private final Minecraft field_147917_g = Minecraft.getMinecraft();
    private IIcon field_94147_f;
    private static final String __OBFID = "CL_00001002";

    public void updateIcons(IIconRegister p_94143_1_)
    {
        this.field_94147_f = p_94143_1_.registerIcon("itemframe_background");
    }

    /**
     * Actually renders the given argument. This is a synthetic bridge method, always casting down its argument and then
     * handing it off to a worker function which does the actual work. In all probabilty, the class Render is generic
     * (Render<T extends Entity) and this method has signature public void func_76986_a(T entity, double d, double d1,
     * double d2, float f, float f1). But JAD is pre 1.5 so doesn't do that.
     */
    public void doRender(EntityItemFrame p_76986_1_, double p_76986_2_, double p_76986_4_, double p_76986_6_, float p_76986_8_, float p_76986_9_)
    {
        GL11.glPushMatrix();
        double d3 = p_76986_1_.posX - p_76986_2_ - 0.5D;
        double d4 = p_76986_1_.posY - p_76986_4_ - 0.5D;
        double d5 = p_76986_1_.posZ - p_76986_6_ - 0.5D;
        int i = p_76986_1_.field_146063_b + Direction.offsetX[p_76986_1_.hangingDirection];
        int j = p_76986_1_.field_146064_c;
        int k = p_76986_1_.field_146062_d + Direction.offsetZ[p_76986_1_.hangingDirection];
        GL11.glTranslated((double)i - d3, (double)j - d4, (double)k - d5);

        if (p_76986_1_.getDisplayedItem() != null && p_76986_1_.getDisplayedItem().getItem() == Items.filled_map)
        {
            this.func_147915_b(p_76986_1_);
        }
        else
        {
            this.renderFrameItemAsBlock(p_76986_1_);
        }

        this.func_82402_b(p_76986_1_);
        GL11.glPopMatrix();
        this.func_147914_a(p_76986_1_, p_76986_2_ + (double)((float)Direction.offsetX[p_76986_1_.hangingDirection] * 0.3F), p_76986_4_ - 0.25D, p_76986_6_ + (double)((float)Direction.offsetZ[p_76986_1_.hangingDirection] * 0.3F));
    }

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected ResourceLocation getEntityTexture(EntityItemFrame p_110775_1_)
    {
        return null;
    }

    private void func_147915_b(EntityItemFrame p_147915_1_)
    {
        GL11.glPushMatrix();
        GL11.glRotatef(p_147915_1_.rotationYaw, 0.0F, 1.0F, 0.0F);
        this.renderManager.renderEngine.bindTexture(TextureMap.locationBlocksTexture);
        Block block = Blocks.planks;
        float f = 0.0625F;
        float f1 = 1.0F;
        float f2 = f1 / 2.0F;
        GL11.glPushMatrix();
        this.field_147916_f.overrideBlockBounds(0.0D, (double)(0.5F - f2 + 0.0625F), (double)(0.5F - f2 + 0.0625F), (double)f, (double)(0.5F + f2 - 0.0625F), (double)(0.5F + f2 - 0.0625F));
        this.field_147916_f.setOverrideBlockTexture(this.field_94147_f);
        this.field_147916_f.renderBlockAsItem(block, 0, 1.0F);
        this.field_147916_f.clearOverrideBlockTexture();
        this.field_147916_f.unlockBlockBounds();
        GL11.glPopMatrix();
        this.field_147916_f.setOverrideBlockTexture(Blocks.planks.getIcon(1, 2));
        GL11.glPushMatrix();
        this.field_147916_f.overrideBlockBounds(0.0D, (double)(0.5F - f2), (double)(0.5F - f2), (double)(f + 1.0E-4F), (double)(f + 0.5F - f2), (double)(0.5F + f2));
        this.field_147916_f.renderBlockAsItem(block, 0, 1.0F);
        GL11.glPopMatrix();
        GL11.glPushMatrix();
        this.field_147916_f.overrideBlockBounds(0.0D, (double)(0.5F + f2 - f), (double)(0.5F - f2), (double)(f + 1.0E-4F), (double)(0.5F + f2), (double)(0.5F + f2));
        this.field_147916_f.renderBlockAsItem(block, 0, 1.0F);
        GL11.glPopMatrix();
        GL11.glPushMatrix();
        this.field_147916_f.overrideBlockBounds(0.0D, (double)(0.5F - f2), (double)(0.5F - f2), (double)f, (double)(0.5F + f2), (double)(f + 0.5F - f2));
        this.field_147916_f.renderBlockAsItem(block, 0, 1.0F);
        GL11.glPopMatrix();
        GL11.glPushMatrix();
        this.field_147916_f.overrideBlockBounds(0.0D, (double)(0.5F - f2), (double)(0.5F + f2 - f), (double)f, (double)(0.5F + f2), (double)(0.5F + f2));
        this.field_147916_f.renderBlockAsItem(block, 0, 1.0F);
        GL11.glPopMatrix();
        this.field_147916_f.unlockBlockBounds();
        this.field_147916_f.clearOverrideBlockTexture();
        GL11.glPopMatrix();
    }

    /**
     * Render the item frame's item as a block.
     */
    private void renderFrameItemAsBlock(EntityItemFrame p_82403_1_)
    {
        GL11.glPushMatrix();
        GL11.glRotatef(p_82403_1_.rotationYaw, 0.0F, 1.0F, 0.0F);
        this.renderManager.renderEngine.bindTexture(TextureMap.locationBlocksTexture);
        Block block = Blocks.planks;
        float f = 0.0625F;
        float f1 = 0.75F;
        float f2 = f1 / 2.0F;
        GL11.glPushMatrix();
        this.field_147916_f.overrideBlockBounds(0.0D, (double)(0.5F - f2 + 0.0625F), (double)(0.5F - f2 + 0.0625F), (double)(f * 0.5F), (double)(0.5F + f2 - 0.0625F), (double)(0.5F + f2 - 0.0625F));
        this.field_147916_f.setOverrideBlockTexture(this.field_94147_f);
        this.field_147916_f.renderBlockAsItem(block, 0, 1.0F);
        this.field_147916_f.clearOverrideBlockTexture();
        this.field_147916_f.unlockBlockBounds();
        GL11.glPopMatrix();
        this.field_147916_f.setOverrideBlockTexture(Blocks.planks.getIcon(1, 2));
        GL11.glPushMatrix();
        this.field_147916_f.overrideBlockBounds(0.0D, (double)(0.5F - f2), (double)(0.5F - f2), (double)(f + 1.0E-4F), (double)(f + 0.5F - f2), (double)(0.5F + f2));
        this.field_147916_f.renderBlockAsItem(block, 0, 1.0F);
        GL11.glPopMatrix();
        GL11.glPushMatrix();
        this.field_147916_f.overrideBlockBounds(0.0D, (double)(0.5F + f2 - f), (double)(0.5F - f2), (double)(f + 1.0E-4F), (double)(0.5F + f2), (double)(0.5F + f2));
        this.field_147916_f.renderBlockAsItem(block, 0, 1.0F);
        GL11.glPopMatrix();
        GL11.glPushMatrix();
        this.field_147916_f.overrideBlockBounds(0.0D, (double)(0.5F - f2), (double)(0.5F - f2), (double)f, (double)(0.5F + f2), (double)(f + 0.5F - f2));
        this.field_147916_f.renderBlockAsItem(block, 0, 1.0F);
        GL11.glPopMatrix();
        GL11.glPushMatrix();
        this.field_147916_f.overrideBlockBounds(0.0D, (double)(0.5F - f2), (double)(0.5F + f2 - f), (double)f, (double)(0.5F + f2), (double)(0.5F + f2));
        this.field_147916_f.renderBlockAsItem(block, 0, 1.0F);
        GL11.glPopMatrix();
        this.field_147916_f.unlockBlockBounds();
        this.field_147916_f.clearOverrideBlockTexture();
        GL11.glPopMatrix();
    }

    private void func_82402_b(EntityItemFrame p_82402_1_)
    {
        ItemStack itemstack = p_82402_1_.getDisplayedItem();

        if (itemstack != null)
        {
            EntityItem entityitem = new EntityItem(p_82402_1_.worldObj, 0.0D, 0.0D, 0.0D, itemstack);
            Item item = entityitem.getEntityItem().getItem();
            entityitem.getEntityItem().stackSize = 1;
            entityitem.hoverStart = 0.0F;
            GL11.glPushMatrix();
            GL11.glTranslatef(-0.453125F * (float)Direction.offsetX[p_82402_1_.hangingDirection], -0.18F, -0.453125F * (float)Direction.offsetZ[p_82402_1_.hangingDirection]);
            GL11.glRotatef(180.0F + p_82402_1_.rotationYaw, 0.0F, 1.0F, 0.0F);
            GL11.glRotatef((float)(-90 * p_82402_1_.getRotation()), 0.0F, 0.0F, 1.0F);

            switch (p_82402_1_.getRotation())
            {
                case 1:
                    GL11.glTranslatef(-0.16F, -0.16F, 0.0F);
                    break;
                case 2:
                    GL11.glTranslatef(0.0F, -0.32F, 0.0F);
                    break;
                case 3:
                    GL11.glTranslatef(0.16F, -0.16F, 0.0F);
            }

            net.minecraftforge.client.event.RenderItemInFrameEvent event = new net.minecraftforge.client.event.RenderItemInFrameEvent(p_82402_1_, this);
            if (!net.minecraftforge.common.MinecraftForge.EVENT_BUS.post(event))
            {
            if (item == Items.filled_map)
            {
                this.renderManager.renderEngine.bindTexture(mapBackgroundTextures);
                Tessellator tessellator = Tessellator.instance;
                GL11.glRotatef(180.0F, 0.0F, 1.0F, 0.0F);
                GL11.glRotatef(180.0F, 0.0F, 0.0F, 1.0F);
                float f = 0.0078125F;
                GL11.glScalef(f, f, f);

                switch (p_82402_1_.getRotation())
                {
                    case 0:
                        GL11.glTranslatef(-64.0F, -87.0F, -1.5F);
                        break;
                    case 1:
                        GL11.glTranslatef(-66.5F, -84.5F, -1.5F);
                        break;
                    case 2:
                        GL11.glTranslatef(-64.0F, -82.0F, -1.5F);
                        break;
                    case 3:
                        GL11.glTranslatef(-61.5F, -84.5F, -1.5F);
                }

                GL11.glNormal3f(0.0F, 0.0F, -1.0F);
                MapData mapdata = Items.filled_map.getMapData(entityitem.getEntityItem(), p_82402_1_.worldObj);
                GL11.glTranslatef(0.0F, 0.0F, -1.0F);

                if (mapdata != null)
                {
                    this.field_147917_g.entityRenderer.getMapItemRenderer().func_148250_a(mapdata, true);
                }
            }
            else
            {
                if (item == Items.compass)
                {
                    TextureManager texturemanager = Minecraft.getMinecraft().getTextureManager();
                    texturemanager.bindTexture(TextureMap.locationItemsTexture);
                    TextureAtlasSprite textureatlassprite1 = ((TextureMap)texturemanager.getTexture(TextureMap.locationItemsTexture)).getAtlasSprite(Items.compass.getIconIndex(entityitem.getEntityItem()).getIconName());

                    if (textureatlassprite1 instanceof TextureCompass)
                    {
                        TextureCompass texturecompass = (TextureCompass)textureatlassprite1;
                        double d0 = texturecompass.currentAngle;
                        double d1 = texturecompass.angleDelta;
                        texturecompass.currentAngle = 0.0D;
                        texturecompass.angleDelta = 0.0D;
                        texturecompass.updateCompass(p_82402_1_.worldObj, p_82402_1_.posX, p_82402_1_.posZ, (double)MathHelper.wrapAngleTo180_float((float)(180 + p_82402_1_.hangingDirection * 90)), false, true);
                        texturecompass.currentAngle = d0;
                        texturecompass.angleDelta = d1;
                    }
                }

                RenderItem.renderInFrame = true;
                RenderManager.instance.renderEntityWithPosYaw(entityitem, 0.0D, 0.0D, 0.0D, 0.0F, 0.0F);
                RenderItem.renderInFrame = false;

                if (item == Items.compass)
                {
                    TextureAtlasSprite textureatlassprite = ((TextureMap)Minecraft.getMinecraft().getTextureManager().getTexture(TextureMap.locationItemsTexture)).getAtlasSprite(Items.compass.getIconIndex(entityitem.getEntityItem()).getIconName());

                    if (textureatlassprite.getFrameCount() > 0)
                    {
                        textureatlassprite.updateAnimation();
                    }
                }
            }
            }

            GL11.glPopMatrix();
        }
    }

    protected void func_147914_a(EntityItemFrame p_147914_1_, double p_147914_2_, double p_147914_4_, double p_147914_6_)
    {
        if (Minecraft.isGuiEnabled() && p_147914_1_.getDisplayedItem() != null && p_147914_1_.getDisplayedItem().hasDisplayName() && this.renderManager.field_147941_i == p_147914_1_)
        {
            float f = 1.6F;
            float f1 = 0.016666668F * f;
            double d3 = p_147914_1_.getDistanceSqToEntity(this.renderManager.livingPlayer);
            float f2 = p_147914_1_.isSneaking() ? 32.0F : 64.0F;

            if (d3 < (double)(f2 * f2))
            {
                String s = p_147914_1_.getDisplayedItem().getDisplayName();

                if (p_147914_1_.isSneaking())
                {
                    FontRenderer fontrenderer = this.getFontRendererFromRenderManager();
                    GL11.glPushMatrix();
                    GL11.glTranslatef((float)p_147914_2_ + 0.0F, (float)p_147914_4_ + p_147914_1_.height + 0.5F, (float)p_147914_6_);
                    GL11.glNormal3f(0.0F, 1.0F, 0.0F);
                    GL11.glRotatef(-this.renderManager.playerViewY, 0.0F, 1.0F, 0.0F);
                    GL11.glRotatef(this.renderManager.playerViewX, 1.0F, 0.0F, 0.0F);
                    GL11.glScalef(-f1, -f1, f1);
                    GL11.glDisable(GL11.GL_LIGHTING);
                    GL11.glTranslatef(0.0F, 0.25F / f1, 0.0F);
                    GL11.glDepthMask(false);
                    GL11.glEnable(GL11.GL_BLEND);
                    GL11.glBlendFunc(GL11.GL_SRC_ALPHA, GL11.GL_ONE_MINUS_SRC_ALPHA);
                    Tessellator tessellator = Tessellator.instance;
                    GL11.glDisable(GL11.GL_TEXTURE_2D);
                    tessellator.startDrawingQuads();
                    int i = fontrenderer.getStringWidth(s) / 2;
                    tessellator.setColorRGBA_F(0.0F, 0.0F, 0.0F, 0.25F);
                    tessellator.addVertex((double)(-i - 1), -1.0D, 0.0D);
                    tessellator.addVertex((double)(-i - 1), 8.0D, 0.0D);
                    tessellator.addVertex((double)(i + 1), 8.0D, 0.0D);
                    tessellator.addVertex((double)(i + 1), -1.0D, 0.0D);
                    tessellator.draw();
                    GL11.glEnable(GL11.GL_TEXTURE_2D);
                    GL11.glDepthMask(true);
                    fontrenderer.drawString(s, -fontrenderer.getStringWidth(s) / 2, 0, 553648127);
                    GL11.glEnable(GL11.GL_LIGHTING);
                    GL11.glDisable(GL11.GL_BLEND);
                    GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
                    GL11.glPopMatrix();
                }
                else
                {
                    this.func_147906_a(p_147914_1_, s, p_147914_2_, p_147914_4_, p_147914_6_, 64);
                }
            }
        }
    }

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected ResourceLocation getEntityTexture(Entity p_110775_1_)
    {
        return this.getEntityTexture((EntityItemFrame)p_110775_1_);
    }

    /**
     * Actually renders the given argument. This is a synthetic bridge method, always casting down its argument and then
     * handing it off to a worker function which does the actual work. In all probabilty, the class Render is generic
     * (Render<T extends Entity) and this method has signature public void func_76986_a(T entity, double d, double d1,
     * double d2, float f, float f1). But JAD is pre 1.5 so doesn't do that.
     */
    public void doRender(Entity p_76986_1_, double p_76986_2_, double p_76986_4_, double p_76986_6_, float p_76986_8_, float p_76986_9_)
    {
        this.doRender((EntityItemFrame)p_76986_1_, p_76986_2_, p_76986_4_, p_76986_6_, p_76986_8_, p_76986_9_);
    }
}