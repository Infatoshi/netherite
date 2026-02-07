package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.resources.ResourcePackListEntry;
import net.minecraft.util.EnumChatFormatting;

@SideOnly(Side.CLIENT)
public abstract class GuiResourcePackList extends GuiListExtended
{
    protected final Minecraft field_148205_k;
    protected final List field_148204_l;
    private static final String __OBFID = "CL_00000825";

    public GuiResourcePackList(Minecraft p_i45055_1_, int p_i45055_2_, int p_i45055_3_, List p_i45055_4_)
    {
        super(p_i45055_1_, p_i45055_2_, p_i45055_3_, 32, p_i45055_3_ - 55 + 4, 36);
        this.field_148205_k = p_i45055_1_;
        this.field_148204_l = p_i45055_4_;
        this.field_148163_i = false;
        this.setHasListHeader(true, (int)((float)p_i45055_1_.fontRenderer.FONT_HEIGHT * 1.5F));
    }

    /**
     * Handles drawing a list's header row.
     */
    protected void drawListHeader(int p_148129_1_, int p_148129_2_, Tessellator p_148129_3_)
    {
        String s = EnumChatFormatting.UNDERLINE + "" + EnumChatFormatting.BOLD + this.func_148202_k();
        this.field_148205_k.fontRenderer.drawString(s, p_148129_1_ + this.width / 2 - this.field_148205_k.fontRenderer.getStringWidth(s) / 2, Math.min(this.top + 3, p_148129_2_), 16777215);
    }

    protected abstract String func_148202_k();

    public List func_148201_l()
    {
        return this.field_148204_l;
    }

    protected int getSize()
    {
        return this.func_148201_l().size();
    }

    /**
     * Gets the IGuiListEntry object for the given index
     */
    public ResourcePackListEntry getListEntry(int p_148180_1_)
    {
        return (ResourcePackListEntry)this.func_148201_l().get(p_148180_1_);
    }

    /**
     * Gets the width of the list
     */
    public int getListWidth()
    {
        return this.width;
    }

    protected int getScrollBarX()
    {
        return this.right - 6;
    }
}