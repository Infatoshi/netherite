package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.network.NetHandlerPlayClient;
import net.minecraft.client.resources.I18n;
import net.minecraft.network.play.client.C00PacketKeepAlive;

@SideOnly(Side.CLIENT)
public class GuiDownloadTerrain extends GuiScreen
{
    private NetHandlerPlayClient field_146594_a;
    private int field_146593_f;
    private static final String __OBFID = "CL_00000708";

    public GuiDownloadTerrain(NetHandlerPlayClient p_i45023_1_)
    {
        this.field_146594_a = p_i45023_1_;
    }

    /**
     * Fired when a key is typed. This is the equivalent of KeyListener.keyTyped(KeyEvent e).
     */
    protected void keyTyped(char p_73869_1_, int p_73869_2_) {}

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        this.buttonList.clear();
    }

    /**
     * Called from the main game loop to update the screen.
     */
    public void updateScreen()
    {
        ++this.field_146593_f;

        if (this.field_146593_f % 20 == 0)
        {
            this.field_146594_a.addToSendQueue(new C00PacketKeepAlive());
        }

        if (this.field_146594_a != null)
        {
            this.field_146594_a.onNetworkTick();
        }
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawBackground(0);
        this.drawCenteredString(this.fontRendererObj, I18n.format("multiplayer.downloadingTerrain", new Object[0]), this.width / 2, this.height / 2 - 50, 16777215);
        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }

    /**
     * Returns true if this GUI should pause the game when it is displayed in single-player
     */
    public boolean doesGuiPauseGame()
    {
        return false;
    }
}