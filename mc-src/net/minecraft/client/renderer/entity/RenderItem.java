package net.minecraft.client.renderer.entity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Random;
import java.util.concurrent.Callable;
import net.minecraft.block.Block;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.renderer.ItemRenderer;
import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.renderer.RenderBlocks;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.renderer.texture.TextureManager;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.client.renderer.texture.TextureUtil;
import net.minecraft.crash.CrashReport;
import net.minecraft.crash.CrashReportCategory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.item.Item;
import net.minecraft.item.ItemBlock;
import net.minecraft.item.ItemCloth;
import net.minecraft.item.ItemStack;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.util.ReportedException;
import net.minecraft.util.ResourceLocation;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;

import net.minecraftforge.client.ForgeHooksClient;

@SideOnly(Side.CLIENT)
public class RenderItem extends Render
{
    private static final ResourceLocation RES_ITEM_GLINT = new ResourceLocation("textures/misc/enchanted_item_glint.png");
    private RenderBlocks renderBlocksRi = new RenderBlocks();
    /** The RNG used in RenderItem (for bobbing itemstacks on the ground) */
    private Random random = new Random();
    public boolean renderWithColor = true;
    /** Defines the zLevel of rendering of item on GUI. */
    public float zLevel;
    public static boolean renderInFrame;
    private static final String __OBFID = "CL_00001003";

    public RenderItem()
    {
        this.shadowSize = 0.15F;
        this.shadowOpaque = 0.75F;
    }

