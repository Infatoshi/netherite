package net.minecraft.entity.boss;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public final class BossStatus
{
    public static float healthScale;
    public static int statusBarTime;
    public static String bossName;
    public static boolean hasColorModifier;
    private static final String __OBFID = "CL_00000941";

    public static void setBossStatus(IBossDisplayData p_82824_0_, boolean p_82824_1_)
    {
        healthScale = p_82824_0_.getHealth() / p_82824_0_.getMaxHealth();
        statusBarTime = 100;
        bossName = p_82824_0_.func_145748_c_().getFormattedText();
        hasColorModifier = p_82824_1_;
    }
}