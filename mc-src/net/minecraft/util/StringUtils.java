package net.minecraft.util;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.regex.Pattern;

public class StringUtils
{
    private static final Pattern patternControlCode = Pattern.compile("(?i)\\u00A7[0-9A-FK-OR]");
    private static final String __OBFID = "CL_00001501";

    /**
     * Returns the time elapsed for the given number of ticks, in "mm:ss" format.
     */
    @SideOnly(Side.CLIENT)
    public static String ticksToElapsedTime(int p_76337_0_)
    {
        int j = p_76337_0_ / 20;
        int k = j / 60;
        j %= 60;
        return j < 10 ? k + ":0" + j : k + ":" + j;
    }

    @SideOnly(Side.CLIENT)
    public static String stripControlCodes(String p_76338_0_)
    {
        return patternControlCode.matcher(p_76338_0_).replaceAll("");
    }

    /**
     * Returns a value indicating whether the given string is null or empty.
     */
    public static boolean isNullOrEmpty(String p_151246_0_)
    {
        return p_151246_0_ == null || "".equals(p_151246_0_);
    }
}