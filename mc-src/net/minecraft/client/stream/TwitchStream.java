package net.minecraft.client.stream;

import com.google.common.collect.Maps;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import java.net.URL;
import java.net.URLEncoder;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.stream.GuiTwitchUserMode;
import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.settings.GameSettings;
import net.minecraft.client.shader.Framebuffer;
import net.minecraft.event.ClickEvent;
import net.minecraft.event.HoverEvent;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.EnumChatFormatting;
import net.minecraft.util.HttpUtil;
import net.minecraft.util.IChatComponent;
import net.minecraft.util.JsonUtils;
import net.minecraft.util.MathHelper;
import net.minecraft.util.Util;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.Marker;
import org.apache.logging.log4j.MarkerManager;
import org.apache.logging.log4j.core.helpers.Strings;
import org.lwjgl.opengl.GL11;
import tv.twitch.AuthToken;
import tv.twitch.ErrorCode;
import tv.twitch.broadcast.EncodingCpuUsage;
import tv.twitch.broadcast.FrameBuffer;
import tv.twitch.broadcast.GameInfo;
import tv.twitch.broadcast.IngestList;
import tv.twitch.broadcast.IngestServer;
import tv.twitch.broadcast.StreamInfo;
import tv.twitch.broadcast.VideoParams;
import tv.twitch.chat.ChatMessage;
import tv.twitch.chat.ChatUserInfo;
import tv.twitch.chat.ChatUserMode;
import tv.twitch.chat.ChatUserSubscription;

@SideOnly(Side.CLIENT)
public class TwitchStream implements BroadcastController.BroadcastListener, ChatController.ChatListener, IngestServerTester.IngestTestListener, IStream
{
    private static final Logger field_152950_b = LogManager.getLogger();
    public static final Marker field_152949_a = MarkerManager.getMarker("STREAM");
    private final BroadcastController field_152951_c;
    private final ChatController field_152952_d;
    private final Minecraft field_152953_e;
    private final IChatComponent field_152954_f = new ChatComponentText("Twitch");
    private final Map field_152955_g = Maps.newHashMap();
    private Framebuffer field_152956_h;
    private boolean field_152957_i;
    private int field_152958_j = 30;
    private long field_152959_k = 0L;
    private boolean field_152960_l = false;
    private boolean field_152961_m;
    private boolean field_152962_n;
    private boolean field_152963_o;
    private IStream.AuthFailureReason field_152964_p;
    private static boolean field_152965_q;
    private static final String __OBFID = "CL_00001812";

