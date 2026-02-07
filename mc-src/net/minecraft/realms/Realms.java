package net.minecraft.realms;

import com.mojang.authlib.GameProfile;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.net.Proxy;
import java.util.UUID;
import net.minecraft.client.Minecraft;
import net.minecraft.util.Session;
import net.minecraft.world.WorldSettings;

@SideOnly(Side.CLIENT)
public class Realms
{
    private static final String __OBFID = "CL_00001892";

    public static boolean isTouchScreen()
    {
        return Minecraft.getMinecraft().gameSettings.touchscreen;
    }

    public static Proxy getProxy()
    {
        return Minecraft.getMinecraft().getProxy();
    }

    public static String sessionId()
    {
        Session session = Minecraft.getMinecraft().getSession();
        return session == null ? null : session.getSessionID();
    }

    public static String userName()
    {
        Session session = Minecraft.getMinecraft().getSession();
        return session == null ? null : session.getUsername();
    }

    public static long currentTimeMillis()
    {
        return Minecraft.getSystemTime();
    }

    public static String getSessionId()
    {
        return Minecraft.getMinecraft().getSession().getSessionID();
    }

    public static String getName()
    {
        return Minecraft.getMinecraft().getSession().getUsername();
    }

    public static String uuidToName(String p_uuidToName_0_)
    {
        return Minecraft.getMinecraft().func_152347_ac().fillProfileProperties(new GameProfile(UUID.fromString(p_uuidToName_0_.replaceAll("(\\w{8})(\\w{4})(\\w{4})(\\w{4})(\\w{12})", "$1-$2-$3-$4-$5")), (String)null), false).getName();
    }

    public static void setScreen(RealmsScreen p_setScreen_0_)
    {
        Minecraft.getMinecraft().displayGuiScreen(p_setScreen_0_.getProxy());
    }

    public static String getGameDirectoryPath()
    {
        return Minecraft.getMinecraft().mcDataDir.getAbsolutePath();
    }

    public static int survivalId()
    {
        return WorldSettings.GameType.SURVIVAL.getID();
    }

    public static int creativeId()
    {
        return WorldSettings.GameType.CREATIVE.getID();
    }

    public static int adventureId()
    {
        return WorldSettings.GameType.ADVENTURE.getID();
    }
}