    /**
     * Actually renders the given argument. This is a synthetic bridge method, always casting down its argument and then
     * handing it off to a worker function which does the actual work. In all probabilty, the class Render is generic
     * (Render<T extends Entity) and this method has signature public void func_76986_a(T entity, double d, double d1,
     * double d2, float f, float f1). But JAD is pre 1.5 so doesn't do that.
     */
    public void doRender(EntityItem p_76986_1_, double p_76986_2_, double p_76986_4_, double p_76986_6_, float p_76986_8_, float p_76986_9_)
    {
        ItemStack itemstack = p_76986_1_.getEntityItem();

        if (itemstack.getItem() != null)
        {
            this.bindEntityTexture(p_76986_1_);
            TextureUtil.func_152777_a(false, false, 1.0F);
            this.random.setSeed(187L);
            GL11.glPushMatrix();
            float f2 = shouldBob() ? MathHelper.sin(((float)p_76986_1_.age + p_76986_9_) / 10.0F + p_76986_1_.hoverStart) * 0.1F + 0.1F : 0F;
            float f3 = (((float)p_76986_1_.age + p_76986_9_) / 20.0F + p_76986_1_.hoverStart) * (180F / (float)Math.PI);
            byte b0 = 1;

            if (p_76986_1_.getEntityItem().stackSize > 1)
            {
                b0 = 2;
            }

            if (p_76986_1_.getEntityItem().stackSize > 5)
            {
                b0 = 3;
            }

            if (p_76986_1_.getEntityItem().stackSize > 20)
            {
                b0 = 4;
            }

            if (p_76986_1_.getEntityItem().stackSize > 40)
            {
                b0 = 5;
            }

            b0 = getMiniBlockCount(itemstack, b0);

            GL11.glTranslatef((float)p_76986_2_, (float)p_76986_4_ + f2, (float)p_76986_6_);
            GL11.glEnable(GL12.GL_RESCALE_NORMAL);
            float f6;
            float f7;
            int k;

            if (ForgeHooksClient.renderEntityItem(p_76986_1_, itemstack, f2, f3, random, renderManager.renderEngine, field_147909_c, b0))
            {
                ;
            }
            else // Code Style break here to prevent the patch from editing this line
            if (itemstack.getItemSpriteNumber() == 0 && itemstack.getItem() instanceof ItemBlock && RenderBlocks.renderItemIn3d(Block.getBlockFromItem(itemstack.getItem()).getRenderType()))
            {
                Block block = Block.getBlockFromItem(itemstack.getItem());
                GL11.glRotatef(f3, 0.0F, 1.0F, 0.0F);

                if (renderInFrame)
                {
                    GL11.glScalef(1.25F, 1.25F, 1.25F);
                    GL11.glTranslatef(0.0F, 0.05F, 0.0F);
                    GL11.glRotatef(-90.0F, 0.0F, 1.0F, 0.0F);
                }

                float f9 = 0.25F;
                k = block.getRenderType();

                if (k == 1 || k == 19 || k == 12 || k == 2)
                {
                    f9 = 0.5F;
                }

                if (block.getRenderBlockPass() > 0)
                {
                    GL11.glAlphaFunc(GL11.GL_GREATER, 0.1F);
                    GL11.glEnable(GL11.GL_BLEND);
                    OpenGlHelper.glBlendFunc(770, 771, 1, 0);
                }

                GL11.glScalef(f9, f9, f9);

                for (int l = 0; l < b0; ++l)
                {
                    GL11.glPushMatrix();

                    if (l > 0)
                    {
                        f6 = (this.random.nextFloat() * 2.0F - 1.0F) * 0.2F / f9;
                        f7 = (this.random.nextFloat() * 2.0F - 1.0F) * 0.2F / f9;
                        float f8 = (this.random.nextFloat() * 2.0F - 1.0F) * 0.2F / f9;
                        GL11.glTranslatef(f6, f7, f8);
                    }

                    this.renderBlocksRi.renderBlockAsItem(block, itemstack.getItemDamage(), 1.0F);
                    GL11.glPopMatrix();
                }

                if (block.getRenderBlockPass() > 0)
                {
                    GL11.glDisable(GL11.GL_BLEND);
                }
            }
            else
            {
                float f5;

                if (/*itemstack.getItemSpriteNumber() == 1 &&*/ itemstack.getItem().requiresMultipleRenderPasses())
                {
                    if (renderInFrame)
                    {
                        GL11.glScalef(0.5128205F, 0.5128205F, 0.5128205F);
                        GL11.glTranslatef(0.0F, -0.05F, 0.0F);
                    }
                    else
                    {
                        GL11.glScalef(0.5F, 0.5F, 0.5F);
                    }

                    for (int j = 0; j < itemstack.getItem().getRenderPasses(itemstack.getItemDamage()); ++j)
                    {
                        this.random.setSeed(187L);
                        IIcon iicon1 = itemstack.getItem().getIcon(itemstack, j);

                        if (this.renderWithColor)
                        {
                            k = itemstack.getItem().getColorFromItemStack(itemstack, j);
                            f5 = (float)(k >> 16 & 255) / 255.0F;
                            f6 = (float)(k >> 8 & 255) / 255.0F;
                            f7 = (float)(k & 255) / 255.0F;
                            GL11.glColor4f(f5, f6, f7, 1.0F);
                            this.renderDroppedItem(p_76986_1_, iicon1, b0, p_76986_9_, f5, f6, f7, j);
                        }
                        else
                        {
                            this.renderDroppedItem(p_76986_1_, iicon1, b0, p_76986_9_, 1.0F, 1.0F, 1.0F,  j);
                        }
                    }
                }
                else
                {
                    if (itemstack != null && itemstack.getItem() instanceof ItemCloth)
                    {
                        GL11.glAlphaFunc(GL11.GL_GREATER, 0.1F);
                        GL11.glEnable(GL11.GL_BLEND);
                        OpenGlHelper.glBlendFunc(770, 771, 1, 0);
                    }

                    if (renderInFrame)
                    {
                        GL11.glScalef(0.5128205F, 0.5128205F, 0.5128205F);
                        GL11.glTranslatef(0.0F, -0.05F, 0.0F);
                    }
                    else
                    {
                        GL11.glScalef(0.5F, 0.5F, 0.5F);
                    }

                    IIcon iicon = itemstack.getIconIndex();

                    if (this.renderWithColor)
                    {
                        int i = itemstack.getItem().getColorFromItemStack(itemstack, 0);
                        float f4 = (float)(i >> 16 & 255) / 255.0F;
                        f5 = (float)(i >> 8 & 255) / 255.0F;
                        f6 = (float)(i & 255) / 255.0F;
                        this.renderDroppedItem(p_76986_1_, iicon, b0, p_76986_9_, f4, f5, f6);
                    }
                    else
                    {
                        this.renderDroppedItem(p_76986_1_, iicon, b0, p_76986_9_, 1.0F, 1.0F, 1.0F);
                    }

                    if (itemstack != null && itemstack.getItem() instanceof ItemCloth)
                    {
                        GL11.glDisable(GL11.GL_BLEND);
                    }
                }
            }

            GL11.glDisable(GL12.GL_RESCALE_NORMAL);
            GL11.glPopMatrix();
            this.bindEntityTexture(p_76986_1_);
            TextureUtil.func_147945_b();
        }
    }

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected ResourceLocation getEntityTexture(EntityItem p_110775_1_)
    {
        return this.renderManager.renderEngine.getResourceLocation(p_110775_1_.getEntityItem().getItemSpriteNumber());
    }

    /**
     * Renders a dropped item
     */
    private void renderDroppedItem(EntityItem p_77020_1_, IIcon p_77020_2_, int p_77020_3_, float p_77020_4_, float p_77020_5_, float p_77020_6_, float p_77020_7_)
    {
        this.renderDroppedItem(p_77020_1_, p_77020_2_, p_77020_3_, p_77020_4_, p_77020_5_, p_77020_6_, p_77020_7_, 0);
    }