    public TwitchStream(Minecraft p_i1012_1_, final String p_i1012_2_)
    {
        this.field_152964_p = IStream.AuthFailureReason.ERROR;
        this.field_152953_e = p_i1012_1_;
        this.field_152951_c = new BroadcastController();
        this.field_152952_d = new ChatController();
        this.field_152951_c.func_152841_a(this);
        this.field_152952_d.func_152990_a(this);
        this.field_152951_c.func_152842_a("nmt37qblda36pvonovdkbopzfzw3wlq");
        this.field_152952_d.func_152984_a("nmt37qblda36pvonovdkbopzfzw3wlq");
        this.field_152954_f.getChatStyle().setColor(EnumChatFormatting.DARK_PURPLE);

        if (Strings.isNotEmpty(p_i1012_2_) && OpenGlHelper.framebufferSupported)
        {
            Thread thread = new Thread("Twitch authenticator")
            {
                private static final String __OBFID = "CL_00001811";
                public void run()
                {
                    try
                    {
                        URL url = new URL("https://api.twitch.tv/kraken?oauth_token=" + URLEncoder.encode(p_i1012_2_, "UTF-8"));
                        String s = HttpUtil.func_152755_a(url);
                        JsonObject jsonobject = JsonUtils.getJsonElementAsJsonObject((new JsonParser()).parse(s), "Response");
                        JsonObject jsonobject1 = JsonUtils.func_152754_s(jsonobject, "token");

                        if (JsonUtils.getJsonObjectBooleanFieldValue(jsonobject1, "valid"))
                        {
                            String s1 = JsonUtils.getJsonObjectStringFieldValue(jsonobject1, "user_name");
                            TwitchStream.field_152950_b.debug(TwitchStream.field_152949_a, "Authenticated with twitch; username is {}", new Object[] {s1});
                            AuthToken authtoken = new AuthToken();
                            authtoken.data = p_i1012_2_;
                            TwitchStream.this.field_152951_c.func_152818_a(s1, authtoken);
                            TwitchStream.this.field_152952_d.func_152998_c(s1);
                            TwitchStream.this.field_152952_d.func_152994_a(authtoken);
                            Runtime.getRuntime().addShutdownHook(new Thread("Twitch shutdown hook")
                            {
                                private static final String __OBFID = "CL_00001810";
                                public void run()
                                {
                                    TwitchStream.this.func_152923_i();
                                }
                            });
                            TwitchStream.this.field_152951_c.func_152817_A();
                        }
                        else
                        {
                            TwitchStream.this.field_152964_p = IStream.AuthFailureReason.INVALID_TOKEN;
                            TwitchStream.field_152950_b.error(TwitchStream.field_152949_a, "Given twitch access token is invalid");
                        }
                    }
                    catch (IOException ioexception)
                    {
                        TwitchStream.this.field_152964_p = IStream.AuthFailureReason.ERROR;
                        TwitchStream.field_152950_b.error(TwitchStream.field_152949_a, "Could not authenticate with twitch", ioexception);
                    }
                }
            };
            thread.setDaemon(true);
            thread.start();
        }
    }

    public void func_152923_i()
    {
        field_152950_b.debug(field_152949_a, "Shutdown streaming");
        this.field_152951_c.func_152851_B();
        this.field_152952_d.func_152993_m();
    }

    public void func_152935_j()
    {
        int i = this.field_152953_e.gameSettings.field_152408_R;
        ChatController.ChatState chatstate = this.field_152952_d.func_153000_j();

        if (i == 2)
        {
            if (chatstate == ChatController.ChatState.Connected)
            {
                field_152950_b.debug(field_152949_a, "Disconnecting from twitch chat per user options");
                this.field_152952_d.func_153002_l();
            }
        }
        else if (i == 1)
        {
            if ((chatstate == ChatController.ChatState.Disconnected || chatstate == ChatController.ChatState.Uninitialized) && this.field_152951_c.func_152849_q())
            {
                field_152950_b.debug(field_152949_a, "Connecting to twitch chat per user options");
                this.func_152942_I();
            }
        }
        else if (i == 0)
        {
            if ((chatstate == ChatController.ChatState.Disconnected || chatstate == ChatController.ChatState.Uninitialized) && this.func_152934_n())
            {
                field_152950_b.debug(field_152949_a, "Connecting to twitch chat as user is streaming");
                this.func_152942_I();
            }
            else if (chatstate == ChatController.ChatState.Connected && !this.func_152934_n())
            {
                field_152950_b.debug(field_152949_a, "Disconnecting from twitch chat as user is no longer streaming");
                this.field_152952_d.func_153002_l();
            }
        }

        this.field_152951_c.func_152821_H();
        this.field_152952_d.func_152997_n();
    }

    protected void func_152942_I()
    {
        ChatController.ChatState chatstate = this.field_152952_d.func_153000_j();
        String s = this.field_152951_c.func_152843_l().name;

        if (chatstate == ChatController.ChatState.Uninitialized)
        {
            this.field_152952_d.func_152985_f(s);
            this.field_152952_d.field_153005_c = s;
        }
        else if (chatstate == ChatController.ChatState.Disconnected)
        {
            this.field_152952_d.func_152986_d(s);
        }
        else
        {
            field_152950_b.warn("Invalid twitch chat state {}", new Object[] {chatstate});
        }
    }

