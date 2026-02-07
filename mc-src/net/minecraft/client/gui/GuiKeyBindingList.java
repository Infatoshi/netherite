package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Arrays;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.settings.GameSettings;
import net.minecraft.client.settings.KeyBinding;
import net.minecraft.util.EnumChatFormatting;
import org.apache.commons.lang3.ArrayUtils;

@SideOnly(Side.CLIENT)
public class GuiKeyBindingList extends GuiListExtended
{
    private final GuiControls field_148191_k;
    private final Minecraft mc;
    private final GuiListExtended.IGuiListEntry[] field_148190_m;
    private int field_148188_n = 0;
    private static final String __OBFID = "CL_00000732";

    public GuiKeyBindingList(GuiControls p_i45031_1_, Minecraft p_i45031_2_)
    {
        super(p_i45031_2_, p_i45031_1_.width, p_i45031_1_.height, 63, p_i45031_1_.height - 32, 20);
        this.field_148191_k = p_i45031_1_;
        this.mc = p_i45031_2_;
        KeyBinding[] akeybinding = (KeyBinding[])ArrayUtils.clone(p_i45031_2_.gameSettings.keyBindings);
        this.field_148190_m = new GuiListExtended.IGuiListEntry[akeybinding.length + KeyBinding.getKeybinds().size()];
        Arrays.sort(akeybinding);
        int i = 0;
        String s = null;
        KeyBinding[] akeybinding1 = akeybinding;
        int j = akeybinding.length;

        for (int k = 0; k < j; ++k)
        {
            KeyBinding keybinding = akeybinding1[k];
            String s1 = keybinding.getKeyCategory();

            if (!s1.equals(s))
            {
                s = s1;
                this.field_148190_m[i++] = new GuiKeyBindingList.CategoryEntry(s1);
            }

            int l = p_i45031_2_.fontRenderer.getStringWidth(I18n.format(keybinding.getKeyDescription(), new Object[0]));

            if (l > this.field_148188_n)
            {
                this.field_148188_n = l;
            }

            this.field_148190_m[i++] = new GuiKeyBindingList.KeyEntry(keybinding, null);
        }
    }

    protected int getSize()
    {
        return this.field_148190_m.length;
    }

    /**
     * Gets the IGuiListEntry object for the given index
     */
    public GuiListExtended.IGuiListEntry getListEntry(int p_148180_1_)
    {
        return this.field_148190_m[p_148180_1_];
    }

    protected int getScrollBarX()
    {
        return super.getScrollBarX() + 15;
    }

    /**
     * Gets the width of the list
     */
    public int getListWidth()
    {
        return super.getListWidth() + 32;
    }

    @SideOnly(Side.CLIENT)
    public class CategoryEntry implements GuiListExtended.IGuiListEntry
    {
        private final String field_148285_b;
        private final int field_148286_c;
        private static final String __OBFID = "CL_00000734";

        public CategoryEntry(String p_i45028_2_)
        {
            this.field_148285_b = I18n.format(p_i45028_2_, new Object[0]);
            this.field_148286_c = GuiKeyBindingList.this.mc.fontRenderer.getStringWidth(this.field_148285_b);
        }

        public void drawEntry(int p_148279_1_, int p_148279_2_, int p_148279_3_, int p_148279_4_, int p_148279_5_, Tessellator p_148279_6_, int p_148279_7_, int p_148279_8_, boolean p_148279_9_)
        {
            GuiKeyBindingList.this.mc.fontRenderer.drawString(this.field_148285_b, GuiKeyBindingList.this.mc.currentScreen.width / 2 - this.field_148286_c / 2, p_148279_3_ + p_148279_5_ - GuiKeyBindingList.this.mc.fontRenderer.FONT_HEIGHT - 1, 16777215);
        }

        /**
         * Returns true if the mouse has been pressed on this control.
         */
        public boolean mousePressed(int p_148278_1_, int p_148278_2_, int p_148278_3_, int p_148278_4_, int p_148278_5_, int p_148278_6_)
        {
            return false;
        }

        /**
         * Fired when the mouse button is released. Arguments: index, x, y, mouseEvent, relativeX, relativeY
         */
        public void mouseReleased(int p_148277_1_, int p_148277_2_, int p_148277_3_, int p_148277_4_, int p_148277_5_, int p_148277_6_) {}
    }

    @SideOnly(Side.CLIENT)
    public class KeyEntry implements GuiListExtended.IGuiListEntry
    {
        private final KeyBinding field_148282_b;
        private final String field_148283_c;
        private final GuiButton btnChangeKeyBinding;
        private final GuiButton btnReset;
        private static final String __OBFID = "CL_00000735";