    private void renderDroppedItem(EntityItem p_77020_1_, IIcon p_77020_2_, int p_77020_3_, float p_77020_4_, float p_77020_5_, float p_77020_6_, float p_77020_7_, int pass)
    {
        Tessellator tessellator = Tessellator.instance;

        if (p_77020_2_ == null)
        {
            TextureManager texturemanager = Minecraft.getMinecraft().getTextureManager();
            ResourceLocation resourcelocation = texturemanager.getResourceLocation(p_77020_1_.getEntityItem().getItemSpriteNumber());
            p_77020_2_ = ((TextureMap)texturemanager.getTexture(resourcelocation)).getAtlasSprite("missingno");
        }

        float f14 = ((IIcon)p_77020_2_).getMinU();
        float f15 = ((IIcon)p_77020_2_).getMaxU();
        float f4 = ((IIcon)p_77020_2_).getMinV();
        float f5 = ((IIcon)p_77020_2_).getMaxV();
        float f6 = 1.0F;
        float f7 = 0.5F;
        float f8 = 0.25F;
        float f10;

        if (this.renderManager.options.fancyGraphics)
        {
            GL11.glPushMatrix();

            if (renderInFrame)
            {
                GL11.glRotatef(180.0F, 0.0F, 1.0F, 0.0F);
            }
            else
            {
                GL11.glRotatef((((float)p_77020_1_.age + p_77020_4_) / 20.0F + p_77020_1_.hoverStart) * (180F / (float)Math.PI), 0.0F, 1.0F, 0.0F);
            }

            float f9 = 0.0625F;
            f10 = 0.021875F;
            ItemStack itemstack = p_77020_1_.getEntityItem();
            int j = itemstack.stackSize;
            byte b0;

            if (j < 2)
            {
                b0 = 1;
            }
            else if (j < 16)
            {
                b0 = 2;
            }
            else if (j < 32)
            {
                b0 = 3;
            }
            else
            {
                b0 = 4;
            }

            b0 = getMiniItemCount(itemstack, b0);

            GL11.glTranslatef(-f7, -f8, -((f9 + f10) * (float)b0 / 2.0F));

            for (int k = 0; k < b0; ++k)
            {
                // Makes items offset when in 3D, like when in 2D, looks much better. Considered a vanilla bug...
                if (k > 0 && shouldSpreadItems())
                {
                    float x = (random.nextFloat() * 2.0F - 1.0F) * 0.3F / 0.5F;
                    float y = (random.nextFloat() * 2.0F - 1.0F) * 0.3F / 0.5F;
                    float z = (random.nextFloat() * 2.0F - 1.0F) * 0.3F / 0.5F;
                    GL11.glTranslatef(x, y, f9 + f10);
                }
                else
                {
                    GL11.glTranslatef(0f, 0f, f9 + f10);
                }

                if (itemstack.getItemSpriteNumber() == 0)
                {
                    this.bindTexture(TextureMap.locationBlocksTexture);
                }
                else
                {
                    this.bindTexture(TextureMap.locationItemsTexture);
                }

                GL11.glColor4f(p_77020_5_, p_77020_6_, p_77020_7_, 1.0F);
                ItemRenderer.renderItemIn2D(tessellator, f15, f4, f14, f5, ((IIcon)p_77020_2_).getIconWidth(), ((IIcon)p_77020_2_).getIconHeight(), f9);

                if (itemstack.hasEffect(pass))
                {
                    GL11.glDepthFunc(GL11.GL_EQUAL);
                    GL11.glDisable(GL11.GL_LIGHTING);
                    this.renderManager.renderEngine.bindTexture(RES_ITEM_GLINT);
                    GL11.glEnable(GL11.GL_BLEND);
                    GL11.glBlendFunc(GL11.GL_SRC_COLOR, GL11.GL_ONE);
                    float f11 = 0.76F;
                    GL11.glColor4f(0.5F * f11, 0.25F * f11, 0.8F * f11, 1.0F);
                    GL11.glMatrixMode(GL11.GL_TEXTURE);
                    GL11.glPushMatrix();
                    float f12 = 0.125F;
                    GL11.glScalef(f12, f12, f12);
                    float f13 = (float)(Minecraft.getSystemTime() % 3000L) / 3000.0F * 8.0F;
                    GL11.glTranslatef(f13, 0.0F, 0.0F);
                    GL11.glRotatef(-50.0F, 0.0F, 0.0F, 1.0F);
                    ItemRenderer.renderItemIn2D(tessellator, 0.0F, 0.0F, 1.0F, 1.0F, 255, 255, f9);
                    GL11.glPopMatrix();
                    GL11.glPushMatrix();
                    GL11.glScalef(f12, f12, f12);
                    f13 = (float)(Minecraft.getSystemTime() % 4873L) / 4873.0F * 8.0F;
                    GL11.glTranslatef(-f13, 0.0F, 0.0F);
                    GL11.glRotatef(10.0F, 0.0F, 0.0F, 1.0F);
                    ItemRenderer.renderItemIn2D(tessellator, 0.0F, 0.0F, 1.0F, 1.0F, 255, 255, f9);
                    GL11.glPopMatrix();
                    GL11.glMatrixMode(GL11.GL_MODELVIEW);
                    GL11.glDisable(GL11.GL_BLEND);
                    GL11.glEnable(GL11.GL_LIGHTING);
                    GL11.glDepthFunc(GL11.GL_LEQUAL);
                }
            }

            GL11.glPopMatrix();
        }
        else
        {
            for (int l = 0; l < p_77020_3_; ++l)
            {
                GL11.glPushMatrix();

                if (l > 0)
                {
                    f10 = (this.random.nextFloat() * 2.0F - 1.0F) * 0.3F;
                    float f16 = (this.random.nextFloat() * 2.0F - 1.0F) * 0.3F;
                    float f17 = (this.random.nextFloat() * 2.0F - 1.0F) * 0.3F;
                    GL11.glTranslatef(f10, f16, f17);
                }

                if (!renderInFrame)
                {
                    GL11.glRotatef(180.0F - this.renderManager.playerViewY, 0.0F, 1.0F, 0.0F);
                }

                GL11.glColor4f(p_77020_5_, p_77020_6_, p_77020_7_, 1.0F);
                tessellator.startDrawingQuads();
                tessellator.setNormal(0.0F, 1.0F, 0.0F);
                tessellator.addVertexWithUV((double)(0.0F - f7), (double)(0.0F - f8), 0.0D, (double)f14, (double)f5);
                tessellator.addVertexWithUV((double)(f6 - f7), (double)(0.0F - f8), 0.0D, (double)f15, (double)f5);
                tessellator.addVertexWithUV((double)(f6 - f7), (double)(1.0F - f8), 0.0D, (double)f15, (double)f4);
                tessellator.addVertexWithUV((double)(0.0F - f7), (double)(1.0F - f8), 0.0D, (double)f14, (double)f4);
                tessellator.draw();
                GL11.glPopMatrix();
            }
        }
    }

