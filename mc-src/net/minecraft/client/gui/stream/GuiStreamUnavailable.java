package net.minecraft.client.gui.stream;

import com.google.common.collect.Lists;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.net.URI;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiButton;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.stream.IStream;
import net.minecraft.client.stream.NullStream;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.IChatComponent;
import net.minecraft.util.Session;
import net.minecraft.util.Util;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GLContext;
import tv.twitch.ErrorCode;

@SideOnly(Side.CLIENT)
public class GuiStreamUnavailable extends GuiScreen
{
    private static final Logger field_152322_a = LogManager.getLogger();
    private final IChatComponent field_152324_f;
    private final GuiScreen field_152325_g;
    private final GuiStreamUnavailable.Reason field_152326_h;
    private final List field_152327_i;
    private final List field_152323_r;
    private static final String __OBFID = "CL_00001840";

    public GuiStreamUnavailable(GuiScreen p_i1070_1_, GuiStreamUnavailable.Reason p_i1070_2_)
    {
        this(p_i1070_1_, p_i1070_2_, (List)null);
    }

    public GuiStreamUnavailable(GuiScreen p_i1071_1_, GuiStreamUnavailable.Reason p_i1071_2_, List p_i1071_3_)
    {
        this.field_152324_f = new ChatComponentTranslation("stream.unavailable.title", new Object[0]);
        this.field_152323_r = Lists.newArrayList();
        this.field_152325_g = p_i1071_1_;
        this.field_152326_h = p_i1071_2_;
        this.field_152327_i = p_i1071_3_;
    }

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        if (this.field_152323_r.isEmpty())
        {
            this.field_152323_r.addAll(this.fontRendererObj.listFormattedStringToWidth(this.field_152326_h.func_152561_a().getFormattedText(), (int)((float)this.width * 0.75F)));

            if (this.field_152327_i != null)
            {
                this.field_152323_r.add("");
                Iterator iterator = this.field_152327_i.iterator();

                while (iterator.hasNext())
                {
                    ChatComponentTranslation chatcomponenttranslation = (ChatComponentTranslation)iterator.next();
                    this.field_152323_r.add(chatcomponenttranslation.getUnformattedTextForChat());
                }
            }
        }