        private KeyEntry(KeyBinding p_i45029_2_)
        {
            this.field_148282_b = p_i45029_2_;
            this.field_148283_c = I18n.format(p_i45029_2_.getKeyDescription(), new Object[0]);
            this.btnChangeKeyBinding = new GuiButton(0, 0, 0, 75, 18, I18n.format(p_i45029_2_.getKeyDescription(), new Object[0]));
            this.btnReset = new GuiButton(0, 0, 0, 50, 18, I18n.format("controls.reset", new Object[0]));
        }

        public void drawEntry(int p_148279_1_, int p_148279_2_, int p_148279_3_, int p_148279_4_, int p_148279_5_, Tessellator p_148279_6_, int p_148279_7_, int p_148279_8_, boolean p_148279_9_)
        {
            boolean flag1 = GuiKeyBindingList.this.field_148191_k.buttonId == this.field_148282_b;
            GuiKeyBindingList.this.mc.fontRenderer.drawString(this.field_148283_c, p_148279_2_ + 90 - GuiKeyBindingList.this.field_148188_n, p_148279_3_ + p_148279_5_ / 2 - GuiKeyBindingList.this.mc.fontRenderer.FONT_HEIGHT / 2, 16777215);
            this.btnReset.xPosition = p_148279_2_ + 190;
            this.btnReset.yPosition = p_148279_3_;
            this.btnReset.enabled = this.field_148282_b.getKeyCode() != this.field_148282_b.getKeyCodeDefault();
            this.btnReset.drawButton(GuiKeyBindingList.this.mc, p_148279_7_, p_148279_8_);
            this.btnChangeKeyBinding.xPosition = p_148279_2_ + 105;
            this.btnChangeKeyBinding.yPosition = p_148279_3_;
            this.btnChangeKeyBinding.displayString = GameSettings.getKeyDisplayString(this.field_148282_b.getKeyCode());
            boolean flag2 = false;

            if (this.field_148282_b.getKeyCode() != 0)
            {
                KeyBinding[] akeybinding = GuiKeyBindingList.this.mc.gameSettings.keyBindings;
                int l1 = akeybinding.length;

                for (int i2 = 0; i2 < l1; ++i2)
                {
                    KeyBinding keybinding = akeybinding[i2];

                    if (keybinding != this.field_148282_b && keybinding.getKeyCode() == this.field_148282_b.getKeyCode())
                    {
                        flag2 = true;
                        break;
                    }
                }
            }

            if (flag1)
            {
                this.btnChangeKeyBinding.displayString = EnumChatFormatting.WHITE + "> " + EnumChatFormatting.YELLOW + this.btnChangeKeyBinding.displayString + EnumChatFormatting.WHITE + " <";
            }
            else if (flag2)
            {
                this.btnChangeKeyBinding.displayString = EnumChatFormatting.RED + this.btnChangeKeyBinding.displayString;
            }

            this.btnChangeKeyBinding.drawButton(GuiKeyBindingList.this.mc, p_148279_7_, p_148279_8_);
        }

        /**
         * Returns true if the mouse has been pressed on this control.
         */
        public boolean mousePressed(int p_148278_1_, int p_148278_2_, int p_148278_3_, int p_148278_4_, int p_148278_5_, int p_148278_6_)
        {
            if (this.btnChangeKeyBinding.mousePressed(GuiKeyBindingList.this.mc, p_148278_2_, p_148278_3_))
            {
                GuiKeyBindingList.this.field_148191_k.buttonId = this.field_148282_b;
                return true;
            }
            else if (this.btnReset.mousePressed(GuiKeyBindingList.this.mc, p_148278_2_, p_148278_3_))
            {
                GuiKeyBindingList.this.mc.gameSettings.setOptionKeyBinding(this.field_148282_b, this.field_148282_b.getKeyCodeDefault());
                KeyBinding.resetKeyBindingArrayAndHash();
                return true;
            }
            else
            {
                return false;
            }
        }

        /**
         * Fired when the mouse button is released. Arguments: index, x, y, mouseEvent, relativeX, relativeY
         */
        public void mouseReleased(int p_148277_1_, int p_148277_2_, int p_148277_3_, int p_148277_4_, int p_148277_5_, int p_148277_6_)
        {
            this.btnChangeKeyBinding.mouseReleased(p_148277_2_, p_148277_3_);
            this.btnReset.mouseReleased(p_148277_2_, p_148277_3_);
        }

        KeyEntry(KeyBinding p_i45030_2_, Object p_i45030_3_)
        {
            this(p_i45030_2_);
        }
    }
}