package net.minecraft.command.server;

import java.util.List;
import net.minecraft.command.CommandBase;
import net.minecraft.command.ICommandSender;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.ChatComponentTranslation;

public class CommandListBans extends CommandBase
{
    private static final String __OBFID = "CL_00000596";

    public String getCommandName()
    {
        return "banlist";
    }

    /**
     * Return the required permission level for this command.
     */
    public int getRequiredPermissionLevel()
    {
        return 3;
    }

    /**
     * Returns true if the given command sender is allowed to use this command.
     */
    public boolean canCommandSenderUseCommand(ICommandSender p_71519_1_)
    {
        return (MinecraftServer.getServer().getConfigurationManager().getBannedIPs().func_152689_b() || MinecraftServer.getServer().getConfigurationManager().func_152608_h().func_152689_b()) && super.canCommandSenderUseCommand(p_71519_1_);
    }

    public String getCommandUsage(ICommandSender p_71518_1_)
    {
        return "commands.banlist.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length >= 1 && p_71515_2_[0].equalsIgnoreCase("ips"))
        {
            p_71515_1_.addChatMessage(new ChatComponentTranslation("commands.banlist.ips", new Object[] {Integer.valueOf(MinecraftServer.getServer().getConfigurationManager().getBannedIPs().func_152685_a().length)}));
            p_71515_1_.addChatMessage(new ChatComponentText(joinNiceString(MinecraftServer.getServer().getConfigurationManager().getBannedIPs().func_152685_a())));
        }
        else
        {
            p_71515_1_.addChatMessage(new ChatComponentTranslation("commands.banlist.players", new Object[] {Integer.valueOf(MinecraftServer.getServer().getConfigurationManager().func_152608_h().func_152685_a().length)}));
            p_71515_1_.addChatMessage(new ChatComponentText(joinNiceString(MinecraftServer.getServer().getConfigurationManager().func_152608_h().func_152685_a())));
        }
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length == 1 ? getListOfStringsMatchingLastWord(p_71516_2_, new String[] {"players", "ips"}): null;
    }
}