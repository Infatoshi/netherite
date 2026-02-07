package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Random;
import net.minecraft.client.gui.inventory.GuiContainer;
import net.minecraft.client.model.ModelBook;
import net.minecraft.client.renderer.RenderHelper;
import net.minecraft.client.resources.I18n;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.InventoryPlayer;
import net.minecraft.inventory.ContainerEnchantment;
import net.minecraft.item.ItemStack;
import net.minecraft.util.EnchantmentNameParts;
import net.minecraft.util.MathHelper;
import net.minecraft.util.ResourceLocation;
import net.minecraft.world.World;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;
import org.lwjgl.util.glu.Project;

@SideOnly(Side.CLIENT)
public class GuiEnchantment extends GuiContainer
{
    private static final ResourceLocation field_147078_C = new ResourceLocation("textures/gui/container/enchanting_table.png");
    private static final ResourceLocation field_147070_D = new ResourceLocation("textures/entity/enchanting_table_book.png");
    private static final ModelBook field_147072_E = new ModelBook();
    private Random field_147074_F = new Random();
    private ContainerEnchantment field_147075_G;
    public int field_147073_u;
    public float field_147071_v;
    public float field_147069_w;
    public float field_147082_x;
    public float field_147081_y;
    public float field_147080_z;
    public float field_147076_A;
    ItemStack field_147077_B;
    private String field_147079_H;
    private static final String __OBFID = "CL_00000757";

    public GuiEnchantment(InventoryPlayer p_i1090_1_, World p_i1090_2_, int p_i1090_3_, int p_i1090_4_, int p_i1090_5_, String p_i1090_6_)
    {
        super(new ContainerEnchantment(p_i1090_1_, p_i1090_2_, p_i1090_3_, p_i1090_4_, p_i1090_5_));
        this.field_147075_G = (ContainerEnchantment)this.inventorySlots;
        this.field_147079_H = p_i1090_6_;
    }

    /**
     * Draw the foreground layer for the GuiContainer (everything in front of the items)
     */
    protected void drawGuiContainerForegroundLayer(int p_146979_1_, int p_146979_2_)
    {
        this.fontRendererObj.drawString(this.field_147079_H == null ? I18n.format("container.enchant", new Object[0]) : this.field_147079_H, 12, 5, 4210752);
        this.fontRendererObj.drawString(I18n.format("container.inventory", new Object[0]), 8, this.ySize - 96 + 2, 4210752);
    }

    /**
     * Called from the main game loop to update the screen.
     */
    public void updateScreen()
    {
        super.updateScreen();
        this.func_147068_g();
    }

    /**
     * Called when the mouse is clicked.
     */
    protected void mouseClicked(int p_73864_1_, int p_73864_2_, int p_73864_3_)
    {
        super.mouseClicked(p_73864_1_, p_73864_2_, p_73864_3_);
        int l = (this.width - this.xSize) / 2;
        int i1 = (this.height - this.ySize) / 2;

        for (int j1 = 0; j1 < 3; ++j1)
        {
            int k1 = p_73864_1_ - (l + 60);
            int l1 = p_73864_2_ - (i1 + 14 + 19 * j1);

            if (k1 >= 0 && l1 >= 0 && k1 < 108 && l1 < 19 && this.field_147075_G.enchantItem(this.mc.thePlayer, j1))
            {
                this.mc.playerController.sendEnchantPacket(this.field_147075_G.windowId, j1);
            }
        }
    }

