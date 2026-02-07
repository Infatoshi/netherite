package net.minecraft.server.management;

import com.google.gson.JsonObject;
import com.mojang.authlib.GameProfile;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.File;
import java.util.Iterator;

public class UserListWhitelist extends UserList
{
    private static final String __OBFID = "CL_00001871";

    public UserListWhitelist(File p_i1132_1_)
    {
        super(p_i1132_1_);
    }

    protected UserListEntry func_152682_a(JsonObject p_152682_1_)
    {
        return new UserListWhitelistEntry(p_152682_1_);
    }

    public String[] func_152685_a()
    {
        String[] astring = new String[this.func_152688_e().size()];
        int i = 0;
        UserListWhitelistEntry userlistwhitelistentry;

        for (Iterator iterator = this.func_152688_e().values().iterator(); iterator.hasNext(); astring[i++] = ((GameProfile)userlistwhitelistentry.func_152640_f()).getName())
        {
            userlistwhitelistentry = (UserListWhitelistEntry)iterator.next();
        }

        return astring;
    }

    @SideOnly(Side.SERVER)
    public boolean func_152705_a(GameProfile p_152705_1_)
    {
        return this.func_152692_d(p_152705_1_);
    }

    protected String func_152704_b(GameProfile p_152704_1_)
    {
        return p_152704_1_.getId().toString();
    }

    public GameProfile func_152706_a(String p_152706_1_)
    {
        Iterator iterator = this.func_152688_e().values().iterator();
        UserListWhitelistEntry userlistwhitelistentry;

        do
        {
            if (!iterator.hasNext())
            {
                return null;
            }

            userlistwhitelistentry = (UserListWhitelistEntry)iterator.next();
        }
        while (!p_152706_1_.equalsIgnoreCase(((GameProfile)userlistwhitelistentry.func_152640_f()).getName()));

        return (GameProfile)userlistwhitelistentry.func_152640_f();
    }

    protected String func_152681_a(Object p_152681_1_)
    {
        return this.func_152704_b((GameProfile)p_152681_1_);
    }
}