package net.minecraft.server.management;

import com.google.gson.JsonObject;
import java.util.Date;

public class IPBanEntry extends BanEntry
{
    private static final String __OBFID = "CL_00001883";

    public IPBanEntry(String p_i1158_1_)
    {
        this(p_i1158_1_, (Date)null, (String)null, (Date)null, (String)null);
    }

    public IPBanEntry(String p_i1159_1_, Date p_i1159_2_, String p_i1159_3_, Date p_i1159_4_, String p_i1159_5_)
    {
        super(p_i1159_1_, p_i1159_2_, p_i1159_3_, p_i1159_4_, p_i1159_5_);
    }

    public IPBanEntry(JsonObject p_i1160_1_)
    {
        super(func_152647_b(p_i1160_1_), p_i1160_1_);
    }

    private static String func_152647_b(JsonObject p_152647_0_)
    {
        return p_152647_0_.has("ip") ? p_152647_0_.get("ip").getAsString() : null;
    }

    protected void func_152641_a(JsonObject p_152641_1_)
    {
        if (this.func_152640_f() != null)
        {
            p_152641_1_.addProperty("ip", (String)this.func_152640_f());
            super.func_152641_a(p_152641_1_);
        }
    }
}