    protected void drawGuiContainerBackgroundLayer(float p_146976_1_, int p_146976_2_, int p_146976_3_)
    {
        GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        this.mc.getTextureManager().bindTexture(field_147078_C);
        int k = (this.width - this.xSize) / 2;
        int l = (this.height - this.ySize) / 2;
        this.drawTexturedModalRect(k, l, 0, 0, this.xSize, this.ySize);
        GL11.glPushMatrix();
        GL11.glMatrixMode(GL11.GL_PROJECTION);
        GL11.glPushMatrix();
        GL11.glLoadIdentity();
        ScaledResolution scaledresolution = new ScaledResolution(this.mc, this.mc.displayWidth, this.mc.displayHeight);
        GL11.glViewport((scaledresolution.getScaledWidth() - 320) / 2 * scaledresolution.getScaleFactor(), (scaledresolution.getScaledHeight() - 240) / 2 * scaledresolution.getScaleFactor(), 320 * scaledresolution.getScaleFactor(), 240 * scaledresolution.getScaleFactor());
        GL11.glTranslatef(-0.34F, 0.23F, 0.0F);
        Project.gluPerspective(90.0F, 1.3333334F, 9.0F, 80.0F);
        float f1 = 1.0F;
        GL11.glMatrixMode(GL11.GL_MODELVIEW);
        GL11.glLoadIdentity();
        RenderHelper.enableStandardItemLighting();
        GL11.glTranslatef(0.0F, 3.3F, -16.0F);
        GL11.glScalef(f1, f1, f1);
        float f2 = 5.0F;
        GL11.glScalef(f2, f2, f2);
        GL11.glRotatef(180.0F, 0.0F, 0.0F, 1.0F);
        this.mc.getTextureManager().bindTexture(field_147070_D);
        GL11.glRotatef(20.0F, 1.0F, 0.0F, 0.0F);
        float f3 = this.field_147076_A + (this.field_147080_z - this.field_147076_A) * p_146976_1_;
        GL11.glTranslatef((1.0F - f3) * 0.2F, (1.0F - f3) * 0.1F, (1.0F - f3) * 0.25F);
        GL11.glRotatef(-(1.0F - f3) * 90.0F - 90.0F, 0.0F, 1.0F, 0.0F);
        GL11.glRotatef(180.0F, 1.0F, 0.0F, 0.0F);
        float f4 = this.field_147069_w + (this.field_147071_v - this.field_147069_w) * p_146976_1_ + 0.25F;
        float f5 = this.field_147069_w + (this.field_147071_v - this.field_147069_w) * p_146976_1_ + 0.75F;
        f4 = (f4 - (float)MathHelper.truncateDoubleToInt((double)f4)) * 1.6F - 0.3F;
        f5 = (f5 - (float)MathHelper.truncateDoubleToInt((double)f5)) * 1.6F - 0.3F;

        if (f4 < 0.0F)
        {
            f4 = 0.0F;
        }

        if (f5 < 0.0F)
        {
            f5 = 0.0F;
        }

        if (f4 > 1.0F)
        {
            f4 = 1.0F;
        }

        if (f5 > 1.0F)
        {
            f5 = 1.0F;
        }

        GL11.glEnable(GL12.GL_RESCALE_NORMAL);
        field_147072_E.render((Entity)null, 0.0F, f4, f5, f3, 0.0F, 0.0625F);
        GL11.glDisable(GL12.GL_RESCALE_NORMAL);
        RenderHelper.disableStandardItemLighting();
        GL11.glMatrixMode(GL11.GL_PROJECTION);
        GL11.glViewport(0, 0, this.mc.displayWidth, this.mc.displayHeight);
        GL11.glPopMatrix();
        GL11.glMatrixMode(GL11.GL_MODELVIEW);
        GL11.glPopMatrix();
        RenderHelper.disableStandardItemLighting();
        GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        EnchantmentNameParts.instance.reseedRandomGenerator(this.field_147075_G.nameSeed);

        for (int i1 = 0; i1 < 3; ++i1)
        {
            String s = EnchantmentNameParts.instance.generateNewRandomName();
            this.zLevel = 0.0F;
            this.mc.getTextureManager().bindTexture(field_147078_C);
            int j1 = this.field_147075_G.enchantLevels[i1];
            GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);

            if (j1 == 0)
            {
                this.drawTexturedModalRect(k + 60, l + 14 + 19 * i1, 0, 185, 108, 19);
            }
            else
            {
                String s1 = "" + j1;
                FontRenderer fontrenderer = this.mc.standardGalacticFontRenderer;
                int k1 = 6839882;

                if (this.mc.thePlayer.experienceLevel < j1 && !this.mc.thePlayer.capabilities.isCreativeMode)
                {
                    this.drawTexturedModalRect(k + 60, l + 14 + 19 * i1, 0, 185, 108, 19);
                    fontrenderer.drawSplitString(s, k + 62, l + 16 + 19 * i1, 104, (k1 & 16711422) >> 1);
                    fontrenderer = this.mc.fontRenderer;
                    k1 = 4226832;
                    fontrenderer.drawStringWithShadow(s1, k + 62 + 104 - fontrenderer.getStringWidth(s1), l + 16 + 19 * i1 + 7, k1);
                }
                else
                {
                    int l1 = p_146976_2_ - (k + 60);
                    int i2 = p_146976_3_ - (l + 14 + 19 * i1);

                    if (l1 >= 0 && i2 >= 0 && l1 < 108 && i2 < 19)
                    {
                        this.drawTexturedModalRect(k + 60, l + 14 + 19 * i1, 0, 204, 108, 19);
                        k1 = 16777088;
                    }
                    else
                    {
                        this.drawTexturedModalRect(k + 60, l + 14 + 19 * i1, 0, 166, 108, 19);
                    }

                    fontrenderer.drawSplitString(s, k + 62, l + 16 + 19 * i1, 104, k1);
                    fontrenderer = this.mc.fontRenderer;
                    k1 = 8453920;
                    fontrenderer.drawStringWithShadow(s1, k + 62 + 104 - fontrenderer.getStringWidth(s1), l + 16 + 19 * i1 + 7, k1);
                }
            }
        }
    }

    public void func_147068_g()
    {
        ItemStack itemstack = this.inventorySlots.getSlot(0).getStack();

        if (!ItemStack.areItemStacksEqual(itemstack, this.field_147077_B))
        {
            this.field_147077_B = itemstack;

            do
            {
                this.field_147082_x += (float)(this.field_147074_F.nextInt(4) - this.field_147074_F.nextInt(4));
            }
            while (this.field_147071_v <= this.field_147082_x + 1.0F && this.field_147071_v >= this.field_147082_x - 1.0F);
        }

        ++this.field_147073_u;
        this.field_147069_w = this.field_147071_v;
        this.field_147076_A = this.field_147080_z;
        boolean flag = false;

        for (int i = 0; i < 3; ++i)
        {
            if (this.field_147075_G.enchantLevels[i] != 0)
            {
                flag = true;
            }
        }

        if (flag)
        {
            this.field_147080_z += 0.2F;
        }
        else
        {
            this.field_147080_z -= 0.2F;
        }

        if (this.field_147080_z < 0.0F)
        {
            this.field_147080_z = 0.0F;
        }

        if (this.field_147080_z > 1.0F)
        {
            this.field_147080_z = 1.0F;
        }

        float f1 = (this.field_147082_x - this.field_147071_v) * 0.4F;
        float f = 0.2F;

        if (f1 < -f)
        {
            f1 = -f;
        }

        if (f1 > f)
        {
            f1 = f;
        }

        this.field_147081_y += (f1 - this.field_147081_y) * 0.9F;
        this.field_147071_v += this.field_147081_y;
    }
}