package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.renderer.RenderHelper;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.renderer.entity.RenderItem;
import net.minecraft.client.resources.I18n;
import net.minecraft.init.Blocks;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.world.gen.FlatGeneratorInfo;
import net.minecraft.world.gen.FlatLayerInfo;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;

@SideOnly(Side.CLIENT)
public class GuiCreateFlatWorld extends GuiScreen
{
    private static RenderItem field_146392_a = new RenderItem();
    private final GuiCreateWorld createWorldGui;
    private FlatGeneratorInfo theFlatGeneratorInfo = FlatGeneratorInfo.getDefaultFlatGenerator();
    private String field_146393_h;
    private String field_146394_i;
    private String field_146391_r;
    private GuiCreateFlatWorld.Details createFlatWorldListSlotGui;
    private GuiButton field_146389_t;
    private GuiButton field_146388_u;
    private GuiButton field_146386_v;
    private static final String __OBFID = "CL_00000687";

    public GuiCreateFlatWorld(GuiCreateWorld p_i1029_1_, String p_i1029_2_)
    {
        this.createWorldGui = p_i1029_1_;
        this.func_146383_a(p_i1029_2_);
    }

    public String func_146384_e()
    {
        return this.theFlatGeneratorInfo.toString();
    }

    public void func_146383_a(String p_146383_1_)
    {
        this.theFlatGeneratorInfo = FlatGeneratorInfo.createFlatGeneratorFromString(p_146383_1_);
    }

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        this.buttonList.clear();
        this.field_146393_h = I18n.format("createWorld.customize.flat.title", new Object[0]);
        this.field_146394_i = I18n.format("createWorld.customize.flat.tile", new Object[0]);
        this.field_146391_r = I18n.format("createWorld.customize.flat.height", new Object[0]);
        this.createFlatWorldListSlotGui = new GuiCreateFlatWorld.Details();
        this.buttonList.add(this.field_146389_t = new GuiButton(2, this.width / 2 - 154, this.height - 52, 100, 20, I18n.format("createWorld.customize.flat.addLayer", new Object[0]) + " (NYI)"));
        this.buttonList.add(this.field_146388_u = new GuiButton(3, this.width / 2 - 50, this.height - 52, 100, 20, I18n.format("createWorld.customize.flat.editLayer", new Object[0]) + " (NYI)"));
        this.buttonList.add(this.field_146386_v = new GuiButton(4, this.width / 2 - 155, this.height - 52, 150, 20, I18n.format("createWorld.customize.flat.removeLayer", new Object[0])));
        this.buttonList.add(new GuiButton(0, this.width / 2 - 155, this.height - 28, 150, 20, I18n.format("gui.done", new Object[0])));
        this.buttonList.add(new GuiButton(5, this.width / 2 + 5, this.height - 52, 150, 20, I18n.format("createWorld.customize.presets", new Object[0])));
        this.buttonList.add(new GuiButton(1, this.width / 2 + 5, this.height - 28, 150, 20, I18n.format("gui.cancel", new Object[0])));
        this.field_146389_t.visible = this.field_146388_u.visible = false;
        this.theFlatGeneratorInfo.func_82645_d();
        this.func_146375_g();
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        int i = this.theFlatGeneratorInfo.getFlatLayers().size() - this.createFlatWorldListSlotGui.field_148228_k - 1;

        if (p_146284_1_.id == 1)
        {
            this.mc.displayGuiScreen(this.createWorldGui);
        }
        else if (p_146284_1_.id == 0)
        {
            this.createWorldGui.field_146334_a = this.func_146384_e();
            this.mc.displayGuiScreen(this.createWorldGui);
        }
        else if (p_146284_1_.id == 5)
        {
            this.mc.displayGuiScreen(new GuiFlatPresets(this));
        }
        else if (p_146284_1_.id == 4 && this.func_146382_i())
        {
            this.theFlatGeneratorInfo.getFlatLayers().remove(i);
            this.createFlatWorldListSlotGui.field_148228_k = Math.min(this.createFlatWorldListSlotGui.field_148228_k, this.theFlatGeneratorInfo.getFlatLayers().size() - 1);
        }

