package net.minecraft.client.stream;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.List;
import tv.twitch.AuthToken;
import tv.twitch.ErrorCode;
import tv.twitch.broadcast.ArchivingState;
import tv.twitch.broadcast.AudioParams;
import tv.twitch.broadcast.ChannelInfo;
import tv.twitch.broadcast.EncodingCpuUsage;
import tv.twitch.broadcast.FrameBuffer;
import tv.twitch.broadcast.GameInfoList;
import tv.twitch.broadcast.IStatCallbacks;
import tv.twitch.broadcast.IStreamCallbacks;
import tv.twitch.broadcast.IngestList;
import tv.twitch.broadcast.IngestServer;
import tv.twitch.broadcast.PixelFormat;
import tv.twitch.broadcast.RTMPState;
import tv.twitch.broadcast.StartFlags;
import tv.twitch.broadcast.StatType;
import tv.twitch.broadcast.Stream;
import tv.twitch.broadcast.StreamInfo;
import tv.twitch.broadcast.UserInfo;
import tv.twitch.broadcast.VideoParams;

@SideOnly(Side.CLIENT)
public class IngestServerTester implements IStatCallbacks, IStreamCallbacks
{
    protected final boolean field_153043_a = true;
    protected IngestServerTester.IngestTestListener field_153044_b = null;
    protected Stream field_153045_c = null;
    protected IngestList field_153046_d = null;
    protected IngestServerTester.IngestTestState field_153047_e;
    protected long field_153048_f;
    protected long field_153049_g;
    protected long field_153050_h;
    protected RTMPState field_153051_i;
    protected VideoParams field_153052_j;
    protected AudioParams field_153053_k;
    protected long field_153054_l;
    protected List field_153055_m;
    protected boolean field_153056_n;
    protected IStreamCallbacks field_153057_o;
    protected IStatCallbacks field_153058_p;
    protected IngestServer field_153059_q;
    protected boolean field_153060_r;
    protected boolean field_153061_s;
    protected int field_153062_t;
    protected int field_153063_u;
    protected long field_153064_v;
    protected float field_153065_w;
    protected float field_153066_x;
    protected boolean field_153067_y;
    private static final String __OBFID = "CL_00001816";

    public void func_153042_a(IngestServerTester.IngestTestListener p_153042_1_)
    {
        this.field_153044_b = p_153042_1_;
    }

    public IngestServer func_153040_c()
    {
        return this.field_153059_q;
    }

    public boolean func_153032_e()
    {
        return this.field_153047_e == IngestServerTester.IngestTestState.Finished || this.field_153047_e == IngestServerTester.IngestTestState.Cancelled || this.field_153047_e == IngestServerTester.IngestTestState.Failed;
    }

    public float func_153030_h()
    {
        return this.field_153066_x;
    }

    public void requestAuthTokenCallback(ErrorCode p_requestAuthTokenCallback_1_, AuthToken p_requestAuthTokenCallback_2_) {}

    public void loginCallback(ErrorCode p_loginCallback_1_, ChannelInfo p_loginCallback_2_) {}

    public void getIngestServersCallback(ErrorCode p_getIngestServersCallback_1_, IngestList p_getIngestServersCallback_2_) {}

    public void getUserInfoCallback(ErrorCode p_getUserInfoCallback_1_, UserInfo p_getUserInfoCallback_2_) {}

    public void getStreamInfoCallback(ErrorCode p_getStreamInfoCallback_1_, StreamInfo p_getStreamInfoCallback_2_) {}

    public void getArchivingStateCallback(ErrorCode p_getArchivingStateCallback_1_, ArchivingState p_getArchivingStateCallback_2_) {}

    public void runCommercialCallback(ErrorCode p_runCommercialCallback_1_) {}

    public void setStreamInfoCallback(ErrorCode p_setStreamInfoCallback_1_) {}

    public void getGameNameListCallback(ErrorCode p_getGameNameListCallback_1_, GameInfoList p_getGameNameListCallback_2_) {}

    public void bufferUnlockCallback(long p_bufferUnlockCallback_1_) {}

    public void startCallback(ErrorCode p_startCallback_1_)
    {
        this.field_153067_y = false;

        if (ErrorCode.succeeded(p_startCallback_1_))
        {
            this.func_153034_a(IngestServerTester.IngestTestState.ConnectingToServer);
            this.field_153054_l = System.currentTimeMillis();
        }
        else
        {
            this.field_153056_n = false;
            this.func_153034_a(IngestServerTester.IngestTestState.DoneTestingServer);
        }
    }

    public void stopCallback(ErrorCode p_stopCallback_1_)
    {
        this.field_153067_y = false;
        this.func_153034_a(IngestServerTester.IngestTestState.DoneTestingServer);
    }