        if (this.field_152326_h.func_152559_b() != null)
        {
            this.buttonList.add(new GuiButton(0, this.width / 2 - 155, this.height - 50, 150, 20, I18n.format("gui.cancel", new Object[0])));
            this.buttonList.add(new GuiButton(1, this.width / 2 - 155 + 160, this.height - 50, 150, 20, I18n.format(this.field_152326_h.func_152559_b().getFormattedText(), new Object[0])));
        }
        else
        {
            this.buttonList.add(new GuiButton(0, this.width / 2 - 75, this.height - 50, 150, 20, I18n.format("gui.cancel", new Object[0])));
        }
    }

    /**
     * Called when the screen is unloaded. Used to disable keyboard repeat events
     */
    public void onGuiClosed() {}

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawDefaultBackground();
        int k = Math.max((int)((double)this.height * 0.85D / 2.0D - (double)((float)(this.field_152323_r.size() * this.fontRendererObj.FONT_HEIGHT) / 2.0F)), 50);
        this.drawCenteredString(this.fontRendererObj, this.field_152324_f.getFormattedText(), this.width / 2, k - this.fontRendererObj.FONT_HEIGHT * 2, 16777215);

        for (Iterator iterator = this.field_152323_r.iterator(); iterator.hasNext(); k += this.fontRendererObj.FONT_HEIGHT)
        {
            String s = (String)iterator.next();
            this.drawCenteredString(this.fontRendererObj, s, this.width / 2, k, 10526880);
        }

        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        if (p_146284_1_.enabled)
        {
            if (p_146284_1_.id == 1)
            {
                switch (GuiStreamUnavailable.SwitchReason.field_152577_a[this.field_152326_h.ordinal()])
                {
                    case 1:
                    case 2:
                        this.func_152320_a("https://account.mojang.com/me/settings");
                        break;
                    case 3:
                        this.func_152320_a("https://account.mojang.com/migrate");
                        break;
                    case 4:
                        this.func_152320_a("http://www.apple.com/osx/");
                        break;
                    case 5:
                    case 6:
                    case 7:
                        this.func_152320_a("http://bugs.mojang.com/browse/MC");
                }
            }

            this.mc.displayGuiScreen(this.field_152325_g);
        }
    }

    private void func_152320_a(String p_152320_1_)
    {
        try
        {
            Class oclass = Class.forName("java.awt.Desktop");
            Object object = oclass.getMethod("getDesktop", new Class[0]).invoke((Object)null, new Object[0]);
            oclass.getMethod("browse", new Class[] {URI.class}).invoke(object, new Object[] {new URI(p_152320_1_)});
        }
        catch (Throwable throwable)
        {
            field_152322_a.error("Couldn\'t open link", throwable);
        }
    }

    public static void func_152321_a(GuiScreen p_152321_0_)
    {
        Minecraft minecraft = Minecraft.getMinecraft();
        IStream istream = minecraft.func_152346_Z();

        if (!OpenGlHelper.framebufferSupported)
        {
            ArrayList arraylist = Lists.newArrayList();
            arraylist.add(new ChatComponentTranslation("stream.unavailable.no_fbo.version", new Object[] {GL11.glGetString(GL11.GL_VERSION)}));
            arraylist.add(new ChatComponentTranslation("stream.unavailable.no_fbo.blend", new Object[] {Boolean.valueOf(GLContext.getCapabilities().GL_EXT_blend_func_separate)}));
            arraylist.add(new ChatComponentTranslation("stream.unavailable.no_fbo.arb", new Object[] {Boolean.valueOf(GLContext.getCapabilities().GL_ARB_framebuffer_object)}));
            arraylist.add(new ChatComponentTranslation("stream.unavailable.no_fbo.ext", new Object[] {Boolean.valueOf(GLContext.getCapabilities().GL_EXT_framebuffer_object)}));
            minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.NO_FBO, arraylist));
        }
        else if (istream instanceof NullStream)
        {
            if (((NullStream)istream).func_152937_a().getMessage().contains("Can\'t load AMD 64-bit .dll on a IA 32-bit platform"))
            {
                minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.LIBRARY_ARCH_MISMATCH));
            }
            else
            {
                minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.LIBRARY_FAILURE));
            }
        }
        else if (!istream.func_152928_D() && istream.func_152912_E() == ErrorCode.TTV_EC_OS_TOO_OLD)
        {
            switch (GuiStreamUnavailable.SwitchReason.field_152578_b[Util.getOSType().ordinal()])
            {
                case 1:
                    minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.UNSUPPORTED_OS_WINDOWS));
                    break;
                case 2:
                    minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.UNSUPPORTED_OS_MAC));
                    break;
                default:
                    minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.UNSUPPORTED_OS_OTHER));
            }
        }
        else if (!minecraft.func_152341_N().containsKey("twitch_access_token"))
        {
            if (minecraft.getSession().func_152428_f() == Session.Type.LEGACY)
            {
                minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.ACCOUNT_NOT_MIGRATED));
            }
            else
            {
                minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.ACCOUNT_NOT_BOUND));
            }
        }
        else if (!istream.func_152913_F())
        {
            switch (GuiStreamUnavailable.SwitchReason.field_152579_c[istream.func_152918_H().ordinal()])
            {
                case 1:
                    minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.FAILED_TWITCH_AUTH));
                    break;
                case 2:
                default:
                    minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.FAILED_TWITCH_AUTH_ERROR));
            }
        }
        else if (istream.func_152912_E() != null)
        {
            List list = Arrays.asList(new ChatComponentTranslation[] {new ChatComponentTranslation("stream.unavailable.initialization_failure.extra", new Object[]{ErrorCode.getString(istream.func_152912_E())})});
            minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.INITIALIZATION_FAILURE, list));
        }
        else
        {
            minecraft.displayGuiScreen(new GuiStreamUnavailable(p_152321_0_, GuiStreamUnavailable.Reason.UNKNOWN));
        }
    }

    @SideOnly(Side.CLIENT)
    public static enum Reason
    {
        NO_FBO(new ChatComponentTranslation("stream.unavailable.no_fbo", new Object[0])),
        LIBRARY_ARCH_MISMATCH(new ChatComponentTranslation("stream.unavailable.library_arch_mismatch", new Object[0])),
        LIBRARY_FAILURE(new ChatComponentTranslation("stream.unavailable.library_failure", new Object[0]), new ChatComponentTranslation("stream.unavailable.report_to_mojang", new Object[0])),
        UNSUPPORTED_OS_WINDOWS(new ChatComponentTranslation("stream.unavailable.not_supported.windows", new Object[0])),
        UNSUPPORTED_OS_MAC(new ChatComponentTranslation("stream.unavailable.not_supported.mac", new Object[0]), new ChatComponentTranslation("stream.unavailable.not_supported.mac.okay", new Object[0])),
        UNSUPPORTED_OS_OTHER(new ChatComponentTranslation("stream.unavailable.not_supported.other", new Object[0])),
        ACCOUNT_NOT_MIGRATED(new ChatComponentTranslation("stream.unavailable.account_not_migrated", new Object[0]), new ChatComponentTranslation("stream.unavailable.account_not_migrated.okay", new Object[0])),
        ACCOUNT_NOT_BOUND(new ChatComponentTranslation("stream.unavailable.account_not_bound", new Object[0]), new ChatComponentTranslation("stream.unavailable.account_not_bound.okay", new Object[0])),
        FAILED_TWITCH_AUTH(new ChatComponentTranslation("stream.unavailable.failed_auth", new Object[0]), new ChatComponentTranslation("stream.unavailable.failed_auth.okay", new Object[0])),
        FAILED_TWITCH_AUTH_ERROR(new ChatComponentTranslation("stream.unavailable.failed_auth_error", new Object[0])),
        INITIALIZATION_FAILURE(new ChatComponentTranslation("stream.unavailable.initialization_failure", new Object[0]), new ChatComponentTranslation("stream.unavailable.report_to_mojang", new Object[0])),
        UNKNOWN(new ChatComponentTranslation("stream.unavailable.unknown", new Object[0]), new ChatComponentTranslation("stream.unavailable.report_to_mojang", new Object[0]));
        private final IChatComponent field_152574_m;
        private final IChatComponent field_152575_n;

        private static final String __OBFID = "CL_00001838";

        private Reason(IChatComponent p_i1066_3_)
        {
            this(p_i1066_3_, (IChatComponent)null);
        }

        private Reason(IChatComponent p_i1067_3_, IChatComponent p_i1067_4_)
        {
            this.field_152574_m = p_i1067_3_;
            this.field_152575_n = p_i1067_4_;
        }

        public IChatComponent func_152561_a()
        {
            return this.field_152574_m;
        }

        public IChatComponent func_152559_b()
        {
            return this.field_152575_n;
        }
    }

    @SideOnly(Side.CLIENT)

    static final class SwitchReason
        {
            static final int[] field_152577_a;

            static final int[] field_152578_b;

            static final int[] field_152579_c = new int[IStream.AuthFailureReason.values().length];
            private static final String __OBFID = "CL_00001839";

            static
            {
                try
                {
                    field_152579_c[IStream.AuthFailureReason.INVALID_TOKEN.ordinal()] = 1;
                }
                catch (NoSuchFieldError var11)
                {
                    ;
                }

                try
                {
                    field_152579_c[IStream.AuthFailureReason.ERROR.ordinal()] = 2;
                }
                catch (NoSuchFieldError var10)
                {
                    ;
                }

                field_152578_b = new int[Util.EnumOS.values().length];

                try
                {
                    field_152578_b[Util.EnumOS.WINDOWS.ordinal()] = 1;
                }
                catch (NoSuchFieldError var9)
                {
                    ;
                }

                try
                {
                    field_152578_b[Util.EnumOS.OSX.ordinal()] = 2;
                }
                catch (NoSuchFieldError var8)
                {
                    ;
                }

                field_152577_a = new int[GuiStreamUnavailable.Reason.values().length];

                try
                {
                    field_152577_a[GuiStreamUnavailable.Reason.ACCOUNT_NOT_BOUND.ordinal()] = 1;
                }
                catch (NoSuchFieldError var7)
                {
                    ;
                }

                try
                {
                    field_152577_a[GuiStreamUnavailable.Reason.FAILED_TWITCH_AUTH.ordinal()] = 2;
                }
                catch (NoSuchFieldError var6)
                {
                    ;
                }

                try
                {
                    field_152577_a[GuiStreamUnavailable.Reason.ACCOUNT_NOT_MIGRATED.ordinal()] = 3;
                }
                catch (NoSuchFieldError var5)
                {
                    ;
                }

                try
                {
                    field_152577_a[GuiStreamUnavailable.Reason.UNSUPPORTED_OS_MAC.ordinal()] = 4;
                }
                catch (NoSuchFieldError var4)
                {
                    ;
                }

                try
                {
                    field_152577_a[GuiStreamUnavailable.Reason.UNKNOWN.ordinal()] = 5;
                }
                catch (NoSuchFieldError var3)
                {
                    ;
                }

                try
                {
                    field_152577_a[GuiStreamUnavailable.Reason.LIBRARY_FAILURE.ordinal()] = 6;
                }
                catch (NoSuchFieldError var2)
                {
                    ;
                }

                try
                {
                    field_152577_a[GuiStreamUnavailable.Reason.INITIALIZATION_FAILURE.ordinal()] = 7;
                }
                catch (NoSuchFieldError var1)
                {
                    ;
                }
            }
        }
}