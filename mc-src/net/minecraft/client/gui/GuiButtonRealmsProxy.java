package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.Minecraft;
import net.minecraft.realms.RealmsButton;

@SideOnly(Side.CLIENT)
public class GuiButtonRealmsProxy extends GuiButton
{
    private RealmsButton field_154318_o;
    private static final String __OBFID = "CL_00001848";

    public GuiButtonRealmsProxy(RealmsButton p_i1089_1_, int p_i1089_2_, int p_i1089_3_, int p_i1089_4_, String p_i1089_5_)
    {
        super(p_i1089_2_, p_i1089_3_, p_i1089_4_, p_i1089_5_);
        this.field_154318_o = p_i1089_1_;
    }

    public GuiButtonRealmsProxy(RealmsButton p_i1090_1_, int p_i1090_2_, int p_i1090_3_, int p_i1090_4_, String p_i1090_5_, int p_i1090_6_, int p_i1090_7_)
    {
        super(p_i1090_2_, p_i1090_3_, p_i1090_4_, p_i1090_6_, p_i1090_7_, p_i1090_5_);
        this.field_154318_o = p_i1090_1_;
    }

    public int func_154314_d()
    {
        return super.id;
    }

    public boolean func_154315_e()
    {
        return super.enabled;
    }

    public void func_154313_b(boolean p_154313_1_)
    {
        super.enabled = p_154313_1_;
    }

    public void func_154311_a(String p_154311_1_)
    {
        super.displayString = p_154311_1_;
    }

    public int getButtonWidth()
    {
        return super.getButtonWidth();
    }

    public int func_154316_f()
    {
        return super.yPosition;
    }

    /**
     * Returns true if the mouse has been pressed on this control. Equivalent of MouseListener.mousePressed(MouseEvent
     * e).
     */
    public boolean mousePressed(Minecraft p_146116_1_, int p_146116_2_, int p_146116_3_)
    {
        if (super.mousePressed(p_146116_1_, p_146116_2_, p_146116_3_))
        {
            this.field_154318_o.clicked(p_146116_2_, p_146116_3_);
        }

        return super.mousePressed(p_146116_1_, p_146116_2_, p_146116_3_);
    }

    /**
     * Fired when the mouse button is released. Equivalent of MouseListener.mouseReleased(MouseEvent e).
     */
    public void mouseReleased(int p_146118_1_, int p_146118_2_)
    {
        this.field_154318_o.released(p_146118_1_, p_146118_2_);
    }

    /**
     * Fired when the mouse button is dragged. Equivalent of MouseListener.mouseDragged(MouseEvent e).
     */
    public void mouseDragged(Minecraft p_146119_1_, int p_146119_2_, int p_146119_3_)
    {
        this.field_154318_o.renderBg(p_146119_2_, p_146119_3_);
    }

    public RealmsButton func_154317_g()
    {
        return this.field_154318_o;
    }

    /**
     * Returns 0 if the button is disabled, 1 if the mouse is NOT hovering over this button and 2 if it IS hovering over
     * this button.
     */
    public int getHoverState(boolean p_146114_1_)
    {
        return this.field_154318_o.getYImage(p_146114_1_);
    }

    public int func_154312_c(boolean p_154312_1_)
    {
        return super.getHoverState(p_154312_1_);
    }
}