    public void sendActionMetaDataCallback(ErrorCode p_sendActionMetaDataCallback_1_) {}

    public void sendStartSpanMetaDataCallback(ErrorCode p_sendStartSpanMetaDataCallback_1_) {}

    public void sendEndSpanMetaDataCallback(ErrorCode p_sendEndSpanMetaDataCallback_1_) {}

    public void statCallback(StatType p_statCallback_1_, long p_statCallback_2_)
    {
        switch (IngestServerTester.SwitchStatType.field_153026_a[p_statCallback_1_.ordinal()])
        {
            case 1:
                this.field_153051_i = RTMPState.lookupValue((int)p_statCallback_2_);
                break;
            case 2:
                this.field_153050_h = p_statCallback_2_;
        }
    }

    public IngestServerTester(Stream p_i1019_1_, IngestList p_i1019_2_)
    {
        this.field_153047_e = IngestServerTester.IngestTestState.Uninitalized;
        this.field_153048_f = 8000L;
        this.field_153049_g = 1000L;
        this.field_153050_h = 0L;
        this.field_153051_i = RTMPState.Invalid;
        this.field_153052_j = null;
        this.field_153053_k = null;
        this.field_153054_l = 0L;
        this.field_153055_m = null;
        this.field_153056_n = false;
        this.field_153057_o = null;
        this.field_153058_p = null;
        this.field_153059_q = null;
        this.field_153060_r = false;
        this.field_153061_s = false;
        this.field_153062_t = -1;
        this.field_153063_u = 0;
        this.field_153064_v = 0L;
        this.field_153065_w = 0.0F;
        this.field_153066_x = 0.0F;
        this.field_153067_y = false;
        this.field_153045_c = p_i1019_1_;
        this.field_153046_d = p_i1019_2_;
    }

    protected void finalize() throws Throwable
    {
        if (this.field_153059_q != null)
        {
            this.func_153035_b(this.field_153059_q);
        }

        this.func_153031_o();
        super.finalize();
    }

    public void func_153033_i()
    {
        if (this.field_153047_e == IngestServerTester.IngestTestState.Uninitalized)
        {
            this.field_153062_t = 0;
            this.field_153060_r = false;
            this.field_153061_s = false;
            this.field_153058_p = this.field_153045_c.getStatCallbacks();
            this.field_153045_c.setStatCallbacks(this);
            this.field_153057_o = this.field_153045_c.getStreamCallbacks();
            this.field_153045_c.setStreamCallbacks(this);
            this.field_153052_j = new VideoParams();
            this.field_153052_j.targetFps = 60;
            this.field_153052_j.maxKbps = 3500;
            this.field_153052_j.outputWidth = 1280;
            this.field_153052_j.outputHeight = 720;
            this.field_153052_j.pixelFormat = PixelFormat.TTV_PF_BGRA;
            this.field_153052_j.encodingCpuUsage = EncodingCpuUsage.TTV_ECU_HIGH;
            this.field_153052_j.disableAdaptiveBitrate = true;
            this.field_153052_j.verticalFlip = false;
            this.field_153045_c.getDefaultParams(this.field_153052_j);
            this.field_153053_k = new AudioParams();
            this.field_153053_k.audioEnabled = false;
            this.field_153055_m = new ArrayList();
            byte b0 = 3;

            for (int i = 0; i < b0; ++i)
            {
                FrameBuffer framebuffer = this.field_153045_c.allocateFrameBuffer(this.field_153052_j.outputWidth * this.field_153052_j.outputHeight * 4);

                if (!framebuffer.getIsValid())
                {
                    this.func_153031_o();
                    this.func_153034_a(IngestServerTester.IngestTestState.Failed);
                    return;
                }

                this.field_153055_m.add(framebuffer);
                this.field_153045_c.randomizeFrameBuffer(framebuffer);
            }

            this.func_153034_a(IngestServerTester.IngestTestState.Starting);
            this.field_153054_l = System.currentTimeMillis();
        }
    }

