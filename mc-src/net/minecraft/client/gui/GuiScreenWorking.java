package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.util.IProgressUpdate;

@SideOnly(Side.CLIENT)
public class GuiScreenWorking extends GuiScreen implements IProgressUpdate
{
    private String field_146591_a = "";
    private String field_146589_f = "";
    private int field_146590_g;
    private boolean field_146592_h;
    private static final String __OBFID = "CL_00000707";

    /**
     * "Saving level", or the loading,or downloading equivelent
     */
    public void displayProgressMessage(String p_73720_1_)
    {
        this.resetProgressAndMessage(p_73720_1_);
    }

    /**
     * this string, followed by "working..." and then the "% complete" are the 3 lines shown. This resets progress to 0,
     * and the WorkingString to "working...".
     */
    public void resetProgressAndMessage(String p_73721_1_)
    {
        this.field_146591_a = p_73721_1_;
        this.resetProgresAndWorkingMessage("Working...");
    }

    /**
     * This is called with "Working..." by resetProgressAndMessage
     */
    public void resetProgresAndWorkingMessage(String p_73719_1_)
    {
        this.field_146589_f = p_73719_1_;
        this.setLoadingProgress(0);
    }

    /**
     * Updates the progress bar on the loading screen to the specified amount. Args: loadProgress
     */
    public void setLoadingProgress(int p_73718_1_)
    {
        this.field_146590_g = p_73718_1_;
    }

    public void func_146586_a()
    {
        this.field_146592_h = true;
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        if (this.field_146592_h)
        {
            this.mc.displayGuiScreen((GuiScreen)null);
        }
        else
        {
            this.drawDefaultBackground();
            this.drawCenteredString(this.fontRendererObj, this.field_146591_a, this.width / 2, 70, 16777215);
            this.drawCenteredString(this.fontRendererObj, this.field_146589_f + " " + this.field_146590_g + "%", this.width / 2, 90, 16777215);
            super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
        }
    }
}