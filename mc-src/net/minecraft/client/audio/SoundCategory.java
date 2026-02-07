package net.minecraft.client.audio;

import com.google.common.collect.Maps;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Map;

@SideOnly(Side.CLIENT)
public enum SoundCategory
{
    MASTER("master", 0),
    MUSIC("music", 1),
    RECORDS("record", 2),
    WEATHER("weather", 3),
    BLOCKS("block", 4),
    MOBS("hostile", 5),
    ANIMALS("neutral", 6),
    PLAYERS("player", 7),
    AMBIENT("ambient", 8);
    private static final Map field_147168_j = Maps.newHashMap();
    private static final Map field_147169_k = Maps.newHashMap();
    private final String categoryName;
    private final int categoryId;

    private static final String __OBFID = "CL_00001686";

    private SoundCategory(String p_i45126_3_, int p_i45126_4_)
    {
        this.categoryName = p_i45126_3_;
        this.categoryId = p_i45126_4_;
    }

    public String getCategoryName()
    {
        return this.categoryName;
    }

    public int getCategoryId()
    {
        return this.categoryId;
    }

    public static SoundCategory func_147154_a(String p_147154_0_)
    {
        return (SoundCategory)field_147168_j.get(p_147154_0_);
    }

    static
    {
        SoundCategory[] var0 = values();
        int var1 = var0.length;

        for (int var2 = 0; var2 < var1; ++var2)
        {
            SoundCategory var3 = var0[var2];

            if (field_147168_j.containsKey(var3.getCategoryName()) || field_147169_k.containsKey(Integer.valueOf(var3.getCategoryId())))
            {
                throw new Error("Clash in Sound Category ID & Name pools! Cannot insert " + var3);
            }

            field_147168_j.put(var3.getCategoryName(), var3);
            field_147169_k.put(Integer.valueOf(var3.getCategoryId()), var3);
        }
    }
}