    public void func_152922_k()
    {
        if (this.field_152951_c.func_152850_m() && !this.field_152951_c.func_152839_p())
        {
            long i = System.nanoTime();
            long j = (long)(1000000000 / this.field_152958_j);
            long k = i - this.field_152959_k;
            boolean flag = k >= j;

            if (flag)
            {
                FrameBuffer framebuffer = this.field_152951_c.func_152822_N();
                Framebuffer framebuffer1 = this.field_152953_e.getFramebuffer();
                this.field_152956_h.bindFramebuffer(true);
                GL11.glMatrixMode(GL11.GL_PROJECTION);
                GL11.glPushMatrix();
                GL11.glLoadIdentity();
                GL11.glOrtho(0.0D, (double)this.field_152956_h.framebufferWidth, (double)this.field_152956_h.framebufferHeight, 0.0D, 1000.0D, 3000.0D);
                GL11.glMatrixMode(GL11.GL_MODELVIEW);
                GL11.glPushMatrix();
                GL11.glLoadIdentity();
                GL11.glTranslatef(0.0F, 0.0F, -2000.0F);
                GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
                GL11.glViewport(0, 0, this.field_152956_h.framebufferWidth, this.field_152956_h.framebufferHeight);
                GL11.glEnable(GL11.GL_TEXTURE_2D);
                GL11.glDisable(GL11.GL_ALPHA_TEST);
                GL11.glDisable(GL11.GL_BLEND);
                float f = (float)this.field_152956_h.framebufferWidth;
                float f1 = (float)this.field_152956_h.framebufferHeight;
                float f2 = (float)framebuffer1.framebufferWidth / (float)framebuffer1.framebufferTextureWidth;
                float f3 = (float)framebuffer1.framebufferHeight / (float)framebuffer1.framebufferTextureHeight;
                framebuffer1.bindFramebufferTexture();
                GL11.glTexParameterf(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, 9729.0F);
                GL11.glTexParameterf(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, 9729.0F);
                Tessellator tessellator = Tessellator.instance;
                tessellator.startDrawingQuads();
                tessellator.addVertexWithUV(0.0D, (double)f1, 0.0D, 0.0D, (double)f3);
                tessellator.addVertexWithUV((double)f, (double)f1, 0.0D, (double)f2, (double)f3);
                tessellator.addVertexWithUV((double)f, 0.0D, 0.0D, (double)f2, 0.0D);
                tessellator.addVertexWithUV(0.0D, 0.0D, 0.0D, 0.0D, 0.0D);
                tessellator.draw();
                framebuffer1.unbindFramebufferTexture();
                GL11.glPopMatrix();
                GL11.glMatrixMode(GL11.GL_PROJECTION);
                GL11.glPopMatrix();
                GL11.glMatrixMode(GL11.GL_MODELVIEW);
                this.field_152951_c.func_152846_a(framebuffer);
                this.field_152956_h.unbindFramebuffer();
                this.field_152951_c.func_152859_b(framebuffer);
                this.field_152959_k = i;
            }
        }
    }

    public boolean func_152936_l()
    {
        return this.field_152951_c.func_152849_q();
    }

    public boolean func_152924_m()
    {
        return this.field_152951_c.func_152857_n();
    }

    public boolean func_152934_n()
    {
        return this.field_152951_c.func_152850_m();
    }

    public void func_152911_a(Metadata p_152911_1_, long p_152911_2_)
    {
        if (this.func_152934_n() && this.field_152957_i)
        {
            long j = this.field_152951_c.func_152844_x();

            if (!this.field_152951_c.func_152840_a(p_152911_1_.func_152810_c(), j + p_152911_2_, p_152911_1_.func_152809_a(), p_152911_1_.func_152806_b()))
            {
                field_152950_b.warn(field_152949_a, "Couldn\'t send stream metadata action at {}: {}", new Object[] {Long.valueOf(j + p_152911_2_), p_152911_1_});
            }
            else
            {
                field_152950_b.debug(field_152949_a, "Sent stream metadata action at {}: {}", new Object[] {Long.valueOf(j + p_152911_2_), p_152911_1_});
            }
        }
    }

    public boolean func_152919_o()
    {
        return this.field_152951_c.func_152839_p();
    }