    /**
     * Renders the item's icon or block into the UI at the specified position.
     */
    public void renderItemIntoGUI(FontRenderer p_77015_1_, TextureManager p_77015_2_, ItemStack p_77015_3_, int p_77015_4_, int p_77015_5_)
    {
        this.renderItemIntoGUI(p_77015_1_, p_77015_2_, p_77015_3_, p_77015_4_, p_77015_5_, false);
    }

    public void renderItemIntoGUI(FontRenderer p_77015_1_, TextureManager p_77015_2_, ItemStack p_77015_3_, int p_77015_4_, int p_77015_5_, boolean renderEffect)
    {
        int k = p_77015_3_.getItemDamage();
        Object object = p_77015_3_.getIconIndex();
        int l;
        float f;
        float f3;
        float f4;

        if (p_77015_3_.getItemSpriteNumber() == 0 && RenderBlocks.renderItemIn3d(Block.getBlockFromItem(p_77015_3_.getItem()).getRenderType()))
        {
            p_77015_2_.bindTexture(TextureMap.locationBlocksTexture);
            Block block = Block.getBlockFromItem(p_77015_3_.getItem());
            GL11.glEnable(GL11.GL_ALPHA_TEST);

            if (block.getRenderBlockPass() != 0)
            {
                GL11.glAlphaFunc(GL11.GL_GREATER, 0.1F);
                GL11.glEnable(GL11.GL_BLEND);
                OpenGlHelper.glBlendFunc(770, 771, 1, 0);
            }
            else
            {
                GL11.glAlphaFunc(GL11.GL_GREATER, 0.5F);
                GL11.glDisable(GL11.GL_BLEND);
            }

            GL11.glPushMatrix();
            GL11.glTranslatef((float)(p_77015_4_ - 2), (float)(p_77015_5_ + 3), -3.0F + this.zLevel);
            GL11.glScalef(10.0F, 10.0F, 10.0F);
            GL11.glTranslatef(1.0F, 0.5F, 1.0F);
            GL11.glScalef(1.0F, 1.0F, -1.0F);
            GL11.glRotatef(210.0F, 1.0F, 0.0F, 0.0F);
            GL11.glRotatef(45.0F, 0.0F, 1.0F, 0.0F);
            l = p_77015_3_.getItem().getColorFromItemStack(p_77015_3_, 0);
            f3 = (float)(l >> 16 & 255) / 255.0F;
            f4 = (float)(l >> 8 & 255) / 255.0F;
            f = (float)(l & 255) / 255.0F;

            if (this.renderWithColor)
            {
                GL11.glColor4f(f3, f4, f, 1.0F);
            }

            GL11.glRotatef(-90.0F, 0.0F, 1.0F, 0.0F);
            this.renderBlocksRi.useInventoryTint = this.renderWithColor;
            this.renderBlocksRi.renderBlockAsItem(block, k, 1.0F);
            this.renderBlocksRi.useInventoryTint = true;

            if (block.getRenderBlockPass() == 0)
            {
                GL11.glAlphaFunc(GL11.GL_GREATER, 0.1F);
            }

            GL11.glPopMatrix();
        }
        else if (p_77015_3_.getItem().requiresMultipleRenderPasses())
        {
            GL11.glDisable(GL11.GL_LIGHTING);
            GL11.glEnable(GL11.GL_ALPHA_TEST);
            p_77015_2_.bindTexture(TextureMap.locationItemsTexture);
            GL11.glDisable(GL11.GL_ALPHA_TEST);
            GL11.glDisable(GL11.GL_TEXTURE_2D);
            GL11.glEnable(GL11.GL_BLEND);
            OpenGlHelper.glBlendFunc(0, 0, 0, 0);
            GL11.glColorMask(false, false, false, true);
            GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
            Tessellator tessellator = Tessellator.instance;
            tessellator.startDrawingQuads();
            tessellator.setColorOpaque_I(-1);
            tessellator.addVertex((double)(p_77015_4_ - 2), (double)(p_77015_5_ + 18), (double)this.zLevel);
            tessellator.addVertex((double)(p_77015_4_ + 18), (double)(p_77015_5_ + 18), (double)this.zLevel);
            tessellator.addVertex((double)(p_77015_4_ + 18), (double)(p_77015_5_ - 2), (double)this.zLevel);
            tessellator.addVertex((double)(p_77015_4_ - 2), (double)(p_77015_5_ - 2), (double)this.zLevel);
            tessellator.draw();
            GL11.glColorMask(true, true, true, true);
            GL11.glEnable(GL11.GL_TEXTURE_2D);
            GL11.glEnable(GL11.GL_ALPHA_TEST);

            Item item = p_77015_3_.getItem();
            for (l = 0; l < item.getRenderPasses(k); ++l)
            {
                OpenGlHelper.glBlendFunc(770, 771, 1, 0);
                p_77015_2_.bindTexture(item.getSpriteNumber() == 0 ? TextureMap.locationBlocksTexture : TextureMap.locationItemsTexture);
                IIcon iicon = item.getIcon(p_77015_3_, l);
                int i1 = p_77015_3_.getItem().getColorFromItemStack(p_77015_3_, l);
                f = (float)(i1 >> 16 & 255) / 255.0F;
                float f1 = (float)(i1 >> 8 & 255) / 255.0F;
                float f2 = (float)(i1 & 255) / 255.0F;

                if (this.renderWithColor)
                {
                    GL11.glColor4f(f, f1, f2, 1.0F);
                }

                GL11.glDisable(GL11.GL_LIGHTING); //Forge: Make sure that render states are reset, ad renderEffect can derp them up.
                GL11.glEnable(GL11.GL_ALPHA_TEST);

                this.renderIcon(p_77015_4_, p_77015_5_, iicon, 16, 16);

                GL11.glDisable(GL11.GL_ALPHA_TEST);
                GL11.glEnable(GL11.GL_LIGHTING);

                if (renderEffect && p_77015_3_.hasEffect(l))
                {
                    renderEffect(p_77015_2_, p_77015_4_, p_77015_5_);
                }
            }

            GL11.glEnable(GL11.GL_LIGHTING);
        }
        else
        {
            GL11.glDisable(GL11.GL_LIGHTING);
            GL11.glEnable(GL11.GL_BLEND);
            OpenGlHelper.glBlendFunc(770, 771, 1, 0);
            ResourceLocation resourcelocation = p_77015_2_.getResourceLocation(p_77015_3_.getItemSpriteNumber());
            p_77015_2_.bindTexture(resourcelocation);

            if (object == null)
            {
                object = ((TextureMap)Minecraft.getMinecraft().getTextureManager().getTexture(resourcelocation)).getAtlasSprite("missingno");
            }

            l = p_77015_3_.getItem().getColorFromItemStack(p_77015_3_, 0);
            f3 = (float)(l >> 16 & 255) / 255.0F;
            f4 = (float)(l >> 8 & 255) / 255.0F;
            f = (float)(l & 255) / 255.0F;

            if (this.renderWithColor)
            {
                GL11.glColor4f(f3, f4, f, 1.0F);
            }

            GL11.glDisable(GL11.GL_LIGHTING); //Forge: Make sure that render states are reset, a renderEffect can derp them up.
            GL11.glEnable(GL11.GL_ALPHA_TEST);
            GL11.glEnable(GL11.GL_BLEND);

            this.renderIcon(p_77015_4_, p_77015_5_, (IIcon)object, 16, 16);

            GL11.glEnable(GL11.GL_LIGHTING);
            GL11.glDisable(GL11.GL_ALPHA_TEST);
            GL11.glDisable(GL11.GL_BLEND);

            if (renderEffect && p_77015_3_.hasEffect(0))
            {
                renderEffect(p_77015_2_, p_77015_4_, p_77015_5_);
            }
            GL11.glEnable(GL11.GL_LIGHTING);
        }

        GL11.glEnable(GL11.GL_CULL_FACE);
    }

