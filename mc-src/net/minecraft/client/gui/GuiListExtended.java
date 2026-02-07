package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.Tessellator;

@SideOnly(Side.CLIENT)
public abstract class GuiListExtended extends GuiSlot
{
    private static final String __OBFID = "CL_00000674";

    public GuiListExtended(Minecraft p_i45010_1_, int p_i45010_2_, int p_i45010_3_, int p_i45010_4_, int p_i45010_5_, int p_i45010_6_)
    {
        super(p_i45010_1_, p_i45010_2_, p_i45010_3_, p_i45010_4_, p_i45010_5_, p_i45010_6_);
    }

    /**
     * The element in the slot that was clicked, boolean for whether it was double clicked or not
     */
    protected void elementClicked(int p_148144_1_, boolean p_148144_2_, int p_148144_3_, int p_148144_4_) {}

    /**
     * Returns true if the element passed in is currently selected
     */
    protected boolean isSelected(int p_148131_1_)
    {
        return false;
    }

    protected void drawBackground() {}

    protected void drawSlot(int p_148126_1_, int p_148126_2_, int p_148126_3_, int p_148126_4_, Tessellator p_148126_5_, int p_148126_6_, int p_148126_7_)
    {
        this.getListEntry(p_148126_1_).drawEntry(p_148126_1_, p_148126_2_, p_148126_3_, this.getListWidth(), p_148126_4_, p_148126_5_, p_148126_6_, p_148126_7_, this.func_148124_c(p_148126_6_, p_148126_7_) == p_148126_1_);
    }

    public boolean func_148179_a(int p_148179_1_, int p_148179_2_, int p_148179_3_)
    {
        if (this.func_148141_e(p_148179_2_))
        {
            int l = this.func_148124_c(p_148179_1_, p_148179_2_);

            if (l >= 0)
            {
                int i1 = this.left + this.width / 2 - this.getListWidth() / 2 + 2;
                int j1 = this.top + 4 - this.getAmountScrolled() + l * this.slotHeight + this.headerPadding;
                int k1 = p_148179_1_ - i1;
                int l1 = p_148179_2_ - j1;

                if (this.getListEntry(l).mousePressed(l, p_148179_1_, p_148179_2_, p_148179_3_, k1, l1))
                {
                    this.func_148143_b(false);
                    return true;
                }
            }
        }

        return false;
    }

    public boolean func_148181_b(int p_148181_1_, int p_148181_2_, int p_148181_3_)
    {
        for (int l = 0; l < this.getSize(); ++l)
        {
            int i1 = this.left + this.width / 2 - this.getListWidth() / 2 + 2;
            int j1 = this.top + 4 - this.getAmountScrolled() + l * this.slotHeight + this.headerPadding;
            int k1 = p_148181_1_ - i1;
            int l1 = p_148181_2_ - j1;
            this.getListEntry(l).mouseReleased(l, p_148181_1_, p_148181_2_, p_148181_3_, k1, l1);
        }

        this.func_148143_b(true);
        return false;
    }

    /**
     * Gets the IGuiListEntry object for the given index
     */
    public abstract GuiListExtended.IGuiListEntry getListEntry(int p_148180_1_);

    @SideOnly(Side.CLIENT)
    public interface IGuiListEntry
    {
        void drawEntry(int p_148279_1_, int p_148279_2_, int p_148279_3_, int p_148279_4_, int p_148279_5_, Tessellator p_148279_6_, int p_148279_7_, int p_148279_8_, boolean p_148279_9_);

        /**
         * Returns true if the mouse has been pressed on this control.
         */
        boolean mousePressed(int p_148278_1_, int p_148278_2_, int p_148278_3_, int p_148278_4_, int p_148278_5_, int p_148278_6_);

        /**
         * Fired when the mouse button is released. Arguments: index, x, y, mouseEvent, relativeX, relativeY
         */
        void mouseReleased(int p_148277_1_, int p_148277_2_, int p_148277_3_, int p_148277_4_, int p_148277_5_, int p_148277_6_);
    }
}