    public void func_152931_p()
    {
        if (this.field_152951_c.func_152830_D())
        {
            field_152950_b.debug(field_152949_a, "Requested commercial from Twitch");
        }
        else
        {
            field_152950_b.warn(field_152949_a, "Could not request commercial from Twitch");
        }
    }

    public void func_152916_q()
    {
        this.field_152951_c.func_152847_F();
        this.field_152962_n = true;
        this.func_152915_s();
    }

    public void func_152933_r()
    {
        this.field_152951_c.func_152854_G();
        this.field_152962_n = false;
        this.func_152915_s();
    }

    public void func_152915_s()
    {
        if (this.func_152934_n())
        {
            float f = this.field_152953_e.gameSettings.field_152402_L;
            boolean flag = this.field_152962_n || f <= 0.0F;
            this.field_152951_c.func_152837_b(flag ? 0.0F : f);
            this.field_152951_c.func_152829_a(this.func_152929_G() ? 0.0F : this.field_152953_e.gameSettings.field_152401_K);
        }
    }

    public void func_152930_t()
    {
        GameSettings gamesettings = this.field_152953_e.gameSettings;
        VideoParams videoparams = this.field_152951_c.func_152834_a(func_152946_b(gamesettings.field_152403_M), func_152948_a(gamesettings.field_152404_N), func_152947_c(gamesettings.field_152400_J), (float)this.field_152953_e.displayWidth / (float)this.field_152953_e.displayHeight);

        switch (gamesettings.field_152405_O)
        {
            case 0:
                videoparams.encodingCpuUsage = EncodingCpuUsage.TTV_ECU_LOW;
                break;
            case 1:
                videoparams.encodingCpuUsage = EncodingCpuUsage.TTV_ECU_MEDIUM;
                break;
            case 2:
                videoparams.encodingCpuUsage = EncodingCpuUsage.TTV_ECU_HIGH;
        }

        if (this.field_152956_h == null)
        {
            this.field_152956_h = new Framebuffer(videoparams.outputWidth, videoparams.outputHeight, false);
        }
        else
        {
            this.field_152956_h.createBindFramebuffer(videoparams.outputWidth, videoparams.outputHeight);
        }

        if (gamesettings.field_152407_Q != null && gamesettings.field_152407_Q.length() > 0)
        {
            IngestServer[] aingestserver = this.func_152925_v();
            int i = aingestserver.length;

            for (int j = 0; j < i; ++j)
            {
                IngestServer ingestserver = aingestserver[j];

                if (ingestserver.serverUrl.equals(gamesettings.field_152407_Q))
                {
                    this.field_152951_c.func_152824_a(ingestserver);
                    break;
                }
            }
        }

        this.field_152958_j = videoparams.targetFps;
        this.field_152957_i = gamesettings.field_152406_P;
        this.field_152951_c.func_152836_a(videoparams);
        field_152950_b.info(field_152949_a, "Streaming at {}/{} at {} kbps to {}", new Object[] {Integer.valueOf(videoparams.outputWidth), Integer.valueOf(videoparams.outputHeight), Integer.valueOf(videoparams.maxKbps), this.field_152951_c.func_152833_s().serverUrl});
        this.field_152951_c.func_152828_a((String)null, "Minecraft", (String)null);
    }

    public void func_152914_u()
    {
        if (this.field_152951_c.func_152819_E())
        {
            field_152950_b.info(field_152949_a, "Stopped streaming to Twitch");
        }
        else
        {
            field_152950_b.warn(field_152949_a, "Could not stop streaming to Twitch");
        }
    }

    public void func_152900_a(ErrorCode p_152900_1_, AuthToken p_152900_2_) {}

    public void func_152897_a(ErrorCode p_152897_1_)
    {
        if (ErrorCode.succeeded(p_152897_1_))
        {
            field_152950_b.debug(field_152949_a, "Login attempt successful");
            this.field_152961_m = true;
        }
        else
        {
            field_152950_b.warn(field_152949_a, "Login attempt unsuccessful: {} (error code {})", new Object[] {ErrorCode.getString(p_152897_1_), Integer.valueOf(p_152897_1_.getValue())});
            this.field_152961_m = false;
        }
    }