    public void func_153041_j()
    {
        if (!this.func_153032_e() && this.field_153047_e != IngestServerTester.IngestTestState.Uninitalized)
        {
            if (!this.field_153067_y)
            {
                if (this.field_153060_r)
                {
                    this.func_153034_a(IngestServerTester.IngestTestState.Cancelled);
                }

                switch (IngestServerTester.SwitchStatType.field_153027_b[this.field_153047_e.ordinal()])
                {
                    case 1:
                    case 2:
                        if (this.field_153059_q != null)
                        {
                            if (!this.field_153061_s && this.field_153056_n)
                            {
                                this.func_153035_b(this.field_153059_q);
                            }
                            else
                            {
                                this.field_153059_q.bitrateKbps = 0.0F;
                                this.field_153054_l = 0L;
                                this.field_153067_y = false;
                            }

                            this.field_153059_q = null;
                        }
                        else if (!this.field_153067_y && this.func_153037_m() >= this.field_153049_g)
                        {
                            this.field_153054_l = 0L;
                            this.field_153061_s = false;
                            this.field_153056_n = true;

                            if (this.field_153047_e != IngestServerTester.IngestTestState.Starting)
                            {
                                ++this.field_153062_t;
                            }

                            if (this.field_153062_t < this.field_153046_d.getServers().length)
                            {
                                this.field_153059_q = this.field_153046_d.getServers()[this.field_153062_t];
                                this.func_153036_a(this.field_153059_q);
                            }
                            else
                            {
                                this.func_153034_a(IngestServerTester.IngestTestState.Finished);
                            }
                        }

                        break;
                    case 3:
                    case 4:
                        this.func_153029_c(this.field_153059_q);
                }

                this.func_153038_n();

                if (this.field_153047_e == IngestServerTester.IngestTestState.Cancelled || this.field_153047_e == IngestServerTester.IngestTestState.Finished)
                {
                    if (this.field_153059_q != null)
                    {
                        if (this.field_153047_e == IngestServerTester.IngestTestState.Cancelled)
                        {
                            this.field_153059_q.bitrateKbps = 0.0F;
                        }

                        this.func_153035_b(this.field_153059_q);
                        this.field_153059_q = null;
                    }

                    if (this.field_153055_m != null)
                    {
                        this.func_153031_o();
                    }
                }
            }
        }
    }

    public void func_153039_l()
    {
        if (!this.func_153032_e())
        {
            this.field_153060_r = true;
        }
    }

    protected boolean func_153036_a(IngestServer p_153036_1_)
    {
        this.field_153056_n = true;
        this.field_153050_h = 0L;
        this.field_153051_i = RTMPState.Idle;
        this.field_153059_q = p_153036_1_;
        this.func_153034_a(IngestServerTester.IngestTestState.ConnectingToServer);
        this.field_153067_y = true;
        ErrorCode errorcode = this.field_153045_c.start(this.field_153052_j, this.field_153053_k, p_153036_1_, StartFlags.TTV_Start_BandwidthTest, true);

        if (ErrorCode.failed(errorcode))
        {
            this.field_153056_n = false;
            this.func_153034_a(IngestServerTester.IngestTestState.DoneTestingServer);
            return false;
        }
        else
        {
            this.field_153064_v = this.field_153050_h;
            p_153036_1_.bitrateKbps = 0.0F;
            this.field_153063_u = 0;
            return true;
        }
    }

    protected void func_153035_b(IngestServer p_153035_1_)
    {
        this.field_153067_y = true;
        this.field_153045_c.stop(true);
        this.field_153045_c.pollStats();
    }

    protected long func_153037_m()
    {
        return System.currentTimeMillis() - this.field_153054_l;
    }

    protected void func_153038_n()
    {
        float f = (float)this.func_153037_m();

        switch (IngestServerTester.SwitchStatType.field_153027_b[this.field_153047_e.ordinal()])
        {
            case 1:
            case 3:
            case 5:
            case 6:
            case 7:
            case 8:
                this.field_153066_x = 0.0F;
                break;
            case 2:
                this.field_153066_x = 1.0F;
                break;
            case 4:
            default:
                this.field_153066_x = f / (float)this.field_153048_f;
        }

        switch (IngestServerTester.SwitchStatType.field_153027_b[this.field_153047_e.ordinal()])
        {
            case 6:
            case 7:
            case 8:
                this.field_153065_w = 1.0F;
                break;
            default:
                this.field_153065_w = (float)this.field_153062_t / (float)this.field_153046_d.getServers().length;
                this.field_153065_w += this.field_153066_x / (float)this.field_153046_d.getServers().length;
        }
    }

