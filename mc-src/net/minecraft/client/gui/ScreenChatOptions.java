package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.settings.GameSettings;

@SideOnly(Side.CLIENT)
public class ScreenChatOptions extends GuiScreen
{
    private static final GameSettings.Options[] field_146399_a = new GameSettings.Options[] {GameSettings.Options.CHAT_VISIBILITY, GameSettings.Options.CHAT_COLOR, GameSettings.Options.CHAT_LINKS, GameSettings.Options.CHAT_OPACITY, GameSettings.Options.CHAT_LINKS_PROMPT, GameSettings.Options.CHAT_SCALE, GameSettings.Options.CHAT_HEIGHT_FOCUSED, GameSettings.Options.CHAT_HEIGHT_UNFOCUSED, GameSettings.Options.CHAT_WIDTH};
    private static final GameSettings.Options[] field_146395_f = new GameSettings.Options[] {GameSettings.Options.SHOW_CAPE};
    private final GuiScreen field_146396_g;
    private final GameSettings field_146400_h;
    private String field_146401_i;
    private String field_146398_r;
    private int field_146397_s;
    private static final String __OBFID = "CL_00000681";

    public ScreenChatOptions(GuiScreen p_i1023_1_, GameSettings p_i1023_2_)
    {
        this.field_146396_g = p_i1023_1_;
        this.field_146400_h = p_i1023_2_;
    }

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        int i = 0;
        this.field_146401_i = I18n.format("options.chat.title", new Object[0]);
        this.field_146398_r = I18n.format("options.multiplayer.title", new Object[0]);
        GameSettings.Options[] aoptions = field_146399_a;
        int j = aoptions.length;
        int k;
        GameSettings.Options options;

        for (k = 0; k < j; ++k)
        {
            options = aoptions[k];

            if (options.getEnumFloat())
            {
                this.buttonList.add(new GuiOptionSlider(options.returnEnumOrdinal(), this.width / 2 - 155 + i % 2 * 160, this.height / 6 + 24 * (i >> 1), options));
            }
            else
            {
                this.buttonList.add(new GuiOptionButton(options.returnEnumOrdinal(), this.width / 2 - 155 + i % 2 * 160, this.height / 6 + 24 * (i >> 1), options, this.field_146400_h.getKeyBinding(options)));
            }

            ++i;
        }

        if (i % 2 == 1)
        {
            ++i;
        }

        this.field_146397_s = this.height / 6 + 24 * (i >> 1);
        i += 2;
        aoptions = field_146395_f;
        j = aoptions.length;

        for (k = 0; k < j; ++k)
        {
            options = aoptions[k];

            if (options.getEnumFloat())
            {
                this.buttonList.add(new GuiOptionSlider(options.returnEnumOrdinal(), this.width / 2 - 155 + i % 2 * 160, this.height / 6 + 24 * (i >> 1), options));
            }
            else
            {
                this.buttonList.add(new GuiOptionButton(options.returnEnumOrdinal(), this.width / 2 - 155 + i % 2 * 160, this.height / 6 + 24 * (i >> 1), options, this.field_146400_h.getKeyBinding(options)));
            }

            ++i;
        }

        this.buttonList.add(new GuiButton(200, this.width / 2 - 100, this.height / 6 + 168, I18n.format("gui.done", new Object[0])));
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        if (p_146284_1_.enabled)
        {
            if (p_146284_1_.id < 100 && p_146284_1_ instanceof GuiOptionButton)
            {
                this.field_146400_h.setOptionValue(((GuiOptionButton)p_146284_1_).returnEnumOptions(), 1);
                p_146284_1_.displayString = this.field_146400_h.getKeyBinding(GameSettings.Options.getEnumOptions(p_146284_1_.id));
            }

            if (p_146284_1_.id == 200)
            {
                this.mc.gameSettings.saveOptions();
                this.mc.displayGuiScreen(this.field_146396_g);
            }
        }
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawDefaultBackground();
        this.drawCenteredString(this.fontRendererObj, this.field_146401_i, this.width / 2, 20, 16777215);
        this.drawCenteredString(this.fontRendererObj, this.field_146398_r, this.width / 2, this.field_146397_s + 7, 16777215);
        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }
}