    public void func_152898_a(ErrorCode p_152898_1_, GameInfo[] p_152898_2_) {}

    public void func_152891_a(BroadcastController.BroadcastState p_152891_1_)
    {
        field_152950_b.debug(field_152949_a, "Broadcast state changed to {}", new Object[] {p_152891_1_});

        if (p_152891_1_ == BroadcastController.BroadcastState.Initialized)
        {
            this.field_152951_c.func_152827_a(BroadcastController.BroadcastState.Authenticated);
        }
    }

    public void func_152895_a()
    {
        field_152950_b.info(field_152949_a, "Logged out of twitch");
    }

    public void func_152894_a(StreamInfo p_152894_1_)
    {
        field_152950_b.debug(field_152949_a, "Stream info updated; {} viewers on stream ID {}", new Object[] {Integer.valueOf(p_152894_1_.viewers), Long.valueOf(p_152894_1_.streamId)});
    }

    public void func_152896_a(IngestList p_152896_1_) {}

    public void func_152893_b(ErrorCode p_152893_1_)
    {
        field_152950_b.warn(field_152949_a, "Issue submitting frame: {} (Error code {})", new Object[] {ErrorCode.getString(p_152893_1_), Integer.valueOf(p_152893_1_.getValue())});
        this.field_152953_e.ingameGUI.getChatGUI().printChatMessageWithOptionalDeletion(new ChatComponentText("Issue streaming frame: " + p_152893_1_ + " (" + ErrorCode.getString(p_152893_1_) + ")"), 2);
    }

    public void func_152899_b()
    {
        this.func_152915_s();
        field_152950_b.info(field_152949_a, "Broadcast to Twitch has started");
    }

    public void func_152901_c()
    {
        field_152950_b.info(field_152949_a, "Broadcast to Twitch has stopped");
    }

    public void func_152892_c(ErrorCode p_152892_1_)
    {
        ChatComponentTranslation chatcomponenttranslation;

        if (p_152892_1_ == ErrorCode.TTV_EC_SOUNDFLOWER_NOT_INSTALLED)
        {
            chatcomponenttranslation = new ChatComponentTranslation("stream.unavailable.soundflower.chat.link", new Object[0]);
            chatcomponenttranslation.getChatStyle().setChatClickEvent(new ClickEvent(ClickEvent.Action.OPEN_URL, "https://help.mojang.com/customer/portal/articles/1374877-configuring-soundflower-for-streaming-on-apple-computers"));
            chatcomponenttranslation.getChatStyle().setUnderlined(Boolean.valueOf(true));
            ChatComponentTranslation chatcomponenttranslation1 = new ChatComponentTranslation("stream.unavailable.soundflower.chat", new Object[] {chatcomponenttranslation});
            chatcomponenttranslation1.getChatStyle().setColor(EnumChatFormatting.DARK_RED);
            this.field_152953_e.ingameGUI.getChatGUI().printChatMessage(chatcomponenttranslation1);
        }
        else
        {
            chatcomponenttranslation = new ChatComponentTranslation("stream.unavailable.unknown.chat", new Object[] {ErrorCode.getString(p_152892_1_)});
            chatcomponenttranslation.getChatStyle().setColor(EnumChatFormatting.DARK_RED);
            this.field_152953_e.ingameGUI.getChatGUI().printChatMessage(chatcomponenttranslation);
        }
    }

    public void func_152907_a(IngestServerTester p_152907_1_, IngestServerTester.IngestTestState p_152907_2_)
    {
        field_152950_b.debug(field_152949_a, "Ingest test state changed to {}", new Object[] {p_152907_2_});

        if (p_152907_2_ == IngestServerTester.IngestTestState.Finished)
        {
            this.field_152960_l = true;
        }
    }

    public static int func_152948_a(float p_152948_0_)
    {
        return MathHelper.floor_float(10.0F + p_152948_0_ * 50.0F);
    }