    protected boolean func_153029_c(IngestServer p_153029_1_)
    {
        if (!this.field_153061_s && this.func_153037_m() < this.field_153048_f)
        {
            if (this.field_153067_y)
            {
                return true;
            }
            else
            {
                ErrorCode errorcode = this.field_153045_c.submitVideoFrame((FrameBuffer)this.field_153055_m.get(this.field_153063_u));

                if (ErrorCode.failed(errorcode))
                {
                    this.field_153056_n = false;
                    this.func_153034_a(IngestServerTester.IngestTestState.DoneTestingServer);
                    return false;
                }
                else
                {
                    this.field_153063_u = (this.field_153063_u + 1) % this.field_153055_m.size();
                    this.field_153045_c.pollStats();

                    if (this.field_153051_i == RTMPState.SendVideo)
                    {
                        this.func_153034_a(IngestServerTester.IngestTestState.TestingServer);
                        long i = this.func_153037_m();

                        if (i > 0L && this.field_153050_h > this.field_153064_v)
                        {
                            p_153029_1_.bitrateKbps = (float)(this.field_153050_h * 8L) / (float)this.func_153037_m();
                            this.field_153064_v = this.field_153050_h;
                        }
                    }

                    return true;
                }
            }
        }
        else
        {
            this.func_153034_a(IngestServerTester.IngestTestState.DoneTestingServer);
            return true;
        }
    }

    protected void func_153031_o()
    {
        this.field_153059_q = null;

        if (this.field_153055_m != null)
        {
            for (int i = 0; i < this.field_153055_m.size(); ++i)
            {
                ((FrameBuffer)this.field_153055_m.get(i)).free();
            }

            this.field_153055_m = null;
        }

        if (this.field_153045_c.getStatCallbacks() == this)
        {
            this.field_153045_c.setStatCallbacks(this.field_153058_p);
            this.field_153058_p = null;
        }

        if (this.field_153045_c.getStreamCallbacks() == this)
        {
            this.field_153045_c.setStreamCallbacks(this.field_153057_o);
            this.field_153057_o = null;
        }
    }

    protected void func_153034_a(IngestServerTester.IngestTestState p_153034_1_)
    {
        if (p_153034_1_ != this.field_153047_e)
        {
            this.field_153047_e = p_153034_1_;

            if (this.field_153044_b != null)
            {
                this.field_153044_b.func_152907_a(this, p_153034_1_);
            }
        }
    }

    public int func_153028_p()
    {
        return this.field_153062_t;
    }

    @SideOnly(Side.CLIENT)
    public interface IngestTestListener
    {
        void func_152907_a(IngestServerTester p_152907_1_, IngestServerTester.IngestTestState p_152907_2_);
    }

    @SideOnly(Side.CLIENT)
    public static enum IngestTestState
    {
        Uninitalized,
        Starting,
        ConnectingToServer,
        TestingServer,
        DoneTestingServer,
        Finished,
        Cancelled,
        Failed;

        private static final String __OBFID = "CL_00001814";
    }

    @SideOnly(Side.CLIENT)

    static final class SwitchStatType
        {
            static final int[] field_153026_a;

            static final int[] field_153027_b = new int[IngestServerTester.IngestTestState.values().length];
            private static final String __OBFID = "CL_00001815";

            static
            {
                try
                {
                    field_153027_b[IngestServerTester.IngestTestState.Starting.ordinal()] = 1;
                }
                catch (NoSuchFieldError var10)
                {
                    ;
                }

                try
                {
                    field_153027_b[IngestServerTester.IngestTestState.DoneTestingServer.ordinal()] = 2;
                }
                catch (NoSuchFieldError var9)
                {
                    ;
                }

                try
                {
                    field_153027_b[IngestServerTester.IngestTestState.ConnectingToServer.ordinal()] = 3;
                }
                catch (NoSuchFieldError var8)
                {
                    ;
                }

                try
                {
                    field_153027_b[IngestServerTester.IngestTestState.TestingServer.ordinal()] = 4;
                }
                catch (NoSuchFieldError var7)
                {
                    ;
                }

                try
                {
                    field_153027_b[IngestServerTester.IngestTestState.Uninitalized.ordinal()] = 5;
                }
                catch (NoSuchFieldError var6)
                {
                    ;
                }

                try
                {
                    field_153027_b[IngestServerTester.IngestTestState.Finished.ordinal()] = 6;
                }
                catch (NoSuchFieldError var5)
                {
                    ;
                }

                try
                {
                    field_153027_b[IngestServerTester.IngestTestState.Cancelled.ordinal()] = 7;
                }
                catch (NoSuchFieldError var4)
                {
                    ;
                }

                try
                {
                    field_153027_b[IngestServerTester.IngestTestState.Failed.ordinal()] = 8;
                }
                catch (NoSuchFieldError var3)
                {
                    ;
                }

                field_153026_a = new int[StatType.values().length];

                try
                {
                    field_153026_a[StatType.TTV_ST_RTMPSTATE.ordinal()] = 1;
                }
                catch (NoSuchFieldError var2)
                {
                    ;
                }

                try
                {
                    field_153026_a[StatType.TTV_ST_RTMPDATASENT.ordinal()] = 2;
                }
                catch (NoSuchFieldError var1)
                {
                    ;
                }
            }
        }
}