    /**
     * Render the item's icon or block into the GUI, including the glint effect.
     */
    @SuppressWarnings("unused")
    public void renderItemAndEffectIntoGUI(FontRenderer p_82406_1_, TextureManager p_82406_2_, final ItemStack p_82406_3_, int p_82406_4_, int p_82406_5_)
    {
        if (p_82406_3_ != null)
        {
            this.zLevel += 50.0F;

            try
            {
                if (!ForgeHooksClient.renderInventoryItem(this.field_147909_c, p_82406_2_, p_82406_3_, renderWithColor, zLevel, (float)p_82406_4_, (float)p_82406_5_))
                {
                    this.renderItemIntoGUI(p_82406_1_, p_82406_2_, p_82406_3_, p_82406_4_, p_82406_5_, true);
                }
            }
            catch (Throwable throwable)
            {
                CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Rendering item");
                CrashReportCategory crashreportcategory = crashreport.makeCategory("Item being rendered");
                crashreportcategory.addCrashSectionCallable("Item Type", new Callable()
                {
                    private static final String __OBFID = "CL_00001004";
                    public String call()
                    {
                        return String.valueOf(p_82406_3_.getItem());
                    }
                });
                crashreportcategory.addCrashSectionCallable("Item Aux", new Callable()
                {
                    private static final String __OBFID = "CL_00001005";
                    public String call()
                    {
                        return String.valueOf(p_82406_3_.getItemDamage());
                    }
                });
                crashreportcategory.addCrashSectionCallable("Item NBT", new Callable()
                {
                    private static final String __OBFID = "CL_00001006";
                    public String call()
                    {
                        return String.valueOf(p_82406_3_.getTagCompound());
                    }
                });
                crashreportcategory.addCrashSectionCallable("Item Foil", new Callable()
                {
                    private static final String __OBFID = "CL_00001007";
                    public String call()
                    {
                        return String.valueOf(p_82406_3_.hasEffect());
                    }
                });
                throw new ReportedException(crashreport);
            }

            // Forge: Bugfix, Move this to a per-render pass, modders must handle themselves
            if (false && p_82406_3_.hasEffect())
            {
                GL11.glDepthFunc(GL11.GL_EQUAL);
                GL11.glDisable(GL11.GL_LIGHTING);
                GL11.glDepthMask(false);
                p_82406_2_.bindTexture(RES_ITEM_GLINT);
                GL11.glEnable(GL11.GL_ALPHA_TEST);
                GL11.glEnable(GL11.GL_BLEND);
                GL11.glColor4f(0.5F, 0.25F, 0.8F, 1.0F);
                this.renderGlint(p_82406_4_ * 431278612 + p_82406_5_ * 32178161, p_82406_4_ - 2, p_82406_5_ - 2, 20, 20);
                OpenGlHelper.glBlendFunc(770, 771, 1, 0);
                GL11.glDepthMask(true);
                GL11.glEnable(GL11.GL_LIGHTING);
                GL11.glDepthFunc(GL11.GL_LEQUAL);
            }

            this.zLevel -= 50.0F;
        }
    }

