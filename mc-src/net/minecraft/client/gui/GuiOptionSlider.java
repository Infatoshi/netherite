package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.Minecraft;
import net.minecraft.client.settings.GameSettings;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class GuiOptionSlider extends GuiButton
{
    private float field_146134_p;
    public boolean field_146135_o;
    private GameSettings.Options field_146133_q;
    private final float field_146132_r;
    private final float field_146131_s;
    private static final String __OBFID = "CL_00000680";

    public GuiOptionSlider(int p_i45016_1_, int p_i45016_2_, int p_i45016_3_, GameSettings.Options p_i45016_4_)
    {
        this(p_i45016_1_, p_i45016_2_, p_i45016_3_, p_i45016_4_, 0.0F, 1.0F);
    }

    public GuiOptionSlider(int p_i45017_1_, int p_i45017_2_, int p_i45017_3_, GameSettings.Options p_i45017_4_, float p_i45017_5_, float p_i45017_6_)
    {
        super(p_i45017_1_, p_i45017_2_, p_i45017_3_, 150, 20, "");
        this.field_146134_p = 1.0F;
        this.field_146133_q = p_i45017_4_;
        this.field_146132_r = p_i45017_5_;
        this.field_146131_s = p_i45017_6_;
        Minecraft minecraft = Minecraft.getMinecraft();
        this.field_146134_p = p_i45017_4_.normalizeValue(minecraft.gameSettings.getOptionFloatValue(p_i45017_4_));
        this.displayString = minecraft.gameSettings.getKeyBinding(p_i45017_4_);
    }

    /**
     * Returns 0 if the button is disabled, 1 if the mouse is NOT hovering over this button and 2 if it IS hovering over
     * this button.
     */
    public int getHoverState(boolean p_146114_1_)
    {
        return 0;
    }

    /**
     * Fired when the mouse button is dragged. Equivalent of MouseListener.mouseDragged(MouseEvent e).
     */
    protected void mouseDragged(Minecraft p_146119_1_, int p_146119_2_, int p_146119_3_)
    {
        if (this.visible)
        {
            if (this.field_146135_o)
            {
                this.field_146134_p = (float)(p_146119_2_ - (this.xPosition + 4)) / (float)(this.width - 8);

                if (this.field_146134_p < 0.0F)
                {
                    this.field_146134_p = 0.0F;
                }

                if (this.field_146134_p > 1.0F)
                {
                    this.field_146134_p = 1.0F;
                }

                float f = this.field_146133_q.denormalizeValue(this.field_146134_p);
                p_146119_1_.gameSettings.setOptionFloatValue(this.field_146133_q, f);
                this.field_146134_p = this.field_146133_q.normalizeValue(f);
                this.displayString = p_146119_1_.gameSettings.getKeyBinding(this.field_146133_q);
            }

            GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
            this.drawTexturedModalRect(this.xPosition + (int)(this.field_146134_p * (float)(this.width - 8)), this.yPosition, 0, 66, 4, 20);
            this.drawTexturedModalRect(this.xPosition + (int)(this.field_146134_p * (float)(this.width - 8)) + 4, this.yPosition, 196, 66, 4, 20);
        }
    }

    /**
     * Returns true if the mouse has been pressed on this control. Equivalent of MouseListener.mousePressed(MouseEvent
     * e).
     */
    public boolean mousePressed(Minecraft p_146116_1_, int p_146116_2_, int p_146116_3_)
    {
        if (super.mousePressed(p_146116_1_, p_146116_2_, p_146116_3_))
        {
            this.field_146134_p = (float)(p_146116_2_ - (this.xPosition + 4)) / (float)(this.width - 8);

            if (this.field_146134_p < 0.0F)
            {
                this.field_146134_p = 0.0F;
            }

            if (this.field_146134_p > 1.0F)
            {
                this.field_146134_p = 1.0F;
            }

            p_146116_1_.gameSettings.setOptionFloatValue(this.field_146133_q, this.field_146133_q.denormalizeValue(this.field_146134_p));
            this.displayString = p_146116_1_.gameSettings.getKeyBinding(this.field_146133_q);
            this.field_146135_o = true;
            return true;
        }
        else
        {
            return false;
        }
    }

    /**
     * Fired when the mouse button is released. Equivalent of MouseListener.mouseReleased(MouseEvent e).
     */
    public void mouseReleased(int p_146118_1_, int p_146118_2_)
    {
        this.field_146135_o = false;
    }
}