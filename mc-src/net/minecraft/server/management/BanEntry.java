package net.minecraft.server.management;

import com.google.gson.JsonObject;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;

public abstract class BanEntry extends UserListEntry
{
    public static final SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss Z");
    protected final Date banStartDate;
    protected final String bannedBy;
    protected final Date banEndDate;
    protected final String reason;
    private static final String __OBFID = "CL_00001395";

    public BanEntry(Object p_i1173_1_, Date p_i1173_2_, String p_i1173_3_, Date p_i1173_4_, String p_i1173_5_)
    {
        super(p_i1173_1_);
        this.banStartDate = p_i1173_2_ == null ? new Date() : p_i1173_2_;
        this.bannedBy = p_i1173_3_ == null ? "(Unknown)" : p_i1173_3_;
        this.banEndDate = p_i1173_4_;
        this.reason = p_i1173_5_ == null ? "Banned by an operator." : p_i1173_5_;
    }

    protected BanEntry(Object p_i1174_1_, JsonObject p_i1174_2_)
    {
        super(p_i1174_1_, p_i1174_2_);
        Date date;

        try
        {
            date = p_i1174_2_.has("created") ? dateFormat.parse(p_i1174_2_.get("created").getAsString()) : new Date();
        }
        catch (ParseException parseexception1)
        {
            date = new Date();
        }

        this.banStartDate = date;
        this.bannedBy = p_i1174_2_.has("source") ? p_i1174_2_.get("source").getAsString() : "(Unknown)";
        Date date1;

        try
        {
            date1 = p_i1174_2_.has("expires") ? dateFormat.parse(p_i1174_2_.get("expires").getAsString()) : null;
        }
        catch (ParseException parseexception)
        {
            date1 = null;
        }

        this.banEndDate = date1;
        this.reason = p_i1174_2_.has("reason") ? p_i1174_2_.get("reason").getAsString() : "Banned by an operator.";
    }

    public Date getBanEndDate()
    {
        return this.banEndDate;
    }

    public String getBanReason()
    {
        return this.reason;
    }

    boolean hasBanExpired()
    {
        return this.banEndDate == null ? false : this.banEndDate.before(new Date());
    }

    protected void func_152641_a(JsonObject p_152641_1_)
    {
        p_152641_1_.addProperty("created", dateFormat.format(this.banStartDate));
        p_152641_1_.addProperty("source", this.bannedBy);
        p_152641_1_.addProperty("expires", this.banEndDate == null ? "forever" : dateFormat.format(this.banEndDate));
        p_152641_1_.addProperty("reason", this.reason);
    }
}