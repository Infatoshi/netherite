package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.realms.RealmsScrolledSelectionList;

@SideOnly(Side.CLIENT)
public class GuiSlotRealmsProxy extends GuiSlot
{
    private final RealmsScrolledSelectionList field_154340_k;
    private static final String __OBFID = "CL_00001846";

    public GuiSlotRealmsProxy(RealmsScrolledSelectionList p_i1085_1_, int p_i1085_2_, int p_i1085_3_, int p_i1085_4_, int p_i1085_5_, int p_i1085_6_)
    {
        super(Minecraft.getMinecraft(), p_i1085_2_, p_i1085_3_, p_i1085_4_, p_i1085_5_, p_i1085_6_);
        this.field_154340_k = p_i1085_1_;
    }

    protected int getSize()
    {
        return this.field_154340_k.getItemCount();
    }

    /**
     * The element in the slot that was clicked, boolean for whether it was double clicked or not
     */
    protected void elementClicked(int p_148144_1_, boolean p_148144_2_, int p_148144_3_, int p_148144_4_)
    {
        this.field_154340_k.selectItem(p_148144_1_, p_148144_2_, p_148144_3_, p_148144_4_);
    }

    /**
     * Returns true if the element passed in is currently selected
     */
    protected boolean isSelected(int p_148131_1_)
    {
        return this.field_154340_k.isSelectedItem(p_148131_1_);
    }

    protected void drawBackground()
    {
        this.field_154340_k.renderBackground();
    }

    protected void drawSlot(int p_148126_1_, int p_148126_2_, int p_148126_3_, int p_148126_4_, Tessellator p_148126_5_, int p_148126_6_, int p_148126_7_)
    {
        this.field_154340_k.renderItem(p_148126_1_, p_148126_2_, p_148126_3_, p_148126_4_, p_148126_6_, p_148126_7_);
    }

    public int func_154338_k()
    {
        return super.width;
    }

    public int func_154339_l()
    {
        return super.mouseY;
    }

    public int func_154337_m()
    {
        return super.mouseX;
    }

    /**
     * Return the height of the content being scrolled
     */
    protected int getContentHeight()
    {
        return this.field_154340_k.getMaxPosition();
    }

    protected int getScrollBarX()
    {
        return this.field_154340_k.getScrollbarPosition();
    }
}