        this.theFlatGeneratorInfo.func_82645_d();
        this.func_146375_g();
    }

    public void func_146375_g()
    {
        boolean flag = this.func_146382_i();
        this.field_146386_v.enabled = flag;
        this.field_146388_u.enabled = flag;
        this.field_146388_u.enabled = false;
        this.field_146389_t.enabled = false;
    }

    private boolean func_146382_i()
    {
        return this.createFlatWorldListSlotGui.field_148228_k > -1 && this.createFlatWorldListSlotGui.field_148228_k < this.theFlatGeneratorInfo.getFlatLayers().size();
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawDefaultBackground();
        this.createFlatWorldListSlotGui.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
        this.drawCenteredString(this.fontRendererObj, this.field_146393_h, this.width / 2, 8, 16777215);
        int k = this.width / 2 - 92 - 16;
        this.drawString(this.fontRendererObj, this.field_146394_i, k, 32, 16777215);
        this.drawString(this.fontRendererObj, this.field_146391_r, k + 2 + 213 - this.fontRendererObj.getStringWidth(this.field_146391_r), 32, 16777215);
        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }

    @SideOnly(Side.CLIENT)
    class Details extends GuiSlot
    {
        public int field_148228_k = -1;
        private static final String __OBFID = "CL_00000688";

        public Details()
        {
            super(GuiCreateFlatWorld.this.mc, GuiCreateFlatWorld.this.width, GuiCreateFlatWorld.this.height, 43, GuiCreateFlatWorld.this.height - 60, 24);
        }

        private void func_148225_a(int p_148225_1_, int p_148225_2_, ItemStack p_148225_3_)
        {
            this.func_148226_e(p_148225_1_ + 1, p_148225_2_ + 1);
            GL11.glEnable(GL12.GL_RESCALE_NORMAL);

            if (p_148225_3_ != null)
            {
                RenderHelper.enableGUIStandardItemLighting();
                GuiCreateFlatWorld.field_146392_a.renderItemIntoGUI(GuiCreateFlatWorld.this.fontRendererObj, GuiCreateFlatWorld.this.mc.getTextureManager(), p_148225_3_, p_148225_1_ + 2, p_148225_2_ + 2);
                RenderHelper.disableStandardItemLighting();
            }

            GL11.glDisable(GL12.GL_RESCALE_NORMAL);
        }

        private void func_148226_e(int p_148226_1_, int p_148226_2_)
        {
            this.func_148224_c(p_148226_1_, p_148226_2_, 0, 0);
        }

        private void func_148224_c(int p_148224_1_, int p_148224_2_, int p_148224_3_, int p_148224_4_)
        {
            GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
            GuiCreateFlatWorld.this.mc.getTextureManager().bindTexture(Gui.statIcons);
            float f = 0.0078125F;
            float f1 = 0.0078125F;
            boolean flag = true;
            boolean flag1 = true;
            Tessellator tessellator = Tessellator.instance;
            tessellator.startDrawingQuads();
            tessellator.addVertexWithUV((double)(p_148224_1_ + 0), (double)(p_148224_2_ + 18), (double)GuiCreateFlatWorld.this.zLevel, (double)((float)(p_148224_3_ + 0) * 0.0078125F), (double)((float)(p_148224_4_ + 18) * 0.0078125F));
            tessellator.addVertexWithUV((double)(p_148224_1_ + 18), (double)(p_148224_2_ + 18), (double)GuiCreateFlatWorld.this.zLevel, (double)((float)(p_148224_3_ + 18) * 0.0078125F), (double)((float)(p_148224_4_ + 18) * 0.0078125F));
            tessellator.addVertexWithUV((double)(p_148224_1_ + 18), (double)(p_148224_2_ + 0), (double)GuiCreateFlatWorld.this.zLevel, (double)((float)(p_148224_3_ + 18) * 0.0078125F), (double)((float)(p_148224_4_ + 0) * 0.0078125F));
            tessellator.addVertexWithUV((double)(p_148224_1_ + 0), (double)(p_148224_2_ + 0), (double)GuiCreateFlatWorld.this.zLevel, (double)((float)(p_148224_3_ + 0) * 0.0078125F), (double)((float)(p_148224_4_ + 0) * 0.0078125F));
            tessellator.draw();
        }

        protected int getSize()
        {
            return GuiCreateFlatWorld.this.theFlatGeneratorInfo.getFlatLayers().size();
        }

        /**
         * The element in the slot that was clicked, boolean for whether it was double clicked or not
         */
        protected void elementClicked(int p_148144_1_, boolean p_148144_2_, int p_148144_3_, int p_148144_4_)
        {
            this.field_148228_k = p_148144_1_;
            GuiCreateFlatWorld.this.func_146375_g();
        }

        /**
         * Returns true if the element passed in is currently selected
         */
        protected boolean isSelected(int p_148131_1_)
        {
            return p_148131_1_ == this.field_148228_k;
        }

        protected void drawBackground() {}

        protected void drawSlot(int p_148126_1_, int p_148126_2_, int p_148126_3_, int p_148126_4_, Tessellator p_148126_5_, int p_148126_6_, int p_148126_7_)
        {
            FlatLayerInfo flatlayerinfo = (FlatLayerInfo)GuiCreateFlatWorld.this.theFlatGeneratorInfo.getFlatLayers().get(GuiCreateFlatWorld.this.theFlatGeneratorInfo.getFlatLayers().size() - p_148126_1_ - 1);
            Item item = Item.getItemFromBlock(flatlayerinfo.func_151536_b());
            ItemStack itemstack = flatlayerinfo.func_151536_b() == Blocks.air ? null : new ItemStack(item, 1, flatlayerinfo.getFillBlockMeta());
            String s = itemstack != null && item != null ? item.getItemStackDisplayName(itemstack) : "Air";
            this.func_148225_a(p_148126_2_, p_148126_3_, itemstack);
            GuiCreateFlatWorld.this.fontRendererObj.drawString(s, p_148126_2_ + 18 + 5, p_148126_3_ + 3, 16777215);
            String s1;

            if (p_148126_1_ == 0)
            {
                s1 = I18n.format("createWorld.customize.flat.layer.top", new Object[] {Integer.valueOf(flatlayerinfo.getLayerCount())});
            }
            else if (p_148126_1_ == GuiCreateFlatWorld.this.theFlatGeneratorInfo.getFlatLayers().size() - 1)
            {
                s1 = I18n.format("createWorld.customize.flat.layer.bottom", new Object[] {Integer.valueOf(flatlayerinfo.getLayerCount())});
            }
            else
            {
                s1 = I18n.format("createWorld.customize.flat.layer", new Object[] {Integer.valueOf(flatlayerinfo.getLayerCount())});
            }

            GuiCreateFlatWorld.this.fontRendererObj.drawString(s1, p_148126_2_ + 2 + 213 - GuiCreateFlatWorld.this.fontRendererObj.getStringWidth(s1), p_148126_3_ + 3, 16777215);
        }

        protected int getScrollBarX()
        {
            return this.width - 70;
        }
    }
}