package net.minecraft.command.server;

import net.minecraft.command.CommandBase;
import net.minecraft.command.ICommandSender;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.IProgressUpdate;
import net.minecraft.world.MinecraftException;
import net.minecraft.world.WorldServer;

public class CommandSaveAll extends CommandBase
{
    private static final String __OBFID = "CL_00000826";

    public String getCommandName()
    {
        return "save-all";
    }

    public String getCommandUsage(ICommandSender p_71518_1_)
    {
        return "commands.save.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        MinecraftServer minecraftserver = MinecraftServer.getServer();
        p_71515_1_.addChatMessage(new ChatComponentTranslation("commands.save.start", new Object[0]));

        if (minecraftserver.getConfigurationManager() != null)
        {
            minecraftserver.getConfigurationManager().saveAllPlayerData();
        }

        try
        {
            int i;
            WorldServer worldserver;
            boolean flag;

            for (i = 0; i < minecraftserver.worldServers.length; ++i)
            {
                if (minecraftserver.worldServers[i] != null)
                {
                    worldserver = minecraftserver.worldServers[i];
                    flag = worldserver.levelSaving;
                    worldserver.levelSaving = false;
                    worldserver.saveAllChunks(true, (IProgressUpdate)null);
                    worldserver.levelSaving = flag;
                }
            }

            if (p_71515_2_.length > 0 && "flush".equals(p_71515_2_[0]))
            {
                p_71515_1_.addChatMessage(new ChatComponentTranslation("commands.save.flushStart", new Object[0]));

                for (i = 0; i < minecraftserver.worldServers.length; ++i)
                {
                    if (minecraftserver.worldServers[i] != null)
                    {
                        worldserver = minecraftserver.worldServers[i];
                        flag = worldserver.levelSaving;
                        worldserver.levelSaving = false;
                        worldserver.saveChunkData();
                        worldserver.levelSaving = flag;
                    }
                }

                p_71515_1_.addChatMessage(new ChatComponentTranslation("commands.save.flushEnd", new Object[0]));
            }
        }
        catch (MinecraftException minecraftexception)
        {
            func_152373_a(p_71515_1_, this, "commands.save.failed", new Object[] {minecraftexception.getMessage()});
            return;
        }

        func_152373_a(p_71515_1_, this, "commands.save.success", new Object[0]);
    }
}