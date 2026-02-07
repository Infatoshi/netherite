package net.minecraft.client.gui.stream;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.gui.GuiButton;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.GuiSlot;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.stream.IngestServerTester;
import net.minecraft.util.EnumChatFormatting;
import tv.twitch.broadcast.IngestServer;

@SideOnly(Side.CLIENT)
public class GuiIngestServers extends GuiScreen
{
    private final GuiScreen field_152309_a;
    private String field_152310_f;
    private GuiIngestServers.ServerList field_152311_g;
    private static final String __OBFID = "CL_00001843";

    public GuiIngestServers(GuiScreen p_i1077_1_)
    {
        this.field_152309_a = p_i1077_1_;
    }

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        this.field_152310_f = I18n.format("options.stream.ingest.title", new Object[0]);
        this.field_152311_g = new GuiIngestServers.ServerList();

        if (!this.mc.func_152346_Z().func_152908_z())
        {
            this.mc.func_152346_Z().func_152909_x();
        }

        this.buttonList.add(new GuiButton(1, this.width / 2 - 155, this.height - 24 - 6, 150, 20, I18n.format("gui.done", new Object[0])));
        this.buttonList.add(new GuiButton(2, this.width / 2 + 5, this.height - 24 - 6, 150, 20, I18n.format("options.stream.ingest.reset", new Object[0])));
    }

    /**
     * Called when the screen is unloaded. Used to disable keyboard repeat events
     */
    public void onGuiClosed()
    {
        if (this.mc.func_152346_Z().func_152908_z())
        {
            this.mc.func_152346_Z().func_152932_y().func_153039_l();
        }
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        if (p_146284_1_.enabled)
        {
            if (p_146284_1_.id == 1)
            {
                this.mc.displayGuiScreen(this.field_152309_a);
            }
            else
            {
                this.mc.gameSettings.field_152407_Q = "";
                this.mc.gameSettings.saveOptions();
            }
        }
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawDefaultBackground();
        this.field_152311_g.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
        this.drawCenteredString(this.fontRendererObj, this.field_152310_f, this.width / 2, 20, 16777215);
        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }

    @SideOnly(Side.CLIENT)
    class ServerList extends GuiSlot
    {
        private static final String __OBFID = "CL_00001842";

        public ServerList()
        {
            super(GuiIngestServers.this.mc, GuiIngestServers.this.width, GuiIngestServers.this.height, 32, GuiIngestServers.this.height - 35, (int)((double)GuiIngestServers.this.mc.fontRenderer.FONT_HEIGHT * 3.5D));
            this.setShowSelectionBox(false);
        }

        protected int getSize()
        {
            return GuiIngestServers.this.mc.func_152346_Z().func_152925_v().length;
        }

        /**
         * The element in the slot that was clicked, boolean for whether it was double clicked or not
         */
        protected void elementClicked(int p_148144_1_, boolean p_148144_2_, int p_148144_3_, int p_148144_4_)
        {
            GuiIngestServers.this.mc.gameSettings.field_152407_Q = GuiIngestServers.this.mc.func_152346_Z().func_152925_v()[p_148144_1_].serverUrl;
            GuiIngestServers.this.mc.gameSettings.saveOptions();
        }

        /**
         * Returns true if the element passed in is currently selected
         */
        protected boolean isSelected(int p_148131_1_)
        {
            return GuiIngestServers.this.mc.func_152346_Z().func_152925_v()[p_148131_1_].serverUrl.equals(GuiIngestServers.this.mc.gameSettings.field_152407_Q);
        }

        protected void drawBackground() {}

        protected void drawSlot(int p_148126_1_, int p_148126_2_, int p_148126_3_, int p_148126_4_, Tessellator p_148126_5_, int p_148126_6_, int p_148126_7_)
        {
            IngestServer ingestserver = GuiIngestServers.this.mc.func_152346_Z().func_152925_v()[p_148126_1_];
            String s = ingestserver.serverUrl.replaceAll("\\{stream_key\\}", "");
            String s1 = (int)ingestserver.bitrateKbps + " kbps";
            String s2 = null;
            IngestServerTester ingestservertester = GuiIngestServers.this.mc.func_152346_Z().func_152932_y();

            if (ingestservertester != null)
            {
                if (ingestserver == ingestservertester.func_153040_c())
                {
                    s = EnumChatFormatting.GREEN + s;
                    s1 = (int)(ingestservertester.func_153030_h() * 100.0F) + "%";
                }
                else if (p_148126_1_ < ingestservertester.func_153028_p())
                {
                    if (ingestserver.bitrateKbps == 0.0F)
                    {
                        s1 = EnumChatFormatting.RED + "Down!";
                    }
                }
                else
                {
                    s1 = EnumChatFormatting.OBFUSCATED + "1234" + EnumChatFormatting.RESET + " kbps";
                }
            }
            else if (ingestserver.bitrateKbps == 0.0F)
            {
                s1 = EnumChatFormatting.RED + "Down!";
            }

            p_148126_2_ -= 15;

            if (this.isSelected(p_148126_1_))
            {
                s2 = EnumChatFormatting.BLUE + "(Preferred)";
            }
            else if (ingestserver.defaultServer)
            {
                s2 = EnumChatFormatting.GREEN + "(Default)";
            }

            GuiIngestServers.this.drawString(GuiIngestServers.this.fontRendererObj, ingestserver.serverName, p_148126_2_ + 2, p_148126_3_ + 5, 16777215);
            GuiIngestServers.this.drawString(GuiIngestServers.this.fontRendererObj, s, p_148126_2_ + 2, p_148126_3_ + GuiIngestServers.this.fontRendererObj.FONT_HEIGHT + 5 + 3, 3158064);
            GuiIngestServers.this.drawString(GuiIngestServers.this.fontRendererObj, s1, this.getScrollBarX() - 5 - GuiIngestServers.this.fontRendererObj.getStringWidth(s1), p_148126_3_ + 5, 8421504);

            if (s2 != null)
            {
                GuiIngestServers.this.drawString(GuiIngestServers.this.fontRendererObj, s2, this.getScrollBarX() - 5 - GuiIngestServers.this.fontRendererObj.getStringWidth(s2), p_148126_3_ + 5 + 3 + GuiIngestServers.this.fontRendererObj.FONT_HEIGHT, 8421504);
            }
        }

        protected int getScrollBarX()
        {
            return super.getScrollBarX() + 15;
        }
    }
}