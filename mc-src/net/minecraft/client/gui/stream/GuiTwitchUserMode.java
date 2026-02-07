package net.minecraft.client.gui.stream;

import com.google.common.collect.Lists;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import net.minecraft.client.gui.GuiButton;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.stream.IStream;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.EnumChatFormatting;
import net.minecraft.util.IChatComponent;
import tv.twitch.chat.ChatUserInfo;
import tv.twitch.chat.ChatUserMode;
import tv.twitch.chat.ChatUserSubscription;

@SideOnly(Side.CLIENT)
public class GuiTwitchUserMode extends GuiScreen
{
    private static final EnumChatFormatting field_152331_a = EnumChatFormatting.DARK_GREEN;
    private static final EnumChatFormatting field_152335_f = EnumChatFormatting.RED;
    private static final EnumChatFormatting field_152336_g = EnumChatFormatting.DARK_PURPLE;
    private final ChatUserInfo field_152337_h;
    private final IChatComponent field_152338_i;
    private final List field_152332_r = Lists.newArrayList();
    private final IStream field_152333_s;
    private int field_152334_t;
    private static final String __OBFID = "CL_00001837";

    public GuiTwitchUserMode(IStream p_i1064_1_, ChatUserInfo p_i1064_2_)
    {
        this.field_152333_s = p_i1064_1_;
        this.field_152337_h = p_i1064_2_;
        this.field_152338_i = new ChatComponentText(p_i1064_2_.displayName);
        this.field_152332_r.addAll(func_152328_a(p_i1064_2_.modes, p_i1064_2_.subscriptions, p_i1064_1_));
    }

    public static List func_152328_a(Set p_152328_0_, Set p_152328_1_, IStream p_152328_2_)
    {
        String s = p_152328_2_ == null ? null : p_152328_2_.func_152921_C();
        boolean flag = p_152328_2_ != null && p_152328_2_.func_152927_B();
        ArrayList arraylist = Lists.newArrayList();
        Iterator iterator = p_152328_0_.iterator();
        IChatComponent ichatcomponent;
        ChatComponentText chatcomponenttext;

        while (iterator.hasNext())
        {
            ChatUserMode chatusermode = (ChatUserMode)iterator.next();
            ichatcomponent = func_152329_a(chatusermode, s, flag);

            if (ichatcomponent != null)
            {
                chatcomponenttext = new ChatComponentText("- ");
                chatcomponenttext.appendSibling(ichatcomponent);
                arraylist.add(chatcomponenttext);
            }
        }

        iterator = p_152328_1_.iterator();

        while (iterator.hasNext())
        {
            ChatUserSubscription chatusersubscription = (ChatUserSubscription)iterator.next();
            ichatcomponent = func_152330_a(chatusersubscription, s, flag);

            if (ichatcomponent != null)
            {
                chatcomponenttext = new ChatComponentText("- ");
                chatcomponenttext.appendSibling(ichatcomponent);
                arraylist.add(chatcomponenttext);
            }
        }

        return arraylist;
    }