    public void renderEffect(TextureManager manager, int x, int y)
    {
        GL11.glDepthFunc(GL11.GL_EQUAL);
        GL11.glDisable(GL11.GL_LIGHTING);
        GL11.glDepthMask(false);
        manager.bindTexture(RES_ITEM_GLINT);
        GL11.glEnable(GL11.GL_ALPHA_TEST);
        GL11.glEnable(GL11.GL_BLEND);
        GL11.glColor4f(0.5F, 0.25F, 0.8F, 1.0F);
        this.renderGlint(x * 431278612 + y * 32178161, x - 2, y - 2, 20, 20);
        GL11.glDepthMask(true);
        GL11.glDisable(GL11.GL_BLEND);
        GL11.glDisable(GL11.GL_ALPHA_TEST);
        GL11.glEnable(GL11.GL_LIGHTING);
        GL11.glDepthFunc(GL11.GL_LEQUAL);
    }

    private void renderGlint(int p_77018_1_, int p_77018_2_, int p_77018_3_, int p_77018_4_, int p_77018_5_)
    {
        for (int j1 = 0; j1 < 2; ++j1)
        {
            OpenGlHelper.glBlendFunc(772, 1, 0, 0);
            float f = 0.00390625F;
            float f1 = 0.00390625F;
            float f2 = (float)(Minecraft.getSystemTime() % (long)(3000 + j1 * 1873)) / (3000.0F + (float)(j1 * 1873)) * 256.0F;
            float f3 = 0.0F;
            Tessellator tessellator = Tessellator.instance;
            float f4 = 4.0F;

            if (j1 == 1)
            {
                f4 = -1.0F;
            }

            tessellator.startDrawingQuads();
            tessellator.addVertexWithUV((double)(p_77018_2_ + 0), (double)(p_77018_3_ + p_77018_5_), (double)this.zLevel, (double)((f2 + (float)p_77018_5_ * f4) * f), (double)((f3 + (float)p_77018_5_) * f1));
            tessellator.addVertexWithUV((double)(p_77018_2_ + p_77018_4_), (double)(p_77018_3_ + p_77018_5_), (double)this.zLevel, (double)((f2 + (float)p_77018_4_ + (float)p_77018_5_ * f4) * f), (double)((f3 + (float)p_77018_5_) * f1));
            tessellator.addVertexWithUV((double)(p_77018_2_ + p_77018_4_), (double)(p_77018_3_ + 0), (double)this.zLevel, (double)((f2 + (float)p_77018_4_) * f), (double)((f3 + 0.0F) * f1));
            tessellator.addVertexWithUV((double)(p_77018_2_ + 0), (double)(p_77018_3_ + 0), (double)this.zLevel, (double)((f2 + 0.0F) * f), (double)((f3 + 0.0F) * f1));
            tessellator.draw();
        }
    }

