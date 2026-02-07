package net.minecraft.realms;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Iterator;
import java.util.List;
import net.minecraft.util.IChatComponent;

@SideOnly(Side.CLIENT)
public class DisconnectedOnlineScreen extends RealmsScreen
{
    private String title;
    private IChatComponent reason;
    private List lines;
    private final RealmsScreen parent;
    private static final String __OBFID = "CL_00001912";

    public DisconnectedOnlineScreen(RealmsScreen p_i1000_1_, String p_i1000_2_, IChatComponent p_i1000_3_)
    {
        this.parent = p_i1000_1_;
        this.title = getLocalizedString(p_i1000_2_);
        this.reason = p_i1000_3_;
    }

    public void init()
    {
        this.buttonsClear();
        this.buttonsAdd(newButton(0, this.width() / 2 - 100, this.height() / 4 + 120 + 12, getLocalizedString("gui.back")));
        this.lines = this.fontSplit(this.reason.getFormattedText(), this.width() - 50);
    }

    public void keyPressed(char p_keyPressed_1_, int p_keyPressed_2_)
    {
        if (p_keyPressed_2_ == 1)
        {
            Realms.setScreen(this.parent);
        }
    }

    public void buttonClicked(RealmsButton p_buttonClicked_1_)
    {
        if (p_buttonClicked_1_.id() == 0)
        {
            Realms.setScreen(this.parent);
        }
    }

    public void render(int p_render_1_, int p_render_2_, float p_render_3_)
    {
        this.renderBackground();
        this.drawCenteredString(this.title, this.width() / 2, this.height() / 2 - 50, 11184810);
        int k = this.height() / 2 - 30;

        if (this.lines != null)
        {
            for (Iterator iterator = this.lines.iterator(); iterator.hasNext(); k += this.fontLineHeight())
            {
                String s = (String)iterator.next();
                this.drawCenteredString(s, this.width() / 2, k, 16777215);
            }
        }

        super.render(p_render_1_, p_render_2_, p_render_3_);
    }
}