package net.minecraft.client.stream;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import tv.twitch.AuthToken;
import tv.twitch.ErrorCode;
import tv.twitch.chat.Chat;
import tv.twitch.chat.ChatChannelInfo;
import tv.twitch.chat.ChatEvent;
import tv.twitch.chat.ChatMessage;
import tv.twitch.chat.ChatMessageList;
import tv.twitch.chat.ChatTokenizedMessage;
import tv.twitch.chat.ChatUserInfo;
import tv.twitch.chat.ChatUserList;
import tv.twitch.chat.IChatCallbacks;
import tv.twitch.chat.StandardChatAPI;

@SideOnly(Side.CLIENT)
public class ChatController implements IChatCallbacks
{
    private static final Logger field_153018_p = LogManager.getLogger();
    protected ChatController.ChatListener field_153003_a = null;
    protected String field_153004_b = "";
    protected String field_153005_c = "";
    protected String field_153006_d = "";
    protected String field_153007_e = "";
    protected Chat field_153008_f = null;
    protected boolean field_153009_g = false;
    protected boolean field_153010_h = false;
    protected ChatController.ChatState field_153011_i;
    protected AuthToken field_153012_j;
    protected List field_153013_k;
    protected LinkedList field_153014_l;
    protected int field_153015_m;
    protected boolean field_153016_n;
    protected boolean field_153017_o;
    private static final String __OBFID = "CL_00001819";

    public void chatStatusCallback(ErrorCode p_chatStatusCallback_1_)
    {
        if (!ErrorCode.succeeded(p_chatStatusCallback_1_))
        {
            this.field_153011_i = ChatController.ChatState.Disconnected;
        }
    }

    public void chatChannelMembershipCallback(ChatEvent p_chatChannelMembershipCallback_1_, ChatChannelInfo p_chatChannelMembershipCallback_2_)
    {
        switch (ChatController.SwitchChatState.field_152982_a[p_chatChannelMembershipCallback_1_.ordinal()])
        {
            case 1:
                this.field_153011_i = ChatController.ChatState.Connected;
                this.func_152999_p();
                break;
            case 2:
                this.field_153011_i = ChatController.ChatState.Disconnected;
        }
    }

    public void chatChannelUserChangeCallback(ChatUserList p_chatChannelUserChangeCallback_1_, ChatUserList p_chatChannelUserChangeCallback_2_, ChatUserList p_chatChannelUserChangeCallback_3_)
    {
        int i;
        int j;

        for (i = 0; i < p_chatChannelUserChangeCallback_2_.userList.length; ++i)
        {
            j = this.field_153013_k.indexOf(p_chatChannelUserChangeCallback_2_.userList[i]);

            if (j >= 0)
            {
                this.field_153013_k.remove(j);
            }
        }

        for (i = 0; i < p_chatChannelUserChangeCallback_3_.userList.length; ++i)
        {
            j = this.field_153013_k.indexOf(p_chatChannelUserChangeCallback_3_.userList[i]);

            if (j >= 0)
            {
                this.field_153013_k.remove(j);
            }

            this.field_153013_k.add(p_chatChannelUserChangeCallback_3_.userList[i]);
        }

        for (i = 0; i < p_chatChannelUserChangeCallback_1_.userList.length; ++i)
        {
            this.field_153013_k.add(p_chatChannelUserChangeCallback_1_.userList[i]);
        }

        try
        {
            if (this.field_153003_a != null)
            {
                this.field_153003_a.func_152904_a(p_chatChannelUserChangeCallback_1_.userList, p_chatChannelUserChangeCallback_2_.userList, p_chatChannelUserChangeCallback_3_.userList);
            }
        }
        catch (Exception exception)
        {
            this.func_152995_h(exception.toString());
        }
    }

    public void chatQueryChannelUsersCallback(ChatUserList p_chatQueryChannelUsersCallback_1_) {}

    public void chatChannelMessageCallback(ChatMessageList p_chatChannelMessageCallback_1_)
    {
        for (int i = 0; i < p_chatChannelMessageCallback_1_.messageList.length; ++i)
        {
            this.field_153014_l.addLast(p_chatChannelMessageCallback_1_.messageList[i]);
        }

        try
        {
            if (this.field_153003_a != null)
            {
                this.field_153003_a.func_152903_a(p_chatChannelMessageCallback_1_.messageList);
            }
        }
        catch (Exception exception)
        {
            this.func_152995_h(exception.toString());
        }

        while (this.field_153014_l.size() > this.field_153015_m)
        {
            this.field_153014_l.removeFirst();
        }
    }

