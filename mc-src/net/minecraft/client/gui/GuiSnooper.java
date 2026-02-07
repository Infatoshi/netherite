package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.TreeMap;
import java.util.Map.Entry;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.settings.GameSettings;

@SideOnly(Side.CLIENT)
public class GuiSnooper extends GuiScreen
{
    private final GuiScreen field_146608_a;
    private final GameSettings field_146603_f;
    private final java.util.List field_146604_g = new ArrayList();
    private final java.util.List field_146609_h = new ArrayList();
    private String field_146610_i;
    private String[] field_146607_r;
    private GuiSnooper.List field_146606_s;
    private GuiButton field_146605_t;
    private static final String __OBFID = "CL_00000714";

    public GuiSnooper(GuiScreen p_i1061_1_, GameSettings p_i1061_2_)
    {
        this.field_146608_a = p_i1061_1_;
        this.field_146603_f = p_i1061_2_;
    }

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        this.field_146610_i = I18n.format("options.snooper.title", new Object[0]);
        String s = I18n.format("options.snooper.desc", new Object[0]);
        ArrayList arraylist = new ArrayList();
        Iterator iterator = this.fontRendererObj.listFormattedStringToWidth(s, this.width - 30).iterator();

        while (iterator.hasNext())
        {
            String s1 = (String)iterator.next();
            arraylist.add(s1);
        }

        this.field_146607_r = (String[])arraylist.toArray(new String[0]);
        this.field_146604_g.clear();
        this.field_146609_h.clear();
        this.buttonList.add(this.field_146605_t = new GuiButton(1, this.width / 2 - 152, this.height - 30, 150, 20, this.field_146603_f.getKeyBinding(GameSettings.Options.SNOOPER_ENABLED)));
        this.buttonList.add(new GuiButton(2, this.width / 2 + 2, this.height - 30, 150, 20, I18n.format("gui.done", new Object[0])));
        boolean flag = this.mc.getIntegratedServer() != null && this.mc.getIntegratedServer().getPlayerUsageSnooper() != null;
        Iterator iterator1 = (new TreeMap(this.mc.getPlayerUsageSnooper().getCurrentStats())).entrySet().iterator();
        Entry entry;

        while (iterator1.hasNext())
        {
            entry = (Entry)iterator1.next();
            this.field_146604_g.add((flag ? "C " : "") + (String)entry.getKey());
            this.field_146609_h.add(this.fontRendererObj.trimStringToWidth((String)entry.getValue(), this.width - 220));
        }

        if (flag)
        {
            iterator1 = (new TreeMap(this.mc.getIntegratedServer().getPlayerUsageSnooper().getCurrentStats())).entrySet().iterator();

            while (iterator1.hasNext())
            {
                entry = (Entry)iterator1.next();
                this.field_146604_g.add("S " + (String)entry.getKey());
                this.field_146609_h.add(this.fontRendererObj.trimStringToWidth((String)entry.getValue(), this.width - 220));
            }
        }

        this.field_146606_s = new GuiSnooper.List();
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        if (p_146284_1_.enabled)
        {
            if (p_146284_1_.id == 2)
            {
                this.field_146603_f.saveOptions();
                this.field_146603_f.saveOptions();
                this.mc.displayGuiScreen(this.field_146608_a);
            }

            if (p_146284_1_.id == 1)
            {
                this.field_146603_f.setOptionValue(GameSettings.Options.SNOOPER_ENABLED, 1);
                this.field_146605_t.displayString = this.field_146603_f.getKeyBinding(GameSettings.Options.SNOOPER_ENABLED);
            }
        }
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawDefaultBackground();
        this.field_146606_s.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
        this.drawCenteredString(this.fontRendererObj, this.field_146610_i, this.width / 2, 8, 16777215);
        int k = 22;
        String[] astring = this.field_146607_r;
        int l = astring.length;

        for (int i1 = 0; i1 < l; ++i1)
        {
            String s = astring[i1];
            this.drawCenteredString(this.fontRendererObj, s, this.width / 2, k, 8421504);
            k += this.fontRendererObj.FONT_HEIGHT;
        }

        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }

    @SideOnly(Side.CLIENT)
    class List extends GuiSlot
    {
        private static final String __OBFID = "CL_00000715";

        public List()
        {
            super(GuiSnooper.this.mc, GuiSnooper.this.width, GuiSnooper.this.height, 80, GuiSnooper.this.height - 40, GuiSnooper.this.fontRendererObj.FONT_HEIGHT + 1);
        }

        protected int getSize()
        {
            return GuiSnooper.this.field_146604_g.size();
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
            GuiSnooper.this.fontRendererObj.drawString((String)GuiSnooper.this.field_146604_g.get(p_148126_1_), 10, p_148126_3_, 16777215);
            GuiSnooper.this.fontRendererObj.drawString((String)GuiSnooper.this.field_146609_h.get(p_148126_1_), 230, p_148126_3_, 16777215);
        }

        protected int getScrollBarX()
        {
            return this.width - 10;
        }
    }
}