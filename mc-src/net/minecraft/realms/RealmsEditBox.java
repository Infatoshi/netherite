package net.minecraft.realms;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.gui.Gui;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.util.ChatAllowedCharacters;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class RealmsEditBox
{
    public static final int BACKWARDS = -1;
    public static final int FORWARDS = 1;
    private static final int CURSOR_INSERT_WIDTH = 1;
    private static final int CURSOR_INSERT_COLOR = -3092272;
    private static final String CURSOR_APPEND_CHARACTER = "_";
    private final FontRenderer font;
    private final int x;
    private final int y;
    private final int width;
    private final int height;
    private String value;
    private int maxLength;
    private int frame;
    private boolean bordered;
    private boolean canLoseFocus;
    private boolean inFocus;
    private boolean isEditable;
    private int displayPos;
    private int cursorPos;
    private int highlightPos;
    private int textColor;
    private int textColorUneditable;
    private boolean visible;
    private static final String __OBFID = "CL_00001858";

    public RealmsEditBox(int p_i1111_1_, int p_i1111_2_, int p_i1111_3_, int p_i1111_4_)
    {
        this(Minecraft.getMinecraft().fontRenderer, p_i1111_1_, p_i1111_2_, p_i1111_3_, p_i1111_4_);
    }

    public RealmsEditBox(FontRenderer p_i1112_1_, int p_i1112_2_, int p_i1112_3_, int p_i1112_4_, int p_i1112_5_)
    {
        this.value = "";
        this.maxLength = 32;
        this.bordered = true;
        this.canLoseFocus = true;
        this.isEditable = true;
        this.textColor = 14737632;
        this.textColorUneditable = 7368816;
        this.visible = true;
        this.font = p_i1112_1_;
        this.x = p_i1112_2_;
        this.y = p_i1112_3_;
        this.width = p_i1112_4_;
        this.height = p_i1112_5_;
    }

    public void tick()
    {
        ++this.frame;
    }

    public void setValue(String p_setValue_1_)
    {
        if (p_setValue_1_.length() > this.maxLength)
        {
            this.value = p_setValue_1_.substring(0, this.maxLength);
        }
        else
        {
            this.value = p_setValue_1_;
        }

        this.moveCursorToEnd();
    }

    public String getValue()
    {
        return this.value;
    }

    public String getHighlighted()
    {
        int i = this.cursorPos < this.highlightPos ? this.cursorPos : this.highlightPos;
        int j = this.cursorPos < this.highlightPos ? this.highlightPos : this.cursorPos;
        return this.value.substring(i, j);
    }

    public void insertText(String p_insertText_1_)
    {
        String s1 = "";
        String s2 = ChatAllowedCharacters.filerAllowedCharacters(p_insertText_1_);
        int i = this.cursorPos < this.highlightPos ? this.cursorPos : this.highlightPos;
        int j = this.cursorPos < this.highlightPos ? this.highlightPos : this.cursorPos;
        int k = this.maxLength - this.value.length() - (i - this.highlightPos);
        boolean flag = false;

        if (this.value.length() > 0)
        {
            s1 = s1 + this.value.substring(0, i);
        }

        int l;

        if (k < s2.length())
        {
            s1 = s1 + s2.substring(0, k);
            l = k;
        }
        else
        {
            s1 = s1 + s2;
            l = s2.length();
        }

        if (this.value.length() > 0 && j < this.value.length())
        {
            s1 = s1 + this.value.substring(j);
        }

        this.value = s1;
        this.moveCursor(i - this.highlightPos + l);
    }

    public void deleteWords(int p_deleteWords_1_)
    {
        if (this.value.length() != 0)
        {
            if (this.highlightPos != this.cursorPos)
            {
                this.insertText("");
            }
            else
            {
                this.deleteChars(this.getWordPosition(p_deleteWords_1_) - this.cursorPos);
            }
        }
    }

    public void deleteChars(int p_deleteChars_1_)
    {
        if (this.value.length() != 0)
        {
            if (this.highlightPos != this.cursorPos)
            {
                this.insertText("");
            }
            else
            {
                boolean flag = p_deleteChars_1_ < 0;
                int j = flag ? this.cursorPos + p_deleteChars_1_ : this.cursorPos;
                int k = flag ? this.cursorPos : this.cursorPos + p_deleteChars_1_;
                String s = "";

                if (j >= 0)
                {
                    s = this.value.substring(0, j);
                }

                if (k < this.value.length())
                {
                    s = s + this.value.substring(k);
                }

                this.value = s;

                if (flag)
                {
                    this.moveCursor(p_deleteChars_1_);
                }
            }
        }
    }

    public int getWordPosition(int p_getWordPosition_1_)
    {
        return this.getWordPosition(p_getWordPosition_1_, this.getCursorPosition());
    }

    public int getWordPosition(int p_getWordPosition_1_, int p_getWordPosition_2_)
    {
        return this.getWordPosition(p_getWordPosition_1_, this.getCursorPosition(), true);
    }

    public int getWordPosition(int p_getWordPosition_1_, int p_getWordPosition_2_, boolean p_getWordPosition_3_)
    {
        int k = p_getWordPosition_2_;
        boolean flag1 = p_getWordPosition_1_ < 0;
        int l = Math.abs(p_getWordPosition_1_);

        for (int i1 = 0; i1 < l; ++i1)
        {
            if (flag1)
            {
                while (p_getWordPosition_3_ && k > 0 && this.value.charAt(k - 1) == 32)
                {
                    --k;
                }

                while (k > 0 && this.value.charAt(k - 1) != 32)
                {
                    --k;
                }
            }
            else
            {
                int j1 = this.value.length();
                k = this.value.indexOf(32, k);

                if (k == -1)
                {
                    k = j1;
                }
                else
                {
                    while (p_getWordPosition_3_ && k < j1 && this.value.charAt(k) == 32)
                    {
                        ++k;
                    }
                }
            }
        }

        return k;
    }

    public void moveCursor(int p_moveCursor_1_)
    {
        this.moveCursorTo(this.highlightPos + p_moveCursor_1_);
    }

    public void moveCursorTo(int p_moveCursorTo_1_)
    {
        this.cursorPos = p_moveCursorTo_1_;
        int j = this.value.length();

        if (this.cursorPos < 0)
        {
            this.cursorPos = 0;
        }

        if (this.cursorPos > j)
        {
            this.cursorPos = j;
        }

        this.setHighlightPos(this.cursorPos);
    }

    public void moveCursorToStart()
    {
        this.moveCursorTo(0);
    }

    public void moveCursorToEnd()
    {
        this.moveCursorTo(this.value.length());
    }

    public boolean keyPressed(char p_keyPressed_1_, int p_keyPressed_2_)
    {
        if (!this.inFocus)
        {
            return false;
        }
        else
        {
            switch (p_keyPressed_1_)
            {
                case 1:
                    this.moveCursorToEnd();
                    this.setHighlightPos(0);
                    return true;
                case 3:
                    GuiScreen.setClipboardString(this.getHighlighted());
                    return true;
                case 22:
                    if (this.isEditable)
                    {
                        this.insertText(GuiScreen.getClipboardString());
                    }

                    return true;
                case 24:
                    GuiScreen.setClipboardString(this.getHighlighted());

                    if (this.isEditable)
                    {
                        this.insertText("");
                    }

                    return true;
                default:
                    switch (p_keyPressed_2_)
                    {
                        case 14:
                            if (GuiScreen.isCtrlKeyDown())
                            {
                                if (this.isEditable)
                                {
                                    this.deleteWords(-1);
                                }
                            }
                            else if (this.isEditable)
                            {
                                this.deleteChars(-1);
                            }

                            return true;
                        case 199:
                            if (GuiScreen.isShiftKeyDown())
                            {
                                this.setHighlightPos(0);
                            }
                            else
                            {
                                this.moveCursorToStart();
                            }

                            return true;
                        case 203:
                            if (GuiScreen.isShiftKeyDown())
                            {
                                if (GuiScreen.isCtrlKeyDown())
                                {
                                    this.setHighlightPos(this.getWordPosition(-1, this.getHighlightPos()));
                                }
                                else
                                {
                                    this.setHighlightPos(this.getHighlightPos() - 1);
                                }
                            }
                            else if (GuiScreen.isCtrlKeyDown())
                            {
                                this.moveCursorTo(this.getWordPosition(-1));
                            }
                            else
                            {
                                this.moveCursor(-1);
                            }

                            return true;
                        case 205:
                            if (GuiScreen.isShiftKeyDown())
                            {
                                if (GuiScreen.isCtrlKeyDown())
                                {
                                    this.setHighlightPos(this.getWordPosition(1, this.getHighlightPos()));
                                }
                                else
                                {
                                    this.setHighlightPos(this.getHighlightPos() + 1);
                                }
                            }
                            else if (GuiScreen.isCtrlKeyDown())
                            {
                                this.moveCursorTo(this.getWordPosition(1));
                            }
                            else
                            {
                                this.moveCursor(1);
                            }

                            return true;
                        case 207:
                            if (GuiScreen.isShiftKeyDown())
                            {
                                this.setHighlightPos(this.value.length());
                            }
                            else
                            {
                                this.moveCursorToEnd();
                            }

                            return true;
                        case 211:
                            if (GuiScreen.isCtrlKeyDown())
                            {
                                if (this.isEditable)
                                {
                                    this.deleteWords(1);
                                }
                            }
                            else if (this.isEditable)
                            {
                                this.deleteChars(1);
                            }

                            return true;
                        default:
                            if (ChatAllowedCharacters.isAllowedCharacter(p_keyPressed_1_))
                            {
                                if (this.isEditable)
                                {
                                    this.insertText(Character.toString(p_keyPressed_1_));
                                }

                                return true;
                            }
                            else
                            {
                                return false;
                            }
                    }
            }
        }
    }

    public void mouseClicked(int p_mouseClicked_1_, int p_mouseClicked_2_, int p_mouseClicked_3_)
    {
        boolean flag = p_mouseClicked_1_ >= this.x && p_mouseClicked_1_ < this.x + this.width && p_mouseClicked_2_ >= this.y && p_mouseClicked_2_ < this.y + this.height;

        if (this.canLoseFocus)
        {
            this.setFocus(flag);
        }

        if (this.inFocus && p_mouseClicked_3_ == 0)
        {
            int l = p_mouseClicked_1_ - this.x;

            if (this.bordered)
            {
                l -= 4;
            }

            String s = this.font.trimStringToWidth(this.value.substring(this.displayPos), this.getInnerWidth());
            this.moveCursorTo(this.font.trimStringToWidth(s, l).length() + this.displayPos);
        }
    }

    public void render()
    {
        if (this.isVisible())
        {
            if (this.isBordered())
            {
                Gui.drawRect(this.x - 1, this.y - 1, this.x + this.width + 1, this.y + this.height + 1, -6250336);
                Gui.drawRect(this.x, this.y, this.x + this.width, this.y + this.height, -16777216);
            }

            int i = this.isEditable ? this.textColor : this.textColorUneditable;
            int j = this.cursorPos - this.displayPos;
            int k = this.highlightPos - this.displayPos;
            String s = this.font.trimStringToWidth(this.value.substring(this.displayPos), this.getInnerWidth());
            boolean flag = j >= 0 && j <= s.length();
            boolean flag1 = this.inFocus && this.frame / 6 % 2 == 0 && flag;
            int l = this.bordered ? this.x + 4 : this.x;
            int i1 = this.bordered ? this.y + (this.height - 8) / 2 : this.y;
            int j1 = l;

            if (k > s.length())
            {
                k = s.length();
            }

            if (s.length() > 0)
            {
                String s1 = flag ? s.substring(0, j) : s;
                j1 = this.font.drawStringWithShadow(s1, l, i1, i);
            }

            boolean flag2 = this.cursorPos < this.value.length() || this.value.length() >= this.getMaxLength();
            int k1 = j1;

            if (!flag)
            {
                k1 = j > 0 ? l + this.width : l;
            }
            else if (flag2)
            {
                k1 = j1 - 1;
                --j1;
            }

            if (s.length() > 0 && flag && j < s.length())
            {
                this.font.drawStringWithShadow(s.substring(j), j1, i1, i);
            }

            if (flag1)
            {
                if (flag2)
                {
                    Gui.drawRect(k1, i1 - 1, k1 + 1, i1 + 1 + this.font.FONT_HEIGHT, -3092272);
                }
                else
                {
                    this.font.drawStringWithShadow("_", k1, i1, i);
                }
            }

            if (k != j)
            {
                int l1 = l + this.font.getStringWidth(s.substring(0, k));
                this.renderHighlight(k1, i1 - 1, l1 - 1, i1 + 1 + this.font.FONT_HEIGHT);
            }
        }
    }

    private void renderHighlight(int p_renderHighlight_1_, int p_renderHighlight_2_, int p_renderHighlight_3_, int p_renderHighlight_4_)
    {
        int i1;

        if (p_renderHighlight_1_ < p_renderHighlight_3_)
        {
            i1 = p_renderHighlight_1_;
            p_renderHighlight_1_ = p_renderHighlight_3_;
            p_renderHighlight_3_ = i1;
        }

        if (p_renderHighlight_2_ < p_renderHighlight_4_)
        {
            i1 = p_renderHighlight_2_;
            p_renderHighlight_2_ = p_renderHighlight_4_;
            p_renderHighlight_4_ = i1;
        }

        if (p_renderHighlight_3_ > this.x + this.width)
        {
            p_renderHighlight_3_ = this.x + this.width;
        }

        if (p_renderHighlight_1_ > this.x + this.width)
        {
            p_renderHighlight_1_ = this.x + this.width;
        }

        Tessellator tessellator = Tessellator.instance;
        GL11.glColor4f(0.0F, 0.0F, 255.0F, 255.0F);
        GL11.glDisable(GL11.GL_TEXTURE_2D);
        GL11.glEnable(GL11.GL_COLOR_LOGIC_OP);
        GL11.glLogicOp(GL11.GL_OR_REVERSE);
        tessellator.startDrawingQuads();
        tessellator.addVertex((double)p_renderHighlight_1_, (double)p_renderHighlight_4_, 0.0D);
        tessellator.addVertex((double)p_renderHighlight_3_, (double)p_renderHighlight_4_, 0.0D);
        tessellator.addVertex((double)p_renderHighlight_3_, (double)p_renderHighlight_2_, 0.0D);
        tessellator.addVertex((double)p_renderHighlight_1_, (double)p_renderHighlight_2_, 0.0D);
        tessellator.draw();
        GL11.glDisable(GL11.GL_COLOR_LOGIC_OP);
        GL11.glEnable(GL11.GL_TEXTURE_2D);
    }

    public void setMaxLength(int p_setMaxLength_1_)
    {
        this.maxLength = p_setMaxLength_1_;

        if (this.value.length() > p_setMaxLength_1_)
        {
            this.value = this.value.substring(0, p_setMaxLength_1_);
        }
    }

    public int getMaxLength()
    {
        return this.maxLength;
    }

    public int getCursorPosition()
    {
        return this.cursorPos;
    }

    public boolean isBordered()
    {
        return this.bordered;
    }

    public void setBordered(boolean p_setBordered_1_)
    {
        this.bordered = p_setBordered_1_;
    }

    public int getTextColor()
    {
        return this.textColor;
    }

    public void setTextColor(int p_setTextColor_1_)
    {
        this.textColor = p_setTextColor_1_;
    }

    public int getTextColorUneditable()
    {
        return this.textColorUneditable;
    }

    public void setTextColorUneditable(int p_setTextColorUneditable_1_)
    {
        this.textColorUneditable = p_setTextColorUneditable_1_;
    }

    public void setFocus(boolean p_setFocus_1_)
    {
        if (p_setFocus_1_ && !this.inFocus)
        {
            this.frame = 0;
        }

        this.inFocus = p_setFocus_1_;
    }

    public boolean isFocused()
    {
        return this.inFocus;
    }

    public boolean isIsEditable()
    {
        return this.isEditable;
    }

    public void setIsEditable(boolean p_setIsEditable_1_)
    {
        this.isEditable = p_setIsEditable_1_;
    }

    public int getHighlightPos()
    {
        return this.highlightPos;
    }

    public int getInnerWidth()
    {
        return this.isBordered() ? this.width - 8 : this.width;
    }

    public void setHighlightPos(int p_setHighlightPos_1_)
    {
        int j = this.value.length();

        if (p_setHighlightPos_1_ > j)
        {
            p_setHighlightPos_1_ = j;
        }

        if (p_setHighlightPos_1_ < 0)
        {
            p_setHighlightPos_1_ = 0;
        }

        this.highlightPos = p_setHighlightPos_1_;

        if (this.font != null)
        {
            if (this.displayPos > j)
            {
                this.displayPos = j;
            }

            int k = this.getInnerWidth();
            String s = this.font.trimStringToWidth(this.value.substring(this.displayPos), k);
            int l = s.length() + this.displayPos;

            if (p_setHighlightPos_1_ == this.displayPos)
            {
                this.displayPos -= this.font.trimStringToWidth(this.value, k, true).length();
            }

            if (p_setHighlightPos_1_ > l)
            {
                this.displayPos += p_setHighlightPos_1_ - l;
            }
            else if (p_setHighlightPos_1_ <= this.displayPos)
            {
                this.displayPos -= this.displayPos - p_setHighlightPos_1_;
            }

            if (this.displayPos < 0)
            {
                this.displayPos = 0;
            }

            if (this.displayPos > j)
            {
                this.displayPos = j;
            }
        }
    }

    public boolean isCanLoseFocus()
    {
        return this.canLoseFocus;
    }

    public void setCanLoseFocus(boolean p_setCanLoseFocus_1_)
    {
        this.canLoseFocus = p_setCanLoseFocus_1_;
    }

    public boolean isVisible()
    {
        return this.visible;
    }

    public void setVisible(boolean p_setVisible_1_)
    {
        this.visible = p_setVisible_1_;
    }
}