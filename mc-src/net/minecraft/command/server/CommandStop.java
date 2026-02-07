package net.minecraft.command.server;

import net.minecraft.command.CommandBase;
import net.minecraft.command.ICommandSender;
import net.minecraft.server.MinecraftServer;

public class CommandStop extends CommandBase
{
    private static final String __OBFID = "CL_00001132";

    public String getCommandName()
    {
        return "stop";
    }

    public String getCommandUsage(ICommandSender p_71518_1_)
    {
        return "commands.stop.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (MinecraftServer.getServer().worldServers != null)
        {
            func_152373_a(p_71515_1_, this, "commands.stop.start", new Object[0]);
        }

        MinecraftServer.getServer().initiateShutdown();
    }
}