package net.minecraft.client.gui;

import com.google.common.collect.Lists;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.EnumChatFormatting;
import net.minecraft.util.IChatComponent;
import net.minecraft.util.MathHelper;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class GuiNewChat extends Gui
{
    private static final Logger logger = LogManager.getLogger();
    private final Minecraft mc;
    /** A list of messages previously sent through the chat GUI */
    private final List sentMessages = new ArrayList();
    /** Chat lines to be displayed in the chat box */
    private final List chatLines = new ArrayList();
    private final List field_146253_i = new ArrayList();
    private int field_146250_j;
    private boolean field_146251_k;
    private static final String __OBFID = "CL_00000669";

    public GuiNewChat(Minecraft p_i1022_1_)
    {
        this.mc = p_i1022_1_;
    }

    public void drawChat(int p_146230_1_)
    {
        if (this.mc.gameSettings.chatVisibility != EntityPlayer.EnumChatVisibility.HIDDEN)
        {
            int j = this.func_146232_i();
            boolean flag = false;
            int k = 0;
            int l = this.field_146253_i.size();
            float f = this.mc.gameSettings.chatOpacity * 0.9F + 0.1F;

            if (l > 0)
            {
                if (this.getChatOpen())
                {
                    flag = true;
                }

                float f1 = this.func_146244_h();
                int i1 = MathHelper.ceiling_float_int((float)this.func_146228_f() / f1);
                GL11.glPushMatrix();
                GL11.glTranslatef(2.0F, 20.0F, 0.0F);
                GL11.glScalef(f1, f1, 1.0F);
                int j1;
                int k1;
                int i2;

                for (j1 = 0; j1 + this.field_146250_j < this.field_146253_i.size() && j1 < j; ++j1)
                {
                    ChatLine chatline = (ChatLine)this.field_146253_i.get(j1 + this.field_146250_j);

                    if (chatline != null)
                    {
                        k1 = p_146230_1_ - chatline.getUpdatedCounter();

                        if (k1 < 200 || flag)
                        {
                            double d0 = (double)k1 / 200.0D;
                            d0 = 1.0D - d0;
                            d0 *= 10.0D;

                            if (d0 < 0.0D)
                            {
                                d0 = 0.0D;
                            }

                            if (d0 > 1.0D)
                            {
                                d0 = 1.0D;
                            }

                            d0 *= d0;
                            i2 = (int)(255.0D * d0);

                            if (flag)
                            {
                                i2 = 255;
                            }

                            i2 = (int)((float)i2 * f);
                            ++k;

                            if (i2 > 3)
                            {
                                byte b0 = 0;
                                int j2 = -j1 * 9;
                                drawRect(b0, j2 - 9, b0 + i1 + 4, j2, i2 / 2 << 24);
                                GL11.glEnable(GL11.GL_BLEND); // FORGE: BugFix MC-36812 Chat Opacity Broken in 1.7.x
                                String s = chatline.func_151461_a().getFormattedText();
                                this.mc.fontRenderer.drawStringWithShadow(s, b0, j2 - 8, 16777215 + (i2 << 24));
                                GL11.glDisable(GL11.GL_ALPHA_TEST);
                            }
                        }
                    }
                }

                if (flag)
                {
                    j1 = this.mc.fontRenderer.FONT_HEIGHT;
                    GL11.glTranslatef(-3.0F, 0.0F, 0.0F);
                    int k2 = l * j1 + l;
                    k1 = k * j1 + k;
                    int l2 = this.field_146250_j * k1 / l;
                    int l1 = k1 * k1 / k2;

                    if (k2 != k1)
                    {
                        i2 = l2 > 0 ? 170 : 96;
                        int i3 = this.field_146251_k ? 13382451 : 3355562;
                        drawRect(0, -l2, 2, -l2 - l1, i3 + (i2 << 24));
                        drawRect(2, -l2, 1, -l2 - l1, 13421772 + (i2 << 24));
                    }
                }

                GL11.glPopMatrix();
            }
        }
    }

    /**
     * Clears the chat.
     */
    public void clearChatMessages()
    {
        this.field_146253_i.clear();
        this.chatLines.clear();
        this.sentMessages.clear();
    }

    public void printChatMessage(IChatComponent p_146227_1_)
    {
        this.printChatMessageWithOptionalDeletion(p_146227_1_, 0);
    }

    /**
     * prints the ChatComponent to Chat. If the ID is not 0, deletes an existing Chat Line of that ID from the GUI
     */
    public void printChatMessageWithOptionalDeletion(IChatComponent p_146234_1_, int p_146234_2_)
    {
        this.func_146237_a(p_146234_1_, p_146234_2_, this.mc.ingameGUI.getUpdateCounter(), false);
        logger.info("[CHAT] " + p_146234_1_.getUnformattedText());
    }

    private String func_146235_b(String p_146235_1_)
    {
        return Minecraft.getMinecraft().gameSettings.chatColours ? p_146235_1_ : EnumChatFormatting.getTextWithoutFormattingCodes(p_146235_1_);
    }

    private void func_146237_a(IChatComponent p_146237_1_, int p_146237_2_, int p_146237_3_, boolean p_146237_4_)
    {
        if (p_146237_2_ != 0)
        {
            this.deleteChatLine(p_146237_2_);
        }

        int k = MathHelper.floor_float((float)this.func_146228_f() / this.func_146244_h());
        int l = 0;
        ChatComponentText chatcomponenttext = new ChatComponentText("");
        ArrayList arraylist = Lists.newArrayList();
        ArrayList arraylist1 = Lists.newArrayList(p_146237_1_);

        for (int i1 = 0; i1 < arraylist1.size(); ++i1)
        {
            IChatComponent ichatcomponent1 = (IChatComponent)arraylist1.get(i1);
            String s = this.func_146235_b(ichatcomponent1.getChatStyle().getFormattingCode() + ichatcomponent1.getUnformattedTextForChat());
            int j1 = this.mc.fontRenderer.getStringWidth(s);
            ChatComponentText chatcomponenttext1 = new ChatComponentText(s);
            chatcomponenttext1.setChatStyle(ichatcomponent1.getChatStyle().createShallowCopy());
            boolean flag1 = false;

            if (l + j1 > k)
            {
                String s1 = this.mc.fontRenderer.trimStringToWidth(s, k - l, false);
                String s2 = s1.length() < s.length() ? s.substring(s1.length()) : null;

                if (s2 != null && s2.length() > 0)
                {
                    int k1 = s1.lastIndexOf(" ");

                    if (k1 >= 0 && this.mc.fontRenderer.getStringWidth(s.substring(0, k1)) > 0)
                    {
                        s1 = s.substring(0, k1);
                        s2 = s.substring(k1);
                    }

                    ChatComponentText chatcomponenttext2 = new ChatComponentText(s2);
                    chatcomponenttext2.setChatStyle(ichatcomponent1.getChatStyle().createShallowCopy());
                    arraylist1.add(i1 + 1, chatcomponenttext2);
                }

                j1 = this.mc.fontRenderer.getStringWidth(s1);
                chatcomponenttext1 = new ChatComponentText(s1);
                chatcomponenttext1.setChatStyle(ichatcomponent1.getChatStyle().createShallowCopy());
                flag1 = true;
            }

            if (l + j1 <= k)
            {
                l += j1;
                chatcomponenttext.appendSibling(chatcomponenttext1);
            }
            else
            {
                flag1 = true;
            }

            if (flag1)
            {
                arraylist.add(chatcomponenttext);
                l = 0;
                chatcomponenttext = new ChatComponentText("");
            }
        }

        arraylist.add(chatcomponenttext);
        boolean flag2 = this.getChatOpen();
        IChatComponent ichatcomponent2;

        for (Iterator iterator = arraylist.iterator(); iterator.hasNext(); this.field_146253_i.add(0, new ChatLine(p_146237_3_, ichatcomponent2, p_146237_2_)))
        {
            ichatcomponent2 = (IChatComponent)iterator.next();

            if (flag2 && this.field_146250_j > 0)
            {
                this.field_146251_k = true;
                this.scroll(1);
            }
        }

        while (this.field_146253_i.size() > 100)
        {
            this.field_146253_i.remove(this.field_146253_i.size() - 1);
        }

        if (!p_146237_4_)
        {
            this.chatLines.add(0, new ChatLine(p_146237_3_, p_146237_1_, p_146237_2_));

            while (this.chatLines.size() > 100)
            {
                this.chatLines.remove(this.chatLines.size() - 1);
            }
        }
    }

    public void refreshChat()
    {
        this.field_146253_i.clear();
        this.resetScroll();

        for (int i = this.chatLines.size() - 1; i >= 0; --i)
        {
            ChatLine chatline = (ChatLine)this.chatLines.get(i);
            this.func_146237_a(chatline.func_151461_a(), chatline.getChatLineID(), chatline.getUpdatedCounter(), true);
        }
    }

    /**
     * Gets the list of messages previously sent through the chat GUI
     */
    public List getSentMessages()
    {
        return this.sentMessages;
    }

    /**
     * Adds this string to the list of sent messages, for recall using the up/down arrow keys
     */
    public void addToSentMessages(String p_146239_1_)
    {
        if (this.sentMessages.isEmpty() || !((String)this.sentMessages.get(this.sentMessages.size() - 1)).equals(p_146239_1_))
        {
            this.sentMessages.add(p_146239_1_);
        }
    }

    /**
     * Resets the chat scroll (executed when the GUI is closed, among others)
     */
    public void resetScroll()
    {
        this.field_146250_j = 0;
        this.field_146251_k = false;
    }

    /**
     * Scrolls the chat by the given number of lines.
     */
    public void scroll(int p_146229_1_)
    {
        this.field_146250_j += p_146229_1_;
        int j = this.field_146253_i.size();

        if (this.field_146250_j > j - this.func_146232_i())
        {
            this.field_146250_j = j - this.func_146232_i();
        }

        if (this.field_146250_j <= 0)
        {
            this.field_146250_j = 0;
            this.field_146251_k = false;
        }
    }

    public IChatComponent func_146236_a(int p_146236_1_, int p_146236_2_)
    {
        if (!this.getChatOpen())
        {
            return null;
        }
        else
        {
            ScaledResolution scaledresolution = new ScaledResolution(this.mc, this.mc.displayWidth, this.mc.displayHeight);
            int k = scaledresolution.getScaleFactor();
            float f = this.func_146244_h();
            int l = p_146236_1_ / k - 3;
            int i1 = p_146236_2_ / k - 27;
            l = MathHelper.floor_float((float)l / f);
            i1 = MathHelper.floor_float((float)i1 / f);

            if (l >= 0 && i1 >= 0)
            {
                int j1 = Math.min(this.func_146232_i(), this.field_146253_i.size());

                if (l <= MathHelper.floor_float((float)this.func_146228_f() / this.func_146244_h()) && i1 < this.mc.fontRenderer.FONT_HEIGHT * j1 + j1)
                {
                    int k1 = i1 / this.mc.fontRenderer.FONT_HEIGHT + this.field_146250_j;

                    if (k1 >= 0 && k1 < this.field_146253_i.size())
                    {
                        ChatLine chatline = (ChatLine)this.field_146253_i.get(k1);
                        int l1 = 0;
                        Iterator iterator = chatline.func_151461_a().iterator();

                        while (iterator.hasNext())
                        {
                            IChatComponent ichatcomponent = (IChatComponent)iterator.next();

                            if (ichatcomponent instanceof ChatComponentText)
                            {
                                l1 += this.mc.fontRenderer.getStringWidth(this.func_146235_b(((ChatComponentText)ichatcomponent).getChatComponentText_TextValue()));

                                if (l1 > l)
                                {
                                    return ichatcomponent;
                                }
                            }
                        }
                    }

                    return null;
                }
                else
                {
                    return null;
                }
            }
            else
            {
                return null;
            }
        }
    }

    /**
     * Returns true if the chat GUI is open
     */
    public boolean getChatOpen()
    {
        return this.mc.currentScreen instanceof GuiChat;
    }

    /**
     * finds and deletes a Chat line by ID
     */
    public void deleteChatLine(int p_146242_1_)
    {
        Iterator iterator = this.field_146253_i.iterator();
        ChatLine chatline;

        while (iterator.hasNext())
        {
            chatline = (ChatLine)iterator.next();

            if (chatline.getChatLineID() == p_146242_1_)
            {
                iterator.remove();
            }
        }

        iterator = this.chatLines.iterator();

        while (iterator.hasNext())
        {
            chatline = (ChatLine)iterator.next();

            if (chatline.getChatLineID() == p_146242_1_)
            {
                iterator.remove();
                break;
            }
        }
    }

    public int func_146228_f()
    {
        return func_146233_a(this.mc.gameSettings.chatWidth);
    }

    public int func_146246_g()
    {
        return func_146243_b(this.getChatOpen() ? this.mc.gameSettings.chatHeightFocused : this.mc.gameSettings.chatHeightUnfocused);
    }

    public float func_146244_h()
    {
        return this.mc.gameSettings.chatScale;
    }

    public static int func_146233_a(float p_146233_0_)
    {
        short short1 = 320;
        byte b0 = 40;
        return MathHelper.floor_float(p_146233_0_ * (float)(short1 - b0) + (float)b0);
    }

    public static int func_146243_b(float p_146243_0_)
    {
        short short1 = 180;
        byte b0 = 20;
        return MathHelper.floor_float(p_146243_0_ * (float)(short1 - b0) + (float)b0);
    }

    public int func_146232_i()
    {
        return this.func_146246_g() / 9;
    }
}