    /**
     * Renders the item's overlay information. Examples being stack count or damage on top of the item's image at the
     * specified position.
     */
    public void renderItemOverlayIntoGUI(FontRenderer p_77021_1_, TextureManager p_77021_2_, ItemStack p_77021_3_, int p_77021_4_, int p_77021_5_)
    {
        this.renderItemOverlayIntoGUI(p_77021_1_, p_77021_2_, p_77021_3_, p_77021_4_, p_77021_5_, (String)null);
    }

    public void renderItemOverlayIntoGUI(FontRenderer p_94148_1_, TextureManager p_94148_2_, ItemStack p_94148_3_, int p_94148_4_, int p_94148_5_, String p_94148_6_)
    {
        if (p_94148_3_ != null)
        {
            if (p_94148_3_.stackSize > 1 || p_94148_6_ != null)
            {
                String s1 = p_94148_6_ == null ? String.valueOf(p_94148_3_.stackSize) : p_94148_6_;
                GL11.glDisable(GL11.GL_LIGHTING);
                GL11.glDisable(GL11.GL_DEPTH_TEST);
                GL11.glDisable(GL11.GL_BLEND);
                p_94148_1_.drawStringWithShadow(s1, p_94148_4_ + 19 - 2 - p_94148_1_.getStringWidth(s1), p_94148_5_ + 6 + 3, 16777215);
                GL11.glEnable(GL11.GL_LIGHTING);
                GL11.glEnable(GL11.GL_DEPTH_TEST);
            }

            if (p_94148_3_.getItem().showDurabilityBar(p_94148_3_))
            {
                double health = p_94148_3_.getItem().getDurabilityForDisplay(p_94148_3_);
                int j1 = (int)Math.round(13.0D - health * 13.0D);
                int k = (int)Math.round(255.0D - health * 255.0D);
                GL11.glDisable(GL11.GL_LIGHTING);
                GL11.glDisable(GL11.GL_DEPTH_TEST);
                GL11.glDisable(GL11.GL_TEXTURE_2D);
                GL11.glDisable(GL11.GL_ALPHA_TEST);
                GL11.glDisable(GL11.GL_BLEND);
                Tessellator tessellator = Tessellator.instance;
                int l = 255 - k << 16 | k << 8;
                int i1 = (255 - k) / 4 << 16 | 16128;
                this.renderQuad(tessellator, p_94148_4_ + 2, p_94148_5_ + 13, 13, 2, 0);
                this.renderQuad(tessellator, p_94148_4_ + 2, p_94148_5_ + 13, 12, 1, i1);
                this.renderQuad(tessellator, p_94148_4_ + 2, p_94148_5_ + 13, j1, 1, l);
                //GL11.glEnable(GL11.GL_BLEND); // Forge: Disable Bled because it screws with a lot of things down the line.
                GL11.glEnable(GL11.GL_ALPHA_TEST);
                GL11.glEnable(GL11.GL_TEXTURE_2D);
                GL11.glEnable(GL11.GL_LIGHTING);
                GL11.glEnable(GL11.GL_DEPTH_TEST);
                GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
            }
        }
    }