    public void chatClearCallback(String p_chatClearCallback_1_)
    {
        this.func_152987_o();
    }

    public void emoticonDataDownloadCallback(ErrorCode p_emoticonDataDownloadCallback_1_)
    {
        if (ErrorCode.succeeded(p_emoticonDataDownloadCallback_1_))
        {
            this.func_152988_s();
        }
    }

    public void chatChannelTokenizedMessageCallback(ChatTokenizedMessage[] p_chatChannelTokenizedMessageCallback_1_) {}

    public void func_152990_a(ChatController.ChatListener p_152990_1_)
    {
        this.field_153003_a = p_152990_1_;
    }

    public boolean func_152991_c()
    {
        return this.field_153011_i == ChatController.ChatState.Connected;
    }

    public void func_152994_a(AuthToken p_152994_1_)
    {
        this.field_153012_j = p_152994_1_;
    }

    public void func_152984_a(String p_152984_1_)
    {
        this.field_153006_d = p_152984_1_;
    }

    public void func_152998_c(String p_152998_1_)
    {
        this.field_153004_b = p_152998_1_;
    }

    public ChatController.ChatState func_153000_j()
    {
        return this.field_153011_i;
    }

    public ChatController()
    {
        this.field_153011_i = ChatController.ChatState.Uninitialized;
        this.field_153012_j = new AuthToken();
        this.field_153013_k = new ArrayList();
        this.field_153014_l = new LinkedList();
        this.field_153015_m = 128;
        this.field_153016_n = false;
        this.field_153017_o = false;
        this.field_153008_f = new Chat(new StandardChatAPI());
    }

    public boolean func_152986_d(String p_152986_1_)
    {
        this.func_153002_l();
        this.field_153010_h = false;
        this.field_153005_c = p_152986_1_;
        return this.func_152985_f(p_152986_1_);
    }

    public boolean func_153002_l()
    {
        if (this.field_153011_i != ChatController.ChatState.Connected && this.field_153011_i != ChatController.ChatState.Connecting)
        {
            if (this.field_153011_i == ChatController.ChatState.Disconnected)
            {
                this.func_152989_q();
            }
        }
        else
        {
            ErrorCode errorcode = this.field_153008_f.disconnect();

            if (ErrorCode.failed(errorcode))
            {
                String s = ErrorCode.getString(errorcode);
                this.func_152995_h(String.format("Error disconnecting: %s", new Object[] {s}));
                return false;
            }

            this.func_152989_q();
        }

        return this.func_152993_m();
    }

    protected boolean func_152985_f(String p_152985_1_)
    {
        if (this.field_153009_g)
        {
            return false;
        }
        else
        {
            ErrorCode errorcode = this.field_153008_f.initialize(p_152985_1_, false);

            if (ErrorCode.failed(errorcode))
            {
                String s1 = ErrorCode.getString(errorcode);
                this.func_152995_h(String.format("Error initializing chat: %s", new Object[] {s1}));
                this.func_152989_q();
                return false;
            }
            else
            {
                this.field_153009_g = true;
                this.field_153008_f.setChatCallbacks(this);
                this.field_153011_i = ChatController.ChatState.Initialized;
                return true;
            }
        }
    }

    protected boolean func_152993_m()
    {
        if (this.field_153009_g)
        {
            ErrorCode errorcode = this.field_153008_f.shutdown();

            if (ErrorCode.failed(errorcode))
            {
                String s = ErrorCode.getString(errorcode);
                this.func_152995_h(String.format("Error shutting down chat: %s", new Object[] {s}));
                return false;
            }
        }

        this.field_153011_i = ChatController.ChatState.Uninitialized;
        this.field_153009_g = false;
        this.func_152996_t();
        this.field_153008_f.setChatCallbacks((IChatCallbacks)null);
        return true;
    }

    public void func_152997_n()
    {
        if (this.field_153009_g)
        {
            ErrorCode errorcode = this.field_153008_f.flushEvents();
            String s;

            if (ErrorCode.failed(errorcode))
            {
                s = ErrorCode.getString(errorcode);
                this.func_152995_h(String.format("Error flushing chat events: %s", new Object[] {s}));
            }

            switch (ChatController.SwitchChatState.field_152983_b[this.field_153011_i.ordinal()])
            {
                case 1:
                case 3:
                case 4:
                default:
                    break;
                case 2:
                    if (this.field_153010_h)
                    {
                        errorcode = this.field_153008_f.connectAnonymous();
                    }
                    else
                    {
                        errorcode = this.field_153008_f.connect(this.field_153005_c, this.field_153012_j.data);
                    }

                    if (ErrorCode.failed(errorcode))
                    {
                        s = ErrorCode.getString(errorcode);
                        this.func_152995_h(String.format("Error connecting: %s", new Object[] {s}));
                        this.func_152993_m();
                        this.func_152989_q();
                    }
                    else
                    {
                        this.field_153011_i = ChatController.ChatState.Connecting;
                        this.func_153001_r();
                    }

                    break;
                case 5:
                    this.func_153002_l();
            }
        }
    }

