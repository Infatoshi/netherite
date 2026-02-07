package net.minecraft.client.gui.stream;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.gui.GuiButton;
import net.minecraft.client.gui.GuiOptionButton;
import net.minecraft.client.gui.GuiOptionSlider;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.settings.GameSettings;
import net.minecraft.util.EnumChatFormatting;

@SideOnly(Side.CLIENT)
public class GuiStreamOptions extends GuiScreen
{
    private static final GameSettings.Options[] field_152312_a = new GameSettings.Options[] {GameSettings.Options.STREAM_BYTES_PER_PIXEL, GameSettings.Options.STREAM_FPS, GameSettings.Options.STREAM_KBPS, GameSettings.Options.STREAM_SEND_METADATA, GameSettings.Options.STREAM_VOLUME_MIC, GameSettings.Options.STREAM_VOLUME_SYSTEM, GameSettings.Options.STREAM_MIC_TOGGLE_BEHAVIOR, GameSettings.Options.STREAM_COMPRESSION};
    private static final GameSettings.Options[] field_152316_f = new GameSettings.Options[] {GameSettings.Options.STREAM_CHAT_ENABLED, GameSettings.Options.STREAM_CHAT_USER_FILTER};
    private final GuiScreen field_152317_g;
    private final GameSettings field_152318_h;
    private String field_152319_i;
    private String field_152313_r;
    private int field_152314_s;
    private boolean field_152315_t = false;
    private static final String __OBFID = "CL_00001841";

    public GuiStreamOptions(GuiScreen p_i1073_1_, GameSettings p_i1073_2_)
    {
        this.field_152317_g = p_i1073_1_;
        this.field_152318_h = p_i1073_2_;
    }

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        int i = 0;
        this.field_152319_i = I18n.format("options.stream.title", new Object[0]);
        this.field_152313_r = I18n.format("options.stream.chat.title", new Object[0]);
        GameSettings.Options[] aoptions = field_152312_a;
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
                this.buttonList.add(new GuiOptionButton(options.returnEnumOrdinal(), this.width / 2 - 155 + i % 2 * 160, this.height / 6 + 24 * (i >> 1), options, this.field_152318_h.getKeyBinding(options)));
            }

            ++i;
        }

        if (i % 2 == 1)
        {
            ++i;
        }

        this.field_152314_s = this.height / 6 + 24 * (i >> 1) + 6;
        i += 2;
        aoptions = field_152316_f;
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
                this.buttonList.add(new GuiOptionButton(options.returnEnumOrdinal(), this.width / 2 - 155 + i % 2 * 160, this.height / 6 + 24 * (i >> 1), options, this.field_152318_h.getKeyBinding(options)));
            }

            ++i;
        }

        this.buttonList.add(new GuiButton(200, this.width / 2 - 155, this.height / 6 + 168, 150, 20, I18n.format("gui.done", new Object[0])));
        GuiButton guibutton = new GuiButton(201, this.width / 2 + 5, this.height / 6 + 168, 150, 20, I18n.format("options.stream.ingestSelection", new Object[0]));
        guibutton.enabled = this.mc.func_152346_Z().func_152924_m() && this.mc.func_152346_Z().func_152925_v().length > 0 || this.mc.func_152346_Z().func_152908_z();
        this.buttonList.add(guibutton);
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        if (p_146284_1_.enabled)
        {
            if (p_146284_1_.id < 100 && p_146284_1_ instanceof GuiOptionButton)
            {
                GameSettings.Options options = ((GuiOptionButton)p_146284_1_).returnEnumOptions();
                this.field_152318_h.setOptionValue(options, 1);
                p_146284_1_.displayString = this.field_152318_h.getKeyBinding(GameSettings.Options.getEnumOptions(p_146284_1_.id));

                if (this.mc.func_152346_Z().func_152934_n() && options != GameSettings.Options.STREAM_CHAT_ENABLED && options != GameSettings.Options.STREAM_CHAT_USER_FILTER)
                {
                    this.field_152315_t = true;
                }
            }
            else if (p_146284_1_ instanceof GuiOptionSlider)
            {
                if (p_146284_1_.id == GameSettings.Options.STREAM_VOLUME_MIC.returnEnumOrdinal())
                {
                    this.mc.func_152346_Z().func_152915_s();
                }
                else if (p_146284_1_.id == GameSettings.Options.STREAM_VOLUME_SYSTEM.returnEnumOrdinal())
                {
                    this.mc.func_152346_Z().func_152915_s();
                }
                else if (this.mc.func_152346_Z().func_152934_n())
                {
                    this.field_152315_t = true;
                }
            }

            if (p_146284_1_.id == 200)
            {
                this.mc.gameSettings.saveOptions();
                this.mc.displayGuiScreen(this.field_152317_g);
            }
            else if (p_146284_1_.id == 201)
            {
                this.mc.gameSettings.saveOptions();
                this.mc.displayGuiScreen(new GuiIngestServers(this));
            }
        }
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawDefaultBackground();
        this.drawCenteredString(this.fontRendererObj, this.field_152319_i, this.width / 2, 20, 16777215);
        this.drawCenteredString(this.fontRendererObj, this.field_152313_r, this.width / 2, this.field_152314_s, 16777215);

        if (this.field_152315_t)
        {
            this.drawCenteredString(this.fontRendererObj, EnumChatFormatting.RED + I18n.format("options.stream.changes", new Object[0]), this.width / 2, 20 + this.fontRendererObj.FONT_HEIGHT, 16777215);
        }

        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }
}