package net.minecraft.command.server;

import net.minecraft.command.CommandBase;
import net.minecraft.command.ICommandSender;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.ChatComponentTranslation;

public class CommandListPlayers extends CommandBase
{
    private static final String __OBFID = "CL_00000615";

    public String getCommandName()
    {
        return "list";
    }

    /**
     * Return the required permission level for this command.
     */
    public int getRequiredPermissionLevel()
    {
        return 0;
    }

    public String getCommandUsage(ICommandSender p_71518_1_)
    {
        return "commands.players.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        p_71515_1_.addChatMessage(new ChatComponentTranslation("commands.players.list", new Object[] {Integer.valueOf(MinecraftServer.getServer().getCurrentPlayerCount()), Integer.valueOf(MinecraftServer.getServer().getMaxPlayers())}));
        p_71515_1_.addChatMessage(new ChatComponentText(MinecraftServer.getServer().getConfigurationManager().func_152609_b(p_71515_2_.length > 0 && "uuids".equalsIgnoreCase(p_71515_2_[0]))));
    }
}