    /**
     * Adds a quad to the tesselator at the specified position with the set width and height and color.  Args:
     * tessellator, x, y, width, height, color
     */
    private void renderQuad(Tessellator p_77017_1_, int p_77017_2_, int p_77017_3_, int p_77017_4_, int p_77017_5_, int p_77017_6_)
    {
        p_77017_1_.startDrawingQuads();
        p_77017_1_.setColorOpaque_I(p_77017_6_);
        p_77017_1_.addVertex((double)(p_77017_2_ + 0), (double)(p_77017_3_ + 0), 0.0D);
        p_77017_1_.addVertex((double)(p_77017_2_ + 0), (double)(p_77017_3_ + p_77017_5_), 0.0D);
        p_77017_1_.addVertex((double)(p_77017_2_ + p_77017_4_), (double)(p_77017_3_ + p_77017_5_), 0.0D);
        p_77017_1_.addVertex((double)(p_77017_2_ + p_77017_4_), (double)(p_77017_3_ + 0), 0.0D);
        p_77017_1_.draw();
    }

    public void renderIcon(int p_94149_1_, int p_94149_2_, IIcon p_94149_3_, int p_94149_4_, int p_94149_5_)
    {
        Tessellator tessellator = Tessellator.instance;
        tessellator.startDrawingQuads();
        tessellator.addVertexWithUV((double)(p_94149_1_ + 0), (double)(p_94149_2_ + p_94149_5_), (double)this.zLevel, (double)p_94149_3_.getMinU(), (double)p_94149_3_.getMaxV());
        tessellator.addVertexWithUV((double)(p_94149_1_ + p_94149_4_), (double)(p_94149_2_ + p_94149_5_), (double)this.zLevel, (double)p_94149_3_.getMaxU(), (double)p_94149_3_.getMaxV());
        tessellator.addVertexWithUV((double)(p_94149_1_ + p_94149_4_), (double)(p_94149_2_ + 0), (double)this.zLevel, (double)p_94149_3_.getMaxU(), (double)p_94149_3_.getMinV());
        tessellator.addVertexWithUV((double)(p_94149_1_ + 0), (double)(p_94149_2_ + 0), (double)this.zLevel, (double)p_94149_3_.getMinU(), (double)p_94149_3_.getMinV());
        tessellator.draw();
    }

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected ResourceLocation getEntityTexture(Entity p_110775_1_)
    {
        return this.getEntityTexture((EntityItem)p_110775_1_);
    }

    /**
     * Actually renders the given argument. This is a synthetic bridge method, always casting down its argument and then
     * handing it off to a worker function which does the actual work. In all probabilty, the class Render is generic
     * (Render<T extends Entity) and this method has signature public void func_76986_a(T entity, double d, double d1,
     * double d2, float f, float f1). But JAD is pre 1.5 so doesn't do that.
     */
    public void doRender(Entity p_76986_1_, double p_76986_2_, double p_76986_4_, double p_76986_6_, float p_76986_8_, float p_76986_9_)
    {
        this.doRender((EntityItem)p_76986_1_, p_76986_2_, p_76986_4_, p_76986_6_, p_76986_8_, p_76986_9_);
    }

    /*==================================== FORGE START ===========================================*/

    /**
     * Items should spread out when rendered in 3d?
     * @return
     */
    public boolean shouldSpreadItems()
    {
        return true;
    }

    /**
     * Items should have a bob effect
     * @return
     */
    public boolean shouldBob()
    {
        return true;
    }

    public byte getMiniBlockCount(ItemStack stack, byte original)
    {
        return original;
    }

    /**
     * Allows for a subclass to override how many rendered items appear in a
     * "mini item 3d stack"
     * @param stack The item stack
     * @param original The default amount vanilla would use
     * @return
     */
    public byte getMiniItemCount(ItemStack stack, byte original)
    {
        return original;
    }

    private static RenderItem instance;
    /**
     * Returns a single lazy loaded instance of RenderItem, for use in mods who
     * don't care about the interaction of other objects on the current state of the RenderItem they are using.
     * @return A global instance of RenderItem
     */
    public static RenderItem getInstance()
    {
        if (instance == null) instance = new RenderItem();
        return instance;
    }
    /*==================================== FORGE END =============================================*/
}