    public static int func_152946_b(float p_152946_0_)
    {
        return MathHelper.floor_float(230.0F + p_152946_0_ * 3270.0F);
    }

    public static float func_152947_c(float p_152947_0_)
    {
        return 0.1F + p_152947_0_ * 0.1F;
    }

    public IngestServer[] func_152925_v()
    {
        return this.field_152951_c.func_152855_t().getServers();
    }

    public void func_152909_x()
    {
        IngestServerTester ingestservertester = this.field_152951_c.func_152838_J();

        if (ingestservertester != null)
        {
            ingestservertester.func_153042_a(this);
        }
    }

    public IngestServerTester func_152932_y()
    {
        return this.field_152951_c.func_152856_w();
    }

    public boolean func_152908_z()
    {
        return this.field_152951_c.func_152825_o();
    }

    public int func_152920_A()
    {
        return this.func_152934_n() ? this.field_152951_c.func_152816_j().viewers : 0;
    }

    public void func_152903_a(ChatMessage[] p_152903_1_)
    {
        ChatMessage[] achatmessage1 = p_152903_1_;
        int i = p_152903_1_.length;

        for (int j = 0; j < i; ++j)
        {
            ChatMessage chatmessage = achatmessage1[j];
            this.func_152939_a(chatmessage.userName, chatmessage);

            if (this.func_152940_a(chatmessage.modes, chatmessage.subscriptions, this.field_152953_e.gameSettings.field_152409_S))
            {
                ChatComponentText chatcomponenttext = new ChatComponentText(chatmessage.userName);
                ChatComponentTranslation chatcomponenttranslation = new ChatComponentTranslation("chat.stream." + (chatmessage.action ? "emote" : "text"), new Object[] {this.field_152954_f, chatcomponenttext, EnumChatFormatting.getTextWithoutFormattingCodes(chatmessage.message)});

                if (chatmessage.action)
                {
                    chatcomponenttranslation.getChatStyle().setItalic(Boolean.valueOf(true));
                }

                ChatComponentText chatcomponenttext1 = new ChatComponentText("");
                chatcomponenttext1.appendSibling(new ChatComponentTranslation("stream.userinfo.chatTooltip", new Object[0]));
                Iterator iterator = GuiTwitchUserMode.func_152328_a(chatmessage.modes, chatmessage.subscriptions, (IStream)null).iterator();

                while (iterator.hasNext())
                {
                    IChatComponent ichatcomponent = (IChatComponent)iterator.next();
                    chatcomponenttext1.appendText("\n");
                    chatcomponenttext1.appendSibling(ichatcomponent);
                }

                chatcomponenttext.getChatStyle().setChatHoverEvent(new HoverEvent(HoverEvent.Action.SHOW_TEXT, chatcomponenttext1));
                chatcomponenttext.getChatStyle().setChatClickEvent(new ClickEvent(ClickEvent.Action.TWITCH_USER_INFO, chatmessage.userName));
                this.field_152953_e.ingameGUI.getChatGUI().printChatMessage(chatcomponenttranslation);
            }
        }
    }

    private void func_152939_a(String p_152939_1_, ChatMessage p_152939_2_)
    {
        ChatUserInfo chatuserinfo = (ChatUserInfo)this.field_152955_g.get(p_152939_1_);

        if (chatuserinfo == null)
        {
            chatuserinfo = new ChatUserInfo();
            chatuserinfo.displayName = p_152939_1_;
            this.field_152955_g.put(p_152939_1_, chatuserinfo);
        }

        chatuserinfo.subscriptions = p_152939_2_.subscriptions;
        chatuserinfo.modes = p_152939_2_.modes;
        chatuserinfo.emoticonSet = p_152939_2_.emoticonSet;
        chatuserinfo.nameColorARGB = p_152939_2_.nameColorARGB;
    }

