package net.minecraft.realms;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public class ServerPing
{
    public volatile String nrOfPlayers = "0";
    public volatile long lastPingSnapshot = 0L;
    private static final String __OBFID = "CL_00001860";
}