    public boolean func_152992_g(String p_152992_1_)
    {
        if (this.field_153011_i != ChatController.ChatState.Connected)
        {
            return false;
        }
        else
        {
            ErrorCode errorcode = this.field_153008_f.sendMessage(p_152992_1_);

            if (ErrorCode.failed(errorcode))
            {
                String s1 = ErrorCode.getString(errorcode);
                this.func_152995_h(String.format("Error sending chat message: %s", new Object[] {s1}));
                return false;
            }
            else
            {
                return true;
            }
        }
    }

    public void func_152987_o()
    {
        this.field_153014_l.clear();

        try
        {
            if (this.field_153003_a != null)
            {
                this.field_153003_a.func_152902_f();
            }
        }
        catch (Exception exception)
        {
            this.func_152995_h(exception.toString());
        }
    }

    protected void func_152999_p()
    {
        try
        {
            if (this.field_153003_a != null)
            {
                this.field_153003_a.func_152906_d();
            }
        }
        catch (Exception exception)
        {
            this.func_152995_h(exception.toString());
        }
    }

    protected void func_152989_q()
    {
        try
        {
            if (this.field_153003_a != null)
            {
                this.field_153003_a.func_152905_e();
            }
        }
        catch (Exception exception)
        {
            this.func_152995_h(exception.toString());
        }
    }

    protected void func_153001_r() {}

    protected void func_152988_s() {}

    protected void func_152996_t() {}

    protected void func_152995_h(String p_152995_1_)
    {
        field_153018_p.error(TwitchStream.field_152949_a, "[Chat controller] {}", new Object[] {p_152995_1_});
    }

    @SideOnly(Side.CLIENT)
    public interface ChatListener
    {
        void func_152903_a(ChatMessage[] p_152903_1_);

        void func_152904_a(ChatUserInfo[] p_152904_1_, ChatUserInfo[] p_152904_2_, ChatUserInfo[] p_152904_3_);

        void func_152906_d();

        void func_152905_e();

        void func_152902_f();
    }

    @SideOnly(Side.CLIENT)
    public static enum ChatState
    {
        Uninitialized,
        Initialized,
        Connecting,
        Connected,
        Disconnected;

        private static final String __OBFID = "CL_00001817";
    }

    @SideOnly(Side.CLIENT)

    static final class SwitchChatState
        {
            static final int[] field_152982_a;

            static final int[] field_152983_b = new int[ChatController.ChatState.values().length];
            private static final String __OBFID = "CL_00001818";

            static
            {
                try
                {
                    field_152983_b[ChatController.ChatState.Uninitialized.ordinal()] = 1;
                }
                catch (NoSuchFieldError var7)
                {
                    ;
                }

                try
                {
                    field_152983_b[ChatController.ChatState.Initialized.ordinal()] = 2;
                }
                catch (NoSuchFieldError var6)
                {
                    ;
                }

                try
                {
                    field_152983_b[ChatController.ChatState.Connecting.ordinal()] = 3;
                }
                catch (NoSuchFieldError var5)
                {
                    ;
                }

                try
                {
                    field_152983_b[ChatController.ChatState.Connected.ordinal()] = 4;
                }
                catch (NoSuchFieldError var4)
                {
                    ;
                }

                try
                {
                    field_152983_b[ChatController.ChatState.Disconnected.ordinal()] = 5;
                }
                catch (NoSuchFieldError var3)
                {
                    ;
                }

                field_152982_a = new int[ChatEvent.values().length];

                try
                {
                    field_152982_a[ChatEvent.TTV_CHAT_JOINED_CHANNEL.ordinal()] = 1;
                }
                catch (NoSuchFieldError var2)
                {
                    ;
                }

                try
                {
                    field_152982_a[ChatEvent.TTV_CHAT_LEFT_CHANNEL.ordinal()] = 2;
                }
                catch (NoSuchFieldError var1)
                {
                    ;
                }
            }
        }
}