    private boolean func_152940_a(HashSet p_152940_1_, HashSet p_152940_2_, int p_152940_3_)
    {
        return p_152940_1_.contains(ChatUserMode.TTV_CHAT_USERMODE_BANNED) ? false : (p_152940_1_.contains(ChatUserMode.TTV_CHAT_USERMODE_ADMINSTRATOR) ? true : (p_152940_1_.contains(ChatUserMode.TTV_CHAT_USERMODE_MODERATOR) ? true : (p_152940_1_.contains(ChatUserMode.TTV_CHAT_USERMODE_STAFF) ? true : (p_152940_3_ == 0 ? true : (p_152940_3_ == 1 ? p_152940_2_.contains(ChatUserSubscription.TTV_CHAT_USERSUB_SUBSCRIBER) : false)))));
    }

    public void func_152904_a(ChatUserInfo[] p_152904_1_, ChatUserInfo[] p_152904_2_, ChatUserInfo[] p_152904_3_)
    {
        ChatUserInfo[] achatuserinfo3 = p_152904_2_;
        int i = p_152904_2_.length;
        int j;
        ChatUserInfo chatuserinfo;

        for (j = 0; j < i; ++j)
        {
            chatuserinfo = achatuserinfo3[j];
            this.field_152955_g.remove(chatuserinfo.displayName);
        }

        achatuserinfo3 = p_152904_3_;
        i = p_152904_3_.length;

        for (j = 0; j < i; ++j)
        {
            chatuserinfo = achatuserinfo3[j];
            this.field_152955_g.put(chatuserinfo.displayName, chatuserinfo);
        }

        achatuserinfo3 = p_152904_1_;
        i = p_152904_1_.length;

        for (j = 0; j < i; ++j)
        {
            chatuserinfo = achatuserinfo3[j];
            this.field_152955_g.put(chatuserinfo.displayName, chatuserinfo);
        }
    }

    public void func_152906_d()
    {
        field_152950_b.debug(field_152949_a, "Chat connected");
    }

    public void func_152905_e()
    {
        field_152950_b.debug(field_152949_a, "Chat disconnected");
        this.field_152955_g.clear();
    }

    public void func_152902_f() {}

    public boolean func_152927_B()
    {
        return this.field_152952_d.func_152991_c() && this.field_152952_d.field_153005_c.equals(this.field_152951_c.func_152843_l().name);
    }

    public String func_152921_C()
    {
        return this.field_152952_d.field_153005_c;
    }

    public ChatUserInfo func_152926_a(String p_152926_1_)
    {
        return (ChatUserInfo)this.field_152955_g.get(p_152926_1_);
    }

    public void func_152917_b(String p_152917_1_)
    {
        this.field_152952_d.func_152992_g(p_152917_1_);
    }

    public boolean func_152928_D()
    {
        return field_152965_q && this.field_152951_c.func_152858_b();
    }

    public ErrorCode func_152912_E()
    {
        return !field_152965_q ? ErrorCode.TTV_EC_OS_TOO_OLD : this.field_152951_c.func_152852_P();
    }

    public boolean func_152913_F()
    {
        return this.field_152961_m;
    }

    public void func_152910_a(boolean p_152910_1_)
    {
        this.field_152963_o = p_152910_1_;
        this.func_152915_s();
    }

    public boolean func_152929_G()
    {
        boolean flag = this.field_152953_e.gameSettings.field_152410_T == 1;
        return this.field_152962_n || this.field_152953_e.gameSettings.field_152401_K <= 0.0F || flag != this.field_152963_o;
    }

    public IStream.AuthFailureReason func_152918_H()
    {
        return this.field_152964_p;
    }

    static
    {
        try
        {
            if (Util.getOSType() == Util.EnumOS.WINDOWS)
            {
                System.loadLibrary("avutil-ttv-51");
                System.loadLibrary("swresample-ttv-0");
                System.loadLibrary("libmp3lame-ttv");

                if (System.getProperty("os.arch").contains("64"))
                {
                    System.loadLibrary("libmfxsw64");
                }
                else
                {
                    System.loadLibrary("libmfxsw32");
                }
            }

            field_152965_q = true;
        }
        catch (Throwable var1)
        {
            field_152965_q = false;
        }
    }
}