    public static IChatComponent func_152330_a(ChatUserSubscription p_152330_0_, String p_152330_1_, boolean p_152330_2_)
    {
        ChatComponentTranslation chatcomponenttranslation = null;

        if (p_152330_0_ == ChatUserSubscription.TTV_CHAT_USERSUB_SUBSCRIBER)
        {
            if (p_152330_1_ == null)
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.subscription.subscriber", new Object[0]);
            }
            else if (p_152330_2_)
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.subscription.subscriber.self", new Object[0]);
            }
            else
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.subscription.subscriber.other", new Object[] {p_152330_1_});
            }

            chatcomponenttranslation.getChatStyle().setColor(field_152331_a);
        }
        else if (p_152330_0_ == ChatUserSubscription.TTV_CHAT_USERSUB_TURBO)
        {
            chatcomponenttranslation = new ChatComponentTranslation("stream.user.subscription.turbo", new Object[0]);
            chatcomponenttranslation.getChatStyle().setColor(field_152336_g);
        }

        return chatcomponenttranslation;
    }

    public static IChatComponent func_152329_a(ChatUserMode p_152329_0_, String p_152329_1_, boolean p_152329_2_)
    {
        ChatComponentTranslation chatcomponenttranslation = null;

        if (p_152329_0_ == ChatUserMode.TTV_CHAT_USERMODE_ADMINSTRATOR)
        {
            chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.administrator", new Object[0]);
            chatcomponenttranslation.getChatStyle().setColor(field_152336_g);
        }
        else if (p_152329_0_ == ChatUserMode.TTV_CHAT_USERMODE_BANNED)
        {
            if (p_152329_1_ == null)
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.banned", new Object[0]);
            }
            else if (p_152329_2_)
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.banned.self", new Object[0]);
            }
            else
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.banned.other", new Object[] {p_152329_1_});
            }

            chatcomponenttranslation.getChatStyle().setColor(field_152335_f);
        }
        else if (p_152329_0_ == ChatUserMode.TTV_CHAT_USERMODE_BROADCASTER)
        {
            if (p_152329_1_ == null)
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.broadcaster", new Object[0]);
            }
            else if (p_152329_2_)
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.broadcaster.self", new Object[0]);
            }
            else
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.broadcaster.other", new Object[0]);
            }

            chatcomponenttranslation.getChatStyle().setColor(field_152331_a);
        }
        else if (p_152329_0_ == ChatUserMode.TTV_CHAT_USERMODE_MODERATOR)
        {
            if (p_152329_1_ == null)
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.moderator", new Object[0]);
            }
            else if (p_152329_2_)
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.moderator.self", new Object[0]);
            }
            else
            {
                chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.moderator.other", new Object[] {p_152329_1_});
            }

            chatcomponenttranslation.getChatStyle().setColor(field_152331_a);
        }
        else if (p_152329_0_ == ChatUserMode.TTV_CHAT_USERMODE_STAFF)
        {
            chatcomponenttranslation = new ChatComponentTranslation("stream.user.mode.staff", new Object[0]);
            chatcomponenttranslation.getChatStyle().setColor(field_152336_g);
        }

        return chatcomponenttranslation;
    }

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        int i = this.width / 3;
        int j = i - 130;
        this.buttonList.add(new GuiButton(1, i * 0 + j / 2, this.height - 70, 130, 20, I18n.format("stream.userinfo.timeout", new Object[0])));
        this.buttonList.add(new GuiButton(0, i * 1 + j / 2, this.height - 70, 130, 20, I18n.format("stream.userinfo.ban", new Object[0])));
        this.buttonList.add(new GuiButton(2, i * 2 + j / 2, this.height - 70, 130, 20, I18n.format("stream.userinfo.mod", new Object[0])));
        this.buttonList.add(new GuiButton(5, i * 0 + j / 2, this.height - 45, 130, 20, I18n.format("gui.cancel", new Object[0])));
        this.buttonList.add(new GuiButton(3, i * 1 + j / 2, this.height - 45, 130, 20, I18n.format("stream.userinfo.unban", new Object[0])));
        this.buttonList.add(new GuiButton(4, i * 2 + j / 2, this.height - 45, 130, 20, I18n.format("stream.userinfo.unmod", new Object[0])));
        int k = 0;
        IChatComponent ichatcomponent;

        for (Iterator iterator = this.field_152332_r.iterator(); iterator.hasNext(); k = Math.max(k, this.fontRendererObj.getStringWidth(ichatcomponent.getFormattedText())))
        {
            ichatcomponent = (IChatComponent)iterator.next();
        }

        this.field_152334_t = this.width / 2 - k / 2;
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        if (p_146284_1_.enabled)
        {
            if (p_146284_1_.id == 0)
            {
                this.field_152333_s.func_152917_b("/ban " + this.field_152337_h.displayName);
            }
            else if (p_146284_1_.id == 3)
            {
                this.field_152333_s.func_152917_b("/unban " + this.field_152337_h.displayName);
            }
            else if (p_146284_1_.id == 2)
            {
                this.field_152333_s.func_152917_b("/mod " + this.field_152337_h.displayName);
            }
            else if (p_146284_1_.id == 4)
            {
                this.field_152333_s.func_152917_b("/unmod " + this.field_152337_h.displayName);
            }
            else if (p_146284_1_.id == 1)
            {
                this.field_152333_s.func_152917_b("/timeout " + this.field_152337_h.displayName);
            }

            this.mc.displayGuiScreen((GuiScreen)null);
        }
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawDefaultBackground();
        this.drawCenteredString(this.fontRendererObj, this.field_152338_i.getUnformattedText(), this.width / 2, 70, 16777215);
        int k = 80;

        for (Iterator iterator = this.field_152332_r.iterator(); iterator.hasNext(); k += this.fontRendererObj.FONT_HEIGHT)
        {
            IChatComponent ichatcomponent = (IChatComponent)iterator.next();
            this.drawString(this.fontRendererObj, ichatcomponent.getFormattedText(), this.field_152334_t, k, 16777215);
        }

        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }
}