package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.multiplayer.ServerData;
import net.minecraft.client.resources.I18n;
import org.lwjgl.input.Keyboard;

@SideOnly(Side.CLIENT)
public class GuiScreenAddServer extends GuiScreen
{
    private final GuiScreen field_146310_a;
    private final ServerData field_146311_h;
    private GuiTextField field_146308_f;
    private GuiTextField field_146309_g;
    private GuiButton field_152176_i;
    private static final String __OBFID = "CL_00000695";

    public GuiScreenAddServer(GuiScreen p_i1033_1_, ServerData p_i1033_2_)
    {
        this.field_146310_a = p_i1033_1_;
        this.field_146311_h = p_i1033_2_;
    }

    /**
     * Called from the main game loop to update the screen.
     */
    public void updateScreen()
    {
        this.field_146309_g.updateCursorCounter();
        this.field_146308_f.updateCursorCounter();
    }

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        Keyboard.enableRepeatEvents(true);
        this.buttonList.clear();
        this.buttonList.add(new GuiButton(0, this.width / 2 - 100, this.height / 4 + 96 + 18, I18n.format("addServer.add", new Object[0])));
        this.buttonList.add(new GuiButton(1, this.width / 2 - 100, this.height / 4 + 120 + 18, I18n.format("gui.cancel", new Object[0])));
        this.buttonList.add(this.field_152176_i = new GuiButton(2, this.width / 2 - 100, this.height / 4 + 72, I18n.format("addServer.resourcePack", new Object[0]) + ": " + this.field_146311_h.func_152586_b().func_152589_a().getFormattedText()));
        this.field_146309_g = new GuiTextField(this.fontRendererObj, this.width / 2 - 100, 66, 200, 20);
        this.field_146309_g.setFocused(true);
        this.field_146309_g.setText(this.field_146311_h.serverName);
        this.field_146308_f = new GuiTextField(this.fontRendererObj, this.width / 2 - 100, 106, 200, 20);
        this.field_146308_f.setMaxStringLength(128);
        this.field_146308_f.setText(this.field_146311_h.serverIP);
        ((GuiButton)this.buttonList.get(0)).enabled = this.field_146308_f.getText().length() > 0 && this.field_146308_f.getText().split(":").length > 0 && this.field_146309_g.getText().length() > 0;
    }

    /**
     * Called when the screen is unloaded. Used to disable keyboard repeat events
     */
    public void onGuiClosed()
    {
        Keyboard.enableRepeatEvents(false);
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        if (p_146284_1_.enabled)
        {
            if (p_146284_1_.id == 2)
            {
                this.field_146311_h.func_152584_a(ServerData.ServerResourceMode.values()[(this.field_146311_h.func_152586_b().ordinal() + 1) % ServerData.ServerResourceMode.values().length]);
                this.field_152176_i.displayString = I18n.format("addServer.resourcePack", new Object[0]) + ": " + this.field_146311_h.func_152586_b().func_152589_a().getFormattedText();
            }
            else if (p_146284_1_.id == 1)
            {
                this.field_146310_a.confirmClicked(false, 0);
            }
            else if (p_146284_1_.id == 0)
            {
                this.field_146311_h.serverName = this.field_146309_g.getText();
                this.field_146311_h.serverIP = this.field_146308_f.getText();
                this.field_146310_a.confirmClicked(true, 0);
            }
        }
    }

    /**
     * Fired when a key is typed. This is the equivalent of KeyListener.keyTyped(KeyEvent e).
     */
    protected void keyTyped(char p_73869_1_, int p_73869_2_)
    {
        this.field_146309_g.textboxKeyTyped(p_73869_1_, p_73869_2_);
        this.field_146308_f.textboxKeyTyped(p_73869_1_, p_73869_2_);

        if (p_73869_2_ == 15)
        {
            this.field_146309_g.setFocused(!this.field_146309_g.isFocused());
            this.field_146308_f.setFocused(!this.field_146308_f.isFocused());
        }

        if (p_73869_2_ == 28 || p_73869_2_ == 156)
        {
            this.actionPerformed((GuiButton)this.buttonList.get(0));
        }

        ((GuiButton)this.buttonList.get(0)).enabled = this.field_146308_f.getText().length() > 0 && this.field_146308_f.getText().split(":").length > 0 && this.field_146309_g.getText().length() > 0;
    }

    /**
     * Called when the mouse is clicked.
     */
    protected void mouseClicked(int p_73864_1_, int p_73864_2_, int p_73864_3_)
    {
        super.mouseClicked(p_73864_1_, p_73864_2_, p_73864_3_);
        this.field_146308_f.mouseClicked(p_73864_1_, p_73864_2_, p_73864_3_);
        this.field_146309_g.mouseClicked(p_73864_1_, p_73864_2_, p_73864_3_);
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawDefaultBackground();
        this.drawCenteredString(this.fontRendererObj, I18n.format("addServer.title", new Object[0]), this.width / 2, 17, 16777215);
        this.drawString(this.fontRendererObj, I18n.format("addServer.enterName", new Object[0]), this.width / 2 - 100, 53, 10526880);
        this.drawString(this.fontRendererObj, I18n.format("addServer.enterIp", new Object[0]), this.width / 2 - 100, 94, 10526880);
        this.field_146309_g.drawTextBox();
        this.field_146308_f